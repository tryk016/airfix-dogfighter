#import "AirfixMetalRenderer.h"

#include "airfix/content/LegacyAircraftHealthGaugeSubmission.hpp"
#include "airfix/content/LegacyAircraftHudIdentityStatusSubmission.hpp"
#include "airfix/content/LegacyAircraftHudInstrumentsSubmission.hpp"
#include "airfix/content/LegacyAircraftHudRenderEvent.hpp"
#include "airfix/content/LegacyAircraftHudRollingDigitsSubmission.hpp"
#include "airfix/content/LegacyAircraftHudWeaponPanelsSubmission.hpp"
#include "airfix/content/LegacyWeaponCrosshairSpriteSubmission.hpp"
#import <Metal/Metal.h>
#import <simd/simd.h>

#include "airfix/content/LegacyAircraftHealthGaugeTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudIdentityStatusTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudInstrumentsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudWeaponPanelTextureSet.hpp"
#include "airfix/content/LegacyWeaponCrosshairTextureSet.hpp"
#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/content/MissionWorldRoomPublication.hpp"
#include "airfix/render/DrawModel.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/LegacyDepthState.hpp"
#include "airfix/render/LegacyGameplayCameraClipPacket.hpp"
#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"
#include "airfix/render/NativeRenderLayout.hpp"
#include "airfix/render/PlayerActorPoseRuntime.hpp"
#include "airfix/render/PlayerActorPoseRuntimePreparation.hpp"
#include "airfix/render/PublicRenderSmokeScene.hpp"
#include "airfix/render/RenderFrameDiagnostics.hpp"
#include "airfix/render/RenderPresentationTransaction.hpp"
#include "airfix/render/SceneTextureSampling.hpp"
#include "airfix/render/SnapshotGpuBudgetLedger.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
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

NSString *const AirfixMetalRendererErrorDomain =
    @"AirfixMetalRendererErrorDomain";

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
  invalidPreparedPresentation,
  invalidPreparedRoom,
  preparedRoomAlreadyPublished,
  presentationSurfaceUnavailable,
  presentationTargetPreparation,
  stalePresentationCandidate,
  unexpectedFailure,
};

constexpr std::size_t kMaximumSyntheticLogicalGpuBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumSyntheticGpuHeapPlanBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumSyntheticCpuPackedBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumPrivateRoomLogicalGpuBytes = 256U * 1024U * 1024U;
constexpr std::size_t kMaximumPrivateRoomGpuHeapPlanBytes =
    256U * 1024U * 1024U;
constexpr std::size_t kMaximumPrivateRoomCpuPackedBytes = 128U * 1024U * 1024U;
constexpr NSUInteger kMaximumScaledSceneDimension = 16384U;
constexpr std::size_t kMaximumScaledSceneTargetBytes = 512U * 1024U * 1024U;
constexpr MTLResourceOptions kSharedTrackedResourceOptions =
    static_cast<MTLResourceOptions>(MTLResourceStorageModeShared |
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

struct alignas(16) GpuGameplayUniforms {
  simd_float4x4 modelFromLocal;
  simd_float4 cameraAxisX;
  simd_float4 cameraAxisY;
  simd_float4 cameraAxisZ;
  simd_float4 cameraTranslationAndInverseScaleSquared;
  simd_float4 projection;
  simd_float4 logicalCanvas;
};

struct alignas(16) GpuOverlayUniforms {
  simd_float4 outputAndPanelSize;
  simd_float4 panelOrigin;
  simd_float4 tint;
  simd_float4 uvRect;
};

struct alignas(16) GpuGaugeUniforms {
  std::array<simd_float4, 4U> outputQuad;
  simd_float4 outputSize;
  simd_float4 tint;
  simd_float4 uvRect;
};

static_assert(sizeof(GpuVertex) == 48U);
static_assert(alignof(GpuVertex) == 16U);
static_assert(sizeof(GpuUniforms) == 64U);
static_assert(sizeof(GpuGameplayUniforms) == 160U);
static_assert(alignof(GpuGameplayUniforms) == 16U);
static_assert(sizeof(GpuOverlayUniforms) == 64U);
static_assert(alignof(GpuOverlayUniforms) == 16U);
static_assert(sizeof(GpuGaugeUniforms) == 112U);
static_assert(alignof(GpuGaugeUniforms) == 16U);
static_assert(offsetof(GpuGaugeUniforms, outputQuad) == 0U);
static_assert(offsetof(GpuGaugeUniforms, outputSize) == 64U);
static_assert(offsetof(GpuGaugeUniforms, tint) == 80U);
static_assert(offsetof(GpuGaugeUniforms, uvRect) == 96U);

[[nodiscard]] simd_float4 normalizedArgb(const std::uint32_t argb) noexcept {
  constexpr float inverseByte = 1.0F / 255.0F;
  return {
      static_cast<float>((argb >> 16U) & 0xFFU) * inverseByte,
      static_cast<float>((argb >> 8U) & 0xFFU) * inverseByte,
      static_cast<float>(argb & 0xFFU) * inverseByte,
      static_cast<float>((argb >> 24U) & 0xFFU) * inverseByte,
  };
}

NSError *makeError(const RendererError code, NSString *description) {
  return [NSError errorWithDomain:AirfixMetalRendererErrorDomain
                             code:static_cast<NSInteger>(code)
                         userInfo:@{NSLocalizedDescriptionKey : description}];
}

bool checkedAdd(const std::size_t left, const std::size_t right,
                std::size_t &output) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  output = left + right;
  return true;
}

bool checkedMultiply(const std::size_t left, const std::size_t right,
                     std::size_t &output) noexcept {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  output = left * right;
  return true;
}

bool checkedAdd64(const std::uint64_t left, const std::uint64_t right,
                  std::uint64_t &output) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  output = left + right;
  return true;
}

bool accountGpuBytes(const std::size_t bytes, std::size_t &aggregateBytes,
                     const std::size_t maximumBytes) noexcept {
  std::size_t next = 0U;
  if (!checkedAdd(aggregateBytes, bytes, next) || next > maximumBytes) {
    return false;
  }
  aggregateBytes = next;
  return true;
}

bool accountHeapResourcePlacement(const MTLSizeAndAlign allocation,
                                  std::size_t &heapBytes,
                                  std::size_t &maximumAlignment,
                                  const std::size_t maximumBytes) noexcept {
  const auto size = static_cast<std::size_t>(allocation.size);
  const auto alignment = static_cast<std::size_t>(allocation.align);
  if (size == 0U || alignment == 0U) {
    return false;
  }
  const auto remainder = heapBytes % alignment;
  std::size_t alignedOffset = heapBytes;
  if (remainder != 0U &&
      !checkedAdd(heapBytes, alignment - remainder, alignedOffset)) {
    return false;
  }
  std::size_t end = 0U;
  if (!checkedAdd(alignedOffset, size, end) || end > maximumBytes) {
    return false;
  }
  heapBytes = end;
  maximumAlignment = std::max(maximumAlignment, alignment);
  return true;
}

bool finalizeHeapPlan(std::size_t &heapBytes,
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
      !checkedAdd(heapBytes, maximumAlignment - remainder, heapBytes)) {
    return false;
  }
  return heapBytes <= maximumBytes;
}

bool fitsNSUInteger(std::size_t value) noexcept;

id<MTLHeap> newSharedTrackedHeap(id<MTLDevice> device, const std::size_t bytes,
                                 NSString *label) {
  if (bytes == 0U || !fitsNSUInteger(bytes)) {
    return nil;
  }
  MTLHeapDescriptor *descriptor = [[MTLHeapDescriptor alloc] init];
  if (descriptor == nil) {
    return nil;
  }
  descriptor.type = MTLHeapTypeAutomatic;
  descriptor.storageMode = MTLStorageModeShared;
  descriptor.cpuCacheMode = MTLCPUCacheModeDefaultCache;
  descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
  descriptor.size = static_cast<NSUInteger>(bytes);
  id<MTLHeap> heap = [device newHeapWithDescriptor:descriptor];
  // Metal may page-round descriptor.size, and iOS exposes no documented
  // pre-creation upper bound for that rounding. The caller immediately
  // measures currentAllocatedSize and obtains any supplemental admission.
  if (heap == nil || heap.size == 0U ||
      heap.size < static_cast<NSUInteger>(bytes) ||
      heap.type != MTLHeapTypeAutomatic ||
      heap.storageMode != MTLStorageModeShared ||
      heap.cpuCacheMode != MTLCPUCacheModeDefaultCache ||
      heap.hazardTrackingMode != MTLHazardTrackingModeTracked) {
    return nil;
  }
  heap.label = label;
  return heap;
}

bool accountCurrentHeapAllocation(id<MTLHeap> heap,
                                  std::size_t &aggregateBytes) noexcept {
  if (heap == nil || heap.currentAllocatedSize == 0U ||
      heap.currentAllocatedSize > heap.size) {
    return false;
  }
  return checkedAdd(aggregateBytes,
                    static_cast<std::size_t>(heap.currentAllocatedSize),
                    aggregateBytes);
}

bool finalizeHeapAllocationReservation(
    airfix::render::SnapshotGpuBudgetLedger &ledger,
    airfix::render::SnapshotGpuBudgetReservation &reservation,
    const std::size_t currentAllocatedBytes) noexcept {
  if (currentAllocatedBytes <= reservation.bytes()) {
    return reservation.reconcile(currentAllocatedBytes);
  }
  const auto supplementalBytes = currentAllocatedBytes - reservation.bytes();
  auto supplement = ledger.tryReserve(supplementalBytes);
  return supplement.has_value() && reservation.absorb(std::move(*supplement));
}

bool accountCpuPackedBytes(
    const std::size_t bytes, std::size_t &aggregateBytes,
    const std::size_t maximumBytes = kMaximumSyntheticCpuPackedBytes) noexcept {
  std::size_t next = 0U;
  if (!checkedAdd(aggregateBytes, bytes, next) || next > maximumBytes) {
    return false;
  }
  aggregateBytes = next;
  return true;
}

bool fitsNSUInteger(const std::size_t value) noexcept {
  if constexpr (sizeof(std::size_t) > sizeof(NSUInteger)) {
    return value <=
           static_cast<std::size_t>(std::numeric_limits<NSUInteger>::max());
  }
  return true;
}

simd_float4x4 toSimdMatrix(const airfix::render::Mat3 &linear,
                           const airfix::render::Vec3 &translation) {
  // Both contracts use column vectors, but their memory layouts are not
  // assumed to match. Repack every component explicitly.
  simd_float4x4 result{};
  result.columns[0] = simd_make_float4(linear.columns[0].x, linear.columns[0].y,
                                       linear.columns[0].z, 0.0F);
  result.columns[1] = simd_make_float4(linear.columns[1].x, linear.columns[1].y,
                                       linear.columns[1].z, 0.0F);
  result.columns[2] = simd_make_float4(linear.columns[2].x, linear.columns[2].y,
                                       linear.columns[2].z, 0.0F);
  result.columns[3] =
      simd_make_float4(translation.x, translation.y, translation.z, 1.0F);
  return result;
}

std::vector<GpuVertex>
repackVertices(const std::vector<airfix::render::DrawVertex> &vertices) {
  std::vector<GpuVertex> gpuVertices;
  gpuVertices.reserve(vertices.size());
  for (const auto &vertex : vertices) {
    gpuVertices.push_back(GpuVertex{
        simd_make_float4(vertex.position.x, vertex.position.y,
                         vertex.position.z, 1.0F),
        simd_make_float4(vertex.normal.x, vertex.normal.y, vertex.normal.z,
                         0.0F),
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
    } else {
      matrix.columns[1].y = aspect;
    }
  }
  return matrix;
}

[[nodiscard]] std::optional<airfix::render::OutputPixelExtent>
outputPixelExtent(id<MTLTexture> texture) noexcept {
  constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
  if (texture == nil || texture.width == 0U || texture.height == 0U ||
      texture.width > maximum || texture.height > maximum) {
    return std::nullopt;
  }
  return airfix::render::OutputPixelExtent{
      .width = static_cast<std::uint32_t>(texture.width),
      .height = static_cast<std::uint32_t>(texture.height),
  };
}

[[nodiscard]] std::optional<airfix::render::OutputPixelExtent>
outputPixelExtent(const CGSize size) noexcept {
  constexpr auto maximum =
      static_cast<CGFloat>(std::numeric_limits<std::uint32_t>::max());
  if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
      size.width <= 0.0 || size.height <= 0.0 || size.width > maximum ||
      size.height > maximum || std::floor(size.width) != size.width ||
      std::floor(size.height) != size.height) {
    return std::nullopt;
  }
  return airfix::render::OutputPixelExtent{
      .width = static_cast<std::uint32_t>(size.width),
      .height = static_cast<std::uint32_t>(size.height),
  };
}

[[nodiscard]] airfix::render::ConvertedNodeTransform
actorWorldFrom(const airfix::simulation::PlayerSpawnPose &pose) noexcept {
  const auto vectorAt = [](const std::array<float, 3U> &value) {
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

[[nodiscard]]
airfix::render::LegacyGameplayCameraStepCoordinatorInitializeInput
gameplayCameraInitializeInput(
    const airfix::content::LoadedMissionWorldRoom &room) noexcept {
  const auto actorWorld = actorWorldFrom(room.playerSpawnPose);
  return {
      .vehicleWorldPosition = actorWorld.translation,
      .vehicleWorldRotation = actorWorld.linear,
      .worldRoomIndex = room.playerSpawnPose.worldRoomIndex,
      .cameraCyclePressCount = 0U,
  };
}

[[nodiscard]] GpuGameplayUniforms
gameplayUniforms(const simd_float4x4 modelFromLocal,
                 const airfix::render::LegacyGameplayCameraClipPacket &camera,
                 const airfix::render::NativeRenderLayout &layout) noexcept {
  const auto &transform = camera.pose().worldToView();
  const auto linear = transform.linear();
  const auto translation = transform.translation();
  const auto &projection = camera.pose().projection();
  const auto logicalCentre = layout.mapReferenceCameraPoint({
      projection.centre().x,
      projection.centre().y,
  });
  const auto logicalExtent = layout.cameraLogicalExtent();
  return {
      .modelFromLocal = modelFromLocal,
      .cameraAxisX = simd_make_float4(linear.columns[0].x, linear.columns[0].y,
                                      linear.columns[0].z, 0.0F),
      .cameraAxisY = simd_make_float4(linear.columns[1].x, linear.columns[1].y,
                                      linear.columns[1].z, 0.0F),
      .cameraAxisZ = simd_make_float4(linear.columns[2].x, linear.columns[2].y,
                                      linear.columns[2].z, 0.0F),
      .cameraTranslationAndInverseScaleSquared =
          simd_make_float4(translation.x, translation.y, translation.z,
                           transform.inverseScaleSquared()),
      .projection =
          simd_make_float4(projection.nearDistance(), projection.farDistance(),
                           projection.projectScale(), 0.0F),
      .logicalCanvas =
          simd_make_float4(logicalCentre.x, logicalCentre.y,
                           logicalExtent.width, logicalExtent.height),
  };
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
  std::optional<airfix::content::LoadedLegacyWeaponCrosshairTextureSet>
      _crosshairs;
  std::optional<airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet>
      _healthGauge;
  std::optional<airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet>
      _rollingDigits;
  std::optional<airfix::content::LoadedLegacyAircraftHudInstrumentTextureSet>
      _hudInstruments;
  std::optional<airfix::content::LoadedLegacyAircraftHudWeaponPanelTextureSet>
      _weaponPanels;
  std::optional<
      airfix::content::LoadedLegacyAircraftHudIdentityStatusTextureSet>
      _identityStatus;
  std::vector<NSUInteger> _indexOffsets;
  std::shared_ptr<airfix::render::PlayerActorPoseRuntime> _scenePoseRuntime;
  std::shared_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>
      _cameraMissionRuntime;
}
@property(nonatomic, strong) NSArray<AirfixMetalMeshBuffers *> *meshBuffers;
@property(nonatomic, strong) NSArray<id<MTLTexture>> *textures;
@property(nonatomic, strong) NSArray<id<MTLTexture>> *crosshairTextures;
@property(nonatomic, strong) NSArray<id<MTLTexture>> *healthGaugeTextures;
@property(nonatomic, strong) NSArray<id<MTLTexture>> *rollingDigitTextures;
@property(nonatomic, strong) NSArray<id<MTLTexture>> *hudInstrumentTextures;
@property(nonatomic, strong) NSArray<id<MTLTexture>> *weaponPanelTextures;
@property(nonatomic, strong) NSArray<id<MTLTexture>> *identityStatusTextures;
@property(nonatomic, strong) id<MTLHeap> bufferHeap;
@property(nonatomic, strong) id<MTLHeap> textureHeap;
@end

@implementation AirfixMetalRoomResources

- (void)dealloc {
  // Suballocated resources must release before their backing heaps. The
  // snapshot token is consumed only after this complete owner is destroyed.
  _scenePoseRuntime.reset();
  _cameraMissionRuntime.reset();
  _meshBuffers = nil;
  _textures = nil;
  _crosshairTextures = nil;
  _healthGaugeTextures = nil;
  _rollingDigitTextures = nil;
  _hudInstrumentTextures = nil;
  _weaponPanelTextures = nil;
  _identityStatusTextures = nil;
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

@interface AirfixMetalDiagnosticsState : NSObject {
@public
  airfix::render::RenderFrameDiagnosticsAccumulator _accumulator;
  std::optional<std::chrono::steady_clock::time_point> _previousFrameStart;
  std::optional<std::chrono::steady_clock::time_point> _lastOverlayRefresh;
  std::atomic<double> _latestGpuFrameMilliseconds;
  std::atomic<bool> _hasGpuFrameMilliseconds;
}
@end

@implementation AirfixMetalDiagnosticsState

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _latestGpuFrameMilliseconds.store(0.0, std::memory_order_relaxed);
    _hasGpuFrameMilliseconds.store(false, std::memory_order_relaxed);
  }
  return self;
}

@end

@interface AirfixGpuBudgetReservationHolder : NSObject {
@public
  std::optional<airfix::render::SnapshotGpuBudgetReservation> _reservation;
}
@end

@implementation AirfixGpuBudgetReservationHolder
@end

@interface AirfixMetalScaledSceneTargetBundle : NSObject {
@public
  __strong id<MTLTexture> _colorTexture;
  __strong id<MTLTexture> _depthTexture;
  __strong AirfixGpuBudgetReservationHolder *_gpuBudgetReservationHolder;
  airfix::render::RenderTargetPixelExtent _extent;
}
@end

@implementation AirfixMetalScaledSceneTargetBundle

- (void)dealloc {
  AirfixGpuBudgetReservationHolder *reservationHolder =
      _gpuBudgetReservationHolder;
  _gpuBudgetReservationHolder = nil;
  @autoreleasepool {
    id<MTLTexture> __attribute__((objc_precise_lifetime)) color = _colorTexture;
    id<MTLTexture> __attribute__((objc_precise_lifetime)) depth = _depthTexture;
    _colorTexture = nil;
    _depthTexture = nil;
    (void)color;
    (void)depth;
  }
  if (reservationHolder != nil) {
    reservationHolder->_reservation.reset();
  }
}

@end

@interface AirfixMetalPresentationTransactionHolder : NSObject {
@public
  airfix::render::RenderPresentationTransaction _transaction;
  airfix::render::RenderPresentationRetrySchedule _retrySchedule;
  airfix::render::RenderPresentationSettings _desiredSettings;
  std::uint64_t _surfaceGeneration;
}
@end

@implementation AirfixMetalPresentationTransactionHolder

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _surfaceGeneration = 1U;
  }
  return self;
}

@end

@interface AirfixMetalPresentationRequest : NSObject {
@public
  airfix::render::RenderPresentationTransaction _transactionSnapshot;
  airfix::render::RenderPresentationSettings _candidate;
  airfix::render::RenderPresentationSurfaceStamp _surface;
  __strong NSObject *_ownerToken;
  __strong AirfixMetalPresentationTransactionHolder *_transactionHolder;
  __strong AirfixSnapshotGpuBudgetLedgerHolder *_gpuBudgetHolder;
  __strong MTKView *_view;
  __strong id<MTLDevice> _device;
  __strong dispatch_queue_t _releaseQueue;
}
@end

@implementation AirfixMetalPresentationRequest
@end

@interface AirfixPreparedMetalPresentation : NSObject {
@public
  std::unique_ptr<airfix::render::PreparedRenderPresentationState> _prepared;
  airfix::render::RenderPresentationSettings _candidate;
  __strong NSObject *_ownerToken;
  __strong AirfixMetalPresentationTransactionHolder *_transactionHolder;
  __strong AirfixSnapshotGpuBudgetLedgerHolder *_gpuBudgetHolder;
  __strong MTKView *_view;
  __strong id<MTLDevice> _device;
  __strong dispatch_queue_t _releaseQueue;
  BOOL _published;
}
@end

@implementation AirfixPreparedMetalPresentation

- (void)dealloc {
  auto *prepared = _prepared.release();
  dispatch_queue_t releaseQueue = _releaseQueue;
  if (prepared != nullptr && releaseQueue != nil) {
    dispatch_async(releaseQueue, ^{
      delete prepared;
    });
  } else {
    delete prepared;
  }
}

@end

