#import "AirfixMetalRenderer.h"

#import <Metal/Metal.h>
#import <simd/simd.h>

#include "airfix/render/DrawModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

NSString* const AirfixMetalRendererErrorDomain = @"AirfixMetalRendererErrorDomain";

enum class RendererError : NSInteger {
    missingDevice = 1,
    missingShaderLibrary,
    missingShaderFunction,
    pipelineCreation,
    depthStateCreation,
    samplerCreation,
    bufferCreation,
    textureCreation,
    invalidPayload,
};

// This layout is deliberately independent from DrawVertex. It is the sole
// CPU/GPU ABI shared with AirfixShaders.metal.
struct alignas(16) GpuVertex {
    simd_float4 position;
    simd_float4 normal;
    simd_float2 uv;
    simd_float2 padding;
};

struct alignas(16) GpuUniforms {
    simd_float4x4 mvp;
};

static_assert(sizeof(GpuVertex) == 48U);
static_assert(alignof(GpuVertex) == 16U);
static_assert(sizeof(GpuUniforms) == 64U);

NSError* makeError(const RendererError code, NSString* description) {
    return [NSError errorWithDomain:AirfixMetalRendererErrorDomain
                               code:static_cast<NSInteger>(code)
                           userInfo:@{NSLocalizedDescriptionKey : description}];
}

simd_float4x4 toSimdMatrix(
    const airfix::render::Mat3& linear,
    const airfix::render::Vec3& translation) {
    // Both contracts use column vectors, but their memory layouts are not
    // assumed to match. Repack every component explicitly.
    simd_float4x4 result{};
    result.columns[0] = simd_make_float4(
        linear.columns[0].x,
        linear.columns[0].y,
        linear.columns[0].z,
        0.0F);
    result.columns[1] = simd_make_float4(
        linear.columns[1].x,
        linear.columns[1].y,
        linear.columns[1].z,
        0.0F);
    result.columns[2] = simd_make_float4(
        linear.columns[2].x,
        linear.columns[2].y,
        linear.columns[2].z,
        0.0F);
    result.columns[3] = simd_make_float4(
        translation.x,
        translation.y,
        translation.z,
        1.0F);
    return result;
}

