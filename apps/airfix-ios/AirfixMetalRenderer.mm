#import "AirfixMetalRenderer.h"

#import <Metal/Metal.h>
#import <simd/simd.h>

#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/content/MissionWorldRoomPublication.hpp"
#include "airfix/render/DrawModel.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/PlayerActorPoseFrame.hpp"
#include "airfix/render/SnapshotGpuBudgetLedger.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
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

constexpr std::size_t kMaximumSyntheticLogicalGpuBytes =
    64U * 1024U * 1024U;
constexpr std::size_t kMaximumSyntheticGpuHeapPlanBytes =
    64U * 1024U * 1024U;
constexpr std::size_t kMaximumSyntheticCpuPackedBytes =
    64U * 1024U * 1024U;
constexpr std::size_t kMaximumPrivateRoomLogicalGpuBytes =
    256U * 1024U * 1024U;
constexpr std::size_t kMaximumPrivateRoomGpuHeapPlanBytes =
    256U * 1024U * 1024U;
constexpr std::size_t kMaximumPrivateRoomCpuPackedBytes =
    128U * 1024U * 1024U;
constexpr MTLResourceOptions kSharedTrackedResourceOptions =
    static_cast<MTLResourceOptions>(
        MTLResourceStorageModeShared |
        MTLResourceHazardTrackingModeTracked);

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
    const std::size_t maximumBytes) noexcept {
    std::size_t next = 0U;
    if (!checkedAdd(aggregateBytes, bytes, next) ||
        next > maximumBytes) {
        return false;
    }
    aggregateBytes = next;
    return true;
}

bool accountHeapResourcePlacement(
    const MTLSizeAndAlign allocation,
    std::size_t& heapBytes,
    std::size_t& maximumAlignment,
    const std::size_t maximumBytes) noexcept {
    const auto size = static_cast<std::size_t>(allocation.size);
    const auto alignment =
        static_cast<std::size_t>(allocation.align);
    if (size == 0U || alignment == 0U) {
        return false;
    }
    const auto remainder = heapBytes % alignment;
    std::size_t alignedOffset = heapBytes;
    if (remainder != 0U &&
        !checkedAdd(
            heapBytes,
            alignment - remainder,
            alignedOffset)) {
        return false;
    }
    std::size_t end = 0U;
    if (!checkedAdd(alignedOffset, size, end) ||
        end > maximumBytes) {
        return false;
    }
    heapBytes = end;
    maximumAlignment =
        std::max(maximumAlignment, alignment);
    return true;
}

bool finalizeHeapPlan(
    std::size_t& heapBytes,
    const std::size_t maximumAlignment,
    const std::size_t maximumBytes) noexcept {
    if (heapBytes == 0U) {
        return maximumAlignment == 0U;
    }
    if (maximumAlignment == 0U) {
        return false;
    }
    const auto remainder = heapBytes % maximumAlignment;
    if (remainder != 0U &&
        !checkedAdd(
            heapBytes,
            maximumAlignment - remainder,
            heapBytes)) {
        return false;
    }
    return heapBytes <= maximumBytes;
}

bool fitsNSUInteger(std::size_t value) noexcept;

id<MTLHeap> newSharedTrackedHeap(
    id<MTLDevice> device,
    const std::size_t bytes,
    NSString* label) {
    if (bytes == 0U || !fitsNSUInteger(bytes)) {
        return nil;
    }
    MTLHeapDescriptor* descriptor =
        [[MTLHeapDescriptor alloc] init];
    if (descriptor == nil) {
        return nil;
    }
    descriptor.type = MTLHeapTypeAutomatic;
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.cpuCacheMode =
        MTLCPUCacheModeDefaultCache;
    descriptor.hazardTrackingMode =
        MTLHazardTrackingModeTracked;
    descriptor.size = static_cast<NSUInteger>(bytes);
    id<MTLHeap> heap =
        [device newHeapWithDescriptor:descriptor];
    // Metal may page-round descriptor.size, and iOS exposes no documented
    // pre-creation upper bound for that rounding. The caller immediately
    // measures currentAllocatedSize and obtains any supplemental admission.
    if (heap == nil ||
        heap.size == 0U ||
        heap.size < static_cast<NSUInteger>(bytes) ||
        heap.type != MTLHeapTypeAutomatic ||
        heap.storageMode != MTLStorageModeShared ||
        heap.cpuCacheMode !=
            MTLCPUCacheModeDefaultCache ||
        heap.hazardTrackingMode !=
            MTLHazardTrackingModeTracked) {
        return nil;
    }
    heap.label = label;
    return heap;
}

bool accountCurrentHeapAllocation(
    id<MTLHeap> heap,
    std::size_t& aggregateBytes) noexcept {
    if (heap == nil ||
        heap.currentAllocatedSize == 0U ||
        heap.currentAllocatedSize > heap.size) {
        return false;
    }
    return checkedAdd(
        aggregateBytes,
        static_cast<std::size_t>(heap.currentAllocatedSize),
        aggregateBytes);
}