@interface AirfixMetalRoomSnapshot : NSObject {
@public
  __strong AirfixMetalRoomResources *_resources;
  __strong dispatch_queue_t _releaseQueue;
  __strong AirfixGpuBudgetReservationHolder *_gpuBudgetReservationHolder;
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
    AirfixGpuBudgetReservationHolder *reservationHolder =
        _gpuBudgetReservationHolder;
    void *retainedResources = (__bridge_retained void *)_resources;
    _resources = nil;
    _gpuBudgetReservationHolder = nil;
    dispatch_async(_releaseQueue, ^{
      @autoreleasepool {
        AirfixMetalRoomResources *__attribute__((
            objc_precise_lifetime)) resources =
            (__bridge_transfer AirfixMetalRoomResources *)retainedResources;
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
  AirfixGpuBudgetReservationHolder *reservationHolder =
      _gpuBudgetReservationHolder;
  _gpuBudgetReservationHolder = nil;
  @autoreleasepool {
    AirfixMetalRoomResources *__attribute__((objc_precise_lifetime)) resources =
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
  __strong AirfixGpuBudgetReservationHolder *_gpuBudgetReservationHolder;
}
@end

@implementation AirfixBudgetedMetalTexture

- (void)dealloc {
  AirfixGpuBudgetReservationHolder *reservationHolder =
      _gpuBudgetReservationHolder;
  _gpuBudgetReservationHolder = nil;
  @autoreleasepool {
    id<MTLTexture> __attribute__((objc_precise_lifetime)) texture = _texture;
    _texture = nil;
    (void)texture;
  }
  @autoreleasepool {
    id<MTLHeap> __attribute__((objc_precise_lifetime)) heap = _heap;
    _heap = nil;
    (void)heap;
  }
  if (reservationHolder != nil) {
    reservationHolder->_reservation.reset();
  }
}

@end

namespace {

struct MetalPresentationTargetFactoryContext final {
  __strong AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder;
};

[[nodiscard]] airfix::render::RenderPresentationSurfaceStamp
presentationSurfaceStamp(MTKView *view,
                         const airfix::render::OutputPixelExtent outputExtent,
                         const std::uint64_t generation) noexcept {
  return {
      .viewIdentity = (__bridge const void *)view,
      .deviceIdentity = (__bridge const void *)view.device,
      .outputExtent = outputExtent,
      .generation = generation,
  };
}

[[nodiscard]] airfix::render::RenderPresentationTargetBundle
prepareMetalPresentationTarget(
    void *opaqueContext,
    const airfix::render::RenderPresentationSurfaceStamp &surface,
    const airfix::render::RenderTargetPixelExtent targetExtent,
    const std::size_t minimumAccountedBytes) noexcept {
  try {
    auto *context =
        static_cast<MetalPresentationTargetFactoryContext *>(opaqueContext);
    AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder =
        context != nullptr ? context->gpuBudgetHolder : nil;
    id<MTLDevice> device =
        (__bridge id<MTLDevice>)const_cast<void *>(surface.deviceIdentity);
    if (gpuBudgetHolder == nil || gpuBudgetHolder->_ledger == nullptr ||
        device == nil || targetExtent.width == 0U ||
        targetExtent.height == 0U ||
        targetExtent.width > kMaximumScaledSceneDimension ||
        targetExtent.height > kMaximumScaledSceneDimension ||
        minimumAccountedBytes == 0U ||
        minimumAccountedBytes > kMaximumScaledSceneTargetBytes) {
      return {};
    }

    auto reservation =
        gpuBudgetHolder->_ledger->tryReserve(minimumAccountedBytes);
    if (!reservation.has_value()) {
      return {};
    }

    const NSUInteger width = static_cast<NSUInteger>(targetExtent.width);
    const NSUInteger height = static_cast<NSUInteger>(targetExtent.height);
    MTLTextureDescriptor *colorDescriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:width
                                    height:height
                                 mipmapped:NO];
    MTLTextureDescriptor *depthDescriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:width
                                    height:height
                                 mipmapped:NO];
    if (colorDescriptor == nil || depthDescriptor == nil) {
      return {};
    }
    colorDescriptor.storageMode = MTLStorageModePrivate;
    colorDescriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
    colorDescriptor.usage =
        MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    depthDescriptor.storageMode = MTLStorageModePrivate;
    depthDescriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
    depthDescriptor.usage = MTLTextureUsageRenderTarget;

    id<MTLTexture> colorTexture =
        [device newTextureWithDescriptor:colorDescriptor];
    if (colorTexture == nil) {
      return {};
    }
    id<MTLTexture> depthTexture =
        [device newTextureWithDescriptor:depthDescriptor];
    if (depthTexture == nil || colorTexture.allocatedSize == 0U ||
        depthTexture.allocatedSize == 0U) {
      return {};
    }
    colorTexture.label = @"Airfix scaled 3D scene color";
    depthTexture.label = @"Airfix scaled 3D scene depth";

    std::size_t actualBytes = 0U;
    if (!checkedAdd(static_cast<std::size_t>(colorTexture.allocatedSize),
                    static_cast<std::size_t>(depthTexture.allocatedSize),
                    actualBytes) ||
        actualBytes > kMaximumScaledSceneTargetBytes ||
        !finalizeHeapAllocationReservation(*gpuBudgetHolder->_ledger,
                                           *reservation, actualBytes)) {
      return {};
    }

    AirfixGpuBudgetReservationHolder *reservationHolder =
        [[AirfixGpuBudgetReservationHolder alloc] init];
    AirfixMetalScaledSceneTargetBundle *bundle =
        [[AirfixMetalScaledSceneTargetBundle alloc] init];
    if (reservationHolder == nil || bundle == nil) {
      return {};
    }
    reservationHolder->_reservation.emplace(std::move(*reservation));
    bundle->_colorTexture = colorTexture;
    bundle->_depthTexture = depthTexture;
    bundle->_gpuBudgetReservationHolder = reservationHolder;
    bundle->_extent = targetExtent;

    void *retainedBundle = (__bridge_retained void *)bundle;
    std::shared_ptr<const void> owner(
        retainedBundle, [](const void *pointer) noexcept {
          @autoreleasepool {
            id __attribute__((objc_precise_lifetime)) object =
                (__bridge_transfer id) const_cast<void *>(pointer);
            (void)object;
          }
        });
    return {
        std::move(owner),
        (__bridge const void *)colorTexture,
        (__bridge const void *)depthTexture,
        actualBytes,
    };
  } catch (...) {
    return {};
  }
}

[[nodiscard]] AirfixMetalScaledSceneTargetBundle *metalPresentationTargetBundle(
    const airfix::render::RenderPresentationTargetBundle
        &targetBundle) noexcept {
  if (!targetBundle.complete()) {
    return nil;
  }
  AirfixMetalScaledSceneTargetBundle *bundle =
      (__bridge AirfixMetalScaledSceneTargetBundle *)const_cast<void *>(
          targetBundle.owner().get());
  if (bundle == nil || bundle->_colorTexture == nil ||
      bundle->_depthTexture == nil ||
      (__bridge const void *)bundle->_colorTexture !=
          targetBundle.colorIdentity() ||
      (__bridge const void *)bundle->_depthTexture !=
          targetBundle.depthIdentity()) {
    return nil;
  }
  return bundle;
}

bool hasActiveGpuBudgetReservation(AirfixMetalRoomSnapshot *snapshot) noexcept {
  if (snapshot == nil || snapshot->_gpuBudgetReservationHolder == nil) {
    return false;
  }
  const auto &reservation = snapshot->_gpuBudgetReservationHolder->_reservation;
  return reservation.has_value() && reservation->active();
}

} // namespace

@interface AirfixPreparedMetalRoom : NSObject {
@public
  __strong NSObject *_ownerToken;
  __strong id<MTLDevice> _device;
  __strong AirfixMetalRoomSnapshot *_snapshot;
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

template <typename LoadedTexture>
void configurePrivateTextureDescriptor(MTLTextureDescriptor *descriptor,
                                       const LoadedTexture &source) noexcept {
  const auto &upload = source.upload;
  const auto &base = upload.uploadLevels.front();
  descriptor.textureType = MTLTextureType2D;
  switch (airfix::render::rgba8TextureEncoding(upload.sampleSpace)) {
  case airfix::render::Rgba8TextureEncoding::unorm:
    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    break;
  case airfix::render::Rgba8TextureEncoding::unormSrgb:
    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
    break;
  case airfix::render::Rgba8TextureEncoding::invalid:
    descriptor.pixelFormat = MTLPixelFormatInvalid;
    break;
  }
  descriptor.width = static_cast<NSUInteger>(base.width);
  descriptor.height = static_cast<NSUInteger>(base.height);
  descriptor.depth = 1U;
  descriptor.mipmapLevelCount =
      static_cast<NSUInteger>(upload.allocatedMipCount);
  descriptor.sampleCount = 1U;
  descriptor.arrayLength = 1U;
  descriptor.usage = MTLTextureUsageShaderRead;
  descriptor.storageMode = MTLStorageModeShared;
  descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
}

bool uint64ToSize(const std::uint64_t value, std::size_t &output) noexcept {
  if constexpr (sizeof(std::uint64_t) > sizeof(std::size_t)) {
    if (value > std::numeric_limits<std::size_t>::max()) {
      return false;
    }
  }
  output = static_cast<std::size_t>(value);
  return true;
}

[[nodiscard]] airfix::render::TextureAssetId
textureAssetId(const airfix::content::LoadedTextureAsset &texture) noexcept {
  return texture.assetId;
}

[[nodiscard]] airfix::render::TextureAssetId
textureAssetId(const airfix::content::LoadedLegacyWeaponCrosshairTexture
                   &texture) noexcept {
  return texture.textureId;
}

[[nodiscard]] airfix::render::TextureAssetId
textureAssetId(const airfix::content::LoadedLegacyAircraftHealthGaugeTexture
                   &texture) noexcept {
  return texture.textureId;
}

[[nodiscard]] airfix::render::TextureAssetId textureAssetId(
    const airfix::content::LoadedLegacyAircraftHudRollingDigitsTexture
        &texture) noexcept {
  return texture.textureId;
}

[[nodiscard]] airfix::render::TextureAssetId
textureAssetId(const airfix::content::LoadedLegacyAircraftHudInstrumentTexture
                   &texture) noexcept {
  return texture.textureId;
}

[[nodiscard]] airfix::render::TextureAssetId
textureAssetId(const airfix::content::LoadedLegacyAircraftHudWeaponPanelTexture
                   &texture) noexcept {
  return texture.textureId;
}

[[nodiscard]] airfix::render::TextureAssetId textureAssetId(
    const airfix::content::LoadedLegacyAircraftHudIdentityStatusTexture
        &texture) noexcept {
  return texture.textureId;
}

template <typename LoadedTexture>
bool validateTextureAsset(const LoadedTexture &texture,
                          const std::size_t textureIndex,
                          std::size_t &aggregateGpuBytes) noexcept {
  using airfix::render::GtiMipPolicy;

  const auto &upload = texture.upload;
  if (textureIndex > std::numeric_limits<std::uint32_t>::max() ||
      textureAssetId(texture).value !=
          static_cast<std::uint32_t>(textureIndex) ||
      upload.request.assetId != textureAssetId(texture) ||
      upload.request.archiveFileIndex != texture.sourceFileIndex ||
      !airfix::render::validTextureSampleSpace(upload.sampleSpace) ||
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
      !fitsNSUInteger(static_cast<std::size_t>(upload.allocatedMipCount))) {
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
  for (std::size_t levelIndex = 0U; levelIndex < upload.uploadLevels.size();
       ++levelIndex) {
    const auto &level = upload.uploadLevels[levelIndex];
    const auto &image = texture.uploadLevels[levelIndex];
    if (levelIndex > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    const auto levelNumber = static_cast<std::uint32_t>(levelIndex);
    const auto expectedWidth = std::max(1U, baseWidth >> levelNumber);
    const auto expectedHeight = std::max(1U, baseHeight >> levelNumber);
    std::size_t rowBytes = 0U;
    std::size_t rgbaBytes = 0U;
    if (!checkedMultiply(static_cast<std::size_t>(expectedWidth), 4U,
                         rowBytes) ||
        !checkedMultiply(rowBytes, static_cast<std::size_t>(expectedHeight),
                         rgbaBytes) ||
        level.level != levelNumber || level.width != expectedWidth ||
        level.height != expectedHeight || level.bytesPerRow != rowBytes ||
        level.rgbaBytes != rgbaBytes || image.width != expectedWidth ||
        image.height != expectedHeight || image.pixels.size() != rgbaBytes ||
        !fitsNSUInteger(rowBytes) || !fitsNSUInteger(rgbaBytes) ||
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
  for (std::uint32_t level = 0U; level < upload.allocatedMipCount; ++level) {
    std::size_t rowBytes = 0U;
    std::size_t levelBytes = 0U;
    if (!checkedMultiply(static_cast<std::size_t>(residentWidth), 4U,
                         rowBytes) ||
        !checkedMultiply(rowBytes, static_cast<std::size_t>(residentHeight),
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
         accountGpuBytes(residentSize, aggregateGpuBytes,
                         kMaximumPrivateRoomLogicalGpuBytes);
}

bool preflightPrivateRoom(id<MTLDevice> device,
                          const airfix::content::LoadedMissionWorldRoom &room,
                          PrivateRoomPreflight &result) {
  if (room.revision.generation == 0U || room.revision.pack.size == 0U ||
      airfix::content::validateMissionWorldRoomPublication(room, room.revision)
          .has_value() ||
      !fitsNSUInteger(room.model.meshes.size()) ||
      !fitsNSUInteger(room.textures.size()) ||
      room.submission.meshUploads.size() != room.model.meshes.size()) {
    return false;
  }

  const auto regenerated =
      airfix::render::buildDrawSubmissionPlan(room.model, room.textures.size());
  if (!regenerated.plan.has_value() || !regenerated.issues.empty() ||
      regenerated.plan->meshUploads != room.submission.meshUploads ||
      regenerated.plan->commands != room.submission.commands) {
    return false;
  }

  const auto maximumBufferLength =
      static_cast<std::size_t>(device.maxBufferLength);
  result.vertexByteCounts.reserve(room.submission.meshUploads.size());
  result.indexByteCounts.reserve(room.submission.meshUploads.size());
  for (std::size_t uploadIndex = 0U;
       uploadIndex < room.submission.meshUploads.size(); ++uploadIndex) {
    const auto &upload = room.submission.meshUploads[uploadIndex];
    if (static_cast<std::size_t>(upload.meshSlot) != uploadIndex ||
        uploadIndex >= room.model.meshes.size()) {
      return false;
    }
    const auto &mesh = room.model.meshes[upload.meshSlot];
    std::size_t vertexBytes = 0U;
    std::size_t indexBytes = 0U;
    if (upload.vertexCount != mesh.vertices.size() ||
        upload.indexCount != mesh.indices.size() ||
        !checkedMultiply(mesh.vertices.size(), sizeof(GpuVertex),
                         vertexBytes) ||
        !checkedMultiply(mesh.indices.size(), sizeof(std::uint32_t),
                         indexBytes) ||
        vertexBytes > maximumBufferLength || indexBytes > maximumBufferLength ||
        !accountGpuBytes(vertexBytes, result.aggregateGpuBytes,
                         kMaximumPrivateRoomLogicalGpuBytes) ||
        !accountGpuBytes(indexBytes, result.aggregateGpuBytes,
                         kMaximumPrivateRoomLogicalGpuBytes) ||
        !accountCpuPackedBytes(vertexBytes, result.aggregateCpuPackedBytes,
                               kMaximumPrivateRoomCpuPackedBytes)) {
      return false;
    }
    result.vertexByteCounts.push_back(vertexBytes);
    result.indexByteCounts.push_back(indexBytes);
  }

  result.indexOffsets.reserve(room.submission.commands.size());
  for (const auto &command : room.submission.commands) {
    if (command.instanceIndex >= room.model.instances.size() ||
        static_cast<std::size_t>(command.meshSlot) >=
            result.indexByteCounts.size() ||
        room.model.instances[command.instanceIndex].meshSlot !=
            command.meshSlot) {
      return false;
    }
    const auto &mesh = room.model.meshes[command.meshSlot];
    if (command.rangeIndex >= mesh.ranges.size()) {
      return false;
    }
    const auto &range = mesh.ranges[command.rangeIndex];
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
    if (!checkedMultiply(static_cast<std::size_t>(command.firstIndex),
                         sizeof(std::uint32_t), offset) ||
        !checkedMultiply(static_cast<std::size_t>(command.indexCount),
                         sizeof(std::uint32_t), drawBytes) ||
        !checkedAdd(offset, drawBytes, drawEnd) ||
        offset >= result.indexByteCounts[command.meshSlot] ||
        drawEnd > result.indexByteCounts[command.meshSlot] ||
        !fitsNSUInteger(offset)) {
      return false;
    }
    result.indexOffsets.push_back(static_cast<NSUInteger>(offset));
  }

  for (std::size_t textureIndex = 0U; textureIndex < room.textures.size();
       ++textureIndex) {
    if (!validateTextureAsset(room.textures[textureIndex], textureIndex,
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
@property(nonatomic, strong) id<MTLRenderPipelineState> gameplayPipelineState;
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLDepthStencilState> gameplayDepthState;
@property(nonatomic, strong) id<MTLDepthStencilState> crosshairDepthState;
@property(nonatomic, strong) id<MTLDepthStencilState> healthGaugeDepthState;
@property(nonatomic, strong) id<MTLSamplerState> classicSceneSamplerState;
@property(nonatomic, strong) id<MTLSamplerState> enhancedSceneSamplerState;
@property(nonatomic, strong) id<MTLSamplerState> overlaySamplerState;
@property(nonatomic, strong) id<MTLSamplerState> crosshairSamplerState;
@property(nonatomic, strong) id<MTLSamplerState> healthGaugeSamplerState;
@property(nonatomic, strong) id<MTLRenderPipelineState>
    presentationPipelineState;
@property(nonatomic, strong) id<MTLRenderPipelineState> overlayPipelineState;
@property(nonatomic, strong) id<MTLRenderPipelineState>
    weaponPanelMultiplyPipelineState;
@property(nonatomic, strong) id<MTLRenderPipelineState> crosshairPipelineState;
@property(nonatomic, strong) id<MTLRenderPipelineState>
    healthGaugeTexturePipelineState;
@property(nonatomic, strong) id<MTLRenderPipelineState>
    healthGaugeSolidPipelineState;
@property(nonatomic, strong) id<MTLSamplerState> presentationSamplerState;
@property(nonatomic, strong) id<MTLTexture> diagnosticsOverlayTexture;
@property(nonatomic) NSUInteger diagnosticsOverlayWidth;
@property(nonatomic) NSUInteger diagnosticsOverlayHeight;
@property(nonatomic) NSUInteger diagnosticsOverlayPixelScale;
@property(nonatomic, strong) AirfixBudgetedMetalTexture *fallbackResource;
@property(atomic, strong) AirfixMetalRoomSnapshot *roomSnapshot;
@property(nonatomic, weak) MTKView *metalView;
#if AIRFIX_IOS_SIMULATOR_SMOKE
@property(nonatomic, copy, nullable) void (^simulatorSmokeFrameObserver)
    (BOOL publicSyntheticScene, NSUInteger sceneDrawCallCount,
     NSUInteger sceneTriangleCount, NSError *_Nullable error);
#endif
@property(nonatomic, strong)
    AirfixMetalPresentationTransactionHolder *presentationTransactionHolder;
@property(nonatomic, strong) NSObject *preparationOwnerToken;
@property(nonatomic, strong) dispatch_queue_t resourceReleaseQueue;
@property(nonatomic, strong)
    AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder;
@property(nonatomic, strong) AirfixMetalDiagnosticsState *diagnosticsState;

- (BOOL)updateDiagnosticsOverlayTextureWithDevice:(id<MTLDevice>)device;
- (BOOL)encodeLegacyWeaponCrosshairSprite:
            (const airfix::content::LegacyWeaponCrosshairSpriteSubmission &)
                submission
                                resources:(AirfixMetalRoomResources *)resources
                            commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                               renderPass:(MTLRenderPassDescriptor *)renderPass
                             outputExtent:
                                 (airfix::render::OutputPixelExtent)outputExtent
                     drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture;
- (NSUInteger)
    encodeLegacyAircraftHealthGauge:
        (const airfix::content::LegacyAircraftHealthGaugeSubmission &)submission
                          resources:(AirfixMetalRoomResources *)resources
                      commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                         renderPass:(MTLRenderPassDescriptor *)renderPass
                       outputExtent:
                           (airfix::render::OutputPixelExtent)outputExtent
               drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture;
- (NSUInteger)
    encodeLegacyAircraftHudInstruments:
        (const airfix::content::LegacyAircraftHudInstrumentsSubmission &)
            submission
                             resources:(AirfixMetalRoomResources *)resources
                         commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                            renderPass:(MTLRenderPassDescriptor *)renderPass
                          outputExtent:
                              (airfix::render::OutputPixelExtent)outputExtent
                  drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture;
- (NSUInteger)
    encodeLegacyAircraftHudRollingDigits:
        (const airfix::content::LegacyAircraftHudRollingDigitsSubmission &)
            submission
                               resources:(AirfixMetalRoomResources *)resources
                           commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                              renderPass:(MTLRenderPassDescriptor *)renderPass
                            outputExtent:
                                (airfix::render::OutputPixelExtent)outputExtent
                    drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture;
- (NSUInteger)
    encodeLegacyAircraftHudWeaponPanels:
        (const airfix::content::LegacyAircraftHudWeaponPanelsSubmission &)
            submission
                              resources:(AirfixMetalRoomResources *)resources
                          commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                             renderPass:(MTLRenderPassDescriptor *)renderPass
                           outputExtent:
                               (airfix::render::OutputPixelExtent)outputExtent
                   drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture;
- (NSUInteger)
    encodeLegacyAircraftHudIdentityStatus:
        (const airfix::content::LegacyAircraftHudIdentityStatusSubmission &)
            submission
                                resources:(AirfixMetalRoomResources *)resources
                            commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                               renderPass:(MTLRenderPassDescriptor *)renderPass
                             outputExtent:
                                 (airfix::render::OutputPixelExtent)outputExtent
                     drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture;
- (NSUInteger)
    encodeLegacyAircraftHudRenderEvent:
        (const airfix::content::LegacyAircraftHudRenderEvent &)event
                             resources:(AirfixMetalRoomResources *)resources
                         commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                            renderPass:(MTLRenderPassDescriptor *)renderPass
                          outputExtent:
                              (airfix::render::OutputPixelExtent)outputExtent
                  drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture;
@end

@implementation AirfixMetalRenderer

#if AIRFIX_IOS_SIMULATOR_SMOKE
- (void)setSimulatorSmokeFrameCompletion:
    (void (^)(BOOL, NSUInteger, NSUInteger, NSError *_Nullable))completion {
  NSAssert(NSThread.isMainThread,
           @"Simulator smoke observation belongs to the main thread");
  self.simulatorSmokeFrameObserver = completion;
}
#endif

- (nullable instancetype)initWithMetalView:(MTKView *)metalView
                                     error:
                                         (NSError *_Nullable *_Nullable)error {
  self = [super init];
  if (self == nil) {
    return nil;
  }

  id<MTLDevice> device = metalView.device;
  if (device == nil) {
    if (error != nullptr) {
      *error = makeError(RendererError::missingDevice,
                         @"Metal is not available on this device.");
    }
    return nil;
  }

  // No C++ allocation or conversion failure may escape this Objective-C
  // initializer boundary.
  try {
    airfix::render::PublicRenderSmokeScene smokeScene =
        airfix::render::makePublicRenderSmokeScene();
    airfix::render::DrawModelPayload payload = std::move(smokeScene.model);
    airfix::render::DrawSubmissionDescription submission =
        airfix::render::buildDrawSubmissionPlan(payload, 1U);
    if (!submission.plan.has_value() || !submission.issues.empty()) {
      if (error != nullptr) {
        *error = makeError(RendererError::invalidPayload,
                           @"The public Metal smoke-test payload is invalid.");
      }
      return nil;
    }
    airfix::render::DrawSubmissionPlan submissionPlan =
        std::move(submission.plan.value());
    if (submissionPlan.meshUploads.size() != payload.meshes.size() ||
        !fitsNSUInteger(submissionPlan.meshUploads.size())) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::invalidPayload,
            @"The public Metal smoke-test upload plan is inconsistent.");
      }
      return nil;
    }

    id<MTLCommandQueue> commandQueue = [device newCommandQueue];
    if (commandQueue == nil) {
      if (error != nullptr) {
        *error = makeError(RendererError::bufferCreation,
                           @"Metal could not create a command queue.");
      }
      return nil;
    }
    NSObject *preparationOwnerToken = [[NSObject alloc] init];
    if (preparationOwnerToken == nil) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::unexpectedFailure,
            @"Metal room preparation ownership could not be created.");
      }
      return nil;
    }
    AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder =
        [[AirfixSnapshotGpuBudgetLedgerHolder alloc] init];
    AirfixMetalDiagnosticsState *diagnosticsState =
        [[AirfixMetalDiagnosticsState alloc] init];
    AirfixMetalPresentationTransactionHolder *presentationTransactionHolder =
        [[AirfixMetalPresentationTransactionHolder alloc] init];
    if (gpuBudgetHolder == nil || diagnosticsState == nil ||
        presentationTransactionHolder == nil) {
      if (error != nullptr) {
        *error = makeError(RendererError::unexpectedFailure,
                           @"Metal GPU accounting, diagnostics, or "
                           @"presentation state could not be created.");
      }
      return nil;
    }
    gpuBudgetHolder->_ledger =
        std::make_shared<airfix::render::SnapshotGpuBudgetLedger>();
    dispatch_queue_t resourceReleaseQueue = dispatch_queue_create(
        "com.tryk016.airfixdogfighter.metal-resource-release",
        DISPATCH_QUEUE_SERIAL);
    if (resourceReleaseQueue == nil) {
      if (error != nullptr) {
        *error =
            makeError(RendererError::unexpectedFailure,
                      @"Metal resource release queue could not be created.");
      }
      return nil;
    }

    NSError *libraryError = nil;
    id<MTLLibrary> library =
        [device newDefaultLibraryWithBundle:NSBundle.mainBundle
                                      error:&libraryError];
    if (library == nil) {
      if (error != nullptr) {
        NSString *reason = libraryError.localizedDescription;
        if (reason == nil) {
          reason = @"default.metallib is missing.";
        }
        *error = makeError(RendererError::missingShaderLibrary,
                           [@"Metal shader library could not be loaded: "
                               stringByAppendingString:reason]);
      }
      return nil;
    }

    id<MTLFunction> vertexFunction =
        [library newFunctionWithName:@"airfixVertexMain"];
    id<MTLFunction> gameplayVertexFunction =
        [library newFunctionWithName:@"airfixGameplayVertexMain"];
    id<MTLFunction> presentationVertexFunction =
        [library newFunctionWithName:@"airfixPresentationVertexMain"];
    id<MTLFunction> overlayVertexFunction =
        [library newFunctionWithName:@"airfixOverlayVertexMain"];
    id<MTLFunction> gaugeVertexFunction =
        [library newFunctionWithName:@"airfixGaugeVertexMain"];
    id<MTLFunction> fragmentFunction =
        [library newFunctionWithName:@"airfixFragmentMain"];
    id<MTLFunction> overlayFragmentFunction =
        [library newFunctionWithName:@"airfixOverlayFragmentMain"];
    id<MTLFunction> overlaySolidFragmentFunction =
        [library newFunctionWithName:@"airfixOverlaySolidFragmentMain"];
    id<MTLFunction> gaugeTextureFragmentFunction =
        [library newFunctionWithName:@"airfixGaugeTextureFragmentMain"];
    id<MTLFunction> gaugeSolidFragmentFunction =
        [library newFunctionWithName:@"airfixGaugeSolidFragmentMain"];
    if (vertexFunction == nil || gameplayVertexFunction == nil ||
        presentationVertexFunction == nil || overlayVertexFunction == nil ||
        gaugeVertexFunction == nil || fragmentFunction == nil ||
        overlayFragmentFunction == nil || overlaySolidFragmentFunction == nil ||
        gaugeTextureFragmentFunction == nil ||
        gaugeSolidFragmentFunction == nil) {
      if (error != nullptr) {
        *error =
            makeError(RendererError::missingShaderFunction,
                      @"default.metallib does not contain every Airfix "
                      @"diagnostic, gameplay, presentation, and HUD shader.");
      }
      return nil;
    }

    MTLRenderPipelineDescriptor *pipelineDescriptor =
        [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"Airfix public smoke-test pipeline";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat =
        MTLPixelFormatBGRA8Unorm;
    pipelineDescriptor.colorAttachments[0].blendingEnabled = NO;
    pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    NSError *pipelineError = nil;
    id<MTLRenderPipelineState> pipelineState =
        [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                               error:&pipelineError];
    if (pipelineState == nil) {
      if (error != nullptr) {
        NSString *reason = pipelineError.localizedDescription;
        if (reason == nil) {
          reason = @"unknown pipeline error";
        }
        *error = makeError(RendererError::pipelineCreation,
                           [@"Metal render pipeline creation failed: "
                               stringByAppendingString:reason]);
      }
      return nil;
    }

    pipelineDescriptor.label = @"Airfix gameplay camera pipeline";
    pipelineDescriptor.vertexFunction = gameplayVertexFunction;
    NSError *gameplayPipelineError = nil;
    id<MTLRenderPipelineState> gameplayPipelineState =
        [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                               error:&gameplayPipelineError];
    if (gameplayPipelineState == nil) {
      if (error != nullptr) {
        NSString *reason = gameplayPipelineError.localizedDescription;
        if (reason == nil) {
          reason = @"unknown gameplay pipeline error";
        }
        *error = makeError(RendererError::pipelineCreation,
                           [@"Metal gameplay camera pipeline creation failed: "
                               stringByAppendingString:reason]);
      }
      return nil;
    }

    pipelineDescriptor.label = @"Airfix render-scale presentation pipeline";
    pipelineDescriptor.vertexFunction = presentationVertexFunction;
    pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
    NSError *presentationPipelineError = nil;
    id<MTLRenderPipelineState> presentationPipelineState = [device
        newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                       error:&presentationPipelineError];
    if (presentationPipelineState == nil) {
      if (error != nullptr) {
        NSString *reason = presentationPipelineError.localizedDescription;
        if (reason == nil) {
          reason = @"unknown presentation pipeline error";
        }
        *error = makeError(
            RendererError::pipelineCreation,
            [@"Metal render-scale presentation pipeline creation failed: "
                stringByAppendingString:reason]);
      }
      return nil;
    }

    pipelineDescriptor.label = @"Airfix render diagnostics overlay pipeline";
    pipelineDescriptor.vertexFunction = overlayVertexFunction;
    pipelineDescriptor.fragmentFunction = overlayFragmentFunction;
    auto *overlayAttachment = pipelineDescriptor.colorAttachments[0];
    overlayAttachment.blendingEnabled = YES;
    overlayAttachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    overlayAttachment.destinationRGBBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    overlayAttachment.rgbBlendOperation = MTLBlendOperationAdd;
    overlayAttachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
    overlayAttachment.destinationAlphaBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    overlayAttachment.alphaBlendOperation = MTLBlendOperationAdd;
    NSError *overlayPipelineError = nil;
    id<MTLRenderPipelineState> overlayPipelineState =
        [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                               error:&overlayPipelineError];
    if (overlayPipelineState == nil) {
      if (error != nullptr) {
        NSString *reason = overlayPipelineError.localizedDescription;
        if (reason == nil) {
          reason = @"unknown overlay pipeline error";
        }
        *error =
            makeError(RendererError::pipelineCreation,
                      [@"Metal diagnostics overlay pipeline creation failed: "
                          stringByAppendingString:reason]);
      }
      return nil;
    }

    MTLRenderPipelineDescriptor *weaponPanelMultiplyDescriptor =
        [pipelineDescriptor copy];
    if (weaponPanelMultiplyDescriptor == nil) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::pipelineCreation,
            @"Metal weapon-panel multiply descriptor could not be allocated.");
      }
      return nil;
    }
    weaponPanelMultiplyDescriptor.label =
        @"Airfix aircraft weapon-panel destination-multiply pipeline";
    weaponPanelMultiplyDescriptor.fragmentFunction =
        overlaySolidFragmentFunction;
    auto *weaponPanelMultiplyAttachment =
        weaponPanelMultiplyDescriptor.colorAttachments[0];
    weaponPanelMultiplyAttachment.blendingEnabled = YES;
    weaponPanelMultiplyAttachment.sourceRGBBlendFactor = MTLBlendFactorZero;
    weaponPanelMultiplyAttachment.destinationRGBBlendFactor =
        MTLBlendFactorSourceColor;
    weaponPanelMultiplyAttachment.rgbBlendOperation = MTLBlendOperationAdd;
    weaponPanelMultiplyAttachment.sourceAlphaBlendFactor = MTLBlendFactorZero;
    weaponPanelMultiplyAttachment.destinationAlphaBlendFactor =
        MTLBlendFactorSourceAlpha;
    weaponPanelMultiplyAttachment.alphaBlendOperation = MTLBlendOperationAdd;
    weaponPanelMultiplyDescriptor.depthAttachmentPixelFormat =
        MTLPixelFormatDepth32Float;
    NSError *weaponPanelMultiplyPipelineError = nil;
    id<MTLRenderPipelineState> weaponPanelMultiplyPipelineState = [device
        newRenderPipelineStateWithDescriptor:weaponPanelMultiplyDescriptor
                                       error:&weaponPanelMultiplyPipelineError];
    if (weaponPanelMultiplyPipelineState == nil) {
      if (error != nullptr) {
        NSString *reason =
            weaponPanelMultiplyPipelineError.localizedDescription;
        if (reason == nil) {
          reason = @"unknown weapon-panel multiply pipeline error";
        }
        *error =
            makeError(RendererError::pipelineCreation,
                      [@"Metal weapon-panel multiply pipeline creation failed: "
                          stringByAppendingString:reason]);
      }
      return nil;
    }

    pipelineDescriptor.label = @"Airfix weapon crosshair sprite pipeline";
    pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    NSError *crosshairPipelineError = nil;
    id<MTLRenderPipelineState> crosshairPipelineState =
        [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                               error:&crosshairPipelineError];
    if (crosshairPipelineState == nil) {
      if (error != nullptr) {
        NSString *reason = crosshairPipelineError.localizedDescription;
        if (reason == nil) {
          reason = @"unknown weapon crosshair pipeline error";
        }
        *error = makeError(RendererError::pipelineCreation,
                           [@"Metal weapon crosshair pipeline creation failed: "
                               stringByAppendingString:reason]);
      }
      return nil;
    }

    pipelineDescriptor.label = @"Airfix aircraft health gauge texture pipeline";
    pipelineDescriptor.vertexFunction = gaugeVertexFunction;
    pipelineDescriptor.fragmentFunction = gaugeTextureFragmentFunction;
    NSError *healthGaugeTexturePipelineError = nil;
    id<MTLRenderPipelineState> healthGaugeTexturePipelineState = [device
        newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                       error:&healthGaugeTexturePipelineError];
    if (healthGaugeTexturePipelineState == nil) {
      if (error != nullptr) {
        NSString *reason = healthGaugeTexturePipelineError.localizedDescription;
        if (reason == nil) {
          reason = @"unknown aircraft health gauge texture pipeline error";
        }
        *error = makeError(
            RendererError::pipelineCreation,
            [@"Metal aircraft health gauge texture pipeline creation failed: "
                stringByAppendingString:reason]);
      }
      return nil;
    }

    pipelineDescriptor.label = @"Airfix aircraft health gauge solid pipeline";
    pipelineDescriptor.fragmentFunction = gaugeSolidFragmentFunction;
    pipelineDescriptor.colorAttachments[0].blendingEnabled = NO;
    NSError *healthGaugeSolidPipelineError = nil;
    id<MTLRenderPipelineState> healthGaugeSolidPipelineState = [device
        newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                       error:&healthGaugeSolidPipelineError];
    if (healthGaugeSolidPipelineState == nil) {
      if (error != nullptr) {
        NSString *reason = healthGaugeSolidPipelineError.localizedDescription;
        if (reason == nil) {
          reason = @"unknown aircraft health gauge solid pipeline error";
        }
        *error = makeError(
            RendererError::pipelineCreation,
            [@"Metal aircraft health gauge solid pipeline creation failed: "
                stringByAppendingString:reason]);
      }
      return nil;
    }

    MTLDepthStencilDescriptor *depthDescriptor =
        [[MTLDepthStencilDescriptor alloc] init];
    depthDescriptor.depthCompareFunction = MTLCompareFunctionLess;
    depthDescriptor.depthWriteEnabled = YES;
    id<MTLDepthStencilState> depthState =
        [device newDepthStencilStateWithDescriptor:depthDescriptor];
    if (depthState == nil) {
      if (error != nullptr) {
        *error = makeError(RendererError::depthStateCreation,
                           @"Metal depth state creation failed.");
      }
      return nil;
    }

    const auto gameplayDepthContract =
        airfix::render::legacyDepthStateForMode(1U);
    if (!gameplayDepthContract.has_value() ||
        gameplayDepthContract->compare !=
            airfix::render::LegacyDepthCompare::greaterEqual ||
        !gameplayDepthContract->writeEnabled) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::depthStateCreation,
            @"The recovered gameplay reverse-depth contract is unavailable.");
      }
      return nil;
    }
    depthDescriptor.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    depthDescriptor.depthWriteEnabled = gameplayDepthContract->writeEnabled;
    id<MTLDepthStencilState> gameplayDepthState =
        [device newDepthStencilStateWithDescriptor:depthDescriptor];
    if (gameplayDepthState == nil) {
      if (error != nullptr) {
        *error =
            makeError(RendererError::depthStateCreation,
                      @"Metal gameplay reverse-depth state creation failed.");
      }
      return nil;
    }

    depthDescriptor.depthCompareFunction = MTLCompareFunctionAlways;
    depthDescriptor.depthWriteEnabled = YES;
    id<MTLDepthStencilState> crosshairDepthState =
        [device newDepthStencilStateWithDescriptor:depthDescriptor];
    if (crosshairDepthState == nil) {
      if (error != nullptr) {
        *error =
            makeError(RendererError::depthStateCreation,
                      @"Metal weapon crosshair depth state creation failed.");
      }
      return nil;
    }

    MTLSamplerDescriptor *samplerDescriptor =
        [[MTLSamplerDescriptor alloc] init];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNearest;
    samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    const auto classicSampling =
        airfix::render::sceneTextureSamplingPolicyForProfile(
            airfix::render::VisualProfile::classic);
    const auto enhancedSampling =
        airfix::render::sceneTextureSamplingPolicyForProfile(
            airfix::render::VisualProfile::enhanced);
    if (!classicSampling.has_value() || !enhancedSampling.has_value()) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::samplerCreation,
            @"The portable scene texture sampling policy is invalid.");
      }
      return nil;
    }
    samplerDescriptor.maxAnisotropy =
        static_cast<NSUInteger>(classicSampling->maximumAnisotropy);
    id<MTLSamplerState> classicSceneSamplerState =
        [device newSamplerStateWithDescriptor:samplerDescriptor];
    if (classicSceneSamplerState == nil) {
      if (error != nullptr) {
        *error = makeError(RendererError::samplerCreation,
                           @"Metal Classic scene sampler creation failed.");
      }
      return nil;
    }
    id<MTLSamplerState> overlaySamplerState =
        [device newSamplerStateWithDescriptor:samplerDescriptor];
    if (overlaySamplerState == nil) {
      if (error != nullptr) {
        *error = makeError(RendererError::samplerCreation,
                           @"Metal overlay sampler creation failed.");
      }
      return nil;
    }
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNearest;
    samplerDescriptor.maxAnisotropy = 1U;
    id<MTLSamplerState> crosshairSamplerState =
        [device newSamplerStateWithDescriptor:samplerDescriptor];
    if (crosshairSamplerState == nil) {
      if (error != nullptr) {
        *error = makeError(RendererError::samplerCreation,
                           @"Metal weapon crosshair sampler creation failed.");
      }
      return nil;
    }
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterLinear;
    samplerDescriptor.maxAnisotropy =
        static_cast<NSUInteger>(enhancedSampling->maximumAnisotropy);
    id<MTLSamplerState> enhancedSceneSamplerState =
        [device newSamplerStateWithDescriptor:samplerDescriptor];
    if (enhancedSceneSamplerState == nil) {
      if (error != nullptr) {
        *error = makeError(RendererError::samplerCreation,
                           @"Metal Enhanced scene sampler creation failed.");
      }
      return nil;
    }
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
    samplerDescriptor.maxAnisotropy = 1U;
    id<MTLSamplerState> presentationSamplerState =
        [device newSamplerStateWithDescriptor:samplerDescriptor];
    if (presentationSamplerState == nil) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::samplerCreation,
            @"Metal render-scale presentation sampler creation failed.");
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
         uploadIndex < submissionPlan.meshUploads.size(); ++uploadIndex) {
      const auto &upload = submissionPlan.meshUploads[uploadIndex];
      if (static_cast<std::size_t>(upload.meshSlot) != uploadIndex ||
          uploadIndex >= payload.meshes.size()) {
        resourcePreflightValid = false;
        break;
      }
      const auto &mesh = payload.meshes[upload.meshSlot];
      if (upload.vertexCount != mesh.vertices.size() ||
          upload.indexCount != mesh.indices.size()) {
        resourcePreflightValid = false;
        break;
      }
      std::size_t vertexBytes = 0U;
      std::size_t indexBytes = 0U;
      if (!checkedMultiply(mesh.vertices.size(), sizeof(GpuVertex),
                           vertexBytes) ||
          !checkedMultiply(mesh.indices.size(), sizeof(std::uint32_t),
                           indexBytes) ||
          vertexBytes > maximumBufferLength ||
          indexBytes > maximumBufferLength ||
          !accountGpuBytes(vertexBytes, aggregateGpuBytes,
                           kMaximumSyntheticLogicalGpuBytes) ||
          !accountGpuBytes(indexBytes, aggregateGpuBytes,
                           kMaximumSyntheticLogicalGpuBytes) ||
          !accountCpuPackedBytes(vertexBytes, aggregateCpuPackedBytes)) {
        resourcePreflightValid = false;
        break;
      }
      vertexByteCounts.push_back(vertexBytes);
      indexByteCounts.push_back(indexBytes);
    }

    const std::size_t syntheticTextureBytes = smokeScene.textureRgba8.size();
    const std::size_t fallbackTextureBytes = smokeScene.fallbackRgba8.size();
    if (!resourcePreflightValid ||
        !accountGpuBytes(syntheticTextureBytes, aggregateGpuBytes,
                         kMaximumSyntheticLogicalGpuBytes) ||
        !accountGpuBytes(fallbackTextureBytes, aggregateGpuBytes,
                         kMaximumSyntheticLogicalGpuBytes)) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The public Metal smoke-test resources exceed "
                           @"their checked GPU budget.");
      }
      return nil;
    }

    std::vector<NSUInteger> indexOffsets;
    indexOffsets.reserve(submissionPlan.commands.size());
    for (const auto &command : submissionPlan.commands) {
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
      const auto &mesh = payload.meshes[command.meshSlot];
      if (command.rangeIndex >= mesh.ranges.size()) {
        resourcePreflightValid = false;
        break;
      }
      const auto &range = mesh.ranges[command.rangeIndex];
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
      if (!checkedMultiply(static_cast<std::size_t>(command.firstIndex),
                           sizeof(std::uint32_t), offset) ||
          !checkedMultiply(static_cast<std::size_t>(command.indexCount),
                           sizeof(std::uint32_t), drawBytes) ||
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
        *error = makeError(RendererError::resourceLimit,
                           @"The public Metal smoke-test draw offsets exceed "
                           @"their checked bounds.");
      }
      return nil;
    }

    // Repack only after all counts, byte lengths, aggregate budgets, command
    // relationships and index ranges have passed preflight.
    std::vector<std::vector<GpuVertex>> packedVertices;
    packedVertices.reserve(submissionPlan.meshUploads.size());
    for (std::size_t meshSlot = 0U; meshSlot < payload.meshes.size();
         ++meshSlot) {
      std::vector<GpuVertex> gpuVertices =
          repackVertices(payload.meshes[meshSlot].vertices);
      std::size_t packedBytes = 0U;
      if (!checkedMultiply(gpuVertices.size(), sizeof(GpuVertex),
                           packedBytes) ||
          packedBytes != vertexByteCounts[meshSlot]) {
        if (error != nullptr) {
          *error = makeError(RendererError::resourceLimit,
                             @"The public Metal smoke-test packed vertex size "
                             @"changed after preflight.");
        }
        return nil;
      }
      packedVertices.push_back(std::move(gpuVertices));
    }

    MTLTextureDescriptor *textureDescriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:2U
                                    height:2U
                                 mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModeShared;
    textureDescriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
    MTLTextureDescriptor *fallbackDescriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:1U
                                    height:1U
                                 mipmapped:NO];
    fallbackDescriptor.usage = MTLTextureUsageShaderRead;
    fallbackDescriptor.storageMode = MTLStorageModeShared;
    fallbackDescriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
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
    for (std::size_t meshSlot = 0U; meshSlot < vertexByteCounts.size();
         ++meshSlot) {
      if ((vertexByteCounts[meshSlot] != 0U &&
           !accountHeapResourcePlacement(
               [device
                   heapBufferSizeAndAlignWithLength:vertexByteCounts[meshSlot]
                                            options:
                                                kSharedTrackedResourceOptions],
               bufferHeapBytes, bufferHeapAlignment,
               kMaximumSyntheticGpuHeapPlanBytes)) ||
          (indexByteCounts[meshSlot] != 0U &&
           !accountHeapResourcePlacement(
               [device
                   heapBufferSizeAndAlignWithLength:indexByteCounts[meshSlot]
                                            options:
                                                kSharedTrackedResourceOptions],
               bufferHeapBytes, bufferHeapAlignment,
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
            [device heapTextureSizeAndAlignWithDescriptor:textureDescriptor],
            textureHeapBytes, textureHeapAlignment,
            kMaximumSyntheticGpuHeapPlanBytes) ||
        !accountHeapResourcePlacement(
            [device heapTextureSizeAndAlignWithDescriptor:fallbackDescriptor],
            fallbackHeapBytes, fallbackHeapAlignment,
            kMaximumSyntheticGpuHeapPlanBytes) ||
        !finalizeHeapPlan(bufferHeapBytes, bufferHeapAlignment,
                          kMaximumSyntheticGpuHeapPlanBytes) ||
        !finalizeHeapPlan(textureHeapBytes, textureHeapAlignment,
                          kMaximumSyntheticGpuHeapPlanBytes) ||
        !finalizeHeapPlan(fallbackHeapBytes, fallbackHeapAlignment,
                          kMaximumSyntheticGpuHeapPlanBytes)) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The public Metal heap plan exceeds its budget.");
      }
      return nil;
    }
    std::size_t admittedHeapPlanBytes = 0U;
    if (!accountGpuBytes(bufferHeapBytes, admittedHeapPlanBytes,
                         kMaximumSyntheticGpuHeapPlanBytes) ||
        !accountGpuBytes(textureHeapBytes, admittedHeapPlanBytes,
                         kMaximumSyntheticGpuHeapPlanBytes) ||
        !accountGpuBytes(fallbackHeapBytes, admittedHeapPlanBytes,
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
        gpuBudgetHolder->_ledger->tryReserve(admittedHeapPlanBytes);
    if (!bootstrapPlanReservation.has_value()) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The public heap plan is unavailable in the "
                           @"aggregate heap-admission budget.");
      }
      return nil;
    }

    // Staging collections and autoreleased Metal command objects drain before
    // the outer plan reservation can be destroyed on any failure.
    @autoreleasepool {
      id<MTLHeap> bufferHeap = newSharedTrackedHeap(
          device, bufferHeapBytes, @"Airfix public snapshot buffer heap");
      id<MTLHeap> textureHeap = newSharedTrackedHeap(
          device, textureHeapBytes, @"Airfix public snapshot texture heap");
      id<MTLHeap> fallbackHeap =
          newSharedTrackedHeap(device, fallbackHeapBytes,
                               @"Airfix persistent fallback texture heap");
      if (bufferHeap == nil || textureHeap == nil || fallbackHeap == nil) {
        if (error != nullptr) {
          *error = makeError(
              RendererError::resourceLimit,
              @"Metal could not create the complete public heap plan.");
        }
        return nil;
      }

      NSMutableArray<AirfixMetalMeshBuffers *> *meshBuffers = [NSMutableArray
          arrayWithCapacity:static_cast<NSUInteger>(packedVertices.size())];
      for (std::size_t meshSlot = 0U; meshSlot < packedVertices.size();
           ++meshSlot) {
        id<MTLBuffer> vertexBuffer = nil;
        if (vertexByteCounts[meshSlot] != 0U) {
          vertexBuffer =
              [bufferHeap newBufferWithLength:vertexByteCounts[meshSlot]
                                      options:kSharedTrackedResourceOptions];
          if (vertexBuffer != nil && vertexBuffer.contents != nullptr) {
            std::memcpy(vertexBuffer.contents, packedVertices[meshSlot].data(),
                        vertexByteCounts[meshSlot]);
          }
        }
        id<MTLBuffer> indexBuffer = nil;
        if (indexByteCounts[meshSlot] != 0U) {
          indexBuffer =
              [bufferHeap newBufferWithLength:indexByteCounts[meshSlot]
                                      options:kSharedTrackedResourceOptions];
          if (indexBuffer != nil && indexBuffer.contents != nullptr) {
            std::memcpy(indexBuffer.contents,
                        payload.meshes[meshSlot].indices.data(),
                        indexByteCounts[meshSlot]);
          }
        }
        if ((vertexByteCounts[meshSlot] != 0U &&
             (vertexBuffer == nil || vertexBuffer.contents == nullptr)) ||
            (indexByteCounts[meshSlot] != 0U &&
             (indexBuffer == nil || indexBuffer.contents == nullptr))) {
          if (error != nullptr) {
            *error = makeError(RendererError::bufferCreation,
                               @"Metal mesh buffer creation failed.");
          }
          return nil;
        }
        AirfixMetalMeshBuffers *buffers = [[AirfixMetalMeshBuffers alloc] init];
        buffers.vertexBuffer = vertexBuffer;
        buffers.indexBuffer = indexBuffer;
        [meshBuffers addObject:buffers];
      }

      id<MTLTexture> syntheticTexture =
          [textureHeap newTextureWithDescriptor:textureDescriptor];
      if (syntheticTexture == nil) {
        if (error != nullptr) {
          *error = makeError(RendererError::textureCreation,
                             @"Metal texture creation failed.");
        }
        return nil;
      }

      [syntheticTexture replaceRegion:MTLRegionMake2D(0U, 0U, 2U, 2U)
                          mipmapLevel:0U
                            withBytes:smokeScene.textureRgba8.data()
                          bytesPerRow:2U * 4U];

      id<MTLTexture> fallbackTexture =
          [fallbackHeap newTextureWithDescriptor:fallbackDescriptor];
      if (fallbackTexture == nil) {
        if (error != nullptr) {
          *error = makeError(RendererError::textureCreation,
                             @"Metal fallback texture creation failed.");
        }
        return nil;
      }
      [fallbackTexture replaceRegion:MTLRegionMake2D(0U, 0U, 1U, 1U)
                         mipmapLevel:0U
                           withBytes:smokeScene.fallbackRgba8.data()
                         bytesPerRow:1U * 4U];
      NSArray<AirfixMetalMeshBuffers *> *meshBufferSnapshot =
          [meshBuffers copy];
      NSArray<id<MTLTexture>> *textureSnapshot = @[ syntheticTexture ];
      for (AirfixMetalMeshBuffers *buffers in meshBufferSnapshot) {
        if ((buffers.vertexBuffer != nil &&
             buffers.vertexBuffer.allocatedSize == 0U) ||
            (buffers.indexBuffer != nil &&
             buffers.indexBuffer.allocatedSize == 0U)) {
          if (error != nullptr) {
            *error = makeError(RendererError::resourceLimit,
                               @"A public heap buffer has no allocation.");
          }
          return nil;
        }
      }
      if (syntheticTexture.allocatedSize == 0U ||
          fallbackTexture.allocatedSize == 0U) {
        if (error != nullptr) {
          *error = makeError(RendererError::resourceLimit,
                             @"A public heap texture has no allocation.");
        }
        return nil;
      }
      std::size_t snapshotHeapCurrentAllocatedBytes = 0U;
      std::size_t fallbackHeapCurrentAllocatedBytes = 0U;
      std::size_t totalHeapCurrentAllocatedBytes = 0U;
      if (!accountCurrentHeapAllocation(bufferHeap,
                                        snapshotHeapCurrentAllocatedBytes) ||
          !accountCurrentHeapAllocation(textureHeap,
                                        snapshotHeapCurrentAllocatedBytes) ||
          !accountCurrentHeapAllocation(fallbackHeap,
                                        fallbackHeapCurrentAllocatedBytes) ||
          !checkedAdd(snapshotHeapCurrentAllocatedBytes,
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
      if (!finalizeHeapAllocationReservation(*gpuBudgetHolder->_ledger,
                                             *bootstrapPlanReservation,
                                             totalHeapCurrentAllocatedBytes)) {
        if (error != nullptr) {
          *error = makeError(RendererError::resourceLimit,
                             @"The public heaps' current allocation is "
                             @"unavailable in the aggregate admission budget.");
        }
        return nil;
      }
      auto fallbackReservation =
          bootstrapPlanReservation->split(fallbackHeapCurrentAllocatedBytes);
      if (!fallbackReservation.has_value()) {
        if (error != nullptr) {
          *error = makeError(
              RendererError::unexpectedFailure,
              @"The public fallback GPU debit could not be separated.");
        }
        return nil;
      }

      AirfixMetalRoomResources *roomResources =
          [[AirfixMetalRoomResources alloc] init];
      AirfixMetalRoomSnapshot *roomSnapshot =
          [[AirfixMetalRoomSnapshot alloc] init];
      AirfixBudgetedMetalTexture *fallbackResource =
          [[AirfixBudgetedMetalTexture alloc] init];
      AirfixGpuBudgetReservationHolder *snapshotReservationHolder =
          [[AirfixGpuBudgetReservationHolder alloc] init];
      AirfixGpuBudgetReservationHolder *fallbackReservationHolder =
          [[AirfixGpuBudgetReservationHolder alloc] init];
      if (roomResources == nil || roomSnapshot == nil ||
          fallbackResource == nil || snapshotReservationHolder == nil ||
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
      roomSnapshot->_gpuBudgetReservationHolder = snapshotReservationHolder;
      fallbackResource->_texture = fallbackTexture;
      fallbackResource->_heap = fallbackHeap;
      fallbackResource->_gpuBudgetReservationHolder = fallbackReservationHolder;

      // Publish only after the complete renderer candidate has been validated
      // and every Metal object has been created successfully.
      self.commandQueue = commandQueue;
      self.pipelineState = pipelineState;
      self.gameplayPipelineState = gameplayPipelineState;
      self.depthState = depthState;
      self.gameplayDepthState = gameplayDepthState;
      self.crosshairDepthState = crosshairDepthState;
      self.healthGaugeDepthState = crosshairDepthState;
      self.classicSceneSamplerState = classicSceneSamplerState;
      self.enhancedSceneSamplerState = enhancedSceneSamplerState;
      self.overlaySamplerState = overlaySamplerState;
      self.crosshairSamplerState = crosshairSamplerState;
      self.healthGaugeSamplerState = crosshairSamplerState;
      self.presentationPipelineState = presentationPipelineState;
      self.overlayPipelineState = overlayPipelineState;
      self.weaponPanelMultiplyPipelineState = weaponPanelMultiplyPipelineState;
      self.crosshairPipelineState = crosshairPipelineState;
      self.healthGaugeTexturePipelineState = healthGaugeTexturePipelineState;
      self.healthGaugeSolidPipelineState = healthGaugeSolidPipelineState;
      self.presentationSamplerState = presentationSamplerState;
      self.fallbackResource = fallbackResource;
      self.roomSnapshot = roomSnapshot;
      self.metalView = metalView;
      self.presentationTransactionHolder = presentationTransactionHolder;
      self.preparationOwnerToken = preparationOwnerToken;
      self.resourceReleaseQueue = resourceReleaseQueue;
      self.gpuBudgetHolder = gpuBudgetHolder;
      self.diagnosticsState = diagnosticsState;

      const auto initialExtent = outputPixelExtent(metalView.drawableSize);
      if (initialExtent.has_value()) {
        const auto surface = presentationSurfaceStamp(
            metalView, *initialExtent,
            presentationTransactionHolder->_surfaceGeneration);
        auto prepared = presentationTransactionHolder->_transaction.prepare(
            presentationTransactionHolder->_desiredSettings, surface);
        if (!prepared.complete() ||
            presentationTransactionHolder->_transaction
                .finalValidate(*prepared.prepared, surface)
                .has_value()) {
          if (error != nullptr) {
            *error = makeError(RendererError::unexpectedFailure,
                               @"The initial Metal presentation snapshot could "
                               @"not be prepared.");
          }
          return nil;
        }
        presentationTransactionHolder->_transaction.commitValidated(
            std::move(*prepared.prepared));
      }

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
  AirfixMetalRoomSnapshot *snapshot = self.roomSnapshot;
  return snapshot != nil && snapshot->_worldRoomInstalled;
}

- (BOOL)
    prepareAndPublishRenderPresentationSettings:
        (const airfix::render::RenderPresentationSettings &)candidate
                                        forView:(MTKView *)view
                                   outputExtent:
                                       (airfix::render::OutputPixelExtent)
                                           outputExtent
                                          error:(NSError *_Nullable *_Nullable)
                                                    error {
  if (error != nullptr) {
    *error = nil;
  }
  AirfixMetalPresentationTransactionHolder *holder =
      self.presentationTransactionHolder;
  AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder = self.gpuBudgetHolder;
  MTKView *ownedView = self.metalView;
  id<MTLDevice> rendererDevice = self.commandQueue.device;
  if (holder == nil || gpuBudgetHolder == nil ||
      gpuBudgetHolder->_ledger == nullptr || view == nil || ownedView == nil ||
      view != ownedView || view.device == nil ||
      view.device != rendererDevice) {
    if (error != nullptr) {
      *error = makeError(RendererError::presentationSurfaceUnavailable,
                         @"The Metal presentation surface is unavailable.");
    }
    return NO;
  }

  const auto surface =
      presentationSurfaceStamp(view, outputExtent, holder->_surfaceGeneration);
  const auto *active = holder->_transaction.activeState();
  if (active != nullptr && active->settings() == candidate &&
      active->surface() == surface) {
    holder->_desiredSettings = candidate;
    return YES;
  }

  MetalPresentationTargetFactoryContext factoryContext{
      .gpuBudgetHolder = gpuBudgetHolder,
  };
  auto prepared = holder->_transaction.prepare(
      candidate, surface,
      {
          .callback = prepareMetalPresentationTarget,
          .context = &factoryContext,
      });
  if (!prepared.complete()) {
    if (error != nullptr) {
      const auto kind = prepared.issue->kind;
      const bool invalidCandidate =
          kind == airfix::render::RenderPresentationTransactionIssueKind::
                      invalidSettings ||
          kind == airfix::render::RenderPresentationTransactionIssueKind::
                      invalidLayout;
      *error = makeError(
          invalidCandidate ? RendererError::invalidPayload
                           : RendererError::presentationTargetPreparation,
          invalidCandidate ? @"The Metal presentation settings are invalid."
                           : @"The complete Metal scene target pair could not "
                             @"be prepared within the GPU budget.");
    }
    return NO;
  }

  const auto currentExtent = outputPixelExtent(ownedView.drawableSize);
  if (!currentExtent.has_value()) {
    if (error != nullptr) {
      *error = makeError(
          RendererError::stalePresentationCandidate,
          @"The Metal presentation surface changed before publication.");
    }
    return NO;
  }
  const auto currentSurface = presentationSurfaceStamp(
      ownedView, *currentExtent, holder->_surfaceGeneration);

  // No callback, allocation, or dispatch is permitted between this fresh
  // validation and the move publication.
  const auto finalIssue =
      holder->_transaction.finalValidate(*prepared.prepared, currentSurface);
  if (finalIssue.has_value()) {
    if (error != nullptr) {
      *error = makeError(
          RendererError::stalePresentationCandidate,
          @"The Metal presentation surface changed before publication.");
    }
    return NO;
  }
  holder->_transaction.commitValidated(std::move(*prepared.prepared));
  holder->_desiredSettings = candidate;
  holder->_retrySchedule.recordSuccess();
  if (!candidate.diagnosticsOverlayEnabled) {
    self.diagnosticsOverlayTexture = nil;
    self.diagnosticsOverlayWidth = 0U;
    self.diagnosticsOverlayHeight = 0U;
    self.diagnosticsOverlayPixelScale = 0U;
  }
  return YES;
}

- (nullable AirfixMetalPresentationRequest *)
    captureRenderPresentationRequest:
        (const airfix::render::RenderPresentationSettings &)candidate
                               error:(NSError *_Nullable *_Nullable)error {
  if (error != nullptr) {
    *error = nil;
  }
  if (!NSThread.isMainThread) {
    if (error != nullptr) {
      *error = makeError(
          RendererError::wrongThread,
          @"Metal presentation settings must be changed on the main thread.");
    }
    return nil;
  }
  MTKView *view = self.metalView;
  AirfixMetalPresentationTransactionHolder *holder =
      self.presentationTransactionHolder;
  AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder = self.gpuBudgetHolder;
  NSObject *ownerToken = self.preparationOwnerToken;
  id<MTLDevice> device = self.commandQueue.device;
  std::optional<airfix::render::OutputPixelExtent> extent;
  if (view != nil) {
    extent = outputPixelExtent(view.drawableSize);
  }
  if (holder == nil || gpuBudgetHolder == nil ||
      gpuBudgetHolder->_ledger == nullptr || ownerToken == nil ||
      device == nil || view == nil || view.device != device ||
      !extent.has_value()) {
    if (error != nullptr) {
      *error = makeError(RendererError::presentationSurfaceUnavailable,
                         @"Metal presentation settings cannot be captured "
                         @"without a complete drawable surface.");
    }
    return nil;
  }
  if (airfix::render::validateRenderPresentationSettings(candidate)
          .has_value()) {
    if (error != nullptr) {
      *error = makeError(RendererError::invalidPayload,
                         @"The Metal presentation settings are invalid.");
    }
    return nil;
  }

  AirfixMetalPresentationRequest *request =
      [[AirfixMetalPresentationRequest alloc] init];
  if (request == nil) {
    if (error != nullptr) {
      *error =
          makeError(RendererError::unexpectedFailure,
                    @"The Metal presentation request could not be captured.");
    }
    return nil;
  }
  request->_transactionSnapshot = holder->_transaction.captureForPreparation();
  request->_candidate = candidate;
  request->_surface =
      presentationSurfaceStamp(view, *extent, holder->_surfaceGeneration);
  request->_ownerToken = ownerToken;
  request->_transactionHolder = holder;
  request->_gpuBudgetHolder = gpuBudgetHolder;
  request->_view = view;
  request->_device = device;
  request->_releaseQueue = self.resourceReleaseQueue;
  return request;
}

- (nullable AirfixPreparedMetalPresentation *)
    prepareCapturedRenderPresentationRequest:
        (AirfixMetalPresentationRequest *)request
                                       error:(NSError *_Nullable *_Nullable)
                                                 error {
  if (error != nullptr) {
    *error = nil;
  }
  if (NSThread.isMainThread) {
    if (error != nullptr) {
      *error = makeError(RendererError::wrongThread,
                         @"Metal presentation resources must be prepared off "
                         @"the main thread.");
    }
    return nil;
  }
  if (request == nil || request->_ownerToken == nil ||
      request->_transactionHolder == nil || request->_gpuBudgetHolder == nil ||
      request->_gpuBudgetHolder->_ledger == nullptr || request->_view == nil ||
      request->_device == nil || !request->_surface.complete()) {
    if (error != nullptr) {
      *error =
          makeError(RendererError::invalidPreparedPresentation,
                    @"The captured Metal presentation request is incomplete.");
    }
    return nil;
  }

  try {
    MetalPresentationTargetFactoryContext factoryContext{
        .gpuBudgetHolder = request->_gpuBudgetHolder,
    };
    auto result = request->_transactionSnapshot.prepare(
        request->_candidate, request->_surface,
        {
            .callback = prepareMetalPresentationTarget,
            .context = &factoryContext,
        });
    if (!result.complete()) {
      if (error != nullptr) {
        const bool invalidCandidate =
            result.issue.has_value() &&
            (result.issue->kind ==
                 airfix::render::RenderPresentationTransactionIssueKind::
                     invalidSettings ||
             result.issue->kind ==
                 airfix::render::RenderPresentationTransactionIssueKind::
                     invalidLayout);
        *error = makeError(
            invalidCandidate ? RendererError::invalidPayload
                             : RendererError::presentationTargetPreparation,
            invalidCandidate ? @"The Metal presentation settings are invalid."
                             : @"The complete Metal scene target pair could "
                               @"not be prepared within the GPU budget.");
      }
      return nil;
    }

    AirfixPreparedMetalPresentation *prepared =
        [[AirfixPreparedMetalPresentation alloc] init];
    if (prepared == nil) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::unexpectedFailure,
            @"The prepared Metal presentation token could not be created.");
      }
      return nil;
    }
    prepared->_prepared =
        std::make_unique<airfix::render::PreparedRenderPresentationState>(
            std::move(*result.prepared));
    prepared->_candidate = request->_candidate;
    prepared->_ownerToken = request->_ownerToken;
    prepared->_transactionHolder = request->_transactionHolder;
    prepared->_gpuBudgetHolder = request->_gpuBudgetHolder;
    prepared->_view = request->_view;
    prepared->_device = request->_device;
    prepared->_releaseQueue = request->_releaseQueue;
    prepared->_published = NO;
    return prepared;
  } catch (...) {
    if (error != nullptr) {
      *error =
          makeError(RendererError::unexpectedFailure,
                    @"Metal presentation preparation failed unexpectedly.");
    }
    return nil;
  }
}

