#pragma once

#include "airfix/render/NativeRenderLayout.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

// This selects bounded visual policy without changing deterministic gameplay.
// Enhanced currently enables improved scene-texture sampling; lighting,
// materials, shadows, and post-processing remain separate later stages.
enum class VisualProfile : std::uint8_t {
    classic,
    enhanced,
};

struct RenderPresentationSettings final {
    float renderScalePercent{
        native_render_policy::defaultRenderScalePercent};
    ScenePresentationMode scenePresentation{
        ScenePresentationMode::widescreenHorPlus};
    VisualProfile visualProfile{VisualProfile::classic};
    bool diagnosticsOverlayEnabled{};
    float verticalFovAdjustmentDegrees{
        native_render_policy::defaultVerticalFovAdjustmentDegrees};
    float uiScalePercent{native_render_policy::defaultUiScalePercent};

    [[nodiscard]] friend constexpr bool operator==(
        const RenderPresentationSettings&,
        const RenderPresentationSettings&) noexcept = default;
};

// Optional values distinguish launch/UI overrides from complete persisted or
// default settings. An absent value never replaces the corresponding base
// value.
struct RenderPresentationSettingsOverride final {
    std::optional<float> renderScalePercent;
    std::optional<ScenePresentationMode> scenePresentation;
    std::optional<VisualProfile> visualProfile;
    std::optional<bool> diagnosticsOverlayEnabled;
    std::optional<float> verticalFovAdjustmentDegrees;
    std::optional<float> uiScalePercent;
};

enum class RenderPresentationSettingsIssueKind : std::uint8_t {
    nonFiniteRenderScale,
    renderScaleOutOfRange,
    unsupportedScenePresentation,
    unsupportedVisualProfile,
    unsupportedSchema,
    invalidStoredDiagnosticsValue,
    nonFiniteVerticalFovAdjustment,
    verticalFovAdjustmentOutOfRange,
    nonFiniteUiScale,
    uiScaleOutOfRange,
};

struct RenderPresentationSettingsIssue final {
    RenderPresentationSettingsIssueKind kind{
        RenderPresentationSettingsIssueKind::nonFiniteRenderScale};
};

// Returns no issue only when every setting is valid. Enum validation uses
// explicit cases so forged underlying values fail closed.
[[nodiscard]] std::optional<RenderPresentationSettingsIssue>
validateRenderPresentationSettings(
    const RenderPresentationSettings& settings) noexcept;

struct RenderPresentationSettingsResolveResult final {
    RenderPresentationSettings settings;
    std::optional<RenderPresentationSettingsIssue> issue;

    [[nodiscard]] constexpr bool accepted() const noexcept {
        return !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return accepted();
    }
};

struct RenderPresentationSettingsDelta final {
    bool scaleTargetsChanged{};
    bool layoutChanged{};
    bool diagnosticsChanged{};
    bool visualProfileChanged{};
    bool uiLayoutChanged{};

    [[nodiscard]] constexpr bool anyChanged() const noexcept {
        return scaleTargetsChanged || layoutChanged ||
            diagnosticsChanged || visualProfileChanged || uiLayoutChanged;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const RenderPresentationSettingsDelta&,
        const RenderPresentationSettingsDelta&) noexcept = default;
};

struct RenderPresentationSettingsDeltaResult final {
    std::optional<RenderPresentationSettingsDelta> delta;
    std::optional<RenderPresentationSettingsIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return delta.has_value() && !issue.has_value();
    }
};

// Applies all present overrides to a candidate and validates that complete
// candidate before publication. Rejection returns base byte-for-field
// unchanged; an invalid base is rejected before overrides are inspected.
// Values are never clamped or normalized.
[[nodiscard]] RenderPresentationSettingsResolveResult
resolveRenderPresentationSettings(
    const RenderPresentationSettings& base,
    const RenderPresentationSettingsOverride& overrides) noexcept;

// Compares two already-resolved snapshots. Scale-target recreation depends
// only on render scale. Layout changes depend on render scale, presentation,
// or safe FOV. UI-content layout, diagnostics, and visual-profile policy remain
// distinct orthogonal domains. Both snapshots are validated before comparison,
// and invalid input produces no delta.
[[nodiscard]] RenderPresentationSettingsDeltaResult
diffRenderPresentationSettings(
    const RenderPresentationSettings& previous,
    const RenderPresentationSettings& candidate) noexcept;

inline constexpr std::uint32_t
    renderPresentationSettingsRecordSchemaVersion = 3U;

// Storage-neutral semantic record. Platform adapters map these named fields
// into their chosen store; the in-memory object representation is not a file
// format and must not be persisted as raw bytes.
struct RenderPresentationSettingsRecord final {
    std::uint32_t schemaVersion{
        renderPresentationSettingsRecordSchemaVersion};
    float renderScalePercent{
        native_render_policy::defaultRenderScalePercent};
    std::uint8_t scenePresentation{};
    std::uint8_t visualProfile{};
    std::uint8_t diagnosticsOverlayEnabled{};
    float verticalFovAdjustmentDegrees{
        native_render_policy::defaultVerticalFovAdjustmentDegrees};
    float uiScalePercent{native_render_policy::defaultUiScalePercent};

    [[nodiscard]] friend constexpr bool operator==(
        const RenderPresentationSettingsRecord&,
        const RenderPresentationSettingsRecord&) noexcept = default;
};

struct RenderPresentationSettingsRecordBuildResult final {
    std::optional<RenderPresentationSettingsRecord> record;
    std::optional<RenderPresentationSettingsIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return record.has_value() && !issue.has_value();
    }
};

struct RenderPresentationSettingsFromRecordResult final {
    std::optional<RenderPresentationSettings> settings;
    std::optional<RenderPresentationSettingsIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return settings.has_value() && !issue.has_value();
    }
};

[[nodiscard]] RenderPresentationSettingsRecordBuildResult
makeRenderPresentationSettingsRecord(
    const RenderPresentationSettings& settings) noexcept;

[[nodiscard]] RenderPresentationSettingsFromRecordResult
renderPresentationSettingsFromRecord(
    const RenderPresentationSettingsRecord& record) noexcept;

} // namespace airfix::render
