#import "AirfixMetalRenderer.h"

#import <Metal/Metal.h>
#import <simd/simd.h>

#include "airfix/render/DrawModel.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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
    resourceLimit,
    unexpectedFailure,
};

constexpr std::size_t kMaximumSyntheticGpuBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumSyntheticCpuPackedBytes =
    64U * 1024U * 1024U;

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

bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

bool checkedMultiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

bool accountGpuBytes(
    const std::size_t bytes,
    std::size_t& aggregateBytes) noexcept {
    std::size_t next = 0U;
    if (!checkedAdd(aggregateBytes, bytes, next) ||
        next > kMaximumSyntheticGpuBytes) {
        return false;
    }
    aggregateBytes = next;
    return true;
}

bool accountCpuPackedBytes(
    const std::size_t bytes,
    std::size_t& aggregateBytes) noexcept {
    std::size_t next = 0U;
    if (!checkedAdd(aggregateBytes, bytes, next) ||
        next > kMaximumSyntheticCpuPackedBytes) {
        return false;
    }
    aggregateBytes = next;
    return true;
}

bool fitsNSUInteger(const std::size_t value) noexcept {
    if constexpr (sizeof(std::size_t) > sizeof(NSUInteger)) {
        return value <=
            static_cast<std::size_t>(
                std::numeric_limits<NSUInteger>::max());
    }
    return true;
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

    // Mesh zero deliberately contains both a missing primary texture and
    // TexcoordMode::none. The renderer maps those explicit states to a
    // one-pixel fallback without modifying the backend-neutral payload.
    airfix::render::DrawMeshPayload meshZero;
    meshZero.vertices = {
        DrawVertex{Vec3{-0.7F, -0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{0.0F, 1.0F}},
        DrawVertex{Vec3{0.7F, -0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{1.0F, 1.0F}},
        DrawVertex{Vec3{0.7F, 0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{1.0F, 0.0F}},
        DrawVertex{Vec3{-0.7F, 0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{0.0F, 0.0F}},
    };
    meshZero.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    meshZero.materials = {
        DrawMaterial{0U, std::nullopt, std::nullopt, std::nullopt},
        DrawMaterial{1U, TextureAssetId{0U}, std::nullopt, std::nullopt},
    };
    meshZero.ranges = {
        DrawRange{0U, 3U, 0U, TexcoordMode::uv0},
        DrawRange{3U, 3U, 1U, TexcoordMode::none},
    };
    meshZero.localBounds = {
        Vec3{-0.7F, -0.7F, 0.0F},
        Vec3{0.7F, 0.7F, 0.0F},
    };

    airfix::render::DrawMeshPayload meshOne;
    meshOne.vertices = {
        DrawVertex{Vec3{-0.65F, -0.55F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{0.0F, 1.0F}},
        DrawVertex{Vec3{0.65F, -0.55F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{1.0F, 1.0F}},
        DrawVertex{Vec3{0.0F, 0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, Vec2{0.5F, 0.0F}},
    };
    meshOne.indices = {0U, 1U, 2U};
    meshOne.materials = {
        DrawMaterial{2U, TextureAssetId{0U}, std::nullopt, std::nullopt},
    };
    meshOne.ranges = {
        DrawRange{0U, 3U, 0U, TexcoordMode::uv0},
    };
    meshOne.localBounds = {
        Vec3{-0.65F, -0.55F, 0.0F},
        Vec3{0.65F, 0.7F, 0.0F},
    };

    airfix::render::DrawModelPayload payload;
    payload.meshes.push_back(std::move(meshZero));
    payload.meshes.push_back(std::move(meshOne));
    // The deliberately non-monotonic order proves that draw submission
    // follows instance order and rebinds reusable mesh buffers: 1, 0, 1.
    payload.instances = {
        DrawMeshInstance{
            .meshSlot = 1U,
            .sourceNodeReference = 1U,
            .modelLinear = airfix::render::Mat3{{
                Vec3{0.48F, 0.0F, 0.0F},
                Vec3{0.0F, 0.48F, 0.0F},
                Vec3{0.0F, 0.0F, 0.48F},
            }},
            .modelTranslation = Vec3{-0.52F, 0.28F, 0.35F},
        },
        DrawMeshInstance{
            .meshSlot = 0U,
            .sourceNodeReference = 2U,
            .modelLinear = airfix::render::Mat3{{
                Vec3{0.46F, 0.0F, 0.0F},
                Vec3{0.0F, 0.46F, 0.0F},
                Vec3{0.0F, 0.0F, 0.46F},
            }},
            .modelTranslation = Vec3{0.0F, -0.3F, 0.2F},
        },
        DrawMeshInstance{
            .meshSlot = 1U,
            .sourceNodeReference = 3U,
            .modelLinear = airfix::render::Mat3{{
                Vec3{0.38F, 0.0F, 0.0F},
                Vec3{0.0F, 0.38F, 0.0F},
                Vec3{0.0F, 0.0F, 0.38F},
            }},
            .modelTranslation = Vec3{0.54F, 0.3F, 0.1F},
        },
    };
    return payload;
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

@interface AirfixMetalMeshBuffers : NSObject
@property(nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property(nonatomic, strong) id<MTLBuffer> indexBuffer;
@end

@implementation AirfixMetalMeshBuffers
@end

@interface AirfixMetalRenderer () {
    airfix::render::DrawModelPayload _payload;
    airfix::render::DrawSubmissionPlan _submissionPlan;
    std::vector<NSUInteger> _indexOffsets;
}
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLSamplerState> samplerState;
@property(nonatomic, strong) NSArray<AirfixMetalMeshBuffers*>* meshBuffers;
@property(nonatomic, strong) id<MTLTexture> syntheticTexture;
@property(nonatomic, strong) id<MTLTexture> fallbackTexture;
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

    // No C++ allocation or conversion failure may escape this Objective-C
    // initializer boundary.
    try {
    airfix::render::DrawModelPayload payload = makeSyntheticPayload();
    airfix::render::DrawSubmissionDescription submission =
        airfix::render::buildDrawSubmissionPlan(payload, 1U);
    if (!submission.plan.has_value() || !submission.issues.empty()) {
        if (error != nullptr) {
            *error = makeError(RendererError::invalidPayload, @"The public Metal smoke-test payload is invalid.");
        }
        return nil;
    }
    airfix::render::DrawSubmissionPlan submissionPlan =
        std::move(submission.plan.value());
    if (submissionPlan.meshUploads.size() != payload.meshes.size() ||
        !fitsNSUInteger(submissionPlan.meshUploads.size())) {
        if (error != nullptr) {
            *error = makeError(RendererError::invalidPayload, @"The public Metal smoke-test upload plan is inconsistent.");
        }
        return nil;
    }

    id<MTLCommandQueue> commandQueue = [device newCommandQueue];
    if (commandQueue == nil) {
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
            NSString* reason = libraryError.localizedDescription;
            if (reason == nil) {
                reason = @"default.metallib is missing.";
            }
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
    id<MTLRenderPipelineState> pipelineState =
        [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                               error:&pipelineError];
    if (pipelineState == nil) {
        if (error != nullptr) {
            NSString* reason = pipelineError.localizedDescription;
            if (reason == nil) {
                reason = @"unknown pipeline error";
            }
            *error = makeError(
                RendererError::pipelineCreation,
                [@"Metal render pipeline creation failed: " stringByAppendingString:reason]);
        }
        return nil;
    }

    MTLDepthStencilDescriptor* depthDescriptor = [[MTLDepthStencilDescriptor alloc] init];
    depthDescriptor.depthCompareFunction = MTLCompareFunctionLess;
    depthDescriptor.depthWriteEnabled = YES;
    id<MTLDepthStencilState> depthState =
        [device newDepthStencilStateWithDescriptor:depthDescriptor];
    if (depthState == nil) {
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
    id<MTLSamplerState> samplerState =
        [device newSamplerStateWithDescriptor:samplerDescriptor];
    if (samplerState == nil) {
        if (error != nullptr) {
            *error = makeError(RendererError::samplerCreation, @"Metal sampler creation failed.");
        }
        return nil;
    }

    // Preflight every byte count and command offset before asking Metal for
    // any resource. The aggregate cap is intentionally small for this public
    // smoke path and device.maxBufferLength guards each individual buffer.
    const std::size_t maximumBufferLength =
        static_cast<std::size_t>(device.maxBufferLength);
    std::size_t aggregateGpuBytes = 0U;
    std::size_t aggregateCpuPackedBytes = 0U;
    std::vector<std::size_t> vertexByteCounts;
    std::vector<std::size_t> indexByteCounts;
    vertexByteCounts.reserve(submissionPlan.meshUploads.size());
    indexByteCounts.reserve(submissionPlan.meshUploads.size());

    // First pass is allocation-free with respect to packed vertex payloads.
    // It bounds both eventual GPU residency and peak packed CPU storage.
    bool resourcePreflightValid = true;
    for (std::size_t uploadIndex = 0U;
         uploadIndex < submissionPlan.meshUploads.size();
         ++uploadIndex) {
        const auto& upload = submissionPlan.meshUploads[uploadIndex];
        if (static_cast<std::size_t>(upload.meshSlot) != uploadIndex ||
            uploadIndex >= payload.meshes.size()) {
            resourcePreflightValid = false;
            break;
        }
        const auto& mesh = payload.meshes[upload.meshSlot];
        if (upload.vertexCount != mesh.vertices.size() ||
            upload.indexCount != mesh.indices.size()) {
            resourcePreflightValid = false;
            break;
        }
        std::size_t vertexBytes = 0U;
        std::size_t indexBytes = 0U;
        if (!checkedMultiply(
                mesh.vertices.size(), sizeof(GpuVertex), vertexBytes) ||
            !checkedMultiply(
                mesh.indices.size(), sizeof(std::uint32_t), indexBytes) ||
            vertexBytes > maximumBufferLength ||
            indexBytes > maximumBufferLength ||
            !accountGpuBytes(vertexBytes, aggregateGpuBytes) ||
            !accountGpuBytes(indexBytes, aggregateGpuBytes) ||
            !accountCpuPackedBytes(
                vertexBytes, aggregateCpuPackedBytes)) {
            resourcePreflightValid = false;
            break;
        }
        vertexByteCounts.push_back(vertexBytes);
        indexByteCounts.push_back(indexBytes);
    }

    constexpr std::size_t syntheticTextureBytes = 2U * 2U * 4U;
    constexpr std::size_t fallbackTextureBytes = 1U * 1U * 4U;
    if (!resourcePreflightValid ||
        !accountGpuBytes(syntheticTextureBytes, aggregateGpuBytes) ||
        !accountGpuBytes(fallbackTextureBytes, aggregateGpuBytes)) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The public Metal smoke-test resources exceed their checked GPU budget.");
        }
        return nil;
    }

    std::vector<NSUInteger> indexOffsets;
    indexOffsets.reserve(submissionPlan.commands.size());
    for (const auto& command : submissionPlan.commands) {
        if (command.instanceIndex >= payload.instances.size() ||
            static_cast<std::size_t>(command.meshSlot) >=
                indexByteCounts.size() ||
            payload.instances[command.instanceIndex].meshSlot !=
                command.meshSlot ||
            vertexByteCounts[command.meshSlot] == 0U ||
            indexByteCounts[command.meshSlot] == 0U) {
            resourcePreflightValid = false;
            break;
        }
        const auto& mesh = payload.meshes[command.meshSlot];
        if (command.rangeIndex >= mesh.ranges.size()) {
            resourcePreflightValid = false;
            break;
        }
        const auto& range = mesh.ranges[command.rangeIndex];
        if (command.firstIndex != range.firstIndex ||
            command.indexCount != range.indexCount ||
            command.materialSlot != range.materialSlot ||
            command.texcoordMode != range.texcoordMode) {
            resourcePreflightValid = false;
            break;
        }
        std::size_t offset = 0U;
        std::size_t drawBytes = 0U;
        std::size_t drawEnd = 0U;
        if (!checkedMultiply(
                static_cast<std::size_t>(command.firstIndex),
                sizeof(std::uint32_t),
                offset) ||
            !checkedMultiply(
                static_cast<std::size_t>(command.indexCount),
                sizeof(std::uint32_t),
                drawBytes) ||
            !checkedAdd(offset, drawBytes, drawEnd) ||
            offset >= indexByteCounts[command.meshSlot] ||
            drawEnd > indexByteCounts[command.meshSlot] ||
            !fitsNSUInteger(offset)) {
            resourcePreflightValid = false;
            break;
        }
        indexOffsets.push_back(static_cast<NSUInteger>(offset));
    }
    if (!resourcePreflightValid ||
        indexOffsets.size() != submissionPlan.commands.size()) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The public Metal smoke-test draw offsets exceed their checked bounds.");
        }
        return nil;
    }

    // Repack only after all counts, byte lengths, aggregate budgets, command
    // relationships and index ranges have passed preflight.
    std::vector<std::vector<GpuVertex>> packedVertices;
    packedVertices.reserve(submissionPlan.meshUploads.size());
    for (std::size_t meshSlot = 0U;
         meshSlot < payload.meshes.size();
         ++meshSlot) {
        std::vector<GpuVertex> gpuVertices =
            repackVertices(payload.meshes[meshSlot].vertices);
        std::size_t packedBytes = 0U;
        if (!checkedMultiply(
                gpuVertices.size(), sizeof(GpuVertex), packedBytes) ||
            packedBytes != vertexByteCounts[meshSlot]) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The public Metal smoke-test packed vertex size changed after preflight.");
            }
            return nil;
        }
        packedVertices.push_back(std::move(gpuVertices));
    }

    NSMutableArray<AirfixMetalMeshBuffers*>* meshBuffers =
        [NSMutableArray
            arrayWithCapacity:
                static_cast<NSUInteger>(packedVertices.size())];
    for (std::size_t meshSlot = 0U;
         meshSlot < packedVertices.size();
         ++meshSlot) {
        id<MTLBuffer> vertexBuffer = nil;
        if (vertexByteCounts[meshSlot] != 0U) {
            vertexBuffer =
                [device newBufferWithBytes:packedVertices[meshSlot].data()
                                   length:vertexByteCounts[meshSlot]
                                  options:MTLResourceStorageModeShared];
        }
        id<MTLBuffer> indexBuffer = nil;
        if (indexByteCounts[meshSlot] != 0U) {
            indexBuffer =
                [device newBufferWithBytes:payload.meshes[meshSlot].indices.data()
                                   length:indexByteCounts[meshSlot]
                                  options:MTLResourceStorageModeShared];
        }
        if ((vertexByteCounts[meshSlot] != 0U && vertexBuffer == nil) ||
            (indexByteCounts[meshSlot] != 0U && indexBuffer == nil)) {
            if (error != nullptr) {
                *error = makeError(RendererError::bufferCreation, @"Metal mesh buffer creation failed.");
            }
            return nil;
        }
        AirfixMetalMeshBuffers* buffers = [[AirfixMetalMeshBuffers alloc] init];
        buffers.vertexBuffer = vertexBuffer;
        buffers.indexBuffer = indexBuffer;
        [meshBuffers addObject:buffers];
    }

    MTLTextureDescriptor* textureDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                          width:2U
                                                         height:2U
                                                      mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> syntheticTexture =
        [device newTextureWithDescriptor:textureDescriptor];
    if (syntheticTexture == nil) {
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
    [syntheticTexture replaceRegion:MTLRegionMake2D(0U, 0U, 2U, 2U)
                        mipmapLevel:0U
                          withBytes:pixels.data()
                        bytesPerRow:2U * 4U];

    MTLTextureDescriptor* fallbackDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                          width:1U
                                                         height:1U
                                                      mipmapped:NO];
    fallbackDescriptor.usage = MTLTextureUsageShaderRead;
    fallbackDescriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> fallbackTexture =
        [device newTextureWithDescriptor:fallbackDescriptor];
    if (fallbackTexture == nil) {
        if (error != nullptr) {
            *error = makeError(RendererError::textureCreation, @"Metal fallback texture creation failed.");
        }
        return nil;
    }
    const std::array<std::uint8_t, 4U> fallbackPixel = {
        255U, 255U, 255U, 255U,
    };
    [fallbackTexture replaceRegion:MTLRegionMake2D(0U, 0U, 1U, 1U)
                       mipmapLevel:0U
                         withBytes:fallbackPixel.data()
                       bytesPerRow:1U * 4U];
    NSArray<AirfixMetalMeshBuffers*>* meshBufferSnapshot =
        [meshBuffers copy];

    // Publish only after the complete renderer candidate has been validated
    // and every Metal object has been created successfully.
    _payload = std::move(payload);
    _submissionPlan = std::move(submissionPlan);
    _indexOffsets = std::move(indexOffsets);
    self.commandQueue = commandQueue;
    self.pipelineState = pipelineState;
    self.depthState = depthState;
    self.samplerState = samplerState;
    self.meshBuffers = meshBufferSnapshot;
    self.syntheticTexture = syntheticTexture;
    self.fallbackTexture = fallbackTexture;

    return self;
    } catch (...) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::unexpectedFailure,
                @"The public Metal smoke-test candidate could not be prepared.");
        }
        return nil;
    }
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
    if (commandBuffer == nil) {
        return;
    }
    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
    if (encoder == nil) {
        return;
    }

    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setDepthStencilState:self.depthState];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setFragmentSamplerState:self.samplerState atIndex:0U];

    const simd_float4x4 viewport = aspectCorrection(view.drawableSize);
    for (std::size_t commandIndex = 0U;
         commandIndex < _submissionPlan.commands.size();
         ++commandIndex) {
        const auto& command = _submissionPlan.commands[commandIndex];
        const auto& instance = _payload.instances[command.instanceIndex];
        const simd_float4x4 model =
            toSimdMatrix(instance.modelLinear, instance.modelTranslation);
        const GpuUniforms uniforms{simd_mul(viewport, model)};
        AirfixMetalMeshBuffers* buffers = self.meshBuffers[command.meshSlot];
        [encoder setVertexBuffer:buffers.vertexBuffer
                         offset:0U
                        atIndex:0U];
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1U];

        id<MTLTexture> texture = self.fallbackTexture;
        if (command.texcoordMode == airfix::render::TexcoordMode::uv0 &&
            command.primary.has_value() &&
            command.primary->value == 0U) {
            // Asset zero is a real, addressable texture. Missing primary data
            // and TexcoordMode::none remain distinct and use the fallback.
            texture = self.syntheticTexture;
        }
        [encoder setFragmentTexture:texture atIndex:0U];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:static_cast<NSUInteger>(command.indexCount)
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:buffers.indexBuffer
                     indexBufferOffset:_indexOffsets[commandIndex]];
    }

    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
