#pragma once

#include "airfix/render/LegacyCanvasLayout.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

struct OutputPixelExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] friend constexpr bool operator==(
        const OutputPixelExtent&,
        const OutputPixelExtent&) noexcept = default;
};

struct RenderTargetPixelExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] friend constexpr bool operator==(
        const RenderTargetPixelExtent&,
        const RenderTargetPixelExtent&) noexcept = default;
};

struct RenderTargetPixelRect final {
    float x{};
    float y{};
    float width{};
    float height{};

    [[nodiscard]] friend constexpr bool operator==(
        const RenderTargetPixelRect&,
        const RenderTargetPixelRect&) noexcept = default;
};

struct OutputPixelRect final {
    float x{};
    float y{};
    float width{};
    float height{};

    [[nodiscard]] friend constexpr bool operator==(
        const OutputPixelRect&,
        const OutputPixelRect&) noexcept = default;
};

struct OutputPixelPoint final {
    float x{};
    float y{};

    [[nodiscard]] friend constexpr bool operator==(
        const OutputPixelPoint&,
        const OutputPixelPoint&) noexcept = default;
};

struct CameraLogicalExtent final {
    float width{};
    float height{};

    [[nodiscard]] friend constexpr bool operator==(
        const CameraLogicalExtent&,
        const CameraLogicalExtent&) noexcept = default;
};

struct CameraLogicalPoint final {
    float x{};
    float y{};

    [[nodiscard]] friend constexpr bool operator==(
        const CameraLogicalPoint&,
        const CameraLogicalPoint&) noexcept = default;
};

struct CameraOutputPoint final {
    OutputPixelPoint point{};
    bool insideSceneViewport{};

    [[nodiscard]] friend constexpr bool operator==(
        const CameraOutputPoint&,
        const CameraOutputPoint&) noexcept = default;
};

struct UiLogicalExtent final {
    float width{};
    float height{};

    [[nodiscard]] friend constexpr bool operator==(
        const UiLogicalExtent&,
        const UiLogicalExtent&) noexcept = default;
};

struct UiLogicalPoint final {
    float x{};
    float y{};

    [[nodiscard]] friend constexpr bool operator==(
        const UiLogicalPoint&,
        const UiLogicalPoint&) noexcept = default;
};

struct SafeAreaInsets final {
    float left{};
    float top{};
    float right{};
    float bottom{};

    [[nodiscard]] friend constexpr bool operator==(
        const SafeAreaInsets&,
        const SafeAreaInsets&) noexcept = default;
};

enum class ScenePresentationMode : std::uint8_t {
    widescreenHorPlus,
    originalFourByThree,
};

namespace native_render_policy {

inline constexpr float minimumRenderScalePercent = 50.0F;
inline constexpr float maximumRenderScalePercent = 200.0F;
inline constexpr float defaultRenderScalePercent = 100.0F;
inline constexpr float minimumVerticalFovAdjustmentDegrees = 0.0F;
inline constexpr float maximumVerticalFovAdjustmentDegrees = 25.0F;
inline constexpr float defaultVerticalFovAdjustmentDegrees = 0.0F;
inline constexpr float minimumUiScalePercent = 75.0F;
inline constexpr float maximumUiScalePercent = 150.0F;
inline constexpr float defaultUiScalePercent = 100.0F;

} // namespace native_render_policy

struct NativeRenderLayoutConfig final {
    OutputPixelExtent outputExtent{};
    float renderScalePercent{
        native_render_policy::defaultRenderScalePercent};
    ScenePresentationMode scenePresentation{
        ScenePresentationMode::widescreenHorPlus};
    float verticalFovAdjustmentDegrees{
        native_render_policy::defaultVerticalFovAdjustmentDegrees};
    CameraLogicalExtent referenceCameraCanvas{
        recovered_legacy_canvas::width,
        recovered_legacy_canvas::height,
    };
    float referenceHorizontalFovDegrees{90.0F};
    UiLogicalExtent uiDesignExtent{
        recovered_legacy_canvas::width,
        recovered_legacy_canvas::height,
    };
    SafeAreaInsets outputSafeArea{};
};

