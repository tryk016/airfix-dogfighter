#include "airfix/render/NativeRenderLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace airfix::render {
namespace {

constexpr double radiansPerDegree =
    std::numbers::pi_v<double> / 180.0;
constexpr double degreesPerRadian =
    180.0 / std::numbers::pi_v<double>;
constexpr float minimumReferenceFovDegrees = 1.0F;
constexpr float maximumReferenceFovDegrees = 175.0F;

[[nodiscard]] NativeRenderLayoutBuildResult failure(
    const NativeRenderLayoutBuildIssueKind kind) noexcept {
    return {
        .layout = std::nullopt,
        .issue = NativeRenderLayoutBuildIssue{.kind = kind},
    };
}

[[nodiscard]] bool finite(const CameraLogicalExtent extent) noexcept {
    return std::isfinite(extent.width) &&
        std::isfinite(extent.height);
}

[[nodiscard]] bool finite(const UiLogicalExtent extent) noexcept {
    return std::isfinite(extent.width) &&
        std::isfinite(extent.height);
}

[[nodiscard]] bool finite(const SafeAreaInsets insets) noexcept {
    return std::isfinite(insets.left) &&
        std::isfinite(insets.top) &&
        std::isfinite(insets.right) &&
        std::isfinite(insets.bottom);
}

template <typename Rect>
[[nodiscard]] bool finite(const Rect rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
        std::isfinite(rect.width) && std::isfinite(rect.height);
}

[[nodiscard]] std::optional<std::uint32_t> scaledDimension(
    const std::uint32_t dimension,
    const float scalePercent) noexcept {
    if (scalePercent ==
        native_render_policy::defaultRenderScalePercent) {
        return dimension;
    }
    const double scaled =
        static_cast<double>(dimension) *
        static_cast<double>(scalePercent) / 100.0;
    const double maximum =
        static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    if (!std::isfinite(scaled) || scaled < 0.5 || scaled > maximum) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(std::floor(scaled + 0.5));
}

[[nodiscard]] RenderTargetPixelRect aspectFit(
    const float targetWidth,
    const float targetHeight,
    const float aspectRatio) noexcept {
    const float targetAspect = targetWidth / targetHeight;
    if (targetAspect >= aspectRatio) {
        const float width = targetHeight * aspectRatio;
        return {
            .x = (targetWidth - width) * 0.5F,
            .y = 0.0F,
            .width = width,
            .height = targetHeight,
        };
    }

    const float height = targetWidth / aspectRatio;
    return {
        .x = 0.0F,
        .y = (targetHeight - height) * 0.5F,
        .width = targetWidth,
        .height = height,
    };
}

} // namespace

