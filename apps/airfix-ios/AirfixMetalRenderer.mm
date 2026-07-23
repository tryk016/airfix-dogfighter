#import "AirfixMetalRenderer.h"

#import <Metal/Metal.h>
#import <simd/simd.h>

#include "airfix/content/WorldRoomLoader.hpp"
#include "airfix/render/DrawModel.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"

#include <algorithm>
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
    wrongThread,
    blitCreation,
    mipGeneration,
    invalidPreparedRoom,
    preparedRoomAlreadyPublished,
    unexpectedFailure,
};

constexpr std::size_t kMaximumSyntheticGpuBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumSyntheticCpuPackedBytes =
    64U * 1024U * 1024U;
constexpr std::size_t kMaximumPrivateRoomGpuBytes =
    256U * 1024U * 1024U;
constexpr std::size_t kMaximumPrivateRoomCpuPackedBytes =
    128U * 1024U * 1024U;
constexpr std::size_t kMaximumSnapshotTransitionGpuBytes =
    384U * 1024U * 1024U;

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

bool checkedAdd64(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& output) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

bool accountGpuBytes(
    const std::size_t bytes,
    std::size_t& aggregateBytes,
    const std::size_t maximumBytes = kMaximumSyntheticGpuBytes) noexcept {
    std::size_t next = 0U;
    if (!checkedAdd(aggregateBytes, bytes, next) ||
        next > maximumBytes) {
        return false;
    }
    aggregateBytes = next;
    return true;
}