enum class NativeRenderLayoutBuildIssueKind : std::uint8_t {
    outputExtentNotPositive,
    nonFiniteConfig,
    renderScaleOutOfRange,
    logicalExtentNotPositive,
    referenceFovOutOfRange,
    verticalFovAdjustmentOutOfRange,
    invalidSafeArea,
    renderTargetExtentOverflow,
    nonFiniteDerivedLayout,
};

struct NativeRenderLayoutBuildIssue final {
    NativeRenderLayoutBuildIssueKind kind{
        NativeRenderLayoutBuildIssueKind::nonFiniteConfig};
};

class NativeRenderLayout;

struct NativeRenderLayoutBuildResult;

// Establishes four deliberately separate coordinate domains:
// - output/back-buffer pixels,
// - the independently scaled 3D render target,
// - the camera's logical projection canvas,
// - the UI design space mapped into the output safe area.
//
// A 100% render scale is an exact identity: the render target has the same
// dimensions as the output. The 640x480 reference remains camera/UI metadata
// only and never becomes an implicit 3D raster resolution. The optional safe
// vertical-FOV increase expands the logical camera canvas around the unchanged
// reference centre; it never changes output, render-target, or UI extents.
[[nodiscard]] NativeRenderLayoutBuildResult buildNativeRenderLayout(
    const NativeRenderLayoutConfig& config) noexcept;

class NativeRenderLayout final {
public:
    [[nodiscard]] constexpr OutputPixelExtent outputExtent() const noexcept {
        return outputExtent_;
    }

    [[nodiscard]] constexpr RenderTargetPixelExtent
    renderTargetExtent() const noexcept {
        return renderTargetExtent_;
    }

    [[nodiscard]] constexpr float renderScalePercent() const noexcept {
        return renderScalePercent_;
    }

    [[nodiscard]] constexpr ScenePresentationMode
    scenePresentation() const noexcept {
        return scenePresentation_;
    }

    [[nodiscard]] constexpr float
    verticalFovAdjustmentDegrees() const noexcept {
        return verticalFovAdjustmentDegrees_;
    }

    [[nodiscard]] constexpr RenderTargetPixelRect
    sceneViewportInRenderTarget() const noexcept {
        return sceneViewportInRenderTarget_;
    }

    [[nodiscard]] constexpr OutputPixelRect
    sceneViewportInOutput() const noexcept {
        return sceneViewportInOutput_;
    }

    [[nodiscard]] constexpr CameraLogicalExtent
    cameraLogicalExtent() const noexcept {
        return cameraLogicalExtent_;
    }

    [[nodiscard]] constexpr CameraLogicalExtent
    referenceCameraLogicalExtent() const noexcept {
        return referenceCameraLogicalExtent_;
    }

    [[nodiscard]] constexpr float
    referenceHorizontalFovDegrees() const noexcept {
        return referenceHorizontalFovDegrees_;
    }

    [[nodiscard]] constexpr CameraLogicalPoint
    cameraLogicalCentre() const noexcept {
        return cameraLogicalCentre_;
    }

    [[nodiscard]] constexpr CameraLogicalPoint mapReferenceCameraPoint(
        const CameraLogicalPoint point) const noexcept {
        return {
            .x = point.x +
                (cameraLogicalExtent_.width -
                 referenceCameraLogicalExtent_.width) *
                    0.5F,
            .y = point.y +
                (cameraLogicalExtent_.height -
                 referenceCameraLogicalExtent_.height) *
                    0.5F,
        };
    }

    [[nodiscard]] constexpr float verticalFovDegrees() const noexcept {
        return verticalFovDegrees_;
    }

    [[nodiscard]] constexpr float horizontalFovDegrees() const noexcept {
        return horizontalFovDegrees_;
    }

    [[nodiscard]] std::optional<CameraLogicalPoint>
    cameraPointFromOutput(
        OutputPixelPoint point) const noexcept;

    // Maps a finite camera-logical point through the actual scene viewport in
    // output pixels. Off-screen points remain available for directional HUD
    // indicators and are labelled instead of being silently clamped.
    [[nodiscard]] std::optional<CameraOutputPoint>
    outputPointFromCamera(
        CameraLogicalPoint point) const noexcept;