- (BOOL)publishPreparedRenderPresentation:
            (AirfixPreparedMetalPresentation *)prepared
                                    error:(NSError *_Nullable *_Nullable)error {
  if (error != nullptr) {
    *error = nil;
  }
  if (!NSThread.isMainThread) {
    if (error != nullptr) {
      *error = makeError(
          RendererError::wrongThread,
          @"Prepared Metal presentation must be published on the main thread.");
    }
    return NO;
  }

  AirfixMetalPresentationTransactionHolder *holder =
      self.presentationTransactionHolder;
  AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder = self.gpuBudgetHolder;
  MTKView *view = self.metalView;
  id<MTLDevice> device = self.commandQueue.device;
  if (prepared == nil || prepared->_published ||
      prepared->_prepared == nullptr || prepared->_ownerToken == nil ||
      prepared->_ownerToken != self.preparationOwnerToken ||
      prepared->_transactionHolder == nil ||
      prepared->_transactionHolder != holder ||
      prepared->_gpuBudgetHolder == nil ||
      prepared->_gpuBudgetHolder != gpuBudgetHolder || prepared->_view == nil ||
      prepared->_view != view || prepared->_device == nil ||
      prepared->_device != device || view.device != device) {
    if (error != nullptr) {
      *error = makeError(RendererError::invalidPreparedPresentation,
                         @"The prepared Metal presentation belongs to a "
                         @"different renderer or device.");
    }
    return NO;
  }

  const auto extent = outputPixelExtent(view.drawableSize);
  if (!extent.has_value()) {
    if (error != nullptr) {
      *error = makeError(
          RendererError::stalePresentationCandidate,
          @"The Metal presentation surface changed before publication.");
    }
    return NO;
  }
  const auto currentSurface =
      presentationSurfaceStamp(view, *extent, holder->_surfaceGeneration);
  if (holder->_transaction.finalValidate(*prepared->_prepared, currentSurface)
          .has_value()) {
    if (error != nullptr) {
      *error = makeError(
          RendererError::stalePresentationCandidate,
          @"The Metal presentation surface changed before publication.");
    }
    return NO;
  }

  // No callback, allocation, or dispatch is permitted between the fresh
  // validation above and this no-fail move publication.
  prepared->_published = YES;
  holder->_transaction.commitValidated(std::move(*prepared->_prepared));
  prepared->_prepared.reset();
  holder->_desiredSettings = prepared->_candidate;
  holder->_retrySchedule.recordSuccess();
  if (!prepared->_candidate.diagnosticsOverlayEnabled) {
    self.diagnosticsOverlayTexture = nil;
    self.diagnosticsOverlayWidth = 0U;
    self.diagnosticsOverlayHeight = 0U;
    self.diagnosticsOverlayPixelScale = 0U;
  }
  return YES;
}