bool accountCpuPackedBytes(
    const std::size_t bytes,
    std::size_t& aggregateBytes,
    const std::size_t maximumBytes =
        kMaximumSyntheticCpuPackedBytes) noexcept {
    std::size_t next = 0U;
    if (!checkedAdd(aggregateBytes, bytes, next) ||
        next > maximumBytes) {
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

@interface AirfixMetalRoomResources : NSObject {
@public
    airfix::content::ContentRevision _revision;
    airfix::render::DrawModelPayload _payload;
    airfix::render::DrawSubmissionPlan _submissionPlan;
    std::vector<NSUInteger> _indexOffsets;
}
@property(nonatomic, strong) NSArray<AirfixMetalMeshBuffers*>* meshBuffers;
@property(nonatomic, strong) NSArray<id<MTLTexture>>* textures;
@end

@implementation AirfixMetalRoomResources
@end

@interface AirfixMetalRoomSnapshot : NSObject {
@public
    __strong AirfixMetalRoomResources* _resources;
    __strong dispatch_queue_t _releaseQueue;
    std::size_t _allocatedGpuBytes;
    BOOL _worldRoomInstalled;
}
@end

@implementation AirfixMetalRoomSnapshot

- (void)dealloc {
    // Snapshot publication/discard is deliberately O(1) on the main thread.
    // Transfer the final ownership token to a serial worker queue so Metal
    // arrays and the potentially large C++ payload are destroyed off-main.
    if (_resources != nil && _releaseQueue != nil) {
        void* retainedResources =
            (__bridge_retained void*)_resources;
        _resources = nil;
        dispatch_async(_releaseQueue, ^{
            @autoreleasepool {
                AirfixMetalRoomResources* resources =
                    (__bridge_transfer AirfixMetalRoomResources*)
                        retainedResources;
                (void)resources;
            }
        });
    }
}

@end

@interface AirfixPreparedMetalRoom : NSObject {
@public
    __strong NSObject* _ownerToken;
    __strong id<MTLDevice> _device;
    __strong AirfixMetalRoomSnapshot* _snapshot;
    BOOL _published;
}
@end

@implementation AirfixPreparedMetalRoom
@end

namespace {

struct PrivateRoomPreflight {
    std::vector<std::size_t> vertexByteCounts;
    std::vector<std::size_t> indexByteCounts;
    std::vector<NSUInteger> indexOffsets;
    std::size_t aggregateGpuBytes{};
    std::size_t aggregateCpuPackedBytes{};
};

bool uint64ToSize(
    const std::uint64_t value,
    std::size_t& output) noexcept {
    if constexpr (sizeof(std::uint64_t) > sizeof(std::size_t)) {
        if (value > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
    }
    output = static_cast<std::size_t>(value);
    return true;
}

bool validateTextureAsset(
    const airfix::content::LoadedTextureAsset& texture,
    const std::size_t textureIndex,
    std::size_t& aggregateGpuBytes) noexcept {
    using airfix::render::GtiMipPolicy;

    const auto& upload = texture.upload;
    if (textureIndex > std::numeric_limits<std::uint32_t>::max() ||
        texture.assetId.value != static_cast<std::uint32_t>(textureIndex) ||
        upload.request.assetId != texture.assetId ||
        upload.request.archiveFileIndex != texture.sourceFileIndex ||
        upload.uploadLevels.size() != texture.uploadLevels.size() ||
        upload.uploadLevels.size() != upload.uploadedMipCount ||
        upload.allocatedMipCount == 0U ||
        upload.allocatedMipCount < upload.uploadedMipCount ||
        upload.uploadLevels.empty()) {
        return false;
    }

    const auto baseWidth = upload.uploadLevels.front().width;
    const auto baseHeight = upload.uploadLevels.front().height;
    if (baseWidth == 0U || baseHeight == 0U ||
        !fitsNSUInteger(static_cast<std::size_t>(baseWidth)) ||
        !fitsNSUInteger(static_cast<std::size_t>(baseHeight)) ||
        !fitsNSUInteger(
            static_cast<std::size_t>(upload.allocatedMipCount))) {
        return false;
    }

    std::uint32_t naturalMipCount = 1U;
    auto naturalWidth = baseWidth;
    auto naturalHeight = baseHeight;
    while (naturalWidth > 1U || naturalHeight > 1U) {
        naturalWidth = std::max(1U, naturalWidth >> 1U);
        naturalHeight = std::max(1U, naturalHeight >> 1U);
        ++naturalMipCount;
    }
    switch (upload.mipPolicy) {
    case GtiMipPolicy::authoredChain:
        if (upload.allocatedMipCount != upload.uploadedMipCount ||
            upload.allocatedMipCount > naturalMipCount) {
            return false;
        }
        break;
    case GtiMipPolicy::generateFromBase:
        if (upload.uploadedMipCount != 1U ||
            upload.allocatedMipCount != naturalMipCount) {
            return false;
        }
        break;
    default:
        return false;
    }

    std::uint64_t decodedBytes = 0U;
    for (std::size_t levelIndex = 0U;
         levelIndex < upload.uploadLevels.size();
         ++levelIndex) {
        const auto& level = upload.uploadLevels[levelIndex];
        const auto& image = texture.uploadLevels[levelIndex];
        if (levelIndex > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const auto levelNumber = static_cast<std::uint32_t>(levelIndex);
        const auto expectedWidth = std::max(1U, baseWidth >> levelNumber);
        const auto expectedHeight = std::max(1U, baseHeight >> levelNumber);
        std::size_t rowBytes = 0U;
        std::size_t rgbaBytes = 0U;
        if (!checkedMultiply(
                static_cast<std::size_t>(expectedWidth),
                4U,
                rowBytes) ||
            !checkedMultiply(
                rowBytes,
                static_cast<std::size_t>(expectedHeight),
                rgbaBytes) ||
            level.level != levelNumber ||
            level.width != expectedWidth ||
            level.height != expectedHeight ||
            level.bytesPerRow != rowBytes ||
            level.rgbaBytes != rgbaBytes ||
            image.width != expectedWidth ||
            image.height != expectedHeight ||
            image.pixels.size() != rgbaBytes ||
            !fitsNSUInteger(rowBytes) ||
            !fitsNSUInteger(rgbaBytes) ||
            !checkedAdd64(decodedBytes, level.rgbaBytes, decodedBytes)) {
            return false;
        }
    }
    if (decodedBytes != upload.decodedRgbaBytes ||
        decodedBytes != upload.uploadRgbaBytes) {
        return false;
    }

    std::uint64_t residentBytes = 0U;
    auto residentWidth = baseWidth;
    auto residentHeight = baseHeight;
    for (std::uint32_t level = 0U;
         level < upload.allocatedMipCount;
         ++level) {
        std::size_t rowBytes = 0U;
        std::size_t levelBytes = 0U;
        if (!checkedMultiply(
                static_cast<std::size_t>(residentWidth),
                4U,
                rowBytes) ||
            !checkedMultiply(
                rowBytes,
                static_cast<std::size_t>(residentHeight),
                levelBytes) ||
            !checkedAdd64(residentBytes, levelBytes, residentBytes)) {
            return false;
        }
        residentWidth = std::max(1U, residentWidth >> 1U);
        residentHeight = std::max(1U, residentHeight >> 1U);
    }
    std::size_t residentSize = 0U;
    return residentBytes == upload.residentRgbaBytes &&
        uint64ToSize(residentBytes, residentSize) &&
        accountGpuBytes(
            residentSize,
            aggregateGpuBytes,
            kMaximumPrivateRoomGpuBytes);
}

bool preflightPrivateRoom(
    id<MTLDevice> device,
    const airfix::content::LoadedWorldRoom& room,
    PrivateRoomPreflight& result) {
    if (room.revision.generation == 0U ||
        room.revision.pack.size == 0U ||
        !fitsNSUInteger(room.model.meshes.size()) ||
        !fitsNSUInteger(room.textures.size()) ||
        room.submission.meshUploads.size() != room.model.meshes.size()) {
        return false;
    }

    const auto regenerated = airfix::render::buildDrawSubmissionPlan(
        room.model, room.textures.size());
    if (!regenerated.plan.has_value() ||
        !regenerated.issues.empty() ||
        regenerated.plan->meshUploads != room.submission.meshUploads ||
        regenerated.plan->commands != room.submission.commands) {
        return false;
    }

    const auto maximumBufferLength =
        static_cast<std::size_t>(device.maxBufferLength);
    result.vertexByteCounts.reserve(room.submission.meshUploads.size());
    result.indexByteCounts.reserve(room.submission.meshUploads.size());
    for (std::size_t uploadIndex = 0U;
         uploadIndex < room.submission.meshUploads.size();
         ++uploadIndex) {
        const auto& upload = room.submission.meshUploads[uploadIndex];
        if (static_cast<std::size_t>(upload.meshSlot) != uploadIndex ||
            uploadIndex >= room.model.meshes.size()) {
            return false;
        }
        const auto& mesh = room.model.meshes[upload.meshSlot];
        std::size_t vertexBytes = 0U;
        std::size_t indexBytes = 0U;
        if (upload.vertexCount != mesh.vertices.size() ||
            upload.indexCount != mesh.indices.size() ||
            !checkedMultiply(
                mesh.vertices.size(), sizeof(GpuVertex), vertexBytes) ||
            !checkedMultiply(
                mesh.indices.size(), sizeof(std::uint32_t), indexBytes) ||
            vertexBytes > maximumBufferLength ||
            indexBytes > maximumBufferLength ||
            !accountGpuBytes(
                vertexBytes,
                result.aggregateGpuBytes,
                kMaximumPrivateRoomGpuBytes) ||
            !accountGpuBytes(
                indexBytes,
                result.aggregateGpuBytes,
                kMaximumPrivateRoomGpuBytes) ||
            !accountCpuPackedBytes(
                vertexBytes,
                result.aggregateCpuPackedBytes,
                kMaximumPrivateRoomCpuPackedBytes)) {
            return false;
        }
        result.vertexByteCounts.push_back(vertexBytes);
        result.indexByteCounts.push_back(indexBytes);
    }

    result.indexOffsets.reserve(room.submission.commands.size());
    for (const auto& command : room.submission.commands) {
        if (command.instanceIndex >= room.model.instances.size() ||
            static_cast<std::size_t>(command.meshSlot) >=
                result.indexByteCounts.size() ||
            room.model.instances[command.instanceIndex].meshSlot !=
                command.meshSlot) {
            return false;
        }
        const auto& mesh = room.model.meshes[command.meshSlot];
        if (command.rangeIndex >= mesh.ranges.size()) {
            return false;
        }
        const auto& range = mesh.ranges[command.rangeIndex];
        if (command.firstIndex != range.firstIndex ||
            command.indexCount != range.indexCount ||
            command.materialSlot != range.materialSlot ||
            command.texcoordMode != range.texcoordMode ||
            command.indexCount == 0U ||
            result.vertexByteCounts[command.meshSlot] == 0U ||
            result.indexByteCounts[command.meshSlot] == 0U) {
            return false;
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
            offset >= result.indexByteCounts[command.meshSlot] ||
            drawEnd > result.indexByteCounts[command.meshSlot] ||
            !fitsNSUInteger(offset)) {
            return false;
        }
        result.indexOffsets.push_back(static_cast<NSUInteger>(offset));
    }

    for (std::size_t textureIndex = 0U;
         textureIndex < room.textures.size();
         ++textureIndex) {
        if (!validateTextureAsset(
                room.textures[textureIndex],
                textureIndex,
                result.aggregateGpuBytes)) {
            return false;
        }
    }
    return result.indexOffsets.size() == room.submission.commands.size();
}

} // namespace

@interface AirfixMetalRenderer ()
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLSamplerState> samplerState;
@property(nonatomic, strong) id<MTLTexture> fallbackTexture;
@property(atomic, strong) AirfixMetalRoomSnapshot* roomSnapshot;
@property(nonatomic, strong) NSObject* preparationOwnerToken;
@property(nonatomic, strong) dispatch_queue_t resourceReleaseQueue;
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
    NSObject* preparationOwnerToken = [[NSObject alloc] init];
    if (preparationOwnerToken == nil) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::unexpectedFailure,
                @"Metal room preparation ownership could not be created.");
        }
        return nil;
    }
    dispatch_queue_t resourceReleaseQueue =
        dispatch_queue_create(
            "com.tryk016.airfixdogfighter.metal-resource-release",
            DISPATCH_QUEUE_SERIAL);
    if (resourceReleaseQueue == nil) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::unexpectedFailure,
                @"Metal resource release queue could not be created.");
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
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNearest;
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
    NSArray<id<MTLTexture>>* textureSnapshot = @[ syntheticTexture ];
    std::size_t snapshotAllocatedGpuBytes = 0U;
    for (AirfixMetalMeshBuffers* buffers in meshBufferSnapshot) {
        if ((buffers.vertexBuffer != nil &&
                (buffers.vertexBuffer.allocatedSize == 0U ||
                 !accountGpuBytes(
                     static_cast<std::size_t>(
                         buffers.vertexBuffer.allocatedSize),
                     snapshotAllocatedGpuBytes))) ||
            (buffers.indexBuffer != nil &&
                (buffers.indexBuffer.allocatedSize == 0U ||
                 !accountGpuBytes(
                     static_cast<std::size_t>(
                         buffers.indexBuffer.allocatedSize),
                     snapshotAllocatedGpuBytes)))) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"Allocated public mesh buffers exceed the Metal budget.");
            }
            return nil;
        }
    }
    if (syntheticTexture.allocatedSize == 0U ||
        !accountGpuBytes(
            static_cast<std::size_t>(syntheticTexture.allocatedSize),
            snapshotAllocatedGpuBytes)) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The allocated public texture exceeds the Metal budget.");
        }
        return nil;
    }
    std::size_t totalAllocatedGpuBytes = snapshotAllocatedGpuBytes;
    if (fallbackTexture.allocatedSize == 0U ||
        !accountGpuBytes(
            static_cast<std::size_t>(fallbackTexture.allocatedSize),
            totalAllocatedGpuBytes)) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The allocated fallback texture exceeds the Metal budget.");
        }
        return nil;
    }
    AirfixMetalRoomResources* roomResources =
        [[AirfixMetalRoomResources alloc] init];
    AirfixMetalRoomSnapshot* roomSnapshot =
        [[AirfixMetalRoomSnapshot alloc] init];
    if (roomResources == nil || roomSnapshot == nil) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::unexpectedFailure,
                @"The public Metal smoke-test snapshot could not be created.");
        }
        return nil;
    }
    roomResources->_payload = std::move(payload);
    roomResources->_submissionPlan = std::move(submissionPlan);
    roomResources->_indexOffsets = std::move(indexOffsets);
    roomResources.meshBuffers = meshBufferSnapshot;
    roomResources.textures = textureSnapshot;
    roomSnapshot->_resources = roomResources;
    roomSnapshot->_releaseQueue = resourceReleaseQueue;
    roomSnapshot->_allocatedGpuBytes = snapshotAllocatedGpuBytes;
    roomSnapshot->_worldRoomInstalled = NO;

    // Publish only after the complete renderer candidate has been validated
    // and every Metal object has been created successfully.
    self.commandQueue = commandQueue;
    self.pipelineState = pipelineState;
    self.depthState = depthState;
    self.samplerState = samplerState;
    self.fallbackTexture = fallbackTexture;
    self.roomSnapshot = roomSnapshot;
    self.preparationOwnerToken = preparationOwnerToken;
    self.resourceReleaseQueue = resourceReleaseQueue;

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