bool finalizeHeapAllocationReservation(
    airfix::render::SnapshotGpuBudgetLedger& ledger,
    airfix::render::SnapshotGpuBudgetReservation& reservation,
    const std::size_t currentAllocatedBytes) noexcept {
    if (currentAllocatedBytes <= reservation.bytes()) {
        return reservation.reconcile(currentAllocatedBytes);
    }
    const auto supplementalBytes =
        currentAllocatedBytes - reservation.bytes();
    auto supplement = ledger.tryReserve(supplementalBytes);
    return supplement.has_value() &&
        reservation.absorb(std::move(*supplement));
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

[[nodiscard]] airfix::render::ConvertedNodeTransform actorWorldFrom(
    const airfix::simulation::PlayerSpawnPose& pose) noexcept {
    const auto vectorAt = [](const std::array<float, 3U>& value) {
        return airfix::render::Vec3{value[0], value[1], value[2]};
    };
    return {
        .linear =
            {
                .columns =
                    {
                        vectorAt(pose.runtimeWorldRotationColumns[0]),
                        vectorAt(pose.runtimeWorldRotationColumns[1]),
                        vectorAt(pose.runtimeWorldRotationColumns[2]),
                    },
            },
        .translation = vectorAt(pose.runtimeWorldPosition),
        .rawScalar = 1.0F,
    };
}

[[nodiscard]] bool sameFloatBits(
    const float left,
    const float right) noexcept {
    return std::bit_cast<std::uint32_t>(left) ==
        std::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool sameVecBits(
    const airfix::render::Vec3& left,
    const airfix::render::Vec3& right) noexcept {
    return sameFloatBits(left.x, right.x) &&
        sameFloatBits(left.y, right.y) &&
        sameFloatBits(left.z, right.z);
}

[[nodiscard]] bool sameMatBits(
    const airfix::render::Mat3& left,
    const airfix::render::Mat3& right) noexcept {
    return sameVecBits(left.columns[0], right.columns[0]) &&
        sameVecBits(left.columns[1], right.columns[1]) &&
        sameVecBits(left.columns[2], right.columns[2]);
}

// This pilot has one immutable scene identity and one initial publication.
// The object is deliberately immovable so its handle remains embedded at one
// stable address for the complete native snapshot lifetime.
class ScenePoseRuntime final {
public:
    ScenePoseRuntime(const ScenePoseRuntime&) = delete;
    ScenePoseRuntime& operator=(const ScenePoseRuntime&) = delete;
    ScenePoseRuntime(ScenePoseRuntime&&) = delete;
    ScenePoseRuntime& operator=(ScenePoseRuntime&&) = delete;

    [[nodiscard]] static std::unique_ptr<ScenePoseRuntime> create(
        const airfix::render::DynamicInstancePoseLimits& limits,
        const airfix::render::PlayerActorPoseFrame& frame) {
        auto runtime = std::unique_ptr<ScenePoseRuntime>(
            new ScenePoseRuntime(limits));
        auto scene =
            runtime->_exchange.tryBeginScene(limits.maximumInstances);
        if (!scene.has_value()) {
            return nullptr;
        }
        runtime->_scene.emplace(std::move(*scene));
        if (runtime->_exchange.tryPublish(
                *runtime->_scene,
                frame.frameView()) !=
            airfix::render::DynamicInstancePosePublishResult::published) {
            return nullptr;
        }
        return runtime;
    }

    [[nodiscard]] std::optional<
        airfix::render::DynamicInstancePoseLease>
    tryAcquire() noexcept {
        if (!_scene.has_value()) {
            return std::nullopt;
        }
        return _exchange.tryAcquire(*_scene);
    }

private:
    explicit ScenePoseRuntime(
        const airfix::render::DynamicInstancePoseLimits& limits)
        : _exchange(limits) {}

    airfix::render::ScenePoseExchange _exchange;
    std::optional<airfix::render::ScenePoseHandle> _scene;
};

enum class ScenePoseRuntimePreparationStatus : std::uint8_t {
    noPlayer,
    ready,
    invalidPayload,
    resourceLimit,
};

struct ScenePoseRuntimePreparation {
    ScenePoseRuntimePreparationStatus status{
        ScenePoseRuntimePreparationStatus::noPlayer};
    std::unique_ptr<ScenePoseRuntime> runtime;
};

struct ScenePoseRuntimePlan {
    ScenePoseRuntimePreparationStatus status{
        ScenePoseRuntimePreparationStatus::noPlayer};
    airfix::render::DynamicInstancePoseLimits exactLimits{};
    std::size_t retainedPoseBytes{};
};

[[nodiscard]] ScenePoseRuntimePlan planScenePoseRuntime(
    const airfix::content::LoadedMissionWorldRoom& room) noexcept {
    if (!room.playerActorBinding.has_value()) {
        return {};
    }

    const airfix::render::DynamicInstancePoseLimits platformCeiling;
    if (room.model.instances.size() >
            static_cast<std::size_t>(platformCeiling.maximumInstances) ||
        room.model.instances.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        return {
            .status =
                ScenePoseRuntimePreparationStatus::resourceLimit,
        };
    }

    const auto& binding = *room.playerActorBinding;
    std::size_t frameBytes = 0U;
    if (binding.instanceCount > platformCeiling.maximumOverrides ||
        !checkedMultiply(
            binding.instanceCount,
            sizeof(airfix::render::DynamicInstancePoseOverride),
            frameBytes) ||
        frameBytes > platformCeiling.maximumFrameBytes) {
        return {
            .status =
                ScenePoseRuntimePreparationStatus::resourceLimit,
        };
    }

    std::size_t retainedPoseBytes = 0U;
    if (!checkedMultiply(frameBytes, 2U, retainedPoseBytes)) {
        return {
            .status =
                ScenePoseRuntimePreparationStatus::resourceLimit,
        };
    }

    return {
        .status = ScenePoseRuntimePreparationStatus::ready,
        .exactLimits =
            {
                .maximumInstances = static_cast<std::uint32_t>(
                    room.model.instances.size()),
                .maximumOverrides = binding.instanceCount,
                .maximumFrameBytes = frameBytes,
            },
        .retainedPoseBytes = retainedPoseBytes,
    };
}

[[nodiscard]] ScenePoseRuntimePreparation prepareScenePoseRuntime(
    const airfix::content::LoadedMissionWorldRoom& room,
    const ScenePoseRuntimePlan& plan) noexcept {
    if (plan.status == ScenePoseRuntimePreparationStatus::noPlayer) {
        return {};
    }
    if (plan.status != ScenePoseRuntimePreparationStatus::ready) {
        return {
            plan.status,
            nullptr,
        };
    }
    if (!room.playerActorBinding.has_value()) {
        return {
            ScenePoseRuntimePreparationStatus::invalidPayload,
            nullptr,
        };
    }
    const auto& binding = *room.playerActorBinding;

    try {
        auto frame = airfix::render::buildPlayerActorPoseFrame(
            room.playerActorBinding,
            room.playerActorInstanceProvenance,
            actorWorldFrom(room.playerSpawnPose),
            0U,
            plan.exactLimits);
        if (!frame.complete() ||
            frame.overrides.size() != binding.instanceCount) {
            return {
                ScenePoseRuntimePreparationStatus::invalidPayload,
                nullptr,
            };
        }

        for (std::size_t index = 0U;
             index < frame.overrides.size();
             ++index) {
            const auto& poseOverride = frame.overrides[index];
            std::size_t expectedInstanceIndex = 0U;
            if (!checkedAdd(
                    binding.firstInstanceIndex,
                    index,
                    expectedInstanceIndex) ||
                expectedInstanceIndex >= room.model.instances.size() ||
                static_cast<std::size_t>(poseOverride.instanceIndex) !=
                    expectedInstanceIndex) {
                return {
                    ScenePoseRuntimePreparationStatus::invalidPayload,
                    nullptr,
                };
            }
            const auto& authored =
                room.model.instances[expectedInstanceIndex];
            if (!sameMatBits(
                    poseOverride.modelLinear, authored.modelLinear) ||
                !sameVecBits(
                    poseOverride.modelTranslation,
                    authored.modelTranslation)) {
                return {
                    ScenePoseRuntimePreparationStatus::invalidPayload,
                    nullptr,
                };
            }
        }

        auto runtime =
            ScenePoseRuntime::create(plan.exactLimits, frame);
        if (runtime == nullptr) {
            return {
                ScenePoseRuntimePreparationStatus::invalidPayload,
                nullptr,
            };
        }
        return {
            ScenePoseRuntimePreparationStatus::ready,
            std::move(runtime),
        };
    } catch (const std::bad_alloc&) {
        return {
            ScenePoseRuntimePreparationStatus::resourceLimit,
            nullptr,
        };
    } catch (const std::length_error&) {
        return {
            ScenePoseRuntimePreparationStatus::resourceLimit,
            nullptr,
        };
    } catch (...) {
        return {
            ScenePoseRuntimePreparationStatus::invalidPayload,
            nullptr,
        };
    }
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
    std::optional<airfix::content::LoadedMissionWorldRoom> _missionRoom;
    std::vector<NSUInteger> _indexOffsets;
    std::unique_ptr<ScenePoseRuntime> _scenePoseRuntime;
}
@property(nonatomic, strong) NSArray<AirfixMetalMeshBuffers*>* meshBuffers;
@property(nonatomic, strong) NSArray<id<MTLTexture>>* textures;
@property(nonatomic, strong) id<MTLHeap> bufferHeap;
@property(nonatomic, strong) id<MTLHeap> textureHeap;
@end

@implementation AirfixMetalRoomResources

- (void)dealloc {
    // Suballocated resources must release before their backing heaps. The
    // snapshot token is consumed only after this complete owner is destroyed.
    _scenePoseRuntime.reset();
    _meshBuffers = nil;
    _textures = nil;
    _bufferHeap = nil;
    _textureHeap = nil;
}

@end

@interface AirfixSnapshotGpuBudgetLedgerHolder : NSObject {
@public
    std::shared_ptr<airfix::render::SnapshotGpuBudgetLedger> _ledger;
}
@end

@implementation AirfixSnapshotGpuBudgetLedgerHolder
@end

@interface AirfixGpuBudgetReservationHolder : NSObject {
@public
    std::optional<airfix::render::SnapshotGpuBudgetReservation>
        _reservation;
}
@end

@implementation AirfixGpuBudgetReservationHolder
@end

@interface AirfixMetalRoomSnapshot : NSObject {
@public
    __strong AirfixMetalRoomResources* _resources;
    __strong dispatch_queue_t _releaseQueue;
    __strong AirfixGpuBudgetReservationHolder*
        _gpuBudgetReservationHolder;
    BOOL _worldRoomInstalled;
}
@end

@implementation AirfixMetalRoomSnapshot

- (void)dealloc {
    // Snapshot publication/discard is deliberately O(1) on the main thread.
    // Transfer the final ownership token to a serial worker queue so Metal
    // arrays and the potentially large C++ payload are destroyed off-main.
    // The exact GPU debit remains live until that destruction is complete.
    if (_resources != nil && _releaseQueue != nil) {
        AirfixGpuBudgetReservationHolder* reservationHolder =
            _gpuBudgetReservationHolder;
        void* retainedResources =
            (__bridge_retained void*)_resources;
        _resources = nil;
        _gpuBudgetReservationHolder = nil;
        dispatch_async(_releaseQueue, ^{
            @autoreleasepool {
                AirfixMetalRoomResources*
                    __attribute__((objc_precise_lifetime)) resources =
                    (__bridge_transfer AirfixMetalRoomResources*)
                        retainedResources;
                (void)resources;
            }
            if (reservationHolder != nil) {
                reservationHolder->_reservation.reset();
            }
        });
        return;
    }

    // Construction always installs a release queue before a reservation.
    // Keep the fallback path ordered as well if that invariant is broken.
    AirfixGpuBudgetReservationHolder* reservationHolder =
        _gpuBudgetReservationHolder;
    _gpuBudgetReservationHolder = nil;
    @autoreleasepool {
        AirfixMetalRoomResources*
            __attribute__((objc_precise_lifetime)) resources =
            _resources;
        _resources = nil;
        (void)resources;
    }
    if (reservationHolder != nil) {
        reservationHolder->_reservation.reset();
    }
}

@end

@interface AirfixBudgetedMetalTexture : NSObject {
@public
    __strong id<MTLTexture> _texture;
    __strong id<MTLHeap> _heap;
    __strong AirfixGpuBudgetReservationHolder*
        _gpuBudgetReservationHolder;
}
@end

@implementation AirfixBudgetedMetalTexture

- (void)dealloc {
    AirfixGpuBudgetReservationHolder* reservationHolder =
        _gpuBudgetReservationHolder;
    _gpuBudgetReservationHolder = nil;
    @autoreleasepool {
        id<MTLTexture> __attribute__((objc_precise_lifetime)) texture =
            _texture;
        _texture = nil;
        (void)texture;
    }
    @autoreleasepool {
        id<MTLHeap> __attribute__((objc_precise_lifetime)) heap =
            _heap;
        _heap = nil;
        (void)heap;
    }
    if (reservationHolder != nil) {
        reservationHolder->_reservation.reset();
    }
}

@end

namespace {

bool hasActiveGpuBudgetReservation(
    AirfixMetalRoomSnapshot* snapshot) noexcept {
    if (snapshot == nil ||
        snapshot->_gpuBudgetReservationHolder == nil) {
        return false;
    }
    const auto& reservation =
        snapshot->_gpuBudgetReservationHolder->_reservation;
    return reservation.has_value() && reservation->active();
}

} // namespace

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

void configurePrivateTextureDescriptor(
    MTLTextureDescriptor* descriptor,
    const airfix::content::LoadedTextureAsset& source) noexcept {
    const auto& upload = source.upload;
    const auto& base = upload.uploadLevels.front();
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
    descriptor.hazardTrackingMode =
        MTLHazardTrackingModeTracked;
}

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
            kMaximumPrivateRoomLogicalGpuBytes);
}