- (airfix::render::RenderPresentationSettings)renderPresentationSettings {
  AirfixMetalPresentationTransactionHolder *holder =
      self.presentationTransactionHolder;
  return holder != nil ? holder->_desiredSettings
                       : airfix::render::RenderPresentationSettings{};
}

- (BOOL)encodeLegacyWeaponCrosshairSprite:
            (const airfix::content::LegacyWeaponCrosshairSpriteSubmission &)
                submission
                                resources:(AirfixMetalRoomResources *)resources
                            commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                               renderPass:(MTLRenderPassDescriptor *)renderPass
                             outputExtent:
                                 (airfix::render::OutputPixelExtent)outputExtent
                     drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture {
  if (resources == nil || commandBuffer == nil || renderPass == nil ||
      drawableDepthTexture == nil || !resources->_crosshairs.has_value() ||
      resources.crosshairTextures == nil ||
      self.crosshairPipelineState == nil || self.crosshairDepthState == nil ||
      self.crosshairSamplerState == nil ||
      submission.blendMode != airfix::content::LegacyWeaponCrosshairBlendMode::
                                  sourceAlphaOneMinusSourceAlpha ||
      submission.depthMode !=
          airfix::content::LegacyWeaponCrosshairDepthMode::alwaysWrite ||
      submission.tintArgb != airfix::content::legacyWeaponCrosshairTintArgb ||
      submission.uv != airfix::content::LegacyWeaponCrosshairUvRect{} ||
      !submission.belongsTo(*resources->_crosshairs)) {
    return NO;
  }
  const auto &rectangle = submission.outputRect;
  if (outputExtent.width == 0U || outputExtent.height == 0U ||
      !std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) ||
      !std::isfinite(rectangle.width) || !std::isfinite(rectangle.height) ||
      rectangle.width <= 0.0F || rectangle.height <= 0.0F) {
    return NO;
  }
  const NSUInteger textureIndex =
      static_cast<NSUInteger>(submission.textureId.value);
  if (textureIndex >= resources.crosshairTextures.count) {
    return NO;
  }
  id<MTLTexture> texture = resources.crosshairTextures[textureIndex];
  id<MTLTexture> outputTexture = renderPass.colorAttachments[0].texture;
  if (texture == nil || outputTexture == nil) {
    return NO;
  }

  MTLRenderPassDescriptor *spritePass =
      [MTLRenderPassDescriptor renderPassDescriptor];
  spritePass.colorAttachments[0].texture = outputTexture;
  spritePass.colorAttachments[0].loadAction = MTLLoadActionLoad;
  spritePass.colorAttachments[0].storeAction = MTLStoreActionStore;
  spritePass.depthAttachment.texture = drawableDepthTexture;
  // The recovered state writes depth with ALWAYS. Existing depth contents
  // are irrelevant to that comparison and no later HUD pass consumes them.
  spritePass.depthAttachment.loadAction = MTLLoadActionDontCare;
  spritePass.depthAttachment.storeAction = MTLStoreActionDontCare;

  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:spritePass];
  if (encoder == nil) {
    return NO;
  }
  const GpuOverlayUniforms uniforms{
      .outputAndPanelSize =
          {
              static_cast<float>(outputExtent.width),
              static_cast<float>(outputExtent.height),
              rectangle.width,
              rectangle.height,
          },
      .panelOrigin = {rectangle.x, rectangle.y, 0.0F, 0.0F},
      .tint = normalizedArgb(submission.tintArgb),
      .uvRect = {0.0F, 0.0F, 1.0F, 1.0F},
  };
  [encoder setRenderPipelineState:self.crosshairPipelineState];
  [encoder setDepthStencilState:self.crosshairDepthState];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:MTLViewport{
                           0.0,
                           0.0,
                           static_cast<double>(outputExtent.width),
                           static_cast<double>(outputExtent.height),
                           0.0,
                           1.0,
                       }];
  [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2U];
  [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:2U];
  [encoder setFragmentTexture:texture atIndex:0U];
  [encoder setFragmentSamplerState:self.crosshairSamplerState atIndex:0U];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0U
              vertexCount:6U];
  [encoder endEncoding];
  return YES;
}