- (BOOL)worldRoomInstalled {
    AirfixMetalRoomSnapshot* snapshot = self.roomSnapshot;
    return snapshot != nil && snapshot->_worldRoomInstalled;
}

- (nullable AirfixPreparedMetalRoom*)prepareLoadedRoom:
    (airfix::content::LoadedWorldRoom&&)room
    error:(NSError* _Nullable* _Nullable)error {
    if (error != nullptr) {
        *error = nil;
    }
    if (NSThread.isMainThread) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::wrongThread,
                @"Private room resources must be prepared off the main thread.");
        }
        return nil;
    }

    // The old atomic snapshot remains untouched until this entire candidate,
    // including generated mips, is complete.
    try {
        id<MTLCommandQueue> commandQueue = self.commandQueue;
        id<MTLDevice> device = commandQueue.device;
        dispatch_queue_t resourceReleaseQueue =
            self.resourceReleaseQueue;
        if (commandQueue == nil || device == nil ||
            resourceReleaseQueue == nil) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::missingDevice,
                    @"Metal is unavailable while preparing the private room.");
            }
            return nil;
        }

        PrivateRoomPreflight preflight;
        if (!preflightPrivateRoom(device, room, preflight)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::invalidPayload,
                    @"The private room failed the bounded Metal snapshot contract.");
            }
            return nil;
        }

        // Reject work that is already known to exceed the transition policy
        // before creating any candidate Metal resource. This is a logical
        // byte preflight; allocatedSize is still checked after every actual
        // allocation and again against the current snapshot at publication.
        std::size_t logicalTransitionGpuBytes = 0U;
        @autoreleasepool {
            AirfixMetalRoomSnapshot* publishedSnapshot =
                self.roomSnapshot;
            if (publishedSnapshot != nil) {
                logicalTransitionGpuBytes =
                    publishedSnapshot->_allocatedGpuBytes;
            }
        }
        if (!accountGpuBytes(
                preflight.aggregateGpuBytes,
                logicalTransitionGpuBytes,
                kMaximumSnapshotTransitionGpuBytes)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The current and candidate room exceed the logical Metal transition budget.");
            }
            return nil;
        }

        NSMutableArray<AirfixMetalMeshBuffers*>* meshBuffers =
            [NSMutableArray
                arrayWithCapacity:
                    static_cast<NSUInteger>(room.model.meshes.size())];
        if (meshBuffers == nil) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::bufferCreation,
                    @"Private room mesh ownership could not be allocated.");
            }
            return nil;
        }
        std::size_t allocatedGpuBytes = 0U;
        for (std::size_t meshSlot = 0U;
             meshSlot < room.model.meshes.size();
             ++meshSlot) {
            const auto& mesh = room.model.meshes[meshSlot];
            auto packedVertices = repackVertices(mesh.vertices);
            std::size_t packedBytes = 0U;
            if (!checkedMultiply(
                    packedVertices.size(),
                    sizeof(GpuVertex),
                    packedBytes) ||
                packedBytes != preflight.vertexByteCounts[meshSlot]) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::resourceLimit,
                        @"Private room vertex packing changed after preflight.");
                }
                return nil;
            }

            id<MTLBuffer> vertexBuffer = nil;
            if (packedBytes != 0U) {
                vertexBuffer =
                    [device newBufferWithBytes:packedVertices.data()
                                       length:packedBytes
                                      options:MTLResourceStorageModeShared];
            }
            id<MTLBuffer> indexBuffer = nil;
            const auto indexBytes = preflight.indexByteCounts[meshSlot];
            if (indexBytes != 0U) {
                indexBuffer =
                    [device newBufferWithBytes:mesh.indices.data()
                                       length:indexBytes
                                      options:MTLResourceStorageModeShared];
            }
            if ((packedBytes != 0U && vertexBuffer == nil) ||
                (indexBytes != 0U && indexBuffer == nil)) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::bufferCreation,
                        @"Metal could not create every private room mesh buffer.");
                }
                return nil;
            }
            if ((vertexBuffer != nil &&
                    (vertexBuffer.allocatedSize == 0U ||
                     !accountGpuBytes(
                         static_cast<std::size_t>(
                             vertexBuffer.allocatedSize),
                         allocatedGpuBytes,
                         kMaximumPrivateRoomGpuBytes))) ||
                (indexBuffer != nil &&
                    (indexBuffer.allocatedSize == 0U ||
                     !accountGpuBytes(
                         static_cast<std::size_t>(
                             indexBuffer.allocatedSize),
                         allocatedGpuBytes,
                         kMaximumPrivateRoomGpuBytes)))) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::resourceLimit,
                        @"Allocated private mesh buffers exceed the Metal budget.");
                }
                return nil;
            }
            AirfixMetalMeshBuffers* buffers =
                [[AirfixMetalMeshBuffers alloc] init];
            if (buffers == nil) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::bufferCreation,
                        @"Private room mesh ownership could not be created.");
                }
                return nil;
            }
            buffers.vertexBuffer = vertexBuffer;
            buffers.indexBuffer = indexBuffer;
            [meshBuffers addObject:buffers];
        }

        NSMutableArray<id<MTLTexture>>* textures =
            [NSMutableArray
                arrayWithCapacity:
                    static_cast<NSUInteger>(room.textures.size())];
        NSMutableArray<id<MTLTexture>>* generatedMipTextures =
            [NSMutableArray array];
        if (textures == nil || generatedMipTextures == nil) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::textureCreation,
                    @"Private texture ownership could not be allocated.");
            }
            return nil;
        }
        for (const auto& source : room.textures) {
            const auto& upload = source.upload;
            const auto& base = upload.uploadLevels.front();
            MTLTextureDescriptor* descriptor =
                [[MTLTextureDescriptor alloc] init];
            if (descriptor == nil) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::textureCreation,
                        @"Private texture metadata could not be allocated.");
                }
                return nil;
            }
            descriptor.textureType = MTLTextureType2D;
            descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
            descriptor.width = static_cast<NSUInteger>(base.width);
            descriptor.height = static_cast<NSUInteger>(base.height);
            descriptor.depth = 1U;
            descriptor.mipmapLevelCount =
                static_cast<NSUInteger>(upload.allocatedMipCount);
            descriptor.sampleCount = 1U;
            descriptor.arrayLength = 1U;
            descriptor.usage = MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModeShared;

            id<MTLTexture> texture =
                [device newTextureWithDescriptor:descriptor];
            if (texture == nil ||
                texture.width != descriptor.width ||
                texture.height != descriptor.height ||
                texture.mipmapLevelCount !=
                    descriptor.mipmapLevelCount ||
                texture.allocatedSize == 0U ||
                !accountGpuBytes(
                    static_cast<std::size_t>(texture.allocatedSize),
                    allocatedGpuBytes,
                    kMaximumPrivateRoomGpuBytes)) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::textureCreation,
                        @"Metal could not create the complete private texture.");
                }
                return nil;
            }

            for (std::size_t levelIndex = 0U;
                 levelIndex < source.uploadLevels.size();
                 ++levelIndex) {
                const auto& level = upload.uploadLevels[levelIndex];
                const auto& image = source.uploadLevels[levelIndex];
                [texture
                    replaceRegion:MTLRegionMake2D(
                        0U,
                        0U,
                        static_cast<NSUInteger>(image.width),
                        static_cast<NSUInteger>(image.height))
                    mipmapLevel:static_cast<NSUInteger>(level.level)
                    withBytes:image.pixels.data()
                    bytesPerRow:
                        static_cast<NSUInteger>(level.bytesPerRow)];
            }
            if (upload.mipPolicy ==
                airfix::render::GtiMipPolicy::generateFromBase) {
                [generatedMipTextures addObject:texture];
            }
            [textures addObject:texture];
        }

        if (generatedMipTextures.count != 0U) {
            id<MTLCommandBuffer> mipCommandBuffer =
                [commandQueue commandBuffer];
            if (mipCommandBuffer == nil) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::blitCreation,
                        @"Metal could not create the private mip command buffer.");
                }
                return nil;
            }
            mipCommandBuffer.label = @"Airfix private texture mip generation";
            id<MTLBlitCommandEncoder> blit =
                [mipCommandBuffer blitCommandEncoder];
            if (blit == nil) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::blitCreation,
                        @"Metal could not create the private mip encoder.");
                }
                return nil;
            }
            for (id<MTLTexture> texture in generatedMipTextures) {
                [blit generateMipmapsForTexture:texture];
            }
            [blit endEncoding];
            [mipCommandBuffer commit];
            [mipCommandBuffer waitUntilCompleted];
            if (mipCommandBuffer.status !=
                MTLCommandBufferStatusCompleted) {
                if (error != nullptr) {
                    NSString* reason =
                        mipCommandBuffer.error.localizedDescription;
                    if (reason == nil) {
                        reason = @"unknown Metal mip-generation failure";
                    }
                    *error = makeError(
                        RendererError::mipGeneration,
                        [@"Private texture mip generation failed: "
                            stringByAppendingString:reason]);
                }
                return nil;
            }
        }

        AirfixMetalRoomResources* candidateResources =
            [[AirfixMetalRoomResources alloc] init];
        AirfixMetalRoomSnapshot* candidate =
            [[AirfixMetalRoomSnapshot alloc] init];
        AirfixPreparedMetalRoom* preparedRoom =
            [[AirfixPreparedMetalRoom alloc] init];
        NSObject* ownerToken = self.preparationOwnerToken;
        if (candidateResources == nil || candidate == nil ||
            preparedRoom == nil ||
            ownerToken == nil) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::unexpectedFailure,
                    @"Private Metal room ownership could not be created.");
            }
            return nil;
        }
        NSArray<AirfixMetalMeshBuffers*>* meshBufferSnapshot =
            [meshBuffers copy];
        NSArray<id<MTLTexture>>* textureSnapshot = [textures copy];
        if (meshBufferSnapshot == nil || textureSnapshot == nil ||
            meshBufferSnapshot.count != room.model.meshes.size() ||
            textureSnapshot.count != room.textures.size()) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::unexpectedFailure,
                    @"Private Metal resource ownership is incomplete.");
            }
            return nil;
        }
        candidateResources.meshBuffers = meshBufferSnapshot;
        candidateResources.textures = textureSnapshot;
        preparedRoom->_ownerToken = ownerToken;
        preparedRoom->_device = device;
        preparedRoom->_published = NO;
        candidateResources->_indexOffsets =
            std::move(preflight.indexOffsets);
        candidateResources->_revision = std::move(room.revision);
        candidateResources->_payload = std::move(room.model);
        candidateResources->_submissionPlan =
            std::move(room.submission);
        candidate->_resources = candidateResources;
        candidate->_releaseQueue = resourceReleaseQueue;
        candidate->_allocatedGpuBytes = allocatedGpuBytes;
        candidate->_worldRoomInstalled = YES;

        // Texture upload bytes are intentionally not retained by the native
        // snapshot after Metal owns the complete resource set.
        std::vector<airfix::content::LoadedTextureAsset>().swap(
            room.textures);

        preparedRoom->_snapshot = candidate;
        return preparedRoom;
    }
    catch (...) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::unexpectedFailure,
                @"The private Metal room snapshot could not be prepared.");
        }
        return nil;
    }
}

