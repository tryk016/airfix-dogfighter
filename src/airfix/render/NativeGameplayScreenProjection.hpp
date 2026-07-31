#pragma once

#include "airfix/render/LegacyGameplayCameraClipPacket.hpp"
#include "airfix/render/NativeRenderLayout.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

enum class NativeGameplayScreenProjectionIssueKind : std::uint8_t {
  cameraLayoutMismatch,
  legacyProjectionFailed,
  nonFiniteOutput,
};

struct NativeGameplayScreenProjectionIssue final {
  NativeGameplayScreenProjectionIssueKind kind{
      NativeGameplayScreenProjectionIssueKind::cameraLayoutMismatch};
  std::optional<LegacyGameplayCameraWorldProjectionIssue> legacyProjectionIssue;
};

struct NativeGameplayScreenProjectedWorldPoint final {
  LegacyGameplayCameraProjectedWorldPoint legacy{};
  CameraLogicalPoint cameraLogicalPoint{};
  CameraOutputPoint output{};
  bool withinRecoveredDepthRange{};

  [[nodiscard]] constexpr bool visible() const noexcept {
    return output.insideSceneViewport && withinRecoveredDepthRange;
  }
};

struct NativeGameplayScreenProjectionResult final {
  std::optional<NativeGameplayScreenProjectedWorldPoint> point;
  std::optional<NativeGameplayScreenProjectionIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return point.has_value() && !issue.has_value();
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return complete();
  }
};

// Composes the recovered world-to-reference-screen operation with the modern
// native presentation layout. The reference point is first translated into
// the expanded Hor+/safe-FOV camera canvas and is then mapped through the
// actual scene viewport in output pixels. Off-screen and out-of-depth points
// remain explicit results so later HUD code can choose between hiding and
// directional indicators without changing camera or simulation state.
[[nodiscard]] NativeGameplayScreenProjectionResult
projectGameplayWorldPointToOutput(const LegacyGameplayCameraClipPacket &camera,
                                  const NativeRenderLayout &layout,
                                  const Vec3 &worldPosition) noexcept;

} // namespace airfix::render