- (NSUInteger)
    encodeLegacyAircraftHealthGauge:
        (const airfix::content::LegacyAircraftHealthGaugeSubmission &)submission
                          resources:(AirfixMetalRoomResources *)resources
                      commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                         renderPass:(MTLRenderPassDescriptor *)renderPass
                       outputExtent:
                           (airfix::render::OutputPixelExtent)outputExtent
               drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture {
  if (resources == nil || commandBuffer == nil || renderPass == nil ||
      drawableDepthTexture == nil || !resources->_healthGauge.has_value() ||
      resources.healthGaugeTextures == nil ||
      resources.healthGaugeTextures.count !=
          airfix::content::legacyAircraftHealthGaugeTextureCount ||
      self.healthGaugeTexturePipelineState == nil ||
      self.healthGaugeSolidPipelineState == nil ||
      self.healthGaugeDepthState == nil ||
      self.healthGaugeSamplerState == nil || outputExtent.width == 0U ||
      outputExtent.height == 0U ||
      !submission.belongsTo(*resources->_healthGauge) ||
      submission.commandCount == 0U ||
      !fitsNSUInteger(submission.commandCount)) {
    return 0U;
  }

  const auto pointIsInsideOutput =
      [outputExtent](const airfix::render::OutputPixelPoint point) noexcept {
        return std::isfinite(point.x) && std::isfinite(point.y) &&
               point.x >= 0.0F && point.y >= 0.0F &&
               point.x <= static_cast<float>(outputExtent.width) &&
               point.y <= static_cast<float>(outputExtent.height);
      };
  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    if (!std::all_of(command.outputQuad.begin(), command.outputQuad.end(),
                     pointIsInsideOutput)) {
      return 0U;
    }
    if (command.textured()) {
      const auto textureIndex =
          static_cast<NSUInteger>(command.textureId.value);
      if (textureIndex >= resources.healthGaugeTextures.count ||
          resources.healthGaugeTextures[textureIndex] == nil) {
        return 0U;
      }
    } else if (command.kind !=
               airfix::render::LegacyAircraftHealthGaugeCommandKind::
                   damageMaskQuad) {
      return 0U;
    }
  }

  id<MTLTexture> outputTexture = renderPass.colorAttachments[0].texture;
  if (outputTexture == nil ||
      outputTexture.pixelFormat != MTLPixelFormatBGRA8Unorm ||
      drawableDepthTexture.pixelFormat != MTLPixelFormatDepth32Float ||
      outputTexture.width != static_cast<NSUInteger>(outputExtent.width) ||
      outputTexture.height != static_cast<NSUInteger>(outputExtent.height) ||
      drawableDepthTexture.width !=
          static_cast<NSUInteger>(outputExtent.width) ||
      drawableDepthTexture.height !=
          static_cast<NSUInteger>(outputExtent.height)) {
    return 0U;
  }
  MTLRenderPassDescriptor *gaugePass =
      [MTLRenderPassDescriptor renderPassDescriptor];
  gaugePass.colorAttachments[0].texture = outputTexture;
  gaugePass.colorAttachments[0].loadAction = MTLLoadActionLoad;
  gaugePass.colorAttachments[0].storeAction = MTLStoreActionStore;
  gaugePass.depthAttachment.texture = drawableDepthTexture;
  // All recovered gauge commands use ALWAYS with writes. Existing depth is
  // irrelevant, and the later diagnostics pass does not consume it.
  gaugePass.depthAttachment.loadAction = MTLLoadActionDontCare;
  gaugePass.depthAttachment.storeAction = MTLStoreActionDontCare;

  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:gaugePass];
  if (encoder == nil) {
    return 0U;
  }
  [encoder setDepthStencilState:self.healthGaugeDepthState];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:MTLViewport{
                           0.0,
                           0.0,
                           static_cast<double>(outputExtent.width),
                           static_cast<double>(outputExtent.height),
                           0.0,
                           1.0,
                       }];
  [encoder setFragmentSamplerState:self.healthGaugeSamplerState atIndex:0U];

  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    GpuGaugeUniforms uniforms{
        .outputSize =
            {
                static_cast<float>(outputExtent.width),
                static_cast<float>(outputExtent.height),
                0.0F,
                0.0F,
            },
        .tint = normalizedArgb(command.colourArgb),
        .uvRect = {0.0F, 0.0F, 1.0F, 1.0F},
    };
    for (std::size_t pointIndex = 0U; pointIndex < command.outputQuad.size();
         ++pointIndex) {
      uniforms.outputQuad[pointIndex] = {
          command.outputQuad[pointIndex].x,
          command.outputQuad[pointIndex].y,
          0.0F,
          0.0F,
      };
    }
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:3U];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:3U];

    id<MTLTexture> texture = nil;
    if (command.textured()) {
      [encoder setRenderPipelineState:self.healthGaugeTexturePipelineState];
      texture = resources.healthGaugeTextures[static_cast<NSUInteger>(
          command.textureId.value)];
    } else {
      [encoder setRenderPipelineState:self.healthGaugeSolidPipelineState];
    }
    [encoder setFragmentTexture:texture atIndex:0U];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0U
                vertexCount:6U];
  }
  [encoder endEncoding];
  return static_cast<NSUInteger>(submission.commandCount);
}

- (NSUInteger)
    encodeLegacyAircraftHudInstruments:
        (const airfix::content::LegacyAircraftHudInstrumentsSubmission &)
            submission
                             resources:(AirfixMetalRoomResources *)resources
                         commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                            renderPass:(MTLRenderPassDescriptor *)renderPass
                          outputExtent:
                              (airfix::render::OutputPixelExtent)outputExtent
                  drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture {
  if (resources == nil || commandBuffer == nil || renderPass == nil ||
      drawableDepthTexture == nil || !resources->_hudInstruments.has_value() ||
      resources.hudInstrumentTextures == nil ||
      resources.hudInstrumentTextures.count !=
          airfix::content::legacyAircraftHudInstrumentTextureCount ||
      self.healthGaugeTexturePipelineState == nil ||
      self.healthGaugeDepthState == nil ||
      self.healthGaugeSamplerState == nil || outputExtent.width == 0U ||
      outputExtent.height == 0U ||
      !submission.belongsTo(*resources->_hudInstruments) ||
      submission.commandCount == 0U ||
      !fitsNSUInteger(submission.commandCount)) {
    return 0U;
  }

  const auto pointIsInsideOutput =
      [outputExtent](const airfix::render::OutputPixelPoint point) noexcept {
        return std::isfinite(point.x) && std::isfinite(point.y) &&
               point.x >= 0.0F && point.y >= 0.0F &&
               point.x <= static_cast<float>(outputExtent.width) &&
               point.y <= static_cast<float>(outputExtent.height);
      };
  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    const auto textureIndex = static_cast<NSUInteger>(command.textureId.value);
    if (!std::all_of(command.outputQuad.begin(), command.outputQuad.end(),
                     pointIsInsideOutput) ||
        textureIndex >= resources.hudInstrumentTextures.count ||
        resources.hudInstrumentTextures[textureIndex] == nil) {
      return 0U;
    }
  }

  id<MTLTexture> outputTexture = renderPass.colorAttachments[0].texture;
  if (outputTexture == nil ||
      outputTexture.pixelFormat != MTLPixelFormatBGRA8Unorm ||
      drawableDepthTexture.pixelFormat != MTLPixelFormatDepth32Float ||
      outputTexture.width != static_cast<NSUInteger>(outputExtent.width) ||
      outputTexture.height != static_cast<NSUInteger>(outputExtent.height) ||
      drawableDepthTexture.width !=
          static_cast<NSUInteger>(outputExtent.width) ||
      drawableDepthTexture.height !=
          static_cast<NSUInteger>(outputExtent.height)) {
    return 0U;
  }
  MTLRenderPassDescriptor *instrumentsPass =
      [MTLRenderPassDescriptor renderPassDescriptor];
  instrumentsPass.colorAttachments[0].texture = outputTexture;
  instrumentsPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
  instrumentsPass.colorAttachments[0].storeAction = MTLStoreActionStore;
  instrumentsPass.depthAttachment.texture = drawableDepthTexture;
  instrumentsPass.depthAttachment.loadAction = MTLLoadActionDontCare;
  instrumentsPass.depthAttachment.storeAction = MTLStoreActionDontCare;

  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:instrumentsPass];
  if (encoder == nil) {
    return 0U;
  }
  [encoder setRenderPipelineState:self.healthGaugeTexturePipelineState];
  [encoder setDepthStencilState:self.healthGaugeDepthState];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:MTLViewport{
                           0.0,
                           0.0,
                           static_cast<double>(outputExtent.width),
                           static_cast<double>(outputExtent.height),
                           0.0,
                           1.0,
                       }];
  [encoder setFragmentSamplerState:self.healthGaugeSamplerState atIndex:0U];

  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    GpuGaugeUniforms uniforms{
        .outputSize =
            {
                static_cast<float>(outputExtent.width),
                static_cast<float>(outputExtent.height),
                0.0F,
                0.0F,
            },
        .tint = normalizedArgb(command.tintArgb),
        .uvRect =
            {
                command.uv.minimumU,
                command.uv.minimumV,
                command.uv.maximumU,
                command.uv.maximumV,
            },
    };
    for (std::size_t pointIndex = 0U; pointIndex < command.outputQuad.size();
         ++pointIndex) {
      uniforms.outputQuad[pointIndex] = {
          command.outputQuad[pointIndex].x,
          command.outputQuad[pointIndex].y,
          0.0F,
          0.0F,
      };
    }
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:3U];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:3U];
    [encoder
        setFragmentTexture:resources
                               .hudInstrumentTextures[static_cast<NSUInteger>(
                                   command.textureId.value)]
                   atIndex:0U];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0U
                vertexCount:6U];
  }
  [encoder endEncoding];
  return static_cast<NSUInteger>(submission.commandCount);
}

- (NSUInteger)
    encodeLegacyAircraftHudRollingDigits:
        (const airfix::content::LegacyAircraftHudRollingDigitsSubmission &)
            submission
                               resources:(AirfixMetalRoomResources *)resources
                           commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                              renderPass:(MTLRenderPassDescriptor *)renderPass
                            outputExtent:
                                (airfix::render::OutputPixelExtent)outputExtent
                    drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture {
  if (resources == nil || commandBuffer == nil || renderPass == nil ||
      drawableDepthTexture == nil || !resources->_rollingDigits.has_value() ||
      resources.rollingDigitTextures == nil ||
      resources.rollingDigitTextures.count !=
          airfix::content::legacyAircraftHudRollingDigitsTextureCount ||
      self.crosshairPipelineState == nil || self.crosshairDepthState == nil ||
      self.crosshairSamplerState == nil || outputExtent.width == 0U ||
      outputExtent.height == 0U ||
      !submission.belongsTo(*resources->_rollingDigits) ||
      submission.commandCount == 0U ||
      submission.commandCount > submission.orderedCommands.size() ||
      !fitsNSUInteger(submission.commandCount)) {
    return 0U;
  }

  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    const auto &rectangle = command.outputRect;
    const auto &uv = command.uv;
    const auto textureIndex = static_cast<NSUInteger>(command.textureId.value);
    if (command.textureRole !=
            airfix::content::LegacyAircraftHudRollingDigitsTextureRole::
                digits ||
        command.blendMode !=
            airfix::content::LegacyAircraftHudRollingDigitsBlendMode::
                sourceAlphaOneMinusSourceAlpha ||
        command.depthMode !=
            airfix::content::LegacyAircraftHudRollingDigitsDepthMode::
                alwaysWrite ||
        command.samplingMode !=
            airfix::content::LegacyAircraftHudRollingDigitsSamplingMode::
                linearClamp ||
        !std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) ||
        !std::isfinite(rectangle.width) || !std::isfinite(rectangle.height) ||
        rectangle.x < 0.0F || rectangle.y < 0.0F || rectangle.width <= 0.0F ||
        rectangle.height <= 0.0F ||
        rectangle.x + rectangle.width >
            static_cast<float>(outputExtent.width) ||
        rectangle.y + rectangle.height >
            static_cast<float>(outputExtent.height) ||
        !std::isfinite(uv.minimumU) || !std::isfinite(uv.minimumV) ||
        !std::isfinite(uv.maximumU) || !std::isfinite(uv.maximumV) ||
        uv.minimumU < 0.0F || uv.minimumV < 0.0F || uv.maximumU > 1.0F ||
        uv.maximumV > 1.0F || uv.minimumU >= uv.maximumU ||
        uv.minimumV >= uv.maximumV ||
        textureIndex >= resources.rollingDigitTextures.count ||
        resources.rollingDigitTextures[textureIndex] == nil) {
      return 0U;
    }
  }

  id<MTLTexture> outputTexture = renderPass.colorAttachments[0].texture;
  if (outputTexture == nil ||
      outputTexture.pixelFormat != MTLPixelFormatBGRA8Unorm ||
      drawableDepthTexture.pixelFormat != MTLPixelFormatDepth32Float ||
      outputTexture.width != static_cast<NSUInteger>(outputExtent.width) ||
      outputTexture.height != static_cast<NSUInteger>(outputExtent.height) ||
      drawableDepthTexture.width !=
          static_cast<NSUInteger>(outputExtent.width) ||
      drawableDepthTexture.height !=
          static_cast<NSUInteger>(outputExtent.height)) {
    return 0U;
  }

  MTLRenderPassDescriptor *digitPass =
      [MTLRenderPassDescriptor renderPassDescriptor];
  digitPass.colorAttachments[0].texture = outputTexture;
  digitPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
  digitPass.colorAttachments[0].storeAction = MTLStoreActionStore;
  digitPass.depthAttachment.texture = drawableDepthTexture;
  digitPass.depthAttachment.loadAction = MTLLoadActionDontCare;
  digitPass.depthAttachment.storeAction = MTLStoreActionDontCare;

  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:digitPass];
  if (encoder == nil) {
    return 0U;
  }
  [encoder setRenderPipelineState:self.crosshairPipelineState];
  [encoder setDepthStencilState:self.crosshairDepthState];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:MTLViewport{
                           0.0,
                           0.0,
                           static_cast<double>(outputExtent.width),
                           static_cast<double>(outputExtent.height),
                           0.0,
                           1.0,
                       }];
  [encoder setFragmentSamplerState:self.crosshairSamplerState atIndex:0U];

  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    const auto &rectangle = command.outputRect;
    const GpuOverlayUniforms uniforms{
        .outputAndPanelSize =
            {
                static_cast<float>(outputExtent.width),
                static_cast<float>(outputExtent.height),
                rectangle.width,
                rectangle.height,
            },
        .panelOrigin = {rectangle.x, rectangle.y, 0.0F, 0.0F},
        .tint = normalizedArgb(command.tintArgb),
        .uvRect =
            {
                command.uv.minimumU,
                command.uv.minimumV,
                command.uv.maximumU,
                command.uv.maximumV,
            },
    };
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2U];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:2U];
    [encoder
        setFragmentTexture:resources
                               .rollingDigitTextures[static_cast<NSUInteger>(
                                   command.textureId.value)]
                   atIndex:0U];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0U
                vertexCount:6U];
  }
  [encoder endEncoding];
  return static_cast<NSUInteger>(submission.commandCount);
}

- (NSUInteger)
    encodeLegacyAircraftHudWeaponPanels:
        (const airfix::content::LegacyAircraftHudWeaponPanelsSubmission &)
            submission
                              resources:(AirfixMetalRoomResources *)resources
                          commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                             renderPass:(MTLRenderPassDescriptor *)renderPass
                           outputExtent:
                               (airfix::render::OutputPixelExtent)outputExtent
                   drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture {
  if (resources == nil || commandBuffer == nil || renderPass == nil ||
      drawableDepthTexture == nil || !resources->_weaponPanels.has_value() ||
      !resources->_rollingDigits.has_value() ||
      resources.weaponPanelTextures == nil ||
      resources.rollingDigitTextures == nil ||
      resources.weaponPanelTextures.count !=
          resources->_weaponPanels->textures.size() ||
      resources.rollingDigitTextures.count !=
          resources->_rollingDigits->textures.size() ||
      self.crosshairPipelineState == nil ||
      self.weaponPanelMultiplyPipelineState == nil ||
      self.crosshairDepthState == nil || self.crosshairSamplerState == nil ||
      outputExtent.width == 0U || outputExtent.height == 0U ||
      !submission.belongsTo(*resources->_weaponPanels,
                            *resources->_rollingDigits) ||
      submission.commandCount == 0U ||
      submission.commandCount > submission.orderedCommands.size() ||
      !fitsNSUInteger(submission.commandCount)) {
    return 0U;
  }

  using TextureNamespace =
      airfix::content::LegacyAircraftHudWeaponPanelTextureNamespace;
  using BlendMode = airfix::content::LegacyAircraftHudWeaponPanelBlendMode;
  using SamplingMode =
      airfix::content::LegacyAircraftHudWeaponPanelSamplingMode;
  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    const auto &rectangle = command.outputRect;
    if (!std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) ||
        !std::isfinite(rectangle.width) || !std::isfinite(rectangle.height) ||
        rectangle.x < 0.0F || rectangle.y < 0.0F || rectangle.width <= 0.0F ||
        rectangle.height <= 0.0F ||
        rectangle.x + rectangle.width >
            static_cast<float>(outputExtent.width) ||
        rectangle.y + rectangle.height >
            static_cast<float>(outputExtent.height)) {
      return 0U;
    }
    const auto textureIndex = static_cast<NSUInteger>(command.textureId.value);
    if (command.textureNamespace == TextureNamespace::weaponPanels) {
      if (command.blendMode != BlendMode::sourceAlphaOneMinusSourceAlpha ||
          command.samplingMode != SamplingMode::linearClamp ||
          textureIndex >= resources.weaponPanelTextures.count ||
          resources.weaponPanelTextures[textureIndex] == nil) {
        return 0U;
      }
    } else if (command.textureNamespace == TextureNamespace::rollingDigits) {
      if (command.blendMode != BlendMode::sourceAlphaOneMinusSourceAlpha ||
          command.samplingMode != SamplingMode::linearClamp ||
          textureIndex >= resources.rollingDigitTextures.count ||
          resources.rollingDigitTextures[textureIndex] == nil) {
        return 0U;
      }
    } else if (command.textureNamespace == TextureNamespace::none) {
      if (command.blendMode != BlendMode::destinationMultiplySourceColour ||
          command.samplingMode != SamplingMode::notApplicable) {
        return 0U;
      }
    } else {
      return 0U;
    }
  }

  id<MTLTexture> outputTexture = renderPass.colorAttachments[0].texture;
  if (outputTexture == nil ||
      outputTexture.pixelFormat != MTLPixelFormatBGRA8Unorm ||
      drawableDepthTexture.pixelFormat != MTLPixelFormatDepth32Float ||
      outputTexture.width != static_cast<NSUInteger>(outputExtent.width) ||
      outputTexture.height != static_cast<NSUInteger>(outputExtent.height) ||
      drawableDepthTexture.width !=
          static_cast<NSUInteger>(outputExtent.width) ||
      drawableDepthTexture.height !=
          static_cast<NSUInteger>(outputExtent.height)) {
    return 0U;
  }

  MTLRenderPassDescriptor *panelPass =
      [MTLRenderPassDescriptor renderPassDescriptor];
  panelPass.colorAttachments[0].texture = outputTexture;
  panelPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
  panelPass.colorAttachments[0].storeAction = MTLStoreActionStore;
  panelPass.depthAttachment.texture = drawableDepthTexture;
  panelPass.depthAttachment.loadAction = MTLLoadActionDontCare;
  panelPass.depthAttachment.storeAction = MTLStoreActionDontCare;

  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:panelPass];
  if (encoder == nil) {
    return 0U;
  }
  [encoder setDepthStencilState:self.crosshairDepthState];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:MTLViewport{
                           0.0,
                           0.0,
                           static_cast<double>(outputExtent.width),
                           static_cast<double>(outputExtent.height),
                           0.0,
                           1.0,
                       }];
  [encoder setFragmentSamplerState:self.crosshairSamplerState atIndex:0U];

  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    const auto &rectangle = command.outputRect;
    const GpuOverlayUniforms uniforms{
        .outputAndPanelSize =
            {
                static_cast<float>(outputExtent.width),
                static_cast<float>(outputExtent.height),
                rectangle.width,
                rectangle.height,
            },
        .panelOrigin = {rectangle.x, rectangle.y, 0.0F, 0.0F},
        .tint = normalizedArgb(command.colourArgb),
        .uvRect =
            {
                command.uv.minimumU,
                command.uv.minimumV,
                command.uv.maximumU,
                command.uv.maximumV,
            },
    };
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2U];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:2U];

    id<MTLTexture> texture = nil;
    if (command.textureNamespace == TextureNamespace::weaponPanels) {
      [encoder setRenderPipelineState:self.crosshairPipelineState];
      texture = resources.weaponPanelTextures[static_cast<NSUInteger>(
          command.textureId.value)];
    } else if (command.textureNamespace == TextureNamespace::rollingDigits) {
      [encoder setRenderPipelineState:self.crosshairPipelineState];
      texture = resources.rollingDigitTextures[static_cast<NSUInteger>(
          command.textureId.value)];
    } else {
      [encoder setRenderPipelineState:self.weaponPanelMultiplyPipelineState];
    }
    [encoder setFragmentTexture:texture atIndex:0U];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0U
                vertexCount:6U];
  }
  [encoder endEncoding];
  return static_cast<NSUInteger>(submission.commandCount);
}

- (NSUInteger)
    encodeLegacyAircraftHudIdentityStatus:
        (const airfix::content::LegacyAircraftHudIdentityStatusSubmission &)
            submission
                                resources:(AirfixMetalRoomResources *)resources
                            commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                               renderPass:(MTLRenderPassDescriptor *)renderPass
                             outputExtent:
                                 (airfix::render::OutputPixelExtent)outputExtent
                     drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture {
  if (resources == nil || commandBuffer == nil || renderPass == nil ||
      drawableDepthTexture == nil || !resources->_identityStatus.has_value() ||
      !resources->_rollingDigits.has_value() ||
      resources.identityStatusTextures == nil ||
      resources.rollingDigitTextures == nil ||
      resources.identityStatusTextures.count !=
          resources->_identityStatus->textures.size() ||
      resources.rollingDigitTextures.count !=
          resources->_rollingDigits->textures.size() ||
      self.crosshairPipelineState == nil || self.crosshairDepthState == nil ||
      self.crosshairSamplerState == nil || outputExtent.width == 0U ||
      outputExtent.height == 0U ||
      !submission.belongsTo(*resources->_identityStatus,
                            *resources->_rollingDigits) ||
      submission.commandCount == 0U ||
      submission.commandCount > submission.orderedCommands.size() ||
      !fitsNSUInteger(submission.commandCount)) {
    return 0U;
  }

  using TextureNamespace =
      airfix::content::LegacyAircraftHudIdentityStatusTextureNamespace;
  using BlendMode = airfix::content::LegacyAircraftHudIdentityStatusBlendMode;
  using DepthMode = airfix::content::LegacyAircraftHudIdentityStatusDepthMode;
  using SamplingMode =
      airfix::content::LegacyAircraftHudIdentityStatusSamplingMode;
  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    const auto &rectangle = command.outputRect;
    const auto &uv = command.uv;
    if (command.blendMode != BlendMode::sourceAlphaOneMinusSourceAlpha ||
        command.depthMode != DepthMode::alwaysWrite ||
        command.samplingMode != SamplingMode::linearClamp ||
        !std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) ||
        !std::isfinite(rectangle.width) || !std::isfinite(rectangle.height) ||
        rectangle.x < 0.0F || rectangle.y < 0.0F || rectangle.width <= 0.0F ||
        rectangle.height <= 0.0F ||
        rectangle.x + rectangle.width >
            static_cast<float>(outputExtent.width) ||
        rectangle.y + rectangle.height >
            static_cast<float>(outputExtent.height) ||
        !std::isfinite(uv.minimumU) || !std::isfinite(uv.minimumV) ||
        !std::isfinite(uv.maximumU) || !std::isfinite(uv.maximumV) ||
        uv.minimumU < 0.0F || uv.minimumV < 0.0F || uv.maximumU > 1.0F ||
        uv.maximumV > 1.0F || uv.minimumU >= uv.maximumU ||
        uv.minimumV >= uv.maximumV) {
      return 0U;
    }
    const auto textureIndex = static_cast<NSUInteger>(command.textureId.value);
    if (command.textureNamespace == TextureNamespace::identityStatus) {
      if (textureIndex >= resources.identityStatusTextures.count ||
          resources.identityStatusTextures[textureIndex] == nil) {
        return 0U;
      }
    } else if (command.textureNamespace == TextureNamespace::rollingDigits) {
      if (textureIndex >= resources.rollingDigitTextures.count ||
          resources.rollingDigitTextures[textureIndex] == nil) {
        return 0U;
      }
    } else {
      return 0U;
    }
  }

  id<MTLTexture> outputTexture = renderPass.colorAttachments[0].texture;
  if (outputTexture == nil ||
      outputTexture.pixelFormat != MTLPixelFormatBGRA8Unorm ||
      drawableDepthTexture.pixelFormat != MTLPixelFormatDepth32Float ||
      outputTexture.width != static_cast<NSUInteger>(outputExtent.width) ||
      outputTexture.height != static_cast<NSUInteger>(outputExtent.height) ||
      drawableDepthTexture.width !=
          static_cast<NSUInteger>(outputExtent.width) ||
      drawableDepthTexture.height !=
          static_cast<NSUInteger>(outputExtent.height)) {
    return 0U;
  }

  MTLRenderPassDescriptor *identityStatusPass =
      [MTLRenderPassDescriptor renderPassDescriptor];
  identityStatusPass.colorAttachments[0].texture = outputTexture;
  identityStatusPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
  identityStatusPass.colorAttachments[0].storeAction = MTLStoreActionStore;
  identityStatusPass.depthAttachment.texture = drawableDepthTexture;
  identityStatusPass.depthAttachment.loadAction = MTLLoadActionDontCare;
  identityStatusPass.depthAttachment.storeAction = MTLStoreActionDontCare;

  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:identityStatusPass];
  if (encoder == nil) {
    return 0U;
  }
  [encoder setRenderPipelineState:self.crosshairPipelineState];
  [encoder setDepthStencilState:self.crosshairDepthState];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:MTLViewport{
                           0.0,
                           0.0,
                           static_cast<double>(outputExtent.width),
                           static_cast<double>(outputExtent.height),
                           0.0,
                           1.0,
                       }];
  [encoder setFragmentSamplerState:self.crosshairSamplerState atIndex:0U];

  for (std::size_t index = 0U; index < submission.commandCount; ++index) {
    const auto &command = submission.orderedCommands[index];
    const auto &rectangle = command.outputRect;
    const GpuOverlayUniforms uniforms{
        .outputAndPanelSize =
            {
                static_cast<float>(outputExtent.width),
                static_cast<float>(outputExtent.height),
                rectangle.width,
                rectangle.height,
            },
        .panelOrigin = {rectangle.x, rectangle.y, 0.0F, 0.0F},
        .tint = normalizedArgb(command.colourArgb),
        .uvRect =
            {
                command.uv.minimumU,
                command.uv.minimumV,
                command.uv.maximumU,
                command.uv.maximumV,
            },
    };
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2U];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:2U];
    id<MTLTexture> texture =
        command.textureNamespace == TextureNamespace::identityStatus
            ? resources.identityStatusTextures[static_cast<NSUInteger>(
                  command.textureId.value)]
            : resources.rollingDigitTextures[static_cast<NSUInteger>(
                  command.textureId.value)];
    [encoder setFragmentTexture:texture atIndex:0U];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0U
                vertexCount:6U];
  }
  [encoder endEncoding];
  return static_cast<NSUInteger>(submission.commandCount);
}

