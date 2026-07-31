#include "airfix/render/LegacyWeaponCrosshairProjection.hpp"

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

static_assert(noexcept(projectLegacyWeaponCrosshairToOutput(
    std::declval<const LegacyGameplayCameraClipPacket &>(),
    std::declval<const NativeRenderLayout &>(),
    LegacyWeaponCrosshairSizeState{}, LegacyWeaponCrosshairProjectionInput{})));
static_assert(
    noexcept(activateLegacyWeaponCrosshair(LegacyWeaponCrosshairSizeState{})));

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

[[nodiscard]] LegacyWeaponCrosshairProjectionInput
inputFor(const LegacyGameplayCameraClipPacket &camera) noexcept {
  return {
      .targetWorldPosition = camera.pose().vehicleWorldAnchor(),
      .originalTextureWidth = 32U,
      .originalTextureHeight = 32U,
      .collisionFraction = 1.0F,
      .localAimOffsetZ = 1.0F,
      .uiScalePercent = 100.0F,
  };
}

void testRecoveredReferenceSizingAndCentering() {
  const auto camera = cameraPacket();
  const auto layout = layoutFor(camera, {640U, 480U});
  const auto result = projectLegacyWeaponCrosshairToOutput(camera, layout, {},
                                                           inputFor(camera));
  require(result.complete() && result.plan->recoveredVisibilitySatisfied(),
          "reference crosshair projection failed");
  require(close(result.plan->logicalDistanceScale, 1.0F) &&
              close(result.plan->outputRect.x, 304.0F) &&
              close(result.plan->outputRect.y, 224.0F) &&
              close(result.plan->outputRect.width, 32.0F) &&
              close(result.plan->outputRect.height, 32.0F),
          "recovered 32x32 centering changed");
  require(result.state ==
              LegacyWeaponCrosshairSizeState{
                  .currentLogicalWidth = 32.0F,
                  .currentLogicalHeight = 32.0F,
                  .targetLogicalWidth = 32.0F,
                  .targetLogicalHeight = 32.0F,
                  .resetCurrentSize = false,
              },
          "first successful projection did not adopt the target size");
}

void testActivationLatchPreservesCurrentSizeUntilReset() {
  const auto camera = cameraPacket();
  const auto layout = layoutFor(camera, {640U, 480U});
  auto nearInput = inputFor(camera);
  nearInput.collisionFraction = 0.0F;
  const auto first =
      projectLegacyWeaponCrosshairToOutput(camera, layout, {}, nearInput);
  require(first.complete() && close(first.plan->outputRect.width, 64.0F),
          "near collision did not produce the recovered 2x target size");

  const auto farInput = inputFor(camera);
  const auto retained = projectLegacyWeaponCrosshairToOutput(
      camera, layout, first.state, farInput);
  require(retained.complete() &&
              close(retained.state.currentLogicalWidth, 64.0F) &&
              close(retained.state.targetLogicalWidth, 32.0F) &&
              close(retained.plan->outputRect.width, 64.0F),
          "base crosshair current/target size separation changed");

  const auto activated = activateLegacyWeaponCrosshair(retained.state);
  require(activated.resetCurrentSize &&
              close(activated.currentLogicalWidth, 64.0F),
          "activation erased the retained dimensions");
  const auto reset =
      projectLegacyWeaponCrosshairToOutput(camera, layout, activated, farInput);
  require(reset.complete() && close(reset.state.currentLogicalWidth, 32.0F) &&
              close(reset.plan->outputRect.width, 32.0F) &&
              !reset.state.resetCurrentSize,
          "reactivation did not latch the next successful target size");
}