NativeRenderLayoutBuildResult buildNativeRenderLayout(
    const NativeRenderLayoutConfig& config) noexcept {
    if (config.outputExtent.width == 0U ||
        config.outputExtent.height == 0U) {
        return failure(
            NativeRenderLayoutBuildIssueKind::outputExtentNotPositive);
    }
    if (!std::isfinite(config.renderScalePercent) ||
        !std::isfinite(config.verticalFovAdjustmentDegrees) ||
        !finite(config.referenceCameraCanvas) ||
        !std::isfinite(config.referenceHorizontalFovDegrees) ||
        !finite(config.uiDesignExtent) ||
        !finite(config.outputSafeArea)) {
        return failure(
            NativeRenderLayoutBuildIssueKind::nonFiniteConfig);
    }
    if (config.renderScalePercent <
            native_render_policy::minimumRenderScalePercent ||
        config.renderScalePercent >
            native_render_policy::maximumRenderScalePercent) {
        return failure(
            NativeRenderLayoutBuildIssueKind::renderScaleOutOfRange);
    }
    if (!(config.referenceCameraCanvas.width > 0.0F) ||
        !(config.referenceCameraCanvas.height > 0.0F) ||
        !(config.uiDesignExtent.width > 0.0F) ||
        !(config.uiDesignExtent.height > 0.0F)) {
        return failure(
            NativeRenderLayoutBuildIssueKind::logicalExtentNotPositive);
    }
    if (config.referenceHorizontalFovDegrees <
            minimumReferenceFovDegrees ||
        config.referenceHorizontalFovDegrees >
            maximumReferenceFovDegrees) {
        return failure(
            NativeRenderLayoutBuildIssueKind::referenceFovOutOfRange);
    }
    if (config.verticalFovAdjustmentDegrees <
            native_render_policy::
                minimumVerticalFovAdjustmentDegrees ||
        config.verticalFovAdjustmentDegrees >
            native_render_policy::
                maximumVerticalFovAdjustmentDegrees) {
        return failure(
            NativeRenderLayoutBuildIssueKind::
                verticalFovAdjustmentOutOfRange);
    }
    if (config.outputSafeArea.left < 0.0F ||
        config.outputSafeArea.top < 0.0F ||
        config.outputSafeArea.right < 0.0F ||
        config.outputSafeArea.bottom < 0.0F) {
        return failure(
            NativeRenderLayoutBuildIssueKind::invalidSafeArea);
    }

    const double horizontalInsets =
        static_cast<double>(config.outputSafeArea.left) +
        static_cast<double>(config.outputSafeArea.right);
    const double verticalInsets =
        static_cast<double>(config.outputSafeArea.top) +
        static_cast<double>(config.outputSafeArea.bottom);
    if (!(horizontalInsets <
          static_cast<double>(config.outputExtent.width)) ||
        !(verticalInsets <
          static_cast<double>(config.outputExtent.height))) {
        return failure(
            NativeRenderLayoutBuildIssueKind::invalidSafeArea);
    }

    const auto renderWidth = scaledDimension(
        config.outputExtent.width,
        config.renderScalePercent);
    const auto renderHeight = scaledDimension(
        config.outputExtent.height,
        config.renderScalePercent);
    if (!renderWidth.has_value() || !renderHeight.has_value()) {
        return failure(
            NativeRenderLayoutBuildIssueKind::renderTargetExtentOverflow);
    }
    const RenderTargetPixelExtent renderTargetExtent{
        .width = *renderWidth,
        .height = *renderHeight,
    };

    const float renderWidthFloat =
        static_cast<float>(renderTargetExtent.width);
    const float renderHeightFloat =
        static_cast<float>(renderTargetExtent.height);
    const float referenceAspect =
        config.referenceCameraCanvas.width /
        config.referenceCameraCanvas.height;
    RenderTargetPixelRect sceneViewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = renderWidthFloat,
        .height = renderHeightFloat,
    };
    if (config.scenePresentation ==
        ScenePresentationMode::originalFourByThree) {
        sceneViewport = aspectFit(
            renderWidthFloat,
            renderHeightFloat,
            referenceAspect);
    }

    const double referenceHorizontalHalfRadians =
        static_cast<double>(config.referenceHorizontalFovDegrees) *
        radiansPerDegree * 0.5;
    const double referenceAspectWide =
        static_cast<double>(referenceAspect);
    const double baseVerticalHalfRadians = std::atan(
        std::tan(referenceHorizontalHalfRadians) /
        referenceAspectWide);
    constexpr double adjustmentCalibrationHorizontalHalfRadians =
        45.0 * radiansPerDegree;
    const double adjustmentCalibrationVerticalHalfRadians = std::atan(
        std::tan(adjustmentCalibrationHorizontalHalfRadians) /
        referenceAspectWide);
    const double adjustedCalibrationVerticalHalfRadians =
        adjustmentCalibrationVerticalHalfRadians +
        static_cast<double>(config.verticalFovAdjustmentDegrees) *
            radiansPerDegree * 0.5;
    const double fovTangentScale =
        std::tan(adjustedCalibrationVerticalHalfRadians) /
        std::tan(adjustmentCalibrationVerticalHalfRadians);
    const double verticalHalfRadians = std::atan(
        std::tan(baseVerticalHalfRadians) * fovTangentScale);

    const float sceneAspect =
        sceneViewport.width / sceneViewport.height;
    const float cameraLogicalHeight = static_cast<float>(
        static_cast<double>(config.referenceCameraCanvas.height) *
        fovTangentScale);
    const CameraLogicalExtent cameraLogicalExtent{
        .width = cameraLogicalHeight * sceneAspect,
        .height = cameraLogicalHeight,
    };
    const CameraLogicalPoint cameraLogicalCentre{
        .x = cameraLogicalExtent.width * 0.5F,
        .y = cameraLogicalExtent.height * 0.5F,
    };

    const double horizontalHalfRadians = std::atan(
        std::tan(verticalHalfRadians) *
        static_cast<double>(sceneAspect));
    const float verticalFovDegrees = static_cast<float>(
        2.0 * verticalHalfRadians * degreesPerRadian);
    const float horizontalFovDegrees = static_cast<float>(
        2.0 * horizontalHalfRadians * degreesPerRadian);

    const float safeWidth =
        static_cast<float>(config.outputExtent.width) -
        config.outputSafeArea.left -
        config.outputSafeArea.right;
    const float safeHeight =
        static_cast<float>(config.outputExtent.height) -
        config.outputSafeArea.top -
        config.outputSafeArea.bottom;
    const OutputPixelRect uiSafeRect{
        .x = config.outputSafeArea.left,
        .y = config.outputSafeArea.top,
        .width = safeWidth,
        .height = safeHeight,
    };
    const float uiScale = std::min(
        safeWidth / config.uiDesignExtent.width,
        safeHeight / config.uiDesignExtent.height);
    const float uiWidth = config.uiDesignExtent.width * uiScale;
    const float uiHeight = config.uiDesignExtent.height * uiScale;
    const OutputPixelRect uiViewport{
        .x = uiSafeRect.x + (safeWidth - uiWidth) * 0.5F,
        .y = uiSafeRect.y + (safeHeight - uiHeight) * 0.5F,
        .width = uiWidth,
        .height = uiHeight,
    };

    if (!(sceneAspect > 0.0F) || !finite(sceneViewport) ||
        !finite(cameraLogicalExtent) ||
        !std::isfinite(cameraLogicalCentre.x) ||
        !std::isfinite(cameraLogicalCentre.y) ||
        !std::isfinite(fovTangentScale) ||
        !(fovTangentScale >= 1.0) ||
        !std::isfinite(verticalFovDegrees) ||
        !std::isfinite(horizontalFovDegrees) ||
        !(verticalFovDegrees > 0.0F) ||
        !(horizontalFovDegrees > 0.0F) ||
        !finite(uiSafeRect) || !finite(uiViewport) ||
        !std::isfinite(uiScale) || !(uiScale > 0.0F)) {
        return failure(
            NativeRenderLayoutBuildIssueKind::nonFiniteDerivedLayout);
    }

    return {
        .layout = NativeRenderLayout{
            config.outputExtent,
            renderTargetExtent,
            config.renderScalePercent,
            config.scenePresentation,
            config.verticalFovAdjustmentDegrees,
            sceneViewport,
            config.referenceCameraCanvas,
            cameraLogicalExtent,
            cameraLogicalCentre,
            verticalFovDegrees,
            horizontalFovDegrees,
            uiSafeRect,
            uiViewport,
            config.uiDesignExtent,
            uiScale,
        },
        .issue = std::nullopt,
    };
}