// Dormant until a verified AirCraft runtime producer owns the complete source
// snapshot. A caller must discard the command buffer unless the returned count
// equals event.totalCommandCount(); ordinary MTKView frames never call this.
- (NSUInteger)
    encodeLegacyAircraftHudRenderEvent:
        (const airfix::content::LegacyAircraftHudRenderEvent &)event
                             resources:(AirfixMetalRoomResources *)resources
                         commandBuffer:(id<MTLCommandBuffer>)commandBuffer
                            renderPass:(MTLRenderPassDescriptor *)renderPass
                          outputExtent:
                              (airfix::render::OutputPixelExtent)outputExtent
                  drawableDepthTexture:(id<MTLTexture>)drawableDepthTexture {
  if (resources == nil || commandBuffer == nil || renderPass == nil ||
      drawableDepthTexture == nil || !resources->_hudInstruments.has_value() ||
      !resources->_rollingDigits.has_value() ||
      !resources->_weaponPanels.has_value() ||
      !resources->_healthGauge.has_value() ||
      !resources->_identityStatus.has_value() ||
      resources.hudInstrumentTextures == nil ||
      resources.rollingDigitTextures == nil ||
      resources.weaponPanelTextures == nil ||
      resources.healthGaugeTextures == nil ||
      resources.identityStatusTextures == nil ||
      resources.hudInstrumentTextures.count !=
          resources->_hudInstruments->textures.size() ||
      resources.rollingDigitTextures.count !=
          resources->_rollingDigits->textures.size() ||
      resources.weaponPanelTextures.count !=
          resources->_weaponPanels->textures.size() ||
      resources.healthGaugeTextures.count !=
          resources->_healthGauge->textures.size() ||
      resources.identityStatusTextures.count !=
          resources->_identityStatus->textures.size() ||
      !event.belongsTo(*resources->_hudInstruments, *resources->_rollingDigits,
                       *resources->_weaponPanels, *resources->_healthGauge,
                       *resources->_identityStatus) ||
      event.totalCommandCount() == 0U ||
      !fitsNSUInteger(event.totalCommandCount())) {
    return 0U;
  }

  for (id<MTLTexture> texture in resources.hudInstrumentTextures) {
    if (texture == nil) {
      return 0U;
    }
  }
  for (id<MTLTexture> texture in resources.rollingDigitTextures) {
    if (texture == nil) {
      return 0U;
    }
  }
  for (id<MTLTexture> texture in resources.weaponPanelTextures) {
    if (texture == nil) {
      return 0U;
    }
  }
  for (id<MTLTexture> texture in resources.healthGaugeTextures) {
    if (texture == nil) {
      return 0U;
    }
  }
  for (id<MTLTexture> texture in resources.identityStatusTextures) {
    if (texture == nil) {
      return 0U;
    }
  }

  NSUInteger encoded =
      [self encodeLegacyAircraftHudInstruments:event.instruments
                                     resources:resources
                                 commandBuffer:commandBuffer
                                    renderPass:renderPass
                                  outputExtent:outputExtent
                          drawableDepthTexture:drawableDepthTexture];
  for (std::size_t index = 0U; index < event.instrumentReadouts.readoutCount;
       ++index) {
    const auto &readout = event.instrumentReadouts.orderedReadouts[index];
    if (readout.digits.has_value()) {
      encoded +=
          [self encodeLegacyAircraftHudRollingDigits:*readout.digits
                                           resources:resources
                                       commandBuffer:commandBuffer
                                          renderPass:renderPass
                                        outputExtent:outputExtent
                                drawableDepthTexture:drawableDepthTexture];
    }
  }
  encoded += [self encodeLegacyAircraftHudWeaponPanels:event.weaponPanels
                                             resources:resources
                                         commandBuffer:commandBuffer
                                            renderPass:renderPass
                                          outputExtent:outputExtent
                                  drawableDepthTexture:drawableDepthTexture];
  encoded += [self encodeLegacyAircraftHealthGauge:event.healthGauge
                                         resources:resources
                                     commandBuffer:commandBuffer
                                        renderPass:renderPass
                                      outputExtent:outputExtent
                              drawableDepthTexture:drawableDepthTexture];
  encoded += [self encodeLegacyAircraftHudIdentityStatus:event.identityStatus
                                               resources:resources
                                           commandBuffer:commandBuffer
                                              renderPass:renderPass
                                            outputExtent:outputExtent
                                    drawableDepthTexture:drawableDepthTexture];
  return encoded == static_cast<NSUInteger>(event.totalCommandCount()) ? encoded
                                                                       : 0U;
}

- (BOOL)updateDiagnosticsOverlayTextureWithDevice:(id<MTLDevice>)device {
  AirfixMetalDiagnosticsState *state = self.diagnosticsState;
  if (device == nil || state == nil ||
      !state->_accumulator.latest().has_value()) {
    return NO;
  }
  try {
    const auto image = airfix::render::rasterizeRenderFrameDiagnostics(
        *state->_accumulator.latest(),
        [self renderPresentationSettings].uiScalePercent);
    if (!image.complete() || !fitsNSUInteger(image.width) ||
        !fitsNSUInteger(image.height) || !fitsNSUInteger(image.bytesPerRow)) {
      return NO;
    }

    const NSUInteger width = static_cast<NSUInteger>(image.width);
    const NSUInteger height = static_cast<NSUInteger>(image.height);
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:width
                                    height:height
                                 mipmapped:NO];
    if (descriptor == nil) {
      return NO;
    }
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
    descriptor.usage = MTLTextureUsageShaderRead;
    // Publish a fresh immutable-per-submission texture. A completed
    // command-buffer handler retains the previous texture, avoiding a CPU
    // replaceRegion race with in-flight GPU sampling.
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    if (texture == nil || texture.width != width || texture.height != height) {
      return NO;
    }
    texture.label = @"Airfix render diagnostics overlay";

    [texture replaceRegion:MTLRegionMake2D(0U, 0U, width, height)
               mipmapLevel:0U
                 withBytes:image.rgba8.data()
               bytesPerRow:static_cast<NSUInteger>(image.bytesPerRow)];
    self.diagnosticsOverlayTexture = texture;
    self.diagnosticsOverlayWidth = width;
    self.diagnosticsOverlayHeight = height;
    self.diagnosticsOverlayPixelScale =
        static_cast<NSUInteger>(image.pixelScale);
    return YES;
  } catch (...) {
    return NO;
  }
}