airfix::render::DrawModelPayload makeSyntheticPayload() {
    using airfix::render::DrawMaterial;
    using airfix::render::DrawMeshInstance;
    using airfix::render::DrawRange;
    using airfix::render::DrawVertex;
    using airfix::render::TexcoordMode;
    using airfix::render::TextureAssetId;
    using airfix::render::Vec2;
    using airfix::render::Vec3;

    airfix::render::DrawMeshPayload mesh;
    mesh.vertices = {
        DrawVertex{Vec3{-0.7F, -0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{0.0F, 1.0F}},
        DrawVertex{Vec3{0.7F, -0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{1.0F, 1.0F}},
        DrawVertex{Vec3{0.7F, 0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{1.0F, 0.0F}},
        DrawVertex{Vec3{-0.7F, 0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{0.0F, 0.0F}},
    };
    mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    mesh.materials = {
        DrawMaterial{0U, TextureAssetId{0U}, std::nullopt, std::nullopt},
        DrawMaterial{1U, TextureAssetId{0U}, std::nullopt, std::nullopt},
    };
    // Keep source triangle order: the smoke test encodes each range in this
    // exact sequence for every model instance.
    mesh.ranges = {
        DrawRange{0U, 3U, 0U, TexcoordMode::uv0},
        DrawRange{3U, 3U, 1U, TexcoordMode::uv0},
    };
    mesh.localBounds = {
        Vec3{-0.7F, -0.7F, 0.0F},
        Vec3{0.7F, 0.7F, 0.0F},
    };
    airfix::render::DrawModelPayload payload;
    payload.meshes.push_back(std::move(mesh));
    payload.instances = {
        DrawMeshInstance{
            .meshSlot = 0U,
            .sourceNodeReference = 1U,
            .modelLinear = airfix::render::Mat3{{
                Vec3{0.72F, 0.0F, 0.0F},
                Vec3{0.0F, 0.72F, 0.0F},
                Vec3{0.0F, 0.0F, 0.72F},
            }},
            .modelTranslation = Vec3{-0.38F, 0.0F, 0.35F},
        },
        DrawMeshInstance{
            .meshSlot = 0U,
            .sourceNodeReference = 2U,
            .modelLinear = airfix::render::Mat3{{
                Vec3{0.55F, 0.0F, 0.0F},
                Vec3{0.0F, 0.55F, 0.0F},
                Vec3{0.0F, 0.0F, 0.55F},
            }},
            .modelTranslation = Vec3{0.38F, 0.08F, 0.2F},
        },
    };
    return payload;
}

bool validatePayload(const airfix::render::DrawModelPayload& payload) {
    // This intentionally small smoke path owns one shared GPU mesh. Reject
    // any future payload shape it cannot faithfully encode.
    if (payload.meshes.size() != 1U || payload.instances.size() < 2U) {
        return false;
    }
    const airfix::render::DrawMeshPayload& mesh = payload.meshes[0];
    if (mesh.vertices.empty() || mesh.indices.empty() ||
        mesh.ranges.size() < 2U) {
        return false;
    }
    for (const airfix::render::DrawMeshInstance& instance : payload.instances) {
        if (instance.meshSlot != 0U) {
            return false;
        }
    }

    std::uint64_t expectedFirstIndex = 0U;
    for (const auto& range : mesh.ranges) {
        if (range.firstIndex != expectedFirstIndex || range.indexCount == 0U ||
            range.materialSlot >= mesh.materials.size()) {
            return false;
        }
        expectedFirstIndex += range.indexCount;
        if (expectedFirstIndex > mesh.indices.size()) {
            return false;
        }
        const airfix::render::DrawMaterial& material =
            mesh.materials[range.materialSlot];
        if (!material.primary.has_value() || material.primary->value != 0U ||
            range.texcoordMode != airfix::render::TexcoordMode::uv0) {
            return false;
        }
    }
    if (expectedFirstIndex != mesh.indices.size()) {
        return false;
    }
    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            return false;
        }
    }
    return true;
}

std::vector<GpuVertex> repackVertices(
    const std::vector<airfix::render::DrawVertex>& vertices) {
    std::vector<GpuVertex> gpuVertices;
    gpuVertices.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        gpuVertices.push_back(GpuVertex{
            simd_make_float4(
                vertex.position.x, vertex.position.y, vertex.position.z, 1.0F),
            simd_make_float4(
                vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0F),
            simd_make_float2(vertex.uv.u, vertex.uv.v),
            simd_make_float2(0.0F, 0.0F),
        });
    }
    return gpuVertices;
}

simd_float4x4 aspectCorrection(const CGSize size) {
    simd_float4x4 matrix = matrix_identity_float4x4;
    if (size.width > 0.0 && size.height > 0.0) {
        const float aspect = static_cast<float>(size.width / size.height);
        if (aspect > 1.0F) {
            matrix.columns[0].x = 1.0F / aspect;
        }
        else {
            matrix.columns[1].y = aspect;
        }
    }
    return matrix;
}

} // namespace