- (BOOL)publishPreparedRoom:(AirfixPreparedMetalRoom*)preparedRoom
                      error:(NSError* _Nullable* _Nullable)error {
    if (error != nullptr) {
        *error = nil;
    }
    if (!NSThread.isMainThread) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::wrongThread,
                @"Prepared Metal rooms must be published on the main thread.");
        }
        return NO;
    }
    if (preparedRoom == nil) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::invalidPreparedRoom,
                @"The prepared Metal room is missing.");
        }
        return NO;
    }
    if (preparedRoom->_published) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::preparedRoomAlreadyPublished,
                @"The prepared Metal room was already published.");
        }
        return NO;
    }
    if (preparedRoom->_ownerToken != self.preparationOwnerToken ||
        preparedRoom->_device != self.commandQueue.device ||
        preparedRoom->_snapshot == nil ||
        preparedRoom->_snapshot->_resources == nil ||
        !preparedRoom->_snapshot->_worldRoomInstalled) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::invalidPreparedRoom,
                @"The prepared Metal room belongs to a different renderer or device.");
        }
        return NO;
    }

    AirfixMetalRoomSnapshot* previousSnapshot = self.roomSnapshot;
    std::size_t transitionGpuBytes =
        previousSnapshot != nil
        ? previousSnapshot->_allocatedGpuBytes
        : 0U;
    if (!accountGpuBytes(
            preparedRoom->_snapshot->_allocatedGpuBytes,
            transitionGpuBytes,
            kMaximumSnapshotTransitionGpuBytes)) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The old and prepared Metal snapshots exceed the bounded transition budget.");
        }
        return NO;
    }

    AirfixMetalRoomSnapshot* candidate = preparedRoom->_snapshot;
    preparedRoom->_published = YES;
    preparedRoom->_snapshot = nil;
    // The coordinator has already revalidated serial and revision. Exactly
    // one atomic strong-pointer assignment now publishes the complete room.
    self.roomSnapshot = candidate;
    return YES;
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
}