- (nullable AirfixPreparedMetalRoom *)
     prepareLoadedMissionRoom:(airfix::content::LoadedMissionWorldRoom &&)room
             weaponCrosshairs:
                 (airfix::content::LoadedLegacyWeaponCrosshairTextureSet &&)
                     crosshairs
          aircraftHealthGauge:
              (airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet &&)
                  healthGauge
     aircraftHudRollingDigits:
         (airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet &&)
             rollingDigits
       aircraftHudInstruments:
           (airfix::content::LoadedLegacyAircraftHudInstrumentTextureSet &&)
               hudInstruments
      aircraftHudWeaponPanels:
          (airfix::content::LoadedLegacyAircraftHudWeaponPanelTextureSet &&)
              weaponPanels
    aircraftHudIdentityStatus:
        (airfix::content::LoadedLegacyAircraftHudIdentityStatusTextureSet &&)
            identityStatus
                        error:(NSError *_Nullable *_Nullable)error {
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
    dispatch_queue_t resourceReleaseQueue = self.resourceReleaseQueue;
    AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder = self.gpuBudgetHolder;
    if (commandQueue == nil || device == nil || resourceReleaseQueue == nil ||
        gpuBudgetHolder == nil || gpuBudgetHolder->_ledger == nullptr) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::missingDevice,
            @"Metal is unavailable while preparing the private room.");
      }
      return nil;
    }

    const auto cameraInitializeInput = gameplayCameraInitializeInput(room);

    PrivateRoomPreflight preflight;
    if (!preflightPrivateRoom(device, room, preflight) || !crosshairs.valid() ||
        crosshairs.revision != room.revision || !healthGauge.valid() ||
        healthGauge.revision != room.revision || !rollingDigits.valid() ||
        rollingDigits.revision != room.revision || !hudInstruments.valid() ||
        hudInstruments.revision != room.revision || !weaponPanels.valid() ||
        weaponPanels.revision != room.revision || !identityStatus.valid() ||
        identityStatus.revision != room.revision) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::invalidPayload,
            @"The private room failed the bounded Metal snapshot contract.");
      }
      return nil;
    }
    for (std::size_t textureIndex = 0U;
         textureIndex < crosshairs.textures.size(); ++textureIndex) {
      if (!validateTextureAsset(crosshairs.textures[textureIndex], textureIndex,
                                preflight.aggregateGpuBytes)) {
        if (error != nullptr) {
          *error = makeError(RendererError::invalidPayload,
                             @"The private crosshair set failed the bounded "
                             @"Metal snapshot contract.");
        }
        return nil;
      }
    }
    for (std::size_t textureIndex = 0U;
         textureIndex < healthGauge.textures.size(); ++textureIndex) {
      if (!validateTextureAsset(healthGauge.textures[textureIndex],
                                textureIndex, preflight.aggregateGpuBytes)) {
        if (error != nullptr) {
          *error = makeError(RendererError::invalidPayload,
                             @"The private health gauge set failed the bounded "
                             @"Metal snapshot contract.");
        }
        return nil;
      }
    }
    for (std::size_t textureIndex = 0U;
         textureIndex < rollingDigits.textures.size(); ++textureIndex) {
      if (!validateTextureAsset(rollingDigits.textures[textureIndex],
                                textureIndex, preflight.aggregateGpuBytes)) {
        if (error != nullptr) {
          *error = makeError(
              RendererError::invalidPayload,
              @"The private rolling-digit atlas failed the bounded Metal "
               "snapshot contract.");
        }
        return nil;
      }
    }
    for (std::size_t textureIndex = 0U;
         textureIndex < hudInstruments.textures.size(); ++textureIndex) {
      if (!validateTextureAsset(hudInstruments.textures[textureIndex],
                                textureIndex, preflight.aggregateGpuBytes)) {
        if (error != nullptr) {
          *error = makeError(
              RendererError::invalidPayload,
              @"The private HUD instrument set failed the bounded Metal "
               "snapshot contract.");
        }
        return nil;
      }
    }
    for (std::size_t textureIndex = 0U;
         textureIndex < weaponPanels.textures.size(); ++textureIndex) {
      if (!validateTextureAsset(weaponPanels.textures[textureIndex],
                                textureIndex, preflight.aggregateGpuBytes)) {
        if (error != nullptr) {
          *error = makeError(
              RendererError::invalidPayload,
              @"The private HUD weapon-panel set failed the bounded Metal "
               "snapshot contract.");
        }
        return nil;
      }
    }
    for (std::size_t textureIndex = 0U;
         textureIndex < identityStatus.textures.size(); ++textureIndex) {
      if (!validateTextureAsset(identityStatus.textures[textureIndex],
                                textureIndex, preflight.aggregateGpuBytes)) {
        if (error != nullptr) {
          *error = makeError(
              RendererError::invalidPayload,
              @"The private HUD identity/status set failed the bounded Metal "
               "snapshot contract.");
        }
        return nil;
      }
    }
    const auto scenePosePlan = airfix::render::planPlayerActorPoseRuntime(
        room.playerActorBinding, room.playerActorInstanceProvenance,
        room.model.instances);
    if (scenePosePlan.status ==
        airfix::render::PlayerActorPoseRuntimePreparationStatus::
            resourceLimit) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::resourceLimit,
            @"The private room pose runtime exceeds its bounded resources.");
      }
      return nil;
    }
    if (!accountCpuPackedBytes(scenePosePlan.retainedPoseBytes,
                               preflight.aggregateCpuPackedBytes,
                               kMaximumPrivateRoomCpuPackedBytes)) {
      if (error != nullptr) {
        *error = makeError(
            RendererError::resourceLimit,
            @"The private room pose runtime exceeds the retained CPU budget.");
      }
      return nil;
    }

    auto scenePosePreparation = airfix::render::preparePlayerActorPoseRuntime(
        room.playerActorBinding, room.playerActorInstanceProvenance,
        room.model.instances, actorWorldFrom(room.playerSpawnPose),
        scenePosePlan);
    if (scenePosePreparation.status ==
        airfix::render::PlayerActorPoseRuntimePreparationStatus::
            resourceLimit) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The private room pose runtime could not reserve "
                           @"its bounded storage.");
      }
      return nil;
    }
    if (scenePosePreparation.status ==
            airfix::render::PlayerActorPoseRuntimePreparationStatus::
                invalidPayload ||
        (scenePosePreparation.status ==
             airfix::render::PlayerActorPoseRuntimePreparationStatus::ready &&
         scenePosePreparation.runtime == nullptr) ||
        (scenePosePreparation.status ==
             airfix::render::PlayerActorPoseRuntimePreparationStatus::
                 noPlayer &&
         scenePosePreparation.runtime != nullptr)) {
      if (error != nullptr) {
        *error = makeError(RendererError::invalidPayload,
                           @"The private room pose runtime is inconsistent.");
      }
      return nil;
    }
    airfix::render::LegacyGameplayCameraMissionRuntimeLimits
        cameraRuntimeLimits;
    cameraRuntimeLimits.maximumAdditionalRetainedBytes =
        kMaximumPrivateRoomCpuPackedBytes - preflight.aggregateCpuPackedBytes;
    auto cameraRuntimeBuild =
        airfix::render::LegacyGameplayCameraMissionRuntime::create(
            std::move(room.spatialArena),
            std::move(room.placedDynamicCollision),
            std::move(room.playerActorCollision), room.runtimeBasis,
            cameraInitializeInput, cameraRuntimeLimits);
    if (!cameraRuntimeBuild.complete()) {
      bool resourceLimit = false;
      if (cameraRuntimeBuild.issue.has_value()) {
        using Issue =
            airfix::render::LegacyGameplayCameraMissionRuntimeBuildIssueKind;
        switch (cameraRuntimeBuild.issue->kind) {
        case Issue::candidateRecordLimitExceeded:
        case Issue::constraintPlaneLimitExceeded:
        case Issue::dynamicMeshCountOverflow:
        case Issue::dynamicObjectCountOverflow:
        case Issue::dynamicObjectLimitExceeded:
        case Issue::dynamicRoomRangeLimitExceeded:
        case Issue::retainedByteSizeOverflow:
        case Issue::retainedByteLimitExceeded:
        case Issue::allocationFailure:
          resourceLimit = true;
          break;
        case Issue::incompleteArena:
        case Issue::invalidBasis:
        case Issue::initialWorldRoomOutOfRange:
        case Issue::invalidPlacedCollision:
        case Issue::placedCollisionRoomCountMismatch:
        case Issue::invalidPlayerCollision:
        case Issue::coordinatorInitializationFailed:
        case Issue::exchangeInitializationFailed:
          break;
        }
      }
      if (error != nullptr) {
        *error = makeError(
            resourceLimit ? RendererError::resourceLimit
                          : RendererError::invalidPayload,
            resourceLimit
                ? @"The private room gameplay camera runtime exceeds its "
                  @"bounded resources."
                : @"The private room gameplay camera runtime is invalid.");
      }
      return nil;
    }
    if (!accountCpuPackedBytes(cameraRuntimeBuild.additionalRetainedBytes,
                               preflight.aggregateCpuPackedBytes,
                               kMaximumPrivateRoomCpuPackedBytes)) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The private room gameplay camera runtime exceeds "
                           @"the retained CPU budget.");
      }
      return nil;
    }
    std::shared_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>
        cameraMissionRuntime;
    try {
      cameraMissionRuntime =
          std::shared_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>(
              std::move(cameraRuntimeBuild.runtime));
    } catch (const std::bad_alloc &) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The private room camera endpoint could not "
                           @"reserve its ownership token.");
      }
      return nil;
    }
    {
      const auto initialCamera = cameraMissionRuntime->tryAcquire();
      if (!initialCamera.has_value() || !initialCamera->valid() ||
          initialCamera->packet() == nullptr ||
          initialCamera->simulationStep() != 0U ||
          initialCamera->cameraPublicationGeneration() != 1U) {
        if (error != nullptr) {
          *error = makeError(
              RendererError::invalidPayload,
              @"The private room gameplay camera bootstrap is invalid.");
        }
        return nil;
      }
    }

    // Keep the cheap logical-byte rejection before building allocator
    // descriptors. It is best-effort; the aligned CAS reservation below
    // is the authoritative admission decision.
    std::size_t logicalAggregateGpuBytes =
        gpuBudgetHolder->_ledger->reservedBytes();
    if (!accountGpuBytes(preflight.aggregateGpuBytes, logicalAggregateGpuBytes,
                         gpuBudgetHolder->_ledger->maximumBytes())) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The live snapshots and candidate exceed the "
                           @"logical Metal aggregate budget.");
      }
      return nil;
    }

    std::size_t bufferHeapBytes = 0U;
    std::size_t bufferHeapAlignment = 0U;
    bool heapPlanValid = true;
    for (std::size_t meshSlot = 0U;
         meshSlot < preflight.vertexByteCounts.size(); ++meshSlot) {
      if ((preflight.vertexByteCounts[meshSlot] != 0U &&
           !accountHeapResourcePlacement(
               [device
                   heapBufferSizeAndAlignWithLength:preflight.vertexByteCounts
                                                        [meshSlot]
                                            options:
                                                kSharedTrackedResourceOptions],
               bufferHeapBytes, bufferHeapAlignment,
               kMaximumPrivateRoomGpuHeapPlanBytes)) ||
          (preflight.indexByteCounts[meshSlot] != 0U &&
           !accountHeapResourcePlacement(
               [device
                   heapBufferSizeAndAlignWithLength:preflight.indexByteCounts
                                                        [meshSlot]
                                            options:
                                                kSharedTrackedResourceOptions],
               bufferHeapBytes, bufferHeapAlignment,
               kMaximumPrivateRoomGpuHeapPlanBytes))) {
        heapPlanValid = false;
        break;
      }
    }
    std::size_t textureHeapBytes = 0U;
    std::size_t textureHeapAlignment = 0U;
    if (heapPlanValid) {
      for (const auto &source : room.textures) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          heapPlanValid = false;
          break;
        }
        configurePrivateTextureDescriptor(descriptor, source);
        if (!accountHeapResourcePlacement(
                [device heapTextureSizeAndAlignWithDescriptor:descriptor],
                textureHeapBytes, textureHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
          heapPlanValid = false;
          break;
        }
      }
    }
    if (heapPlanValid) {
      for (const auto &source : crosshairs.textures) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          heapPlanValid = false;
          break;
        }
        configurePrivateTextureDescriptor(descriptor, source);
        if (!accountHeapResourcePlacement(
                [device heapTextureSizeAndAlignWithDescriptor:descriptor],
                textureHeapBytes, textureHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
          heapPlanValid = false;
          break;
        }
      }
    }
    if (heapPlanValid) {
      for (const auto &source : healthGauge.textures) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          heapPlanValid = false;
          break;
        }
        configurePrivateTextureDescriptor(descriptor, source);
        if (!accountHeapResourcePlacement(
                [device heapTextureSizeAndAlignWithDescriptor:descriptor],
                textureHeapBytes, textureHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
          heapPlanValid = false;
          break;
        }
      }
    }
    if (heapPlanValid) {
      for (const auto &source : rollingDigits.textures) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          heapPlanValid = false;
          break;
        }
        configurePrivateTextureDescriptor(descriptor, source);
        if (!accountHeapResourcePlacement(
                [device heapTextureSizeAndAlignWithDescriptor:descriptor],
                textureHeapBytes, textureHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
          heapPlanValid = false;
          break;
        }
      }
    }
    if (heapPlanValid) {
      for (const auto &source : hudInstruments.textures) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          heapPlanValid = false;
          break;
        }
        configurePrivateTextureDescriptor(descriptor, source);
        if (!accountHeapResourcePlacement(
                [device heapTextureSizeAndAlignWithDescriptor:descriptor],
                textureHeapBytes, textureHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
          heapPlanValid = false;
          break;
        }
      }
    }
    if (heapPlanValid) {
      for (const auto &source : weaponPanels.textures) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          heapPlanValid = false;
          break;
        }
        configurePrivateTextureDescriptor(descriptor, source);
        if (!accountHeapResourcePlacement(
                [device heapTextureSizeAndAlignWithDescriptor:descriptor],
                textureHeapBytes, textureHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
          heapPlanValid = false;
          break;
        }
      }
    }
    if (heapPlanValid) {
      for (const auto &source : identityStatus.textures) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          heapPlanValid = false;
          break;
        }
        configurePrivateTextureDescriptor(descriptor, source);
        if (!accountHeapResourcePlacement(
                [device heapTextureSizeAndAlignWithDescriptor:descriptor],
                textureHeapBytes, textureHeapAlignment,
                kMaximumPrivateRoomGpuHeapPlanBytes)) {
          heapPlanValid = false;
          break;
        }
      }
    }
    if (!heapPlanValid ||
        !finalizeHeapPlan(bufferHeapBytes, bufferHeapAlignment,
                          kMaximumPrivateRoomGpuHeapPlanBytes) ||
        !finalizeHeapPlan(textureHeapBytes, textureHeapAlignment,
                          kMaximumPrivateRoomGpuHeapPlanBytes)) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The private Metal heap plan exceeds its budget.");
      }
      return nil;
    }
    std::size_t admittedHeapPlanBytes = 0U;
    if (!accountGpuBytes(bufferHeapBytes, admittedHeapPlanBytes,
                         kMaximumPrivateRoomGpuHeapPlanBytes) ||
        !accountGpuBytes(textureHeapBytes, admittedHeapPlanBytes,
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
        gpuBudgetHolder->_ledger->tryReserve(admittedHeapPlanBytes);
    if (!privatePlanReservation.has_value()) {
      if (error != nullptr) {
        *error = makeError(RendererError::resourceLimit,
                           @"The private heap plan is unavailable in the "
                           @"aggregate heap-admission budget.");
      }
      return nil;
    }

    // The pool owns all autoreleased staging wrappers and Metal command
    // objects. It drains before the outer plan reservation on every
    // failure.
    @autoreleasepool {
      id<MTLHeap> bufferHeap =
          bufferHeapBytes != 0U
              ? newSharedTrackedHeap(device, bufferHeapBytes,
                                     @"Airfix private snapshot buffer heap")
              : nil;
      id<MTLHeap> textureHeap =
          textureHeapBytes != 0U
              ? newSharedTrackedHeap(device, textureHeapBytes,
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

      NSMutableArray<AirfixMetalMeshBuffers *> *meshBuffers = [NSMutableArray
          arrayWithCapacity:static_cast<NSUInteger>(room.model.meshes.size())];
      if (meshBuffers == nil) {
        if (error != nullptr) {
          *error =
              makeError(RendererError::bufferCreation,
                        @"Private room mesh ownership could not be allocated.");
        }
        return nil;
      }
      for (std::size_t meshSlot = 0U; meshSlot < room.model.meshes.size();
           ++meshSlot) {
        const auto &mesh = room.model.meshes[meshSlot];
        auto packedVertices = repackVertices(mesh.vertices);
        std::size_t packedBytes = 0U;
        if (!checkedMultiply(packedVertices.size(), sizeof(GpuVertex),
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
              [bufferHeap newBufferWithLength:packedBytes
                                      options:kSharedTrackedResourceOptions];
          if (vertexBuffer != nil && vertexBuffer.contents != nullptr) {
            std::memcpy(vertexBuffer.contents, packedVertices.data(),
                        packedBytes);
          }
        }
        id<MTLBuffer> indexBuffer = nil;
        const auto indexBytes = preflight.indexByteCounts[meshSlot];
        if (indexBytes != 0U) {
          indexBuffer =
              [bufferHeap newBufferWithLength:indexBytes
                                      options:kSharedTrackedResourceOptions];
          if (indexBuffer != nil && indexBuffer.contents != nullptr) {
            std::memcpy(indexBuffer.contents, mesh.indices.data(), indexBytes);
          }
        }
        if ((packedBytes != 0U &&
             (vertexBuffer == nil || vertexBuffer.contents == nullptr)) ||
            (indexBytes != 0U &&
             (indexBuffer == nil || indexBuffer.contents == nullptr))) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::bufferCreation,
                @"Metal could not create every private room mesh buffer.");
          }
          return nil;
        }
        if ((vertexBuffer != nil && vertexBuffer.allocatedSize == 0U) ||
            (indexBuffer != nil && indexBuffer.allocatedSize == 0U)) {
          if (error != nullptr) {
            *error = makeError(RendererError::resourceLimit,
                               @"A private heap buffer has no allocation.");
          }
          return nil;
        }
        AirfixMetalMeshBuffers *buffers = [[AirfixMetalMeshBuffers alloc] init];
        if (buffers == nil) {
          if (error != nullptr) {
            *error =
                makeError(RendererError::bufferCreation,
                          @"Private room mesh ownership could not be created.");
          }
          return nil;
        }
        buffers.vertexBuffer = vertexBuffer;
        buffers.indexBuffer = indexBuffer;
        [meshBuffers addObject:buffers];
      }

      NSMutableArray<id<MTLTexture>> *textures = [NSMutableArray
          arrayWithCapacity:static_cast<NSUInteger>(room.textures.size())];
      NSMutableArray<id<MTLTexture>> *crosshairTextures =
          [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(
                                                crosshairs.textures.size())];
      NSMutableArray<id<MTLTexture>> *healthGaugeTextures =
          [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(
                                                healthGauge.textures.size())];
      NSMutableArray<id<MTLTexture>> *rollingDigitTextures =
          [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(
                                                rollingDigits.textures.size())];
      NSMutableArray<id<MTLTexture>> *hudInstrumentTextures = [NSMutableArray
          arrayWithCapacity:static_cast<NSUInteger>(
                                hudInstruments.textures.size())];
      NSMutableArray<id<MTLTexture>> *weaponPanelTextures =
          [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(
                                                weaponPanels.textures.size())];
      NSMutableArray<id<MTLTexture>> *identityStatusTextures = [NSMutableArray
          arrayWithCapacity:static_cast<NSUInteger>(
                                identityStatus.textures.size())];
      NSMutableArray<id<MTLTexture>> *generatedMipTextures =
          [NSMutableArray array];
      if (textures == nil || crosshairTextures == nil ||
          healthGaugeTextures == nil || rollingDigitTextures == nil ||
          hudInstrumentTextures == nil || weaponPanelTextures == nil ||
          identityStatusTextures == nil || generatedMipTextures == nil) {
        if (error != nullptr) {
          *error =
              makeError(RendererError::textureCreation,
                        @"Private texture ownership could not be allocated.");
        }
        return nil;
      }
      for (const auto &source : room.textures) {
        const auto &upload = source.upload;
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          if (error != nullptr) {
            *error =
                makeError(RendererError::textureCreation,
                          @"Private texture metadata could not be allocated.");
          }
          return nil;
        }
        configurePrivateTextureDescriptor(descriptor, source);

        id<MTLTexture> texture =
            [textureHeap newTextureWithDescriptor:descriptor];
        if (texture == nil || texture.width != descriptor.width ||
            texture.height != descriptor.height ||
            texture.mipmapLevelCount != descriptor.mipmapLevelCount ||
            texture.allocatedSize == 0U) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Metal could not create the complete private texture.");
          }
          return nil;
        }

        for (std::size_t levelIndex = 0U;
             levelIndex < source.uploadLevels.size(); ++levelIndex) {
          const auto &level = upload.uploadLevels[levelIndex];
          const auto &image = source.uploadLevels[levelIndex];
          [texture
              replaceRegion:MTLRegionMake2D(
                                0U, 0U, static_cast<NSUInteger>(image.width),
                                static_cast<NSUInteger>(image.height))
                mipmapLevel:static_cast<NSUInteger>(level.level)
                  withBytes:image.pixels.data()
                bytesPerRow:static_cast<NSUInteger>(level.bytesPerRow)];
        }
        if (upload.mipPolicy ==
            airfix::render::GtiMipPolicy::generateFromBase) {
          [generatedMipTextures addObject:texture];
        }
        [textures addObject:texture];
      }

      for (const auto &source : crosshairs.textures) {
        const auto &upload = source.upload;
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Private crosshair metadata could not be allocated.");
          }
          return nil;
        }
        configurePrivateTextureDescriptor(descriptor, source);

        id<MTLTexture> texture =
            [textureHeap newTextureWithDescriptor:descriptor];
        if (texture == nil || texture.width != descriptor.width ||
            texture.height != descriptor.height ||
            texture.mipmapLevelCount != descriptor.mipmapLevelCount ||
            texture.allocatedSize == 0U) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Metal could not create every private crosshair texture.");
          }
          return nil;
        }

        for (std::size_t levelIndex = 0U;
             levelIndex < source.uploadLevels.size(); ++levelIndex) {
          const auto &level = upload.uploadLevels[levelIndex];
          const auto &image = source.uploadLevels[levelIndex];
          [texture
              replaceRegion:MTLRegionMake2D(
                                0U, 0U, static_cast<NSUInteger>(image.width),
                                static_cast<NSUInteger>(image.height))
                mipmapLevel:static_cast<NSUInteger>(level.level)
                  withBytes:image.pixels.data()
                bytesPerRow:static_cast<NSUInteger>(level.bytesPerRow)];
        }
        if (upload.mipPolicy ==
            airfix::render::GtiMipPolicy::generateFromBase) {
          [generatedMipTextures addObject:texture];
        }
        [crosshairTextures addObject:texture];
      }

      for (const auto &source : healthGauge.textures) {
        const auto &upload = source.upload;
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Private health gauge metadata could not be allocated.");
          }
          return nil;
        }
        configurePrivateTextureDescriptor(descriptor, source);

        id<MTLTexture> texture =
            [textureHeap newTextureWithDescriptor:descriptor];
        if (texture == nil || texture.width != descriptor.width ||
            texture.height != descriptor.height ||
            texture.mipmapLevelCount != descriptor.mipmapLevelCount ||
            texture.allocatedSize == 0U) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Metal could not create every private health gauge texture.");
          }
          return nil;
        }

        for (std::size_t levelIndex = 0U;
             levelIndex < source.uploadLevels.size(); ++levelIndex) {
          const auto &level = upload.uploadLevels[levelIndex];
          const auto &image = source.uploadLevels[levelIndex];
          [texture
              replaceRegion:MTLRegionMake2D(
                                0U, 0U, static_cast<NSUInteger>(image.width),
                                static_cast<NSUInteger>(image.height))
                mipmapLevel:static_cast<NSUInteger>(level.level)
                  withBytes:image.pixels.data()
                bytesPerRow:static_cast<NSUInteger>(level.bytesPerRow)];
        }
        if (upload.mipPolicy ==
            airfix::render::GtiMipPolicy::generateFromBase) {
          [generatedMipTextures addObject:texture];
        }
        [healthGaugeTextures addObject:texture];
      }

      for (const auto &source : rollingDigits.textures) {
        const auto &upload = source.upload;
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Private rolling-digit metadata could not be allocated.");
          }
          return nil;
        }
        configurePrivateTextureDescriptor(descriptor, source);

        id<MTLTexture> texture =
            [textureHeap newTextureWithDescriptor:descriptor];
        if (texture == nil || texture.width != descriptor.width ||
            texture.height != descriptor.height ||
            texture.mipmapLevelCount != descriptor.mipmapLevelCount ||
            texture.allocatedSize == 0U) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Metal could not create the private rolling-digit atlas.");
          }
          return nil;
        }

        for (std::size_t levelIndex = 0U;
             levelIndex < source.uploadLevels.size(); ++levelIndex) {
          const auto &level = upload.uploadLevels[levelIndex];
          const auto &image = source.uploadLevels[levelIndex];
          [texture
              replaceRegion:MTLRegionMake2D(
                                0U, 0U, static_cast<NSUInteger>(image.width),
                                static_cast<NSUInteger>(image.height))
                mipmapLevel:static_cast<NSUInteger>(level.level)
                  withBytes:image.pixels.data()
                bytesPerRow:static_cast<NSUInteger>(level.bytesPerRow)];
        }
        if (upload.mipPolicy ==
            airfix::render::GtiMipPolicy::generateFromBase) {
          [generatedMipTextures addObject:texture];
        }
        [rollingDigitTextures addObject:texture];
      }

      for (const auto &source : hudInstruments.textures) {
        const auto &upload = source.upload;
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Private HUD instrument metadata could not be allocated.");
          }
          return nil;
        }
        configurePrivateTextureDescriptor(descriptor, source);

        id<MTLTexture> texture =
            [textureHeap newTextureWithDescriptor:descriptor];
        if (texture == nil || texture.width != descriptor.width ||
            texture.height != descriptor.height ||
            texture.mipmapLevelCount != descriptor.mipmapLevelCount ||
            texture.allocatedSize == 0U) {
          if (error != nullptr) {
            *error = makeError(RendererError::textureCreation,
                               @"Metal could not create every private HUD "
                               @"instrument texture.");
          }
          return nil;
        }

        for (std::size_t levelIndex = 0U;
             levelIndex < source.uploadLevels.size(); ++levelIndex) {
          const auto &level = upload.uploadLevels[levelIndex];
          const auto &image = source.uploadLevels[levelIndex];
          [texture
              replaceRegion:MTLRegionMake2D(
                                0U, 0U, static_cast<NSUInteger>(image.width),
                                static_cast<NSUInteger>(image.height))
                mipmapLevel:static_cast<NSUInteger>(level.level)
                  withBytes:image.pixels.data()
                bytesPerRow:static_cast<NSUInteger>(level.bytesPerRow)];
        }
        if (upload.mipPolicy ==
            airfix::render::GtiMipPolicy::generateFromBase) {
          [generatedMipTextures addObject:texture];
        }
        [hudInstrumentTextures addObject:texture];
      }

      for (const auto &source : weaponPanels.textures) {
        const auto &upload = source.upload;
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::textureCreation,
                @"Private HUD weapon-panel metadata could not be allocated.");
          }
          return nil;
        }
        configurePrivateTextureDescriptor(descriptor, source);

        id<MTLTexture> texture =
            [textureHeap newTextureWithDescriptor:descriptor];
        if (texture == nil || texture.width != descriptor.width ||
            texture.height != descriptor.height ||
            texture.mipmapLevelCount != descriptor.mipmapLevelCount ||
            texture.allocatedSize == 0U) {
          if (error != nullptr) {
            *error = makeError(RendererError::textureCreation,
                               @"Metal could not create every private HUD "
                               @"weapon-panel texture.");
          }
          return nil;
        }

        for (std::size_t levelIndex = 0U;
             levelIndex < source.uploadLevels.size(); ++levelIndex) {
          const auto &level = upload.uploadLevels[levelIndex];
          const auto &image = source.uploadLevels[levelIndex];
          [texture
              replaceRegion:MTLRegionMake2D(
                                0U, 0U, static_cast<NSUInteger>(image.width),
                                static_cast<NSUInteger>(image.height))
                mipmapLevel:static_cast<NSUInteger>(level.level)
                  withBytes:image.pixels.data()
                bytesPerRow:static_cast<NSUInteger>(level.bytesPerRow)];
        }
        if (upload.mipPolicy ==
            airfix::render::GtiMipPolicy::generateFromBase) {
          [generatedMipTextures addObject:texture];
        }
        [weaponPanelTextures addObject:texture];
      }

      for (const auto &source : identityStatus.textures) {
        const auto &upload = source.upload;
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        if (descriptor == nil) {
          if (error != nullptr) {
            *error = makeError(RendererError::textureCreation,
                               @"Private HUD identity/status metadata could "
                               @"not be allocated.");
          }
          return nil;
        }
        configurePrivateTextureDescriptor(descriptor, source);

        id<MTLTexture> texture =
            [textureHeap newTextureWithDescriptor:descriptor];
        if (texture == nil || texture.width != descriptor.width ||
            texture.height != descriptor.height ||
            texture.mipmapLevelCount != descriptor.mipmapLevelCount ||
            texture.allocatedSize == 0U) {
          if (error != nullptr) {
            *error = makeError(RendererError::textureCreation,
                               @"Metal could not create every private HUD "
                                "identity/status texture.");
          }
          return nil;
        }

        for (std::size_t levelIndex = 0U;
             levelIndex < source.uploadLevels.size(); ++levelIndex) {
          const auto &level = upload.uploadLevels[levelIndex];
          const auto &image = source.uploadLevels[levelIndex];
          [texture
              replaceRegion:MTLRegionMake2D(
                                0U, 0U, static_cast<NSUInteger>(image.width),
                                static_cast<NSUInteger>(image.height))
                mipmapLevel:static_cast<NSUInteger>(level.level)
                  withBytes:image.pixels.data()
                bytesPerRow:static_cast<NSUInteger>(level.bytesPerRow)];
        }
        if (upload.mipPolicy ==
            airfix::render::GtiMipPolicy::generateFromBase) {
          [generatedMipTextures addObject:texture];
        }
        [identityStatusTextures addObject:texture];
      }

      if (generatedMipTextures.count != 0U) {
        id<MTLCommandBuffer> mipCommandBuffer = [commandQueue commandBuffer];
        if (mipCommandBuffer == nil) {
          if (error != nullptr) {
            *error = makeError(
                RendererError::blitCreation,
                @"Metal could not create the private mip command buffer.");
          }
          return nil;
        }
        mipCommandBuffer.label = @"Airfix private texture mip generation";
        id<MTLBlitCommandEncoder> blit = [mipCommandBuffer blitCommandEncoder];
        if (blit == nil) {
          if (error != nullptr) {
            *error =
                makeError(RendererError::blitCreation,
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
        if (mipCommandBuffer.status != MTLCommandBufferStatusCompleted) {
          if (error != nullptr) {
            NSString *reason = mipCommandBuffer.error.localizedDescription;
            if (reason == nil) {
              reason = @"unknown Metal mip-generation failure";
            }
            *error = makeError(RendererError::mipGeneration,
                               [@"Private texture mip generation failed: "
                                   stringByAppendingString:reason]);
          }
          return nil;
        }
      }

      std::size_t currentAllocatedHeapBytes = 0U;
      if ((bufferHeap != nil && !accountCurrentHeapAllocation(
                                    bufferHeap, currentAllocatedHeapBytes)) ||
          (textureHeap != nil && !accountCurrentHeapAllocation(
                                     textureHeap, currentAllocatedHeapBytes))) {
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
      if (!finalizeHeapAllocationReservation(*gpuBudgetHolder->_ledger,
                                             *privatePlanReservation,
                                             currentAllocatedHeapBytes)) {
        if (error != nullptr) {
          *error = makeError(RendererError::resourceLimit,
                             @"The private heaps' current allocation is "
                             @"unavailable in the aggregate admission budget.");
        }
        return nil;
      }

      AirfixMetalRoomResources *candidateResources =
          [[AirfixMetalRoomResources alloc] init];
      AirfixMetalRoomSnapshot *candidate =
          [[AirfixMetalRoomSnapshot alloc] init];
      AirfixPreparedMetalRoom *preparedRoom =
          [[AirfixPreparedMetalRoom alloc] init];
      AirfixGpuBudgetReservationHolder *reservationHolder =
          [[AirfixGpuBudgetReservationHolder alloc] init];
      NSObject *ownerToken = self.preparationOwnerToken;
      if (candidateResources == nil || candidate == nil ||
          preparedRoom == nil || reservationHolder == nil ||
          ownerToken == nil) {
        if (error != nullptr) {
          *error =
              makeError(RendererError::unexpectedFailure,
                        @"Private Metal room ownership could not be created.");
        }
        return nil;
      }
      NSArray<AirfixMetalMeshBuffers *> *meshBufferSnapshot =
          [meshBuffers copy];
      NSArray<id<MTLTexture>> *textureSnapshot = [textures copy];
      NSArray<id<MTLTexture>> *crosshairTextureSnapshot =
          [crosshairTextures copy];
      NSArray<id<MTLTexture>> *healthGaugeTextureSnapshot =
          [healthGaugeTextures copy];
      NSArray<id<MTLTexture>> *rollingDigitTextureSnapshot =
          [rollingDigitTextures copy];
      NSArray<id<MTLTexture>> *hudInstrumentTextureSnapshot =
          [hudInstrumentTextures copy];
      NSArray<id<MTLTexture>> *weaponPanelTextureSnapshot =
          [weaponPanelTextures copy];
      NSArray<id<MTLTexture>> *identityStatusTextureSnapshot =
          [identityStatusTextures copy];
      if (meshBufferSnapshot == nil || textureSnapshot == nil ||
          crosshairTextureSnapshot == nil ||
          healthGaugeTextureSnapshot == nil ||
          rollingDigitTextureSnapshot == nil ||
          hudInstrumentTextureSnapshot == nil ||
          weaponPanelTextureSnapshot == nil ||
          identityStatusTextureSnapshot == nil ||
          meshBufferSnapshot.count != room.model.meshes.size() ||
          textureSnapshot.count != room.textures.size() ||
          crosshairTextureSnapshot.count != crosshairs.textures.size() ||
          healthGaugeTextureSnapshot.count != healthGauge.textures.size() ||
          rollingDigitTextureSnapshot.count != rollingDigits.textures.size() ||
          hudInstrumentTextureSnapshot.count !=
              hudInstruments.textures.size() ||
          weaponPanelTextureSnapshot.count != weaponPanels.textures.size() ||
          identityStatusTextureSnapshot.count !=
              identityStatus.textures.size()) {
        if (error != nullptr) {
          *error =
              makeError(RendererError::unexpectedFailure,
                        @"Private Metal resource ownership is incomplete.");
        }
        return nil;
      }
      candidateResources.meshBuffers = meshBufferSnapshot;
      candidateResources.textures = textureSnapshot;
      candidateResources.crosshairTextures = crosshairTextureSnapshot;
      candidateResources.healthGaugeTextures = healthGaugeTextureSnapshot;
      candidateResources.rollingDigitTextures = rollingDigitTextureSnapshot;
      candidateResources.hudInstrumentTextures = hudInstrumentTextureSnapshot;
      candidateResources.weaponPanelTextures = weaponPanelTextureSnapshot;
      candidateResources.identityStatusTextures = identityStatusTextureSnapshot;
      candidateResources.bufferHeap = bufferHeap;
      candidateResources.textureHeap = textureHeap;
      preparedRoom->_ownerToken = ownerToken;
      preparedRoom->_device = device;
      preparedRoom->_published = NO;
      candidateResources->_indexOffsets = std::move(preflight.indexOffsets);
      candidateResources->_revision = room.revision;
      candidateResources->_scenePoseRuntime =
          std::move(scenePosePreparation.runtime);
      candidateResources->_cameraMissionRuntime =
          std::move(cameraMissionRuntime);
      candidate->_resources = candidateResources;
      candidate->_releaseQueue = resourceReleaseQueue;
      candidate->_worldRoomInstalled = YES;

      // Texture upload bytes are intentionally not retained by the native
      // snapshot after Metal owns the complete resource set.
      std::vector<airfix::content::LoadedTextureAsset>().swap(room.textures);
      candidateResources->_missionRoom.emplace(std::move(room));
      candidateResources->_crosshairs.emplace(std::move(crosshairs));
      candidateResources->_healthGauge.emplace(std::move(healthGauge));
      candidateResources->_rollingDigits.emplace(std::move(rollingDigits));
      candidateResources->_hudInstruments.emplace(std::move(hudInstruments));
      candidateResources->_weaponPanels.emplace(std::move(weaponPanels));
      candidateResources->_identityStatus.emplace(std::move(identityStatus));

      reservationHolder->_reservation.emplace(
          std::move(*privatePlanReservation));
      candidate->_gpuBudgetReservationHolder = reservationHolder;
      preparedRoom->_snapshot = candidate;
      return preparedRoom;
    }
  } catch (...) {
    if (error != nullptr) {
      *error =
          makeError(RendererError::unexpectedFailure,
                    @"The private Metal room snapshot could not be prepared.");
    }
    return nil;
  }
}

- (AirfixPlayerActorPoseRuntimeEndpoint)
    playerActorPoseRuntimeEndpointForPreparedRoom:
        (AirfixPreparedMetalRoom *)preparedRoom {
  // An engaged empty weak pointer is an invalid candidate sentinel. The
  // authenticated no-player path is the only path returning nullopt.
  if (!NSThread.isMainThread || preparedRoom == nil ||
      preparedRoom->_published ||
      preparedRoom->_ownerToken != self.preparationOwnerToken ||
      preparedRoom->_device != self.commandQueue.device ||
      preparedRoom->_snapshot == nil ||
      preparedRoom->_snapshot->_resources == nil ||
      !hasActiveGpuBudgetReservation(preparedRoom->_snapshot) ||
      !preparedRoom->_snapshot->_worldRoomInstalled ||
      preparedRoom->_snapshot->_resources->_cameraMissionRuntime == nullptr) {
    return AirfixPlayerActorPoseRuntimeEndpoint{
        std::in_place, std::weak_ptr<airfix::render::PlayerActorPoseRuntime>{}};
  }
  const auto &runtime = preparedRoom->_snapshot->_resources->_scenePoseRuntime;
  if (runtime == nullptr) {
    return std::nullopt;
  }
  return AirfixPlayerActorPoseRuntimeEndpoint{
      std::in_place,
      std::weak_ptr<airfix::render::PlayerActorPoseRuntime>{runtime}};
}

- (AirfixGameplayCameraMissionRuntimeEndpoint)
    gameplayCameraMissionRuntimeEndpointForPreparedRoom:
        (AirfixPreparedMetalRoom *)preparedRoom {
  if (!NSThread.isMainThread || preparedRoom == nil ||
      preparedRoom->_published ||
      preparedRoom->_ownerToken != self.preparationOwnerToken ||
      preparedRoom->_device != self.commandQueue.device ||
      preparedRoom->_snapshot == nil ||
      preparedRoom->_snapshot->_resources == nil ||
      !hasActiveGpuBudgetReservation(preparedRoom->_snapshot) ||
      !preparedRoom->_snapshot->_worldRoomInstalled ||
      preparedRoom->_snapshot->_resources->_cameraMissionRuntime == nullptr) {
    return {};
  }
  return std::weak_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>{
      preparedRoom->_snapshot->_resources->_cameraMissionRuntime};
}

- (BOOL)validatePreparedRoomForCommit:(AirfixPreparedMetalRoom *)preparedRoom
                                error:(NSError *_Nullable *_Nullable)error {
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
      *error = makeError(RendererError::invalidPreparedRoom,
                         @"The prepared Metal room is missing.");
    }
    return NO;
  }
  if (preparedRoom->_published) {
    if (error != nullptr) {
      *error = makeError(RendererError::preparedRoomAlreadyPublished,
                         @"The prepared Metal room was already published.");
    }
    return NO;
  }
  if (preparedRoom->_ownerToken != self.preparationOwnerToken ||
      preparedRoom->_device != self.commandQueue.device ||
      preparedRoom->_snapshot == nil ||
      preparedRoom->_snapshot->_resources == nil ||
      !hasActiveGpuBudgetReservation(preparedRoom->_snapshot) ||
      !preparedRoom->_snapshot->_worldRoomInstalled ||
      preparedRoom->_snapshot->_resources->_cameraMissionRuntime == nullptr) {
    if (error != nullptr) {
      *error = makeError(RendererError::invalidPreparedRoom,
                         @"The prepared Metal room belongs to a different "
                         @"renderer or device.");
    }
    return NO;
  }

  return YES;
}

