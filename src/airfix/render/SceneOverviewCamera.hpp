#pragma once

#include "airfix/render/DrawModel.hpp"
#include "airfix/render/LegacyGameplayCameraClipPacket.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

// Explicit developer-capture policy. This camera is not a recovered gameplay
// behavior and must never be published to the gameplay camera runtime.
struct SceneOverviewCameraConfig final {
  std::uint32_t logicalCanvasWidth{1920U};
  std::uint32_t logicalCanvasHeight{1080U};
  float horizontalFovDegrees{80.0F};
  // Fraction reserved independently at every viewport edge.
  float viewportMarginFraction{0.04F};
  // World-space direction from the overview camera toward the scene centre.
  Vec3 forwardDirection{0.35F, -1.40F, 0.45F};
  float minimumDepthPadding{0.25F};
  float relativeDepthPadding{0.05F};
};

enum class SceneOverviewCameraIssueKind : std::uint8_t {
  invalidConfig,
  emptyModel,
  invalidMeshSlot,
  invalidLocalBounds,
  invalidInstanceTransform,
  nonFiniteWorldBounds,
  cameraTransformFailed,
  cameraPoseFailed,
  cameraClipPacketFailed,
  sceneDoesNotFit,
};

struct SceneOverviewCameraSnapshot final {
  Bounds3 worldBounds{};
  Vec3 cameraWorldPosition{};
  LegacyGameplayCameraClipPacket clipPacket;
  float viewportMarginFraction{};
};

struct SceneOverviewCameraBuildResult final {
  std::optional<SceneOverviewCameraSnapshot> snapshot;
  std::optional<SceneOverviewCameraIssueKind> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return snapshot.has_value() && !issue.has_value();
  }
};

// Fits the world AABB of every non-empty placed mesh into one centred
// diagnostic view. The model remains immutable and the returned packet is an
// owning one-frame value; no simulation or camera state is read or changed.
[[nodiscard]] SceneOverviewCameraBuildResult
buildSceneOverviewCamera(const DrawModelPayload &model,
                         const SceneOverviewCameraConfig &config = {}) noexcept;

} // namespace airfix::render