    [[nodiscard]] constexpr OutputPixelRect
    uiSafeRectInOutput() const noexcept {
        return uiSafeRectInOutput_;
    }

    [[nodiscard]] constexpr OutputPixelRect
    uiViewportInOutput() const noexcept {
        return uiViewportInOutput_;
    }

    [[nodiscard]] constexpr UiLogicalExtent uiDesignExtent() const noexcept {
        return uiDesignExtent_;
    }

    [[nodiscard]] constexpr float uiScale() const noexcept {
        return uiScale_;
    }

    [[nodiscard]] std::optional<UiLogicalPoint> uiPointFromOutput(
        OutputPixelPoint point) const noexcept;

    [[nodiscard]] std::optional<OutputPixelPoint> outputPointFromUi(
        UiLogicalPoint point) const noexcept;

private:
    friend NativeRenderLayoutBuildResult buildNativeRenderLayout(
        const NativeRenderLayoutConfig& config) noexcept;

    constexpr NativeRenderLayout(
        const OutputPixelExtent outputExtent,
        const RenderTargetPixelExtent renderTargetExtent,
        const float renderScalePercent,
        const ScenePresentationMode scenePresentation,
        const float verticalFovAdjustmentDegrees,
        const RenderTargetPixelRect sceneViewportInRenderTarget,
        const OutputPixelRect sceneViewportInOutput,
        const CameraLogicalExtent referenceCameraLogicalExtent,
        const float referenceHorizontalFovDegrees,
        const CameraLogicalExtent cameraLogicalExtent,
        const CameraLogicalPoint cameraLogicalCentre,
        const float verticalFovDegrees,
        const float horizontalFovDegrees,
        const OutputPixelRect uiSafeRectInOutput,
        const OutputPixelRect uiViewportInOutput,
        const UiLogicalExtent uiDesignExtent,
        const float uiScale) noexcept
        : outputExtent_(outputExtent),
          renderTargetExtent_(renderTargetExtent),
          renderScalePercent_(renderScalePercent),
          scenePresentation_(scenePresentation),
          verticalFovAdjustmentDegrees_(
              verticalFovAdjustmentDegrees),
          sceneViewportInRenderTarget_(sceneViewportInRenderTarget),
          sceneViewportInOutput_(sceneViewportInOutput),
          referenceCameraLogicalExtent_(referenceCameraLogicalExtent),
          referenceHorizontalFovDegrees_(
              referenceHorizontalFovDegrees),
          cameraLogicalExtent_(cameraLogicalExtent),
          cameraLogicalCentre_(cameraLogicalCentre),
          verticalFovDegrees_(verticalFovDegrees),
          horizontalFovDegrees_(horizontalFovDegrees),
          uiSafeRectInOutput_(uiSafeRectInOutput),
          uiViewportInOutput_(uiViewportInOutput),
          uiDesignExtent_(uiDesignExtent),
          uiScale_(uiScale) {}

    const OutputPixelExtent outputExtent_;
    const RenderTargetPixelExtent renderTargetExtent_;
    const float renderScalePercent_;
    const ScenePresentationMode scenePresentation_;
    const float verticalFovAdjustmentDegrees_;
    const RenderTargetPixelRect sceneViewportInRenderTarget_;
    const OutputPixelRect sceneViewportInOutput_;
    const CameraLogicalExtent referenceCameraLogicalExtent_;
    const float referenceHorizontalFovDegrees_;
    const CameraLogicalExtent cameraLogicalExtent_;
    const CameraLogicalPoint cameraLogicalCentre_;
    const float verticalFovDegrees_;
    const float horizontalFovDegrees_;
    const OutputPixelRect uiSafeRectInOutput_;
    const OutputPixelRect uiViewportInOutput_;
    const UiLogicalExtent uiDesignExtent_;
    const float uiScale_;
};

struct NativeRenderLayoutBuildResult final {
    std::optional<NativeRenderLayout> layout;
    std::optional<NativeRenderLayoutBuildIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return layout.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

} // namespace airfix::render
