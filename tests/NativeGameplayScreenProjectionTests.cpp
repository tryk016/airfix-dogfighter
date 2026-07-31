#include "airfix/render/NativeGameplayScreenProjection.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0U};

void recordAllocation() noexcept {
  if (trackAllocations.load(std::memory_order_relaxed)) {
    allocationCount.fetch_add(1U, std::memory_order_relaxed);
  }
}

} // namespace

void *operator new(const std::size_t size) {
  recordAllocation();
  if (void *memory = std::malloc(size == 0U ? 1U : size); memory != nullptr) {
    return memory;
  }
  throw std::bad_alloc();
}

void *operator new[](const std::size_t size) { return ::operator new(size); }

void operator delete(void *memory) noexcept { std::free(memory); }

void operator delete[](void *memory) noexcept { ::operator delete(memory); }

void operator delete(void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}

void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}

namespace {

using namespace airfix::render;

static_assert(noexcept(projectGameplayWorldPointToOutput(
    std::declval<const LegacyGameplayCameraClipPacket &>(),
    std::declval<const NativeRenderLayout &>(), Vec3{})));

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 1.0e-3F) noexcept {
  return std::fabs(actual - expected) <= tolerance;
}

[[nodiscard]] LegacyGameplayCameraClipPacket cameraPacket() {
  const auto built = buildLegacyGameplayCameraBootstrapClipPacket({
      .vehicleWorldPosition = {0.0F, 0.0F, 0.0F},
      .vehicleWorldRotation =
          {
              .columns =
                  {
                      Vec3{1.0F, 0.0F, 0.0F},
                      Vec3{0.0F, 1.0F, 0.0F},
                      Vec3{0.0F, 0.0F, 1.0F},
                  },
          },
      .worldRoomIndex = 0U,
  });
  require(built.complete(), "camera bootstrap failed");
  return *built.packet;
}

[[nodiscard]] NativeRenderLayout
layoutFor(const LegacyGameplayCameraClipPacket &camera,
          const OutputPixelExtent output,
          const float renderScalePercent = 100.0F,
          const ScenePresentationMode presentation =
              ScenePresentationMode::widescreenHorPlus,
          const float safeFovDegrees = 0.0F,
          const CameraLogicalExtent referenceCanvas = {}) {
  const CameraLogicalExtent resolvedReference =
      referenceCanvas.width > 0.0F && referenceCanvas.height > 0.0F
          ? referenceCanvas
          : CameraLogicalExtent{
                camera.logicalCanvasWidth(),
                camera.logicalCanvasHeight(),
            };
  const auto built = buildNativeRenderLayout({
      .outputExtent = output,
      .renderScalePercent = renderScalePercent,
      .scenePresentation = presentation,
      .verticalFovAdjustmentDegrees = safeFovDegrees,
      .referenceCameraCanvas = resolvedReference,
      .referenceHorizontalFovDegrees =
          camera.pose().projection().horizontalFovDegrees(),
  });
  require(built.complete(), "native layout build failed");
  return *built.layout;
}

[[nodiscard]] Vec3 worldFromCamera(const LegacyGameplayCameraClipPacket &camera,
                                   const Vec3 cameraPoint) noexcept {
  const auto &transform = camera.pose().worldToView();
  const auto linear = transform.linear();
  const auto origin = transform.translation();
  return {
      origin.x + linear.columns[0].x * cameraPoint.x +
          linear.columns[1].x * cameraPoint.y +
          linear.columns[2].x * cameraPoint.z,
      origin.y + linear.columns[0].y * cameraPoint.x +
          linear.columns[1].y * cameraPoint.y +
          linear.columns[2].y * cameraPoint.z,
      origin.z + linear.columns[0].z * cameraPoint.x +
          linear.columns[1].z * cameraPoint.y +
          linear.columns[2].z * cameraPoint.z,
  };
}