void testNativeOutputUsesUiScaleNotRenderScale() {
  const auto camera = cameraPacket();
  constexpr std::array outputs{
      OutputPixelExtent{1600U, 1200U}, OutputPixelExtent{1920U, 1200U},
      OutputPixelExtent{1920U, 1080U}, OutputPixelExtent{2535U, 1170U},
      OutputPixelExtent{2520U, 1080U}, OutputPixelExtent{3840U, 1080U},
  };
  constexpr std::array renderScales{50.0F, 100.0F, 200.0F};
  for (const auto output : outputs) {
    std::optional<OutputPixelRect> baseline;
    for (const float renderScale : renderScales) {
      const auto layout = layoutFor(camera, output, renderScale);
      const auto result = projectLegacyWeaponCrosshairToOutput(
          camera, layout, {}, inputFor(camera));
      const float expectedSize = 32.0F * layout.uiScale();
      require(result.complete() &&
                  close(result.plan->outputRect.width, expectedSize) &&
                  close(result.plan->outputRect.height, expectedSize) &&
                  close(result.plan->outputRect.x + expectedSize * 0.5F,
                        static_cast<float>(output.width) * 0.5F) &&
                  close(result.plan->outputRect.y + expectedSize * 0.5F,
                        static_cast<float>(output.height) * 0.5F),
              "native aspect ratio moved or mis-scaled the crosshair");
      if (!baseline.has_value()) {
        baseline = result.plan->outputRect;
      } else {
        require(result.plan->outputRect == *baseline,
                "3D render scale changed the output-pixel crosshair");
      }
    }
  }
}

void testUserUiScaleAndOriginalFourByThreeRemainIndependent() {
  const auto camera = cameraPacket();
  const auto layout = layoutFor(camera, {1920U, 1080U}, 125.0F,
                                ScenePresentationMode::originalFourByThree);
  require(layout.sceneViewportInOutput() ==
              OutputPixelRect{240.0F, 0.0F, 1440.0F, 1080.0F},
          "Original 4:3 viewport changed");

  auto input = inputFor(camera);
  input.uiScalePercent = 75.0F;
  const auto small =
      projectLegacyWeaponCrosshairToOutput(camera, layout, {}, input);
  input.uiScalePercent = 150.0F;
  const auto large =
      projectLegacyWeaponCrosshairToOutput(camera, layout, {}, input);
  require(small.complete() && large.complete() &&
              close(small.plan->outputRect.width, 54.0F) &&
              close(large.plan->outputRect.width, 108.0F) &&
              close(small.plan->outputRect.x + 27.0F, 960.0F) &&
              close(large.plan->outputRect.x + 54.0F, 960.0F),
          "user UI scale or Original 4:3 centering changed");
}

void testViewportAndDepthLabelsRemainCallerPolicy() {
  const auto camera = cameraPacket();
  const auto layout = layoutFor(camera, {1920U, 1080U});

  auto input = inputFor(camera);
  input.targetWorldPosition = worldFromCamera(camera, {20.0F, 0.0F, 10.0F});
  const auto offscreen =
      projectLegacyWeaponCrosshairToOutput(camera, layout, {}, input);
  require(offscreen.complete() && !offscreen.plan->insideSceneViewport &&
              offscreen.plan->withinRecoveredDepthRange &&
              !offscreen.plan->recoveredVisibilitySatisfied(),
          "off-screen result was discarded or clamped");

  input.targetWorldPosition = worldFromCamera(camera, {0.0F, 0.0F, 250.0F});
  const auto beyondFar =
      projectLegacyWeaponCrosshairToOutput(camera, layout, {}, input);
  require(beyondFar.complete() && beyondFar.plan->insideSceneViewport &&
              !beyondFar.plan->withinRecoveredDepthRange &&
              !beyondFar.plan->recoveredVisibilitySatisfied(),
          "out-of-depth result was hidden inside the planner");
}