bool preflightPrivateRoom(
    id<MTLDevice> device,
    const airfix::content::LoadedMissionWorldRoom& room,
    PrivateRoomPreflight& result) {
    if (room.revision.generation == 0U ||
        room.revision.pack.size == 0U ||
        airfix::content::validateMissionWorldRoomPublication(
            room, room.revision).has_value() ||
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
                kMaximumPrivateRoomLogicalGpuBytes) ||
            !accountGpuBytes(
                indexBytes,
                result.aggregateGpuBytes,
                kMaximumPrivateRoomLogicalGpuBytes) ||
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
@property(nonatomic, strong) AirfixBudgetedMetalTexture* fallbackResource;
@property(atomic, strong) AirfixMetalRoomSnapshot* roomSnapshot;
@property(nonatomic, strong) NSObject* preparationOwnerToken;
@property(nonatomic, strong) dispatch_queue_t resourceReleaseQueue;
@property(nonatomic, strong)
    AirfixSnapshotGpuBudgetLedgerHolder* gpuBudgetHolder;
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
    AirfixSnapshotGpuBudgetLedgerHolder* gpuBudgetHolder =
        [[AirfixSnapshotGpuBudgetLedgerHolder alloc] init];
    if (gpuBudgetHolder == nil) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::unexpectedFailure,
                @"Metal snapshot GPU accounting could not be created.");
        }
        return nil;
    }
    gpuBudgetHolder->_ledger =
        std::make_shared<airfix::render::SnapshotGpuBudgetLedger>();
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
            !accountGpuBytes(
                vertexBytes,
                aggregateGpuBytes,
                kMaximumSyntheticLogicalGpuBytes) ||
            !accountGpuBytes(
                indexBytes,
                aggregateGpuBytes,
                kMaximumSyntheticLogicalGpuBytes) ||
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
        !accountGpuBytes(
            syntheticTextureBytes,
            aggregateGpuBytes,
            kMaximumSyntheticLogicalGpuBytes) ||
        !accountGpuBytes(
            fallbackTextureBytes,
            aggregateGpuBytes,
            kMaximumSyntheticLogicalGpuBytes)) {
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

    MTLTextureDescriptor* textureDescriptor =
        [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:2U
                                        height:2U
                                     mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModeShared;
    textureDescriptor.hazardTrackingMode =
        MTLHazardTrackingModeTracked;
    MTLTextureDescriptor* fallbackDescriptor =
        [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:1U
                                        height:1U
                                     mipmapped:NO];
    fallbackDescriptor.usage = MTLTextureUsageShaderRead;
    fallbackDescriptor.storageMode = MTLStorageModeShared;
    fallbackDescriptor.hazardTrackingMode =
        MTLHazardTrackingModeTracked;
    if (textureDescriptor == nil || fallbackDescriptor == nil) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"The public Metal texture descriptors could not be created.");
        }
        return nil;
    }

    std::size_t bufferHeapBytes = 0U;
    std::size_t bufferHeapAlignment = 0U;
    bool heapPlanValid = true;
    for (std::size_t meshSlot = 0U;
         meshSlot < vertexByteCounts.size();
         ++meshSlot) {
        if ((vertexByteCounts[meshSlot] != 0U &&
                !accountHeapResourcePlacement(
                    [device
                        heapBufferSizeAndAlignWithLength:
                            vertexByteCounts[meshSlot]
                        options:kSharedTrackedResourceOptions],
                    bufferHeapBytes,
                    bufferHeapAlignment,
                    kMaximumSyntheticGpuHeapPlanBytes)) ||
            (indexByteCounts[meshSlot] != 0U &&
                !accountHeapResourcePlacement(
                    [device
                        heapBufferSizeAndAlignWithLength:
                            indexByteCounts[meshSlot]
                        options:kSharedTrackedResourceOptions],
                    bufferHeapBytes,
                    bufferHeapAlignment,
                    kMaximumSyntheticGpuHeapPlanBytes))) {
            heapPlanValid = false;
            break;
        }
    }
    std::size_t textureHeapBytes = 0U;
    std::size_t textureHeapAlignment = 0U;
    std::size_t fallbackHeapBytes = 0U;
    std::size_t fallbackHeapAlignment = 0U;
    if (!heapPlanValid ||
        !accountHeapResourcePlacement(
            [device
                heapTextureSizeAndAlignWithDescriptor:
                    textureDescriptor],
            textureHeapBytes,
            textureHeapAlignment,
            kMaximumSyntheticGpuHeapPlanBytes) ||
        !accountHeapResourcePlacement(
            [device
                heapTextureSizeAndAlignWithDescriptor:
                    fallbackDescriptor],
            fallbackHeapBytes,
            fallbackHeapAlignment,
            kMaximumSyntheticGpuHeapPlanBytes) ||
        !finalizeHeapPlan(
            bufferHeapBytes,
            bufferHeapAlignment,
            kMaximumSyntheticGpuHeapPlanBytes) ||
        !finalizeHeapPlan(
            textureHeapBytes,
            textureHeapAlignment,
            kMaximumSyntheticGpuHeapPlanBytes) ||
        !finalizeHeapPlan(
            fallbackHeapBytes,
            fallbackHeapAlignment,
            kMaximumSyntheticGpuHeapPlanBytes)) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The public Metal heap plan exceeds its budget.");
        }
        return nil;
    }
    std::size_t admittedHeapPlanBytes = 0U;
    if (!accountGpuBytes(
            bufferHeapBytes,
            admittedHeapPlanBytes,
            kMaximumSyntheticGpuHeapPlanBytes) ||
        !accountGpuBytes(
            textureHeapBytes,
            admittedHeapPlanBytes,
            kMaximumSyntheticGpuHeapPlanBytes) ||
        !accountGpuBytes(
            fallbackHeapBytes,
            admittedHeapPlanBytes,
            kMaximumSyntheticGpuHeapPlanBytes)) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The aggregate public Metal heap plan exceeds its budget.");
        }
        return nil;
    }

    // Admit the checked descriptor plan before the first heap exists. Metal's
    // undocumented page-rounding delta, if any, is measured and admitted
    // separately before the candidate can publish.
    auto bootstrapPlanReservation =
        gpuBudgetHolder->_ledger->tryReserve(
            admittedHeapPlanBytes);
    if (!bootstrapPlanReservation.has_value()) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The public heap plan is unavailable in the aggregate heap-admission budget.");
        }
        return nil;
    }

    // Staging collections and autoreleased Metal command objects drain before
    // the outer plan reservation can be destroyed on any failure.
    @autoreleasepool {
    id<MTLHeap> bufferHeap =
        newSharedTrackedHeap(
            device,
            bufferHeapBytes,
            @"Airfix public snapshot buffer heap");
    id<MTLHeap> textureHeap =
        newSharedTrackedHeap(
            device,
            textureHeapBytes,
            @"Airfix public snapshot texture heap");
    id<MTLHeap> fallbackHeap =
        newSharedTrackedHeap(
            device,
            fallbackHeapBytes,
            @"Airfix persistent fallback texture heap");
    if (bufferHeap == nil ||
        textureHeap == nil ||
        fallbackHeap == nil) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"Metal could not create the complete public heap plan.");
        }
        return nil;
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
                [bufferHeap
                    newBufferWithLength:vertexByteCounts[meshSlot]
                    options:kSharedTrackedResourceOptions];
            if (vertexBuffer != nil &&
                vertexBuffer.contents != nullptr) {
                std::memcpy(
                    vertexBuffer.contents,
                    packedVertices[meshSlot].data(),
                    vertexByteCounts[meshSlot]);
            }
        }
        id<MTLBuffer> indexBuffer = nil;
        if (indexByteCounts[meshSlot] != 0U) {
            indexBuffer =
                [bufferHeap
                    newBufferWithLength:indexByteCounts[meshSlot]
                    options:kSharedTrackedResourceOptions];
            if (indexBuffer != nil &&
                indexBuffer.contents != nullptr) {
                std::memcpy(
                    indexBuffer.contents,
                    payload.meshes[meshSlot].indices.data(),
                    indexByteCounts[meshSlot]);
            }
        }
        if ((vertexByteCounts[meshSlot] != 0U &&
                (vertexBuffer == nil ||
                 vertexBuffer.contents == nullptr)) ||
            (indexByteCounts[meshSlot] != 0U &&
                (indexBuffer == nil ||
                 indexBuffer.contents == nullptr))) {
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

    id<MTLTexture> syntheticTexture =
        [textureHeap
            newTextureWithDescriptor:textureDescriptor];
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

    id<MTLTexture> fallbackTexture =
        [fallbackHeap
            newTextureWithDescriptor:fallbackDescriptor];
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
    for (AirfixMetalMeshBuffers* buffers in meshBufferSnapshot) {
        if ((buffers.vertexBuffer != nil &&
                buffers.vertexBuffer.allocatedSize == 0U) ||
            (buffers.indexBuffer != nil &&
                buffers.indexBuffer.allocatedSize == 0U)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"A public heap buffer has no allocation.");
            }
            return nil;
        }
    }
    if (syntheticTexture.allocatedSize == 0U ||
        fallbackTexture.allocatedSize == 0U) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"A public heap texture has no allocation.");
        }
        return nil;
    }
    std::size_t snapshotHeapCurrentAllocatedBytes = 0U;
    std::size_t fallbackHeapCurrentAllocatedBytes = 0U;
    std::size_t totalHeapCurrentAllocatedBytes = 0U;
    if (!accountCurrentHeapAllocation(
            bufferHeap,
            snapshotHeapCurrentAllocatedBytes) ||
        !accountCurrentHeapAllocation(
            textureHeap,
            snapshotHeapCurrentAllocatedBytes) ||
        !accountCurrentHeapAllocation(
            fallbackHeap,
            fallbackHeapCurrentAllocatedBytes) ||
        !checkedAdd(
            snapshotHeapCurrentAllocatedBytes,
            fallbackHeapCurrentAllocatedBytes,
            totalHeapCurrentAllocatedBytes)) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The created public heaps have invalid current allocations.");
        }
        return nil;
    }
    // Descriptor plans are admitted before creation. Metal may page-round a
    // heap beyond that plan, so the measured current allocation must obtain
    // any supplemental aggregate admission before this snapshot can publish.
    if (!finalizeHeapAllocationReservation(
            *gpuBudgetHolder->_ledger,
            *bootstrapPlanReservation,
            totalHeapCurrentAllocatedBytes)) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::resourceLimit,
                @"The public heaps' current allocation is unavailable in the aggregate admission budget.");
        }
        return nil;
    }
    auto fallbackReservation =
        bootstrapPlanReservation->split(
            fallbackHeapCurrentAllocatedBytes);
    if (!fallbackReservation.has_value()) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::unexpectedFailure,
                @"The public fallback GPU debit could not be separated.");
        }
        return nil;
    }

    AirfixMetalRoomResources* roomResources =
        [[AirfixMetalRoomResources alloc] init];
    AirfixMetalRoomSnapshot* roomSnapshot =
        [[AirfixMetalRoomSnapshot alloc] init];
    AirfixBudgetedMetalTexture* fallbackResource =
        [[AirfixBudgetedMetalTexture alloc] init];
    AirfixGpuBudgetReservationHolder* snapshotReservationHolder =
        [[AirfixGpuBudgetReservationHolder alloc] init];
    AirfixGpuBudgetReservationHolder* fallbackReservationHolder =
        [[AirfixGpuBudgetReservationHolder alloc] init];
    if (roomResources == nil || roomSnapshot == nil ||
        fallbackResource == nil ||
        snapshotReservationHolder == nil ||
        fallbackReservationHolder == nil) {
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
    roomResources.bufferHeap = bufferHeap;
    roomResources.textureHeap = textureHeap;
    roomSnapshot->_resources = roomResources;
    roomSnapshot->_releaseQueue = resourceReleaseQueue;
    roomSnapshot->_worldRoomInstalled = NO;
    snapshotReservationHolder->_reservation.emplace(
        std::move(*bootstrapPlanReservation));
    fallbackReservationHolder->_reservation.emplace(
        std::move(*fallbackReservation));
    roomSnapshot->_gpuBudgetReservationHolder =
        snapshotReservationHolder;
    fallbackResource->_texture = fallbackTexture;
    fallbackResource->_heap = fallbackHeap;
    fallbackResource->_gpuBudgetReservationHolder =
        fallbackReservationHolder;

    // Publish only after the complete renderer candidate has been validated
    // and every Metal object has been created successfully.
    self.commandQueue = commandQueue;
    self.pipelineState = pipelineState;
    self.depthState = depthState;
    self.samplerState = samplerState;
    self.fallbackResource = fallbackResource;
    self.roomSnapshot = roomSnapshot;
    self.preparationOwnerToken = preparationOwnerToken;
    self.resourceReleaseQueue = resourceReleaseQueue;
    self.gpuBudgetHolder = gpuBudgetHolder;

    return self;
    }
    } catch (...) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::unexpectedFailure,
                @"The public Metal smoke-test candidate could not be prepared.");
        }
        return nil;
    }
}