void testRequiredAspectsAndRenderScalesKeepTheReticleCentred() {
  const auto camera = cameraPacket();
  constexpr std::array outputs{
      OutputPixelExtent{1600U, 1200U}, OutputPixelExtent{1920U, 1200U},
      OutputPixelExtent{1920U, 1080U}, OutputPixelExtent{2535U, 1170U},
      OutputPixelExtent{2520U, 1080U}, OutputPixelExtent{3840U, 1080U},
  };
  constexpr std::array scales{50.0F, 100.0F, 125.0F, 200.0F};

  for (const auto output : outputs) {
    for (const float scale : scales) {
      const auto layout = layoutFor(camera, output, scale);
      const auto projected = projectGameplayWorldPointToOutput(
          camera, layout, camera.pose().vehicleWorldAnchor());
      require(projected.complete() && projected.point->visible() &&
                  projected.point->output.insideSceneViewport &&
                  close(projected.point->output.point.x,
                        static_cast<float>(output.width) * 0.5F) &&
                  close(projected.point->output.point.y,
                        static_cast<float>(output.height) * 0.5F) &&
                  close(projected.point->cameraLogicalPoint.x,
                        layout.cameraLogicalCentre().x) &&
                  close(projected.point->cameraLogicalPoint.y,
                        layout.cameraLogicalCentre().y),
              "aspect ratio or render scale moved the camera reticle");
    }
  }
}

void testOriginalFourByThreeAndSafeFovRemainExplicit() {
  const auto camera = cameraPacket();
  const auto original = layoutFor(camera, {1920U, 1080U}, 125.0F,
                                  ScenePresentationMode::originalFourByThree);
  require(original.sceneViewportInOutput() ==
              OutputPixelRect{240.0F, 0.0F, 1440.0F, 1080.0F},
          "Original 4:3 output viewport changed");
  const auto originalCentre = projectGameplayWorldPointToOutput(
      camera, original, camera.pose().vehicleWorldAnchor());
  require(originalCentre.complete() && originalCentre.point->visible() &&
              close(originalCentre.point->output.point.x, 960.0F) &&
              close(originalCentre.point->output.point.y, 540.0F),
          "Original 4:3 moved the camera reticle into a bar");

  const auto safeFov =
      layoutFor(camera, {2535U, 1170U}, 50.0F,
                ScenePresentationMode::widescreenHorPlus, 25.0F);
  const auto safeFovCentre = projectGameplayWorldPointToOutput(
      camera, safeFov, camera.pose().vehicleWorldAnchor());
  require(safeFovCentre.complete() && safeFovCentre.point->visible() &&
              close(safeFovCentre.point->output.point.x, 1267.5F) &&
              close(safeFovCentre.point->output.point.y, 585.0F) &&
              close(safeFovCentre.point->cameraLogicalPoint.x,
                    safeFov.cameraLogicalCentre().x) &&
              close(safeFovCentre.point->cameraLogicalPoint.y,
                    safeFov.cameraLogicalCentre().y),
          "safe FOV did not expand around the unchanged reticle centre");
}

void testOffscreenAndDepthStatesRemainAvailable() {
  const auto camera = cameraPacket();
  const auto layout = layoutFor(camera, {1920U, 1080U});

  const auto offscreen = projectGameplayWorldPointToOutput(
      camera, layout, worldFromCamera(camera, {20.0F, 0.0F, 10.0F}));
  require(offscreen.complete() &&
              !offscreen.point->output.insideSceneViewport &&
              offscreen.point->withinRecoveredDepthRange &&
              !offscreen.point->visible() &&
              offscreen.point->output.point.x >
                  layout.sceneViewportInOutput().x +
                      layout.sceneViewportInOutput().width,
          "off-screen world point was clamped or hidden from the caller");

  const auto beyondFar = projectGameplayWorldPointToOutput(
      camera, layout, worldFromCamera(camera, {0.0F, 0.0F, 250.0F}));
  require(beyondFar.complete() && beyondFar.point->output.insideSceneViewport &&
              !beyondFar.point->withinRecoveredDepthRange &&
              !beyondFar.point->visible(),
          "far-depth visibility was not labelled independently");

  const auto beforeNear = projectGameplayWorldPointToOutput(
      camera, layout, worldFromCamera(camera, {0.0F, 0.0F, 0.1F}));
  require(beforeNear.complete() &&
              beforeNear.point->legacy.projected.usedNearFallback &&
              !beforeNear.point->withinRecoveredDepthRange &&
              !beforeNear.point->visible(),
          "legacy near fallback was mistaken for visible depth");
}