@interface AirfixMetalRenderer () {
    airfix::render::DrawModelPayload _payload;
}
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLSamplerState> samplerState;
@property(nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property(nonatomic, strong) id<MTLBuffer> indexBuffer;
@property(nonatomic, strong) id<MTLTexture> syntheticTexture;
@end

@implementation AirfixMetalRenderer

- (nullable instancetype)initWithMetalView:(MTKView*)metalView
                                     error:(NSError* _Nullable* _Nullable)error {
    self = [super init];
    if (self == nil) {
        return nil;
    }

    id<MTLDevice> device = metalView.device;
    if (device == nil) {
        if (error != nullptr) {
            *error = makeError(RendererError::missingDevice, @"Metal is not available on this device.");
        }
        return nil;
    }

    _payload = makeSyntheticPayload();
    if (!validatePayload(_payload)) {
        if (error != nullptr) {
            *error = makeError(RendererError::invalidPayload, @"The public Metal smoke-test payload is invalid.");
        }
        return nil;
    }

    self.commandQueue = [device newCommandQueue];
    if (self.commandQueue == nil) {
        if (error != nullptr) {
            *error = makeError(RendererError::bufferCreation, @"Metal could not create a command queue.");
        }
        return nil;
    }

    NSError* libraryError = nil;
    id<MTLLibrary> library = [device newDefaultLibraryWithBundle:NSBundle.mainBundle
                                                            error:&libraryError];
    if (library == nil) {
        if (error != nullptr) {
            NSString* reason = libraryError.localizedDescription ?: @"default.metallib is missing.";
            *error = makeError(
                RendererError::missingShaderLibrary,
                [@"Metal shader library could not be loaded: " stringByAppendingString:reason]);
        }
        return nil;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"airfixVertexMain"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"airfixFragmentMain"];
    if (vertexFunction == nil || fragmentFunction == nil) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::missingShaderFunction,
                @"default.metallib does not contain the Airfix smoke-test shaders.");
        }
        return nil;
    }

    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"Airfix public smoke-test pipeline";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipelineDescriptor.colorAttachments[0].blendingEnabled = NO;
    pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    NSError* pipelineError = nil;
    self.pipelineState = [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                                                 error:&pipelineError];
    if (self.pipelineState == nil) {
        if (error != nullptr) {
            NSString* reason = pipelineError.localizedDescription ?: @"unknown pipeline error";
            *error = makeError(
                RendererError::pipelineCreation,
                [@"Metal render pipeline creation failed: " stringByAppendingString:reason]);
        }
        return nil;
    }

    MTLDepthStencilDescriptor* depthDescriptor = [[MTLDepthStencilDescriptor alloc] init];
    depthDescriptor.depthCompareFunction = MTLCompareFunctionLess;
    depthDescriptor.depthWriteEnabled = YES;
    self.depthState = [device newDepthStencilStateWithDescriptor:depthDescriptor];
    if (self.depthState == nil) {
        if (error != nullptr) {
            *error = makeError(RendererError::depthStateCreation, @"Metal depth state creation failed.");
        }
        return nil;
    }

    MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
    samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    self.samplerState = [device newSamplerStateWithDescriptor:samplerDescriptor];
    if (self.samplerState == nil) {
        if (error != nullptr) {
            *error = makeError(RendererError::samplerCreation, @"Metal sampler creation failed.");
        }
        return nil;
    }

    const airfix::render::DrawMeshPayload& mesh = _payload.meshes[0];
    const std::vector<GpuVertex> gpuVertices = repackVertices(mesh.vertices);
    self.vertexBuffer = [device newBufferWithBytes:gpuVertices.data()
                                           length:gpuVertices.size() * sizeof(GpuVertex)
                                          options:MTLResourceStorageModeShared];
    self.indexBuffer = [device newBufferWithBytes:mesh.indices.data()
                                          length:mesh.indices.size() * sizeof(std::uint32_t)
                                         options:MTLResourceStorageModeShared];
    if (self.vertexBuffer == nil || self.indexBuffer == nil) {
        if (error != nullptr) {
            *error = makeError(RendererError::bufferCreation, @"Metal mesh buffer creation failed.");
        }
        return nil;
    }

    MTLTextureDescriptor* textureDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                          width:2U
                                                         height:2U
                                                      mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModeShared;
    self.syntheticTexture = [device newTextureWithDescriptor:textureDescriptor];
    if (self.syntheticTexture == nil) {
        if (error != nullptr) {
            *error = makeError(RendererError::textureCreation, @"Metal texture creation failed.");
        }
        return nil;
    }

    const std::array<std::uint8_t, 16U> pixels = {
        238U, 91U, 72U, 255U,
        247U, 201U, 72U, 255U,
        54U, 179U, 126U, 255U,
        64U, 129U, 216U, 255U,
    };
    [self.syntheticTexture replaceRegion:MTLRegionMake2D(0U, 0U, 2U, 2U)
                             mipmapLevel:0U
                               withBytes:pixels.data()
                             bytesPerRow:2U * 4U];

    return self;
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
}

- (void)drawInMTKView:(MTKView*)view {
    MTLRenderPassDescriptor* renderPass = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (renderPass == nil || drawable == nil) {
        return;
    }

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
    if (commandBuffer == nil || encoder == nil) {
        return;
    }

    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setDepthStencilState:self.depthState];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setVertexBuffer:self.vertexBuffer offset:0U atIndex:0U];
    [encoder setFragmentSamplerState:self.samplerState atIndex:0U];

    const simd_float4x4 viewport = aspectCorrection(view.drawableSize);
    for (const airfix::render::DrawMeshInstance& instance : _payload.instances) {
        const airfix::render::DrawMeshPayload& mesh = _payload.meshes[instance.meshSlot];
        const simd_float4x4 model =
            toSimdMatrix(instance.modelLinear, instance.modelTranslation);
        const GpuUniforms uniforms{simd_mul(viewport, model)};
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1U];

        for (const airfix::render::DrawRange& range : mesh.ranges) {
            const airfix::render::DrawMaterial& material =
                mesh.materials[range.materialSlot];
            if (material.primary->value == 0U) {
                [encoder setFragmentTexture:self.syntheticTexture atIndex:0U];
            }
            const NSUInteger indexOffset =
                static_cast<NSUInteger>(range.firstIndex) * sizeof(std::uint32_t);
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:static_cast<NSUInteger>(range.indexCount)
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:self.indexBuffer
                         indexBufferOffset:indexOffset];
        }
    }

    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