- (void)drawInMTKView:(MTKView*)view {
    AirfixMetalRoomSnapshot* snapshot = self.roomSnapshot;
    if (snapshot == nil) {
        return;
    }
    AirfixMetalRoomResources* resources = snapshot->_resources;
    if (resources == nil) {
        return;
    }
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
         commandIndex < resources->_submissionPlan.commands.size();
         ++commandIndex) {
        const auto& command =
            resources->_submissionPlan.commands[commandIndex];
        const auto& instance =
            resources->_payload.instances[command.instanceIndex];
        const simd_float4x4 model =
            toSimdMatrix(instance.modelLinear, instance.modelTranslation);
        const GpuUniforms uniforms{simd_mul(viewport, model)};
        AirfixMetalMeshBuffers* buffers =
            resources.meshBuffers[command.meshSlot];
        [encoder setVertexBuffer:buffers.vertexBuffer
                         offset:0U
                        atIndex:0U];
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1U];

        id<MTLTexture> texture = self.fallbackTexture;
        if (command.texcoordMode == airfix::render::TexcoordMode::uv0 &&
            command.primary.has_value()) {
            const auto assetIndex =
                static_cast<NSUInteger>(command.primary->value);
            if (assetIndex < resources.textures.count) {
                // Asset zero is an ordinary dense texture. Missing primary
                // data and TexcoordMode::none remain distinct and use fallback.
                texture = resources.textures[assetIndex];
            }
        }
        [encoder setFragmentTexture:texture atIndex:0U];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:static_cast<NSUInteger>(command.indexCount)
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:buffers.indexBuffer
                     indexBufferOffset:
                         resources->_indexOffsets[commandIndex]];
    }

    [encoder endEncoding];
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        (void)completed;
        // Retain the immutable resource owner explicitly until this GPU
        // submission no longer references its buffers or textures.
        (void)snapshot;
    }];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
