#include "airfix/render/RenderPresentationSettings.hpp"

#include <cmath>

namespace airfix::render {
namespace {

inline constexpr std::uint8_t widescreenHorPlusRecordValue = 0U;
inline constexpr std::uint8_t originalFourByThreeRecordValue = 1U;
inline constexpr std::uint8_t classicVisualProfileRecordValue = 0U;
inline constexpr std::uint8_t enhancedVisualProfileRecordValue = 1U;

[[nodiscard]] constexpr bool valid(
    const ScenePresentationMode mode) noexcept {
    switch (mode) {
    case ScenePresentationMode::widescreenHorPlus:
    case ScenePresentationMode::originalFourByThree:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid(const VisualProfile profile) noexcept {
    switch (profile) {
    case VisualProfile::classic:
    case VisualProfile::enhanced:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr std::uint8_t scenePresentationRecordValue(
    const ScenePresentationMode mode) noexcept {
    switch (mode) {
    case ScenePresentationMode::widescreenHorPlus:
        return widescreenHorPlusRecordValue;
    case ScenePresentationMode::originalFourByThree:
        return originalFourByThreeRecordValue;
    }
    return widescreenHorPlusRecordValue;
}

[[nodiscard]] constexpr std::uint8_t visualProfileRecordValue(
    const VisualProfile profile) noexcept {
    switch (profile) {
    case VisualProfile::classic:
        return classicVisualProfileRecordValue;
    case VisualProfile::enhanced:
        return enhancedVisualProfileRecordValue;
    }
    return classicVisualProfileRecordValue;
}

[[nodiscard]] constexpr std::optional<ScenePresentationMode>
scenePresentationFromRecordValue(const std::uint8_t value) noexcept {
    switch (value) {
    case widescreenHorPlusRecordValue:
        return ScenePresentationMode::widescreenHorPlus;
    case originalFourByThreeRecordValue:
        return ScenePresentationMode::originalFourByThree;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] constexpr std::optional<VisualProfile>
visualProfileFromRecordValue(const std::uint8_t value) noexcept {
    switch (value) {
    case classicVisualProfileRecordValue:
        return VisualProfile::classic;
    case enhancedVisualProfileRecordValue:
        return VisualProfile::enhanced;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] constexpr RenderPresentationSettingsIssue issue(
    const RenderPresentationSettingsIssueKind kind) noexcept {
    return {.kind = kind};
}

} // namespace

std::optional<RenderPresentationSettingsIssue>
validateRenderPresentationSettings(
    const RenderPresentationSettings& settings) noexcept {
    if (!std::isfinite(settings.renderScalePercent)) {
        return issue(
            RenderPresentationSettingsIssueKind::nonFiniteRenderScale);
    }
    if (settings.renderScalePercent <
            native_render_policy::minimumRenderScalePercent ||
        settings.renderScalePercent >
            native_render_policy::maximumRenderScalePercent) {
        return issue(
            RenderPresentationSettingsIssueKind::renderScaleOutOfRange);
    }
    if (!valid(settings.scenePresentation)) {
        return issue(
            RenderPresentationSettingsIssueKind::
                unsupportedScenePresentation);
    }
    if (!valid(settings.visualProfile)) {
        return issue(
            RenderPresentationSettingsIssueKind::unsupportedVisualProfile);
    }
    return std::nullopt;
}

RenderPresentationSettingsResolveResult
resolveRenderPresentationSettings(
    const RenderPresentationSettings& base,
    const RenderPresentationSettingsOverride& overrides) noexcept {
    const auto baseValidation =
        validateRenderPresentationSettings(base);
    if (baseValidation.has_value()) {
        return {
            .settings = base,
            .issue = baseValidation,
        };
    }

    RenderPresentationSettings candidate = base;
    if (overrides.renderScalePercent.has_value()) {
        candidate.renderScalePercent = *overrides.renderScalePercent;
    }
    if (overrides.scenePresentation.has_value()) {
        candidate.scenePresentation = *overrides.scenePresentation;
    }
    if (overrides.visualProfile.has_value()) {
        candidate.visualProfile = *overrides.visualProfile;
    }
    if (overrides.diagnosticsOverlayEnabled.has_value()) {
        candidate.diagnosticsOverlayEnabled =
            *overrides.diagnosticsOverlayEnabled;
    }

    const auto validation =
        validateRenderPresentationSettings(candidate);
    if (validation.has_value()) {
        return {
            .settings = base,
            .issue = validation,
        };
    }
    return {
        .settings = candidate,
        .issue = std::nullopt,
    };
}

RenderPresentationSettingsDeltaResult
diffRenderPresentationSettings(
    const RenderPresentationSettings& previous,
    const RenderPresentationSettings& candidate) noexcept {
    const auto previousValidation =
        validateRenderPresentationSettings(previous);
    if (previousValidation.has_value()) {
        return {
            .delta = std::nullopt,
            .issue = previousValidation,
        };
    }
    const auto candidateValidation =
        validateRenderPresentationSettings(candidate);
    if (candidateValidation.has_value()) {
        return {
            .delta = std::nullopt,
            .issue = candidateValidation,
        };
    }

    const bool scaleChanged =
        previous.renderScalePercent != candidate.renderScalePercent;
    return {
        .delta =
            RenderPresentationSettingsDelta{
                .scaleTargetsChanged = scaleChanged,
                .layoutChanged =
                    scaleChanged ||
                    previous.scenePresentation !=
                        candidate.scenePresentation,
                .diagnosticsChanged =
                    previous.diagnosticsOverlayEnabled !=
                        candidate.diagnosticsOverlayEnabled,
                .visualProfileChanged =
                    previous.visualProfile != candidate.visualProfile,
            },
        .issue = std::nullopt,
    };
}

RenderPresentationSettingsRecordBuildResult
makeRenderPresentationSettingsRecord(
    const RenderPresentationSettings& settings) noexcept {
    const auto validation =
        validateRenderPresentationSettings(settings);
    if (validation.has_value()) {
        return {
            .record = std::nullopt,
            .issue = validation,
        };
    }

    return {
        .record =
            RenderPresentationSettingsRecord{
                .schemaVersion =
                    renderPresentationSettingsRecordSchemaVersion,
                .renderScalePercent = settings.renderScalePercent,
                .scenePresentation =
                    scenePresentationRecordValue(
                        settings.scenePresentation),
                .visualProfile =
                    visualProfileRecordValue(settings.visualProfile),
                .diagnosticsOverlayEnabled =
                    static_cast<std::uint8_t>(
                        settings.diagnosticsOverlayEnabled ? 1U : 0U),
            },
        .issue = std::nullopt,
    };
}

RenderPresentationSettingsFromRecordResult
renderPresentationSettingsFromRecord(
    const RenderPresentationSettingsRecord& record) noexcept {
    if (record.schemaVersion !=
        renderPresentationSettingsRecordSchemaVersion) {
        return {
            .settings = std::nullopt,
            .issue =
                issue(
                    RenderPresentationSettingsIssueKind::
                        unsupportedSchema),
        };
    }
    if (record.diagnosticsOverlayEnabled > 1U) {
        return {
            .settings = std::nullopt,
            .issue =
                issue(
                    RenderPresentationSettingsIssueKind::
                        invalidStoredDiagnosticsValue),
        };
    }

    const auto scenePresentation =
        scenePresentationFromRecordValue(record.scenePresentation);
    if (!scenePresentation.has_value()) {
        return {
            .settings = std::nullopt,
            .issue =
                issue(
                    RenderPresentationSettingsIssueKind::
                        unsupportedScenePresentation),
        };
    }
    const auto visualProfile =
        visualProfileFromRecordValue(record.visualProfile);
    if (!visualProfile.has_value()) {
        return {
            .settings = std::nullopt,
            .issue =
                issue(
                    RenderPresentationSettingsIssueKind::
                        unsupportedVisualProfile),
        };
    }

    const RenderPresentationSettings settings{
        .renderScalePercent = record.renderScalePercent,
        .scenePresentation = *scenePresentation,
        .visualProfile = *visualProfile,
        .diagnosticsOverlayEnabled =
            record.diagnosticsOverlayEnabled != 0U,
    };
    const auto validation =
        validateRenderPresentationSettings(settings);
    if (validation.has_value()) {
        return {
            .settings = std::nullopt,
            .issue = validation,
        };
    }
    return {
        .settings = settings,
        .issue = std::nullopt,
    };
}

} // namespace airfix::render