- (BOOL)missionWorldRoomInstalled {
    AirfixMetalRoomSnapshot* snapshot = self.roomSnapshot;
    return snapshot != nil && snapshot->_worldRoomInstalled;
}

- (nullable AirfixPreparedMetalRoom*)prepareLoadedMissionRoom:
    (airfix::content::LoadedMissionWorldRoom&&)room
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
        AirfixSnapshotGpuBudgetLedgerHolder* gpuBudgetHolder =
            self.gpuBudgetHolder;
        if (commandQueue == nil || device == nil ||
            resourceReleaseQueue == nil ||
            gpuBudgetHolder == nil ||
            gpuBudgetHolder->_ledger == nullptr) {
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

        const auto scenePosePlan = planScenePoseRuntime(room);
        if (scenePosePlan.status ==
            ScenePoseRuntimePreparationStatus::resourceLimit) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The private room pose runtime exceeds its bounded resources.");
            }
            return nil;
        }
        if (!accountCpuPackedBytes(
                scenePosePlan.retainedPoseBytes,
                preflight.aggregateCpuPackedBytes,
                kMaximumPrivateRoomCpuPackedBytes)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The private room pose runtime exceeds the retained CPU budget.");
            }
            return nil;
        }

        auto scenePosePreparation =
            prepareScenePoseRuntime(room, scenePosePlan);
        if (scenePosePreparation.status ==
            ScenePoseRuntimePreparationStatus::resourceLimit) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The private room pose runtime could not reserve its bounded storage.");
            }
            return nil;
        }
        if (scenePosePreparation.status ==
                ScenePoseRuntimePreparationStatus::invalidPayload ||
            (scenePosePreparation.status ==
                 ScenePoseRuntimePreparationStatus::ready &&
             scenePosePreparation.runtime == nullptr) ||
            (scenePosePreparation.status ==
                 ScenePoseRuntimePreparationStatus::noPlayer &&
             scenePosePreparation.runtime != nullptr)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::invalidPayload,
                    @"The private room pose runtime is inconsistent.");
            }
            return nil;
        }

        // Keep the cheap logical-byte rejection before building allocator
        // descriptors. It is best-effort; the aligned CAS reservation below
        // is the authoritative admission decision.
        std::size_t logicalAggregateGpuBytes =
            gpuBudgetHolder->_ledger->reservedBytes();
        if (!accountGpuBytes(
                preflight.aggregateGpuBytes,
                logicalAggregateGpuBytes,
                gpuBudgetHolder->_ledger->maximumBytes())) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The live snapshots and candidate exceed the logical Metal aggregate budget.");
            }
            return nil;
        }

        std::size_t bufferHeapBytes = 0U;
        std::size_t bufferHeapAlignment = 0U;
        bool heapPlanValid = true;
        for (std::size_t meshSlot = 0U;
             meshSlot < preflight.vertexByteCounts.size();
             ++meshSlot) {
            if ((preflight.vertexByteCounts[meshSlot] != 0U &&
                    !accountHeapResourcePlacement(
                        [device
                            heapBufferSizeAndAlignWithLength:
                                preflight.vertexByteCounts[meshSlot]
                            options:kSharedTrackedResourceOptions],
                        bufferHeapBytes,
                        bufferHeapAlignment,
                        kMaximumPrivateRoomGpuHeapPlanBytes)) ||
                (preflight.indexByteCounts[meshSlot] != 0U &&
                    !accountHeapResourcePlacement(
                        [device
                            heapBufferSizeAndAlignWithLength:
                                preflight.indexByteCounts[meshSlot]
                            options:kSharedTrackedResourceOptions],
                        bufferHeapBytes,
                        bufferHeapAlignment,
                        kMaximumPrivateRoomGpuHeapPlanBytes))) {
                heapPlanValid = false;
                break;
            }
        }
        std::size_t textureHeapBytes = 0U;
        std::size_t textureHeapAlignment = 0U;
        if (heapPlanValid) {
            for (const auto& source : room.textures) {
                MTLTextureDescriptor* descriptor =
                    [[MTLTextureDescriptor alloc] init];
                if (descriptor == nil) {
                    heapPlanValid = false;
                    break;
                }
                configurePrivateTextureDescriptor(
                    descriptor, source);
                if (!accountHeapResourcePlacement(
                        [device
                            heapTextureSizeAndAlignWithDescriptor:
                                descriptor],
                        textureHeapBytes,
                        textureHeapAlignment,
                        kMaximumPrivateRoomGpuHeapPlanBytes)) {
                    heapPlanValid = false;
                    break;
                }
            }
        }
        if (!heapPlanValid ||
            !finalizeHeapPlan(
                bufferHeapBytes,
                bufferHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes) ||
            !finalizeHeapPlan(
                textureHeapBytes,
                textureHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The private Metal heap plan exceeds its budget.");
            }
            return nil;
        }
        std::size_t admittedHeapPlanBytes = 0U;
        if (!accountGpuBytes(
                bufferHeapBytes,
                admittedHeapPlanBytes,
                kMaximumPrivateRoomGpuHeapPlanBytes) ||
            !accountGpuBytes(
                textureHeapBytes,
                admittedHeapPlanBytes,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The aggregate private Metal heap plan exceeds its budget.");
            }
            return nil;
        }

        // Admit the checked descriptor plan before the first heap exists.
        // Any Metal page-rounding delta is measured and admitted separately
        // before this candidate can leave preparation.
        auto privatePlanReservation =
            gpuBudgetHolder->_ledger->tryReserve(
                admittedHeapPlanBytes);
        if (!privatePlanReservation.has_value()) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The private heap plan is unavailable in the aggregate heap-admission budget.");
            }
            return nil;
        }

        // The pool owns all autoreleased staging wrappers and Metal command
        // objects. It drains before the outer plan reservation on every
        // failure.
        @autoreleasepool {
        id<MTLHeap> bufferHeap =
            bufferHeapBytes != 0U
            ? newSharedTrackedHeap(
                  device,
                  bufferHeapBytes,
                  @"Airfix private snapshot buffer heap")
            : nil;
        id<MTLHeap> textureHeap =
            textureHeapBytes != 0U
            ? newSharedTrackedHeap(
                  device,
                  textureHeapBytes,
                  @"Airfix private snapshot texture heap")
            : nil;
        if ((bufferHeapBytes != 0U && bufferHeap == nil) ||
            (textureHeapBytes != 0U && textureHeap == nil)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"Metal could not create the complete private heap plan.");
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
                    [bufferHeap
                        newBufferWithLength:packedBytes
                        options:kSharedTrackedResourceOptions];
                if (vertexBuffer != nil &&
                    vertexBuffer.contents != nullptr) {
                    std::memcpy(
                        vertexBuffer.contents,
                        packedVertices.data(),
                        packedBytes);
                }
            }
            id<MTLBuffer> indexBuffer = nil;
            const auto indexBytes = preflight.indexByteCounts[meshSlot];
            if (indexBytes != 0U) {
                indexBuffer =
                    [bufferHeap
                        newBufferWithLength:indexBytes
                        options:kSharedTrackedResourceOptions];
                if (indexBuffer != nil &&
                    indexBuffer.contents != nullptr) {
                    std::memcpy(
                        indexBuffer.contents,
                        mesh.indices.data(),
                        indexBytes);
                }
            }
            if ((packedBytes != 0U &&
                    (vertexBuffer == nil ||
                     vertexBuffer.contents == nullptr)) ||
                (indexBytes != 0U &&
                    (indexBuffer == nil ||
                     indexBuffer.contents == nullptr))) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::bufferCreation,
                        @"Metal could not create every private room mesh buffer.");
                }
                return nil;
            }
            if ((vertexBuffer != nil &&
                    vertexBuffer.allocatedSize == 0U) ||
                (indexBuffer != nil &&
                    indexBuffer.allocatedSize == 0U)) {
                if (error != nullptr) {
                    *error = makeError(
                        RendererError::resourceLimit,
                        @"A private heap buffer has no allocation.");
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
            configurePrivateTextureDescriptor(descriptor, source);

            id<MTLTexture> texture =
                [textureHeap
                    newTextureWithDescriptor:descriptor];
            if (texture == nil ||
                texture.width != descriptor.width ||
                texture.height != descriptor.height ||
                texture.mipmapLevelCount !=
                    descriptor.mipmapLevelCount ||
                texture.allocatedSize == 0U) {
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

        std::size_t currentAllocatedHeapBytes = 0U;
        if ((bufferHeap != nil &&
                !accountCurrentHeapAllocation(
                    bufferHeap,
                    currentAllocatedHeapBytes)) ||
            (textureHeap != nil &&
                !accountCurrentHeapAllocation(
                    textureHeap,
                    currentAllocatedHeapBytes))) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The created private heaps have invalid current allocations.");
            }
            return nil;
        }

        // Admission before newHeap covers the descriptor plan. If Metal page
        // rounding makes the measured current allocation larger, obtain the
        // exact supplement now; otherwise this unpublished candidate is
        // destroyed before the plan reservation is consumed.
        if (!finalizeHeapAllocationReservation(
                *gpuBudgetHolder->_ledger,
                *privatePlanReservation,
                currentAllocatedHeapBytes)) {
            if (error != nullptr) {
                *error = makeError(
                    RendererError::resourceLimit,
                    @"The private heaps' current allocation is unavailable in the aggregate admission budget.");
            }
            return nil;
        }

        AirfixMetalRoomResources* candidateResources =
            [[AirfixMetalRoomResources alloc] init];
        AirfixMetalRoomSnapshot* candidate =
            [[AirfixMetalRoomSnapshot alloc] init];
        AirfixPreparedMetalRoom* preparedRoom =
            [[AirfixPreparedMetalRoom alloc] init];
        AirfixGpuBudgetReservationHolder* reservationHolder =
            [[AirfixGpuBudgetReservationHolder alloc] init];
        NSObject* ownerToken = self.preparationOwnerToken;
        if (candidateResources == nil || candidate == nil ||
            preparedRoom == nil ||
            reservationHolder == nil ||
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
        candidateResources.bufferHeap = bufferHeap;
        candidateResources.textureHeap = textureHeap;
        preparedRoom->_ownerToken = ownerToken;
        preparedRoom->_device = device;
        preparedRoom->_published = NO;
        candidateResources->_indexOffsets =
            std::move(preflight.indexOffsets);
        candidateResources->_revision = room.revision;
        candidateResources->_scenePoseRuntime =
            std::move(scenePosePreparation.runtime);
        candidate->_resources = candidateResources;
        candidate->_releaseQueue = resourceReleaseQueue;
        candidate->_worldRoomInstalled = YES;

        // Texture upload bytes are intentionally not retained by the native
        // snapshot after Metal owns the complete resource set.
        std::vector<airfix::content::LoadedTextureAsset>().swap(
            room.textures);
        candidateResources->_missionRoom.emplace(std::move(room));

        reservationHolder->_reservation.emplace(
            std::move(*privatePlanReservation));
        candidate->_gpuBudgetReservationHolder =
            reservationHolder;
        preparedRoom->_snapshot = candidate;
        return preparedRoom;
        }
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

- (BOOL)validatePreparedRoomForCommit:(AirfixPreparedMetalRoom*)preparedRoom
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
        !hasActiveGpuBudgetReservation(preparedRoom->_snapshot) ||
        !preparedRoom->_snapshot->_worldRoomInstalled) {
        if (error != nullptr) {
            *error = makeError(
                RendererError::invalidPreparedRoom,
                @"The prepared Metal room belongs to a different renderer or device.");
        }
        return NO;
    }

    return YES;
}

