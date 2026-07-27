#include "airfix/render/LegacyCanvasLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace airfix::render {
namespace {

[[nodiscard]] bool finite(const LegacyCanvasRect& rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
        std::isfinite(rect.width) && std::isfinite(rect.height);
}

[[nodiscard]] bool finiteEndpoints(
    const LegacyCanvasRect& rect) noexcept {
    const auto checkedEndpoint = [](const float origin,
                                    const float extent) noexcept {
        constexpr double lowest =
            static_cast<double>(std::numeric_limits<float>::lowest());
        constexpr double maximum =
            static_cast<double>(std::numeric_limits<float>::max());
        const double originWide = static_cast<double>(origin);
        const double extentWide = static_cast<double>(extent);

        // Check headroom before addition because FLT_MAX plus a small finite
        // extent may round back to FLT_MAX even after promotion to double.
        if (extentWide > maximum - originWide) {
            return false;
        }
        const double endpoint = originWide + extentWide;
        return std::isfinite(endpoint) &&
            endpoint >= lowest && endpoint <= maximum;
    };

    return checkedEndpoint(rect.x, rect.width) &&
        checkedEndpoint(rect.y, rect.height);
}

[[nodiscard]] LegacyCanvasAspectFitBuildResult buildFailure(
    const LegacyCanvasLayoutBuildIssueKind kind) noexcept {
    return {
        .layout = std::nullopt,
        .issue = LegacyCanvasLayoutBuildIssue{.kind = kind},
    };
}

} // namespace

LegacyCanvasAspectFitBuildResult buildLegacyCanvasAspectFitLayout(
    const LegacyCanvasRect& targetRect) noexcept {
    if (!finite(targetRect)) {
        return buildFailure(
            LegacyCanvasLayoutBuildIssueKind::nonFiniteTargetRect);
    }
    if (!(targetRect.width > 0.0F) ||
        !(targetRect.height > 0.0F)) {
        return buildFailure(
            LegacyCanvasLayoutBuildIssueKind::targetExtentNotPositive);
    }
    if (!finiteEndpoints(targetRect)) {
        return buildFailure(
            LegacyCanvasLayoutBuildIssueKind::nonFiniteDerivedLayout);
    }

    const float horizontalScale =
        targetRect.width / recovered_legacy_canvas::width;
    const float verticalScale =
        targetRect.height / recovered_legacy_canvas::height;
    const float scale = std::min(horizontalScale, verticalScale);
    const float fittedWidth =
        recovered_legacy_canvas::width * scale;
    const float fittedHeight =
        recovered_legacy_canvas::height * scale;
    const float fittedX =
        targetRect.x + (targetRect.width - fittedWidth) * 0.5F;
    const float fittedY =
        targetRect.y + (targetRect.height - fittedHeight) * 0.5F;

    const LegacyCanvasRect fittedRect{
        .x = fittedX,
        .y = fittedY,
        .width = fittedWidth,
        .height = fittedHeight,
    };
    if (!std::isfinite(scale) || !(scale > 0.0F) ||
        !finite(fittedRect) || !finiteEndpoints(fittedRect)) {
        return buildFailure(
            LegacyCanvasLayoutBuildIssueKind::nonFiniteDerivedLayout);
    }

    return {
        .layout = LegacyCanvasAspectFitLayout{
            targetRect,
            fittedRect,
            scale,
        },
        .issue = std::nullopt,
    };
}

} // namespace airfix::render