std::optional<CameraLogicalPoint>
NativeRenderLayout::cameraPointFromOutput(
    const OutputPixelPoint point) const noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        return std::nullopt;
    }
    const float renderX =
        point.x * static_cast<float>(renderTargetExtent_.width) /
        static_cast<float>(outputExtent_.width);
    const float renderY =
        point.y * static_cast<float>(renderTargetExtent_.height) /
        static_cast<float>(outputExtent_.height);
    const float viewportRight =
        sceneViewportInRenderTarget_.x +
        sceneViewportInRenderTarget_.width;
    const float viewportBottom =
        sceneViewportInRenderTarget_.y +
        sceneViewportInRenderTarget_.height;
    if (renderX < sceneViewportInRenderTarget_.x ||
        renderY < sceneViewportInRenderTarget_.y ||
        renderX > viewportRight ||
        renderY > viewportBottom) {
        return std::nullopt;
    }

    const CameraLogicalPoint logical{
        .x =
            (renderX - sceneViewportInRenderTarget_.x) /
            sceneViewportInRenderTarget_.width *
            cameraLogicalExtent_.width,
        .y =
            (renderY - sceneViewportInRenderTarget_.y) /
            sceneViewportInRenderTarget_.height *
            cameraLogicalExtent_.height,
    };
    if (!std::isfinite(logical.x) || !std::isfinite(logical.y)) {
        return std::nullopt;
    }
    return logical;
}

std::optional<UiLogicalPoint> NativeRenderLayout::uiPointFromOutput(
    const OutputPixelPoint point) const noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        return std::nullopt;
    }
    const float right =
        uiViewportInOutput_.x + uiViewportInOutput_.width;
    const float bottom =
        uiViewportInOutput_.y + uiViewportInOutput_.height;
    if (point.x < uiViewportInOutput_.x ||
        point.y < uiViewportInOutput_.y ||
        point.x > right ||
        point.y > bottom) {
        return std::nullopt;
    }

    const UiLogicalPoint logical{
        .x =
            (point.x - uiViewportInOutput_.x) / uiScale_,
        .y =
            (point.y - uiViewportInOutput_.y) / uiScale_,
    };
    if (!std::isfinite(logical.x) || !std::isfinite(logical.y)) {
        return std::nullopt;
    }
    return logical;
}

std::optional<OutputPixelPoint> NativeRenderLayout::outputPointFromUi(
    const UiLogicalPoint point) const noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        point.x < 0.0F || point.y < 0.0F ||
        point.x > uiDesignExtent_.width ||
        point.y > uiDesignExtent_.height) {
        return std::nullopt;
    }
    const OutputPixelPoint output{
        .x = uiViewportInOutput_.x + point.x * uiScale_,
        .y = uiViewportInOutput_.y + point.y * uiScale_,
    };
    if (!std::isfinite(output.x) || !std::isfinite(output.y)) {
        return std::nullopt;
    }
    return output;
}

} // namespace airfix::render