- (void)commitValidatedPreparedRoom:(AirfixPreparedMetalRoom*)preparedRoom {
    AirfixMetalRoomSnapshot* candidate = preparedRoom->_snapshot;
    preparedRoom->_published = YES;
    preparedRoom->_snapshot = nil;
    // The coordinator has already revalidated serial and revision, and the
    // candidate already owns its aggregate budget debit. Exactly one atomic
    // strong-pointer assignment publishes the complete room without any
    // budget mutation.
    self.roomSnapshot = candidate;
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
    const airfix::render::DrawModelPayload& payload =
        resources->_missionRoom.has_value()
            ? resources->_missionRoom->model
            : resources->_payload;
    const airfix::render::DrawSubmissionPlan& submissionPlan =
        resources->_missionRoom.has_value()
            ? resources->_missionRoom->submission
            : resources->_submissionPlan;
    AirfixBudgetedMetalTexture* fallbackResource =
        self.fallbackResource;
    if (fallbackResource == nil ||
        fallbackResource->_texture == nil) {
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

    // One lease covers the whole encoded frame. This pilot has no producer
    // endpoint after the immutable step-zero publication; failed acquisition
    // therefore safely falls back to the authored instance transforms.
    std::optional<airfix::render::DynamicInstancePoseLease> poseLease;
    if (resources->_scenePoseRuntime != nullptr) {
        poseLease = resources->_scenePoseRuntime->tryAcquire();
    }

    const simd_float4x4 viewport = aspectCorrection(view.drawableSize);
    for (std::size_t commandIndex = 0U;
         commandIndex < submissionPlan.commands.size();
         ++commandIndex) {
        const auto& command =
            submissionPlan.commands[commandIndex];
        const auto& instance =
            payload.instances[command.instanceIndex];
        const auto resolvedPose =
            poseLease.has_value()
            ? poseLease->resolve(
                  command.instanceIndex,
                  instance.modelLinear,
                  instance.modelTranslation)
            : airfix::render::ResolvedInstancePose{
                  .modelLinear = instance.modelLinear,
                  .modelTranslation = instance.modelTranslation,
              };
        const simd_float4x4 model =
            toSimdMatrix(
                resolvedPose.modelLinear,
                resolvedPose.modelTranslation);
        const GpuUniforms uniforms{simd_mul(viewport, model)};
        AirfixMetalMeshBuffers* buffers =
            resources.meshBuffers[command.meshSlot];
        [encoder setVertexBuffer:buffers.vertexBuffer
                         offset:0U
                        atIndex:0U];
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1U];

        id<MTLTexture> texture = fallbackResource->_texture;
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
    poseLease.reset();
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        (void)completed;
        // Retain the immutable resource owner explicitly until this GPU
        // submission no longer references its buffers or textures.
        (void)snapshot;
        (void)fallbackResource;
    }];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