void testMismatchesAndFailuresFailClosed() {
  const auto camera = cameraPacket();
  const auto mismatchedCanvas = layoutFor(
      camera, {1920U, 1080U}, 100.0F, ScenePresentationMode::widescreenHorPlus,
      0.0F, {800.0F, 480.0F});
  const auto canvasResult = projectGameplayWorldPointToOutput(
      camera, mismatchedCanvas, camera.pose().vehicleWorldAnchor());
  require(!canvasResult.complete() && canvasResult.issue.has_value() &&
              canvasResult.issue->kind ==
                  NativeGameplayScreenProjectionIssueKind::cameraLayoutMismatch,
          "camera/layout canvas mismatch was accepted");

  const auto fovBuild = buildNativeRenderLayout({
      .outputExtent = {1920U, 1080U},
      .referenceCameraCanvas =
          {
              camera.logicalCanvasWidth(),
              camera.logicalCanvasHeight(),
          },
      .referenceHorizontalFovDegrees = 91.0F,
  });
  require(fovBuild.complete(), "mismatch FOV layout build failed");
  const auto fovResult = projectGameplayWorldPointToOutput(
      camera, *fovBuild.layout, camera.pose().vehicleWorldAnchor());
  require(!fovResult.complete() && fovResult.issue.has_value() &&
              fovResult.issue->kind ==
                  NativeGameplayScreenProjectionIssueKind::cameraLayoutMismatch,
          "camera/layout FOV mismatch was accepted");

  const auto layout = layoutFor(camera, {1920U, 1080U});
  const auto invalidWorld = projectGameplayWorldPointToOutput(
      camera, layout, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
  require(
      !invalidWorld.complete() && invalidWorld.issue.has_value() &&
          invalidWorld.issue->kind ==
              NativeGameplayScreenProjectionIssueKind::legacyProjectionFailed &&
          invalidWorld.issue->legacyProjectionIssue.has_value(),
      "invalid world point lost the legacy projection failure");

  constexpr float legacyScreenXBeforeNativeOverflow =
      std::numeric_limits<float>::max() * 0.5F;
  const auto overflow = projectGameplayWorldPointToOutput(
      camera, layout,
      worldFromCamera(
          camera, {legacyScreenXBeforeNativeOverflow / 320.0F, 0.0F, 1.0F}));
  require(!overflow.complete() && overflow.issue.has_value() &&
              overflow.issue->kind ==
                  NativeGameplayScreenProjectionIssueKind::nonFiniteOutput,
          "finite legacy projection overflowed native output silently");
}

void testProjectionDoesNotAllocate() {
  const auto camera = cameraPacket();
  const auto layout = layoutFor(camera, {3840U, 1080U}, 200.0F);
  const auto world = camera.pose().vehicleWorldAnchor();

  allocationCount.store(0U, std::memory_order_relaxed);
  trackAllocations.store(true, std::memory_order_release);
  bool complete = true;
  float checksum = 0.0F;
  for (std::size_t index = 0U; index < 4096U; ++index) {
    const auto projected =
        projectGameplayWorldPointToOutput(camera, layout, world);
    if (!projected.complete()) {
      complete = false;
      break;
    }
    checksum += projected.point->output.point.x;
  }
  trackAllocations.store(false, std::memory_order_release);

  require(complete && std::isfinite(checksum),
          "repeated native screen projection failed");
  require(allocationCount.load(std::memory_order_relaxed) == 0U,
          "native screen projection allocated");
}

} // namespace

int main() {
  try {
    testRequiredAspectsAndRenderScalesKeepTheReticleCentred();
    testOriginalFourByThreeAndSafeFovRemainExplicit();
    testOffscreenAndDepthStatesRemainAvailable();
    testMismatchesAndFailuresFailClosed();
    testProjectionDoesNotAllocate();
    std::cout << "Native gameplay screen projection tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    trackAllocations.store(false, std::memory_order_release);
    std::cerr << "Native gameplay screen projection tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