void testInvalidInputAndProjectionFailureRetainState() {
  const auto camera = cameraPacket();
  const auto layout = layoutFor(camera, {1920U, 1080U});
  const LegacyWeaponCrosshairSizeState state{
      .currentLogicalWidth = 48.0F,
      .currentLogicalHeight = 48.0F,
      .targetLogicalWidth = 64.0F,
      .targetLogicalHeight = 64.0F,
      .resetCurrentSize = false,
  };

  const auto requireIssue =
      [&](LegacyWeaponCrosshairProjectionInput input,
          const LegacyWeaponCrosshairProjectionIssueKind expected) {
        const auto result =
            projectLegacyWeaponCrosshairToOutput(camera, layout, state, input);
        require(!result.complete() && !result.plan.has_value() &&
                    result.issue.has_value() &&
                    result.issue->kind == expected && result.state == state,
                "invalid crosshair input did not fail transactionally");
      };

  auto input = inputFor(camera);
  input.originalTextureWidth = 0U;
  requireIssue(
      input,
      LegacyWeaponCrosshairProjectionIssueKind::textureExtentNotPositive);
  input = inputFor(camera);
  input.collisionFraction = -0.01F;
  requireIssue(
      input,
      LegacyWeaponCrosshairProjectionIssueKind::collisionFractionOutOfRange);
  input = inputFor(camera);
  input.localAimOffsetZ = 3.0F;
  requireIssue(
      input, LegacyWeaponCrosshairProjectionIssueKind::nonPositiveLogicalSize);
  input = inputFor(camera);
  input.uiScalePercent = 150.01F;
  requireIssue(input,
               LegacyWeaponCrosshairProjectionIssueKind::uiScaleOutOfRange);
  input = inputFor(camera);
  input.localAimOffsetZ = std::numeric_limits<float>::quiet_NaN();
  requireIssue(input, LegacyWeaponCrosshairProjectionIssueKind::nonFiniteInput);

  const auto mismatched =
      layoutFor(camera, {1920U, 1080U}, 100.0F,
                ScenePresentationMode::widescreenHorPlus, {800.0F, 480.0F});
  const auto projectionFailure = projectLegacyWeaponCrosshairToOutput(
      camera, mismatched, state, inputFor(camera));
  require(!projectionFailure.complete() && projectionFailure.state == state &&
              projectionFailure.issue.has_value() &&
              projectionFailure.issue->kind ==
                  LegacyWeaponCrosshairProjectionIssueKind::
                      screenProjectionFailed &&
              projectionFailure.issue->screenProjectionIssue.has_value() &&
              projectionFailure.issue->screenProjectionIssue->kind ==
                  NativeGameplayScreenProjectionIssueKind::cameraLayoutMismatch,
          "nested screen projection failure was not preserved");
}

void testProjectionDoesNotAllocate() {
  const auto camera = cameraPacket();
  const auto layout = layoutFor(camera, {3840U, 2160U}, 200.0F);
  const auto input = inputFor(camera);

  allocationCount.store(0U, std::memory_order_relaxed);
  trackAllocations.store(true, std::memory_order_release);
  bool complete = true;
  LegacyWeaponCrosshairSizeState state{};
  float checksum = 0.0F;
  for (std::size_t index = 0U; index < 4096U; ++index) {
    const auto result =
        projectLegacyWeaponCrosshairToOutput(camera, layout, state, input);
    if (!result.complete()) {
      complete = false;
      break;
    }
    state = result.state;
    checksum += result.plan->outputRect.x;
  }
  trackAllocations.store(false, std::memory_order_release);

  require(complete && std::isfinite(checksum),
          "repeated crosshair projection failed");
  require(allocationCount.load(std::memory_order_relaxed) == 0U,
          "crosshair projection allocated");
}

} // namespace

int main() {
  try {
    testRecoveredReferenceSizingAndCentering();
    testActivationLatchPreservesCurrentSizeUntilReset();
    testNativeOutputUsesUiScaleNotRenderScale();
    testUserUiScaleAndOriginalFourByThreeRemainIndependent();
    testViewportAndDepthLabelsRemainCallerPolicy();
    testInvalidInputAndProjectionFailureRetainState();
    testProjectionDoesNotAllocate();
    std::cout << "Legacy weapon crosshair projection tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    trackAllocations.store(false, std::memory_order_release);
    std::cerr << "Legacy weapon crosshair projection tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