- (void)commitValidatedPreparedRoom:(AirfixPreparedMetalRoom *)preparedRoom {
  AirfixMetalRoomSnapshot *candidate = preparedRoom->_snapshot;
  preparedRoom->_published = YES;
  preparedRoom->_snapshot = nil;
  // The coordinator has already revalidated serial and revision, and the
  // candidate already owns its aggregate budget debit. Exactly one atomic
  // strong-pointer assignment publishes the complete room without any
  // budget mutation.
  self.roomSnapshot = candidate;
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
  if (!NSThread.isMainThread) {
    return;
  }
  if (view == nil || view != self.metalView || view.device == nil ||
      view.device != self.commandQueue.device) {
    return;
  }
  AirfixMetalPresentationTransactionHolder *holder =
      self.presentationTransactionHolder;
  if (holder == nil) {
    return;
  }
  holder->_surfaceGeneration =
      holder->_surfaceGeneration == std::numeric_limits<std::uint64_t>::max()
          ? 1U
          : holder->_surfaceGeneration + 1U;
  self.diagnosticsOverlayTexture = nil;
  self.diagnosticsOverlayWidth = 0U;
  self.diagnosticsOverlayHeight = 0U;
  self.diagnosticsOverlayPixelScale = 0U;
  AirfixMetalDiagnosticsState *diagnosticsState = self.diagnosticsState;
  if (diagnosticsState != nil) {
    diagnosticsState->_lastOverlayRefresh.reset();
  }

  holder->_retrySchedule.resetForExplicitResize();
  const auto extent = outputPixelExtent(size);
  if (!extent.has_value()) {
    if (size.width <= 0.0 || size.height <= 0.0) {
      holder->_retrySchedule.resetForZeroExtent();
    } else {
      holder->_retrySchedule.recordExplicitFailure();
    }
    return;
  }

  NSError *ignoredError = nil;
  if (![self
          prepareAndPublishRenderPresentationSettings:holder->_desiredSettings
                                              forView:view
                                         outputExtent:*extent
                                                error:&ignoredError]) {
    holder->_retrySchedule.recordExplicitFailure();
  }
}

- (void)drawInMTKView:(MTKView *)view {
  if (!NSThread.isMainThread) {
    return;
  }
  if (view == nil || view != self.metalView || view.device == nil ||
      view.device != self.commandQueue.device) {
    return;
  }
  AirfixMetalPresentationTransactionHolder *presentationHolder =
      self.presentationTransactionHolder;
  const auto viewExtent = outputPixelExtent(view.drawableSize);
  if (presentationHolder == nil || !viewExtent.has_value()) {
    return;
  }
  const auto currentSurface = presentationSurfaceStamp(
      view, *viewExtent, presentationHolder->_surfaceGeneration);
  const auto *currentPresentation =
      presentationHolder->_transaction.activeState();
  if (currentPresentation == nullptr ||
      currentPresentation->surface() != currentSurface) {
    bool attemptIsAutomatic = false;
    bool shouldAttempt = true;
    if (presentationHolder->_retrySchedule.pending()) {
      shouldAttempt = presentationHolder->_retrySchedule.beginFrameRetry();
      attemptIsAutomatic = shouldAttempt;
    }
    if (shouldAttempt) {
      NSError *ignoredError = nil;
      if (![self
              prepareAndPublishRenderPresentationSettings:presentationHolder
                                                              ->_desiredSettings
                                                  forView:view
                                             outputExtent:*viewExtent
                                                    error:&ignoredError]) {
        if (attemptIsAutomatic) {
          presentationHolder->_retrySchedule.recordAutomaticFailure();
        } else {
          presentationHolder->_retrySchedule.recordExplicitFailure();
        }
      }
    }
    currentPresentation = presentationHolder->_transaction.activeState();
  }
  if (currentPresentation == nullptr ||
      currentPresentation->surface() != currentSurface) {
    return;
  }
  const airfix::render::RenderPresentationActiveState presentationSnapshot =
      *currentPresentation;
  const auto presentationSettings = presentationSnapshot.settings();
  const auto sceneTextureSampling =
      airfix::render::sceneTextureSamplingPolicyForProfile(
          presentationSettings.visualProfile);
  if (!sceneTextureSampling.has_value()) {
    return;
  }
  id<MTLSamplerState> sceneSamplerState =
      sceneTextureSampling->mode ==
              airfix::render::SceneTextureSamplingMode::anisotropicMipLinear
          ? self.enhancedSceneSamplerState
          : self.classicSceneSamplerState;
  if (sceneSamplerState == nil) {
    return;
  }

  AirfixMetalRoomSnapshot *snapshot = self.roomSnapshot;
  if (snapshot == nil) {
    return;
  }
  AirfixMetalRoomResources *resources = snapshot->_resources;
  if (resources == nil) {
    return;
  }
  AirfixMetalDiagnosticsState *diagnosticsState = self.diagnosticsState;
  AirfixSnapshotGpuBudgetLedgerHolder *gpuBudgetHolder = self.gpuBudgetHolder;
  if (diagnosticsState == nil || gpuBudgetHolder == nil ||
      gpuBudgetHolder->_ledger == nullptr) {
    return;
  }
  const auto frameStarted = std::chrono::steady_clock::now();
  double frameIntervalMilliseconds = 1000.0 / 60.0;
  if (diagnosticsState->_previousFrameStart.has_value()) {
    frameIntervalMilliseconds =
        std::chrono::duration<double, std::milli>(
            frameStarted - *diagnosticsState->_previousFrameStart)
            .count();
    if (!std::isfinite(frameIntervalMilliseconds) ||
        frameIntervalMilliseconds <= 0.0) {
      frameIntervalMilliseconds = 1000.0 / 60.0;
    }
  }
  diagnosticsState->_previousFrameStart = frameStarted;
  const bool gameplayCameraActive = resources->_missionRoom.has_value();
  std::optional<airfix::render::LegacyGameplayCameraPacketLease>
      gameplayCameraLease;
  if (gameplayCameraActive && resources->_cameraMissionRuntime != nullptr) {
    gameplayCameraLease = resources->_cameraMissionRuntime->tryAcquire();
  }
  const auto *gameplayCamera =
      gameplayCameraLease.has_value() ? gameplayCameraLease->packet() : nullptr;
  if (gameplayCameraActive && gameplayCamera == nullptr) {
    return;
  }
  const airfix::render::DrawModelPayload &payload =
      gameplayCameraActive ? resources->_missionRoom->model
                           : resources->_payload;
  const airfix::render::DrawSubmissionPlan &submissionPlan =
      gameplayCameraActive ? resources->_missionRoom->submission
                           : resources->_submissionPlan;
  AirfixBudgetedMetalTexture *fallbackResource = self.fallbackResource;
  if (fallbackResource == nil || fallbackResource->_texture == nil) {
    return;
  }
  MTLRenderPassDescriptor *renderPass = view.currentRenderPassDescriptor;
  id<CAMetalDrawable> drawable = view.currentDrawable;
  if (renderPass == nil || drawable == nil ||
      renderPass.colorAttachments[0].texture == nil ||
      renderPass.depthAttachment.texture == nil) {
    return;
  }
  id<MTLTexture> drawableDepthTexture = renderPass.depthAttachment.texture;
  const auto outputExtent = outputPixelExtent(drawable.texture);
  if (!outputExtent.has_value() ||
      *outputExtent != presentationSnapshot.surface().outputExtent) {
    return;
  }
  auto layoutConfig = airfix::render::NativeRenderLayoutConfig{
      .outputExtent = *outputExtent,
      .renderScalePercent = presentationSettings.renderScalePercent,
      .scenePresentation = presentationSettings.scenePresentation,
      .verticalFovAdjustmentDegrees =
          presentationSettings.verticalFovAdjustmentDegrees,
  };
  if (gameplayCameraActive) {
    layoutConfig.referenceCameraCanvas = {
        gameplayCamera->logicalCanvasWidth(),
        gameplayCamera->logicalCanvasHeight(),
    };
    layoutConfig.referenceHorizontalFovDegrees =
        gameplayCamera->pose().projection().horizontalFovDegrees();
  }
  const auto builtLayout =
      airfix::render::buildNativeRenderLayout(layoutConfig);
  if (!builtLayout.complete()) {
    return;
  }
  const airfix::render::NativeRenderLayout &layout = *builtLayout.layout;
  const auto renderTargetExtent = layout.renderTargetExtent();
  const bool usesScaledSceneTarget =
      presentationSettings.renderScalePercent !=
      airfix::render::native_render_policy::defaultRenderScalePercent;
  if (renderTargetExtent != presentationSnapshot.targetExtent()) {
    return;
  }

  MTLRenderPassDescriptor *sceneRenderPass = renderPass;
  id<MTLTexture> retainedScaledSceneColor = nil;
  id<MTLTexture> retainedScaledSceneDepth = nil;
  if (usesScaledSceneTarget) {
    if (!presentationSnapshot.targetBundle().has_value()) {
      return;
    }
    AirfixMetalScaledSceneTargetBundle *targetBundle =
        metalPresentationTargetBundle(*presentationSnapshot.targetBundle());
    if (targetBundle == nil || targetBundle->_extent != renderTargetExtent) {
      return;
    }
    retainedScaledSceneColor = targetBundle->_colorTexture;
    retainedScaledSceneDepth = targetBundle->_depthTexture;

    sceneRenderPass = [MTLRenderPassDescriptor renderPassDescriptor];
    sceneRenderPass.colorAttachments[0].texture = retainedScaledSceneColor;
    sceneRenderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
    sceneRenderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    sceneRenderPass.colorAttachments[0].clearColor =
        renderPass.colorAttachments[0].clearColor;
    sceneRenderPass.depthAttachment.texture = retainedScaledSceneDepth;
    sceneRenderPass.depthAttachment.loadAction = MTLLoadActionClear;
    sceneRenderPass.depthAttachment.storeAction = MTLStoreActionDontCare;
    sceneRenderPass.depthAttachment.clearDepth =
        gameplayCameraActive ? airfix::render::legacyReverseDepthClearValue
                             : renderPass.depthAttachment.clearDepth;
  } else if (gameplayCameraActive) {
    sceneRenderPass.depthAttachment.clearDepth =
        airfix::render::legacyReverseDepthClearValue;
  }

  id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
  if (commandBuffer == nil) {
    return;
  }
  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:sceneRenderPass];
  if (encoder == nil) {
    return;
  }

  [encoder setRenderPipelineState:gameplayCameraActive
                                      ? self.gameplayPipelineState
                                      : self.pipelineState];
  [encoder setDepthStencilState:gameplayCameraActive ? self.gameplayDepthState
                                                     : self.depthState];
  const auto sceneViewport = layout.sceneViewportInRenderTarget();
  const MTLViewport metalSceneViewport{
      static_cast<double>(sceneViewport.x),
      static_cast<double>(sceneViewport.y),
      static_cast<double>(sceneViewport.width),
      static_cast<double>(sceneViewport.height),
      0.0,
      1.0,
  };
  [encoder setViewport:metalSceneViewport];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setFragmentSamplerState:sceneSamplerState atIndex:0U];

  // One lease covers the whole encoded frame. The SPSC producer may publish
  // between frames; failed acquisition safely falls back to the immutable
  // authored instance transforms without acquiring per draw command.
  std::optional<airfix::render::DynamicInstancePoseLease> poseLease;
  if (resources->_scenePoseRuntime != nullptr) {
    poseLease = resources->_scenePoseRuntime->tryAcquire();
  }

  const simd_float4x4 viewport =
      aspectCorrection(CGSizeMake(static_cast<CGFloat>(sceneViewport.width),
                                  static_cast<CGFloat>(sceneViewport.height)));
  std::uint64_t sceneTriangleCount = 0U;
  for (std::size_t commandIndex = 0U;
       commandIndex < submissionPlan.commands.size(); ++commandIndex) {
    const auto &command = submissionPlan.commands[commandIndex];
    sceneTriangleCount += command.indexCount / 3U;
    const auto &instance = payload.instances[command.instanceIndex];
    const auto resolvedPose =
        poseLease.has_value()
            ? poseLease->resolve(command.instanceIndex, instance.modelLinear,
                                 instance.modelTranslation)
            : airfix::render::ResolvedInstancePose{
                  .modelLinear = instance.modelLinear,
                  .modelTranslation = instance.modelTranslation,
              };
    const simd_float4x4 model =
        toSimdMatrix(resolvedPose.modelLinear, resolvedPose.modelTranslation);
    AirfixMetalMeshBuffers *buffers = resources.meshBuffers[command.meshSlot];
    [encoder setVertexBuffer:buffers.vertexBuffer offset:0U atIndex:0U];
    if (gameplayCamera != nullptr) {
      const auto uniforms = gameplayUniforms(model, *gameplayCamera, layout);
      [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1U];
    } else {
      const GpuUniforms uniforms{simd_mul(viewport, model)};
      [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1U];
    }

    id<MTLTexture> texture = fallbackResource->_texture;
    if (command.texcoordMode == airfix::render::TexcoordMode::uv0 &&
        command.primary.has_value()) {
      const auto assetIndex = static_cast<NSUInteger>(command.primary->value);
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
                 indexBufferOffset:resources->_indexOffsets[commandIndex]];
  }

  [encoder endEncoding];
  poseLease.reset();
  if (usesScaledSceneTarget) {
    // The drawable remains at native output resolution. Only this
    // presentation pass samples the independently sized 3D scene target.
    renderPass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    renderPass.depthAttachment.texture = nil;
    id<MTLRenderCommandEncoder> presentationEncoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
    if (presentationEncoder == nil) {
      return;
    }
    [presentationEncoder setRenderPipelineState:self.presentationPipelineState];
    [presentationEncoder setCullMode:MTLCullModeNone];
    [presentationEncoder
        setViewport:MTLViewport{
                        0.0,
                        0.0,
                        static_cast<double>(outputExtent->width),
                        static_cast<double>(outputExtent->height),
                        0.0,
                        1.0,
                    }];
    [presentationEncoder setFragmentTexture:retainedScaledSceneColor
                                    atIndex:0U];
    [presentationEncoder setFragmentSamplerState:self.presentationSamplerState
                                         atIndex:0U];
    [presentationEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:0U
                            vertexCount:3U];
    [presentationEncoder endEncoding];
  }

  std::optional<double> latestGpuMilliseconds;
  if (diagnosticsState->_hasGpuFrameMilliseconds.load(
          std::memory_order_acquire)) {
    latestGpuMilliseconds = diagnosticsState->_latestGpuFrameMilliseconds.load(
        std::memory_order_relaxed);
  }
  std::uint64_t trackedGpuBytes =
      static_cast<std::uint64_t>(gpuBudgetHolder->_ledger->reservedBytes());
  const auto accountResource = [&trackedGpuBytes](
                                   id<MTLResource> resource) noexcept {
    if (resource == nil) {
      return;
    }
    const auto bytes = static_cast<std::uint64_t>(resource.allocatedSize);
    if (bytes > std::numeric_limits<std::uint64_t>::max() - trackedGpuBytes) {
      trackedGpuBytes = std::numeric_limits<std::uint64_t>::max();
      return;
    }
    trackedGpuBytes += bytes;
  };
  accountResource(drawable.texture);
  accountResource(drawableDepthTexture);
  accountResource(self.diagnosticsOverlayTexture);

  const auto cpuSampled = std::chrono::steady_clock::now();
  const double cpuFrameMilliseconds =
      std::chrono::duration<double, std::milli>(cpuSampled - frameStarted)
          .count();
  const auto sceneDrawCallCount =
      static_cast<std::uint64_t>(submissionPlan.commands.size());
  const std::uint64_t auxiliaryDrawCallCount = usesScaledSceneTarget ? 1U : 0U;
  const bool diagnosticAccepted = diagnosticsState->_accumulator.record({
      .outputExtent = *outputExtent,
      .renderTargetExtent = renderTargetExtent,
      .renderScalePercent = presentationSettings.renderScalePercent,
      .visualProfile = presentationSettings.visualProfile,
      .sceneTextureSampling = *sceneTextureSampling,
      .frameIntervalMilliseconds = frameIntervalMilliseconds,
      .cpuFrameMilliseconds = cpuFrameMilliseconds,
      .gpuFrameMilliseconds = latestGpuMilliseconds,
      .drawCallCount = sceneDrawCallCount + auxiliaryDrawCallCount,
      .sceneDrawCallCount = sceneDrawCallCount,
      .triangleCount = sceneTriangleCount + auxiliaryDrawCallCount,
      .sceneTriangleCount = sceneTriangleCount,
      .activeLightCount = 0U,
      .gpuMemoryBytes = trackedGpuBytes,
      .gpuMemoryMeasurement =
          airfix::render::GpuMemoryMeasurement::backendReported,
  });
  id<MTLTexture> retainedDiagnosticsOverlay = nil;
  if (diagnosticAccepted && presentationSettings.diagnosticsOverlayEnabled) {
    constexpr auto refreshInterval = std::chrono::milliseconds(250);
    if (!diagnosticsState->_lastOverlayRefresh.has_value() ||
        frameStarted - *diagnosticsState->_lastOverlayRefresh >=
            refreshInterval ||
        self.diagnosticsOverlayTexture == nil) {
      if ([self updateDiagnosticsOverlayTextureWithDevice:self.commandQueue
                                                              .device]) {
        diagnosticsState->_lastOverlayRefresh = frameStarted;
      }
    }
    retainedDiagnosticsOverlay = self.diagnosticsOverlayTexture;
  }
  if (retainedDiagnosticsOverlay != nil && self.diagnosticsOverlayWidth != 0U &&
      self.diagnosticsOverlayHeight != 0U) {
    renderPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    renderPass.depthAttachment.texture = nil;
    id<MTLRenderCommandEncoder> overlayEncoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
    if (overlayEncoder != nil) {
      const float margin =
          static_cast<float>(self.diagnosticsOverlayPixelScale * 8U);
      float originX = margin;
      float originY = margin;
      const CGSize logicalBounds = view.bounds.size;
      const UIEdgeInsets safeArea = view.safeAreaInsets;
      if (std::isfinite(logicalBounds.width) &&
          std::isfinite(logicalBounds.height) && logicalBounds.width > 0.0 &&
          logicalBounds.height > 0.0 && std::isfinite(safeArea.left) &&
          std::isfinite(safeArea.top) && safeArea.left >= 0.0 &&
          safeArea.top >= 0.0) {
        originX += static_cast<float>(
            safeArea.left * static_cast<CGFloat>(outputExtent->width) /
            logicalBounds.width);
        originY += static_cast<float>(
            safeArea.top * static_cast<CGFloat>(outputExtent->height) /
            logicalBounds.height);
      }
      const GpuOverlayUniforms uniforms{
          .outputAndPanelSize =
              {
                  static_cast<float>(outputExtent->width),
                  static_cast<float>(outputExtent->height),
                  static_cast<float>(self.diagnosticsOverlayWidth),
                  static_cast<float>(self.diagnosticsOverlayHeight),
              },
          .panelOrigin = {originX, originY, 0.0F, 0.0F},
          .tint = {1.0F, 1.0F, 1.0F, 1.0F},
          .uvRect = {0.0F, 0.0F, 1.0F, 1.0F},
      };
      [overlayEncoder setRenderPipelineState:self.overlayPipelineState];
      [overlayEncoder setCullMode:MTLCullModeNone];
      [overlayEncoder setViewport:MTLViewport{
                                      0.0,
                                      0.0,
                                      static_cast<double>(outputExtent->width),
                                      static_cast<double>(outputExtent->height),
                                      0.0,
                                      1.0,
                                  }];
      [overlayEncoder setVertexBytes:&uniforms
                              length:sizeof(uniforms)
                             atIndex:2U];
      [overlayEncoder setFragmentBytes:&uniforms
                                length:sizeof(uniforms)
                               atIndex:2U];
      [overlayEncoder setFragmentTexture:retainedDiagnosticsOverlay atIndex:0U];
      [overlayEncoder setFragmentSamplerState:self.overlaySamplerState
                                      atIndex:0U];
      [overlayEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                         vertexStart:0U
                         vertexCount:6U];
      [overlayEncoder endEncoding];
    }
  }
  AirfixMetalDiagnosticsState *retainedDiagnosticsState = diagnosticsState;
#if AIRFIX_IOS_SIMULATOR_SMOKE
  void (^simulatorSmokeCompletion)(BOOL, NSUInteger, NSUInteger,
                                   NSError *_Nullable) =
      self.simulatorSmokeFrameObserver;
  self.simulatorSmokeFrameObserver = nil;
  const BOOL simulatorSmokePublicSyntheticScene = !gameplayCameraActive;
  const NSUInteger simulatorSmokeDrawCallCount =
      static_cast<NSUInteger>(sceneDrawCallCount);
  const NSUInteger simulatorSmokeTriangleCount =
      static_cast<NSUInteger>(sceneTriangleCount);
#endif
  [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    const double gpuStart = completed.GPUStartTime;
    const double gpuEnd = completed.GPUEndTime;
    if (std::isfinite(gpuStart) && std::isfinite(gpuEnd) && gpuStart >= 0.0 &&
        gpuEnd > gpuStart) {
      retainedDiagnosticsState->_latestGpuFrameMilliseconds.store(
          (gpuEnd - gpuStart) * 1000.0, std::memory_order_relaxed);
      retainedDiagnosticsState->_hasGpuFrameMilliseconds.store(
          true, std::memory_order_release);
    }
    // Retain the immutable resource owner explicitly until this GPU
    // submission no longer references its buffers or textures.
    (void)snapshot;
    (void)presentationSnapshot;
    (void)fallbackResource;
    (void)retainedScaledSceneColor;
    (void)retainedScaledSceneDepth;
    (void)retainedDiagnosticsOverlay;
    (void)drawableDepthTexture;
#if AIRFIX_IOS_SIMULATOR_SMOKE
    if (simulatorSmokeCompletion != nil) {
      NSError *completionError = nil;
      if (completed.status != MTLCommandBufferStatusCompleted) {
        completionError = completed.error;
        if (completionError == nil) {
          completionError = makeError(
              RendererError::unexpectedFailure,
              @"The simulator smoke Metal command buffer did not complete.");
        }
      }
      dispatch_async(dispatch_get_main_queue(), ^{
        simulatorSmokeCompletion(simulatorSmokePublicSyntheticScene,
                                 simulatorSmokeDrawCallCount,
                                 simulatorSmokeTriangleCount, completionError);
      });
    }
#endif
  }];
  [commandBuffer presentDrawable:drawable];
  [commandBuffer commit];
}

@end
