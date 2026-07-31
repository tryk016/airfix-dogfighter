#include "airfix/render/RenderPresentationSettings.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {

using namespace airfix::render;

static_assert(
    std::is_trivially_copyable_v<RenderPresentationSettings>);
static_assert(
    std::is_trivially_copyable_v<RenderPresentationSettingsRecord>);
static_assert(noexcept(validateRenderPresentationSettings({})));
static_assert(noexcept(resolveRenderPresentationSettings({}, {})));
static_assert(noexcept(diffRenderPresentationSettings({}, {})));
static_assert(noexcept(makeRenderPresentationSettingsRecord({})));
static_assert(noexcept(renderPresentationSettingsFromRecord({})));

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireIssue(
    const std::optional<RenderPresentationSettingsIssue>& issue,
    const RenderPresentationSettingsIssueKind expected,
    const char* const message) {
    require(
        issue.has_value() && issue->kind == expected,
        message);
}

void testDefaultsAndBoundaries() {
    const RenderPresentationSettings defaults;
    require(
        defaults ==
                RenderPresentationSettings{
                    .renderScalePercent = 100.0F,
                    .scenePresentation =
                        ScenePresentationMode::widescreenHorPlus,
                    .visualProfile = VisualProfile::classic,
                    .diagnosticsOverlayEnabled = false,
                    .verticalFovAdjustmentDegrees = 0.0F,
                } &&
            !validateRenderPresentationSettings(defaults).has_value(),
        "default render presentation settings changed");

    for (const float boundary : std::array{50.0F, 200.0F}) {
        const auto result = resolveRenderPresentationSettings(
            defaults,
            {.renderScalePercent = boundary});
        require(
            result.accepted() &&
                result.settings.renderScalePercent == boundary,
            "valid render-scale boundary was rejected");
    }
    for (const float boundary : std::array{0.0F, 25.0F}) {
        const auto result = resolveRenderPresentationSettings(
            defaults,
            {.verticalFovAdjustmentDegrees = boundary});
        require(
            result.accepted() &&
                result.settings.verticalFovAdjustmentDegrees ==
                    boundary,
            "valid vertical-FOV boundary was rejected");
    }
}

void testInvalidSettingsAndAtomicResolve() {
    const RenderPresentationSettings base{
        .renderScalePercent = 125.0F,
        .scenePresentation =
            ScenePresentationMode::originalFourByThree,
        .visualProfile = VisualProfile::enhanced,
        .diagnosticsOverlayEnabled = true,
        .verticalFovAdjustmentDegrees = 10.0F,
    };

    struct ScaleCase final {
        float value;
        RenderPresentationSettingsIssueKind issue;
    };
    const std::array scaleCases{
        ScaleCase{
            std::numeric_limits<float>::quiet_NaN(),
            RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
        },
        ScaleCase{
            std::numeric_limits<float>::infinity(),
            RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
        },
        ScaleCase{
            -std::numeric_limits<float>::infinity(),
            RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
        },
        ScaleCase{
            49.0F,
            RenderPresentationSettingsIssueKind::renderScaleOutOfRange,
        },
        ScaleCase{
            201.0F,
            RenderPresentationSettingsIssueKind::renderScaleOutOfRange,
        },
    };
    for (const auto& test : scaleCases) {
        const auto result = resolveRenderPresentationSettings(
            base,
            {.renderScalePercent = test.value});
        require(!result.accepted(), "invalid render scale was accepted");
        require(
            result.settings == base,
            "invalid render scale partially changed settings");
        requireIssue(
            result.issue, test.issue,
            "invalid render scale reported the wrong issue");
    }

    struct FovCase final {
        float value;
        RenderPresentationSettingsIssueKind issue;
    };
    const std::array fovCases{
        FovCase{
            std::numeric_limits<float>::quiet_NaN(),
            RenderPresentationSettingsIssueKind::
                nonFiniteVerticalFovAdjustment,
        },
        FovCase{
            std::numeric_limits<float>::infinity(),
            RenderPresentationSettingsIssueKind::
                nonFiniteVerticalFovAdjustment,
        },
        FovCase{
            -0.01F,
            RenderPresentationSettingsIssueKind::
                verticalFovAdjustmentOutOfRange,
        },
        FovCase{
            25.01F,
            RenderPresentationSettingsIssueKind::
                verticalFovAdjustmentOutOfRange,
        },
    };
    for (const auto& test : fovCases) {
        const auto result = resolveRenderPresentationSettings(
            base,
            {.verticalFovAdjustmentDegrees = test.value});
        require(
            !result.accepted() && result.settings == base,
            "invalid vertical-FOV adjustment changed settings");
        requireIssue(
            result.issue, test.issue,
            "invalid vertical-FOV adjustment reported the wrong issue");
    }

    const auto forgedPresentation =
        static_cast<ScenePresentationMode>(0xFFU);
    const auto presentationResult =
        resolveRenderPresentationSettings(
            base,
            {.scenePresentation = forgedPresentation});
    require(
        !presentationResult.accepted() &&
            presentationResult.settings == base,
        "forged scene-presentation enum changed settings");
    requireIssue(
        presentationResult.issue,
        RenderPresentationSettingsIssueKind::
            unsupportedScenePresentation,
        "forged scene-presentation enum reported the wrong issue");

    const auto forgedProfile = static_cast<VisualProfile>(0xFFU);
    const auto profileResult = resolveRenderPresentationSettings(
        base,
        {.visualProfile = forgedProfile});
    require(
        !profileResult.accepted() &&
            profileResult.settings == base,
        "forged visual-profile enum changed settings");
    requireIssue(
        profileResult.issue,
        RenderPresentationSettingsIssueKind::unsupportedVisualProfile,
        "forged visual-profile enum reported the wrong issue");

    auto invalidForEncoding = base;
    invalidForEncoding.visualProfile = forgedProfile;
    const auto recordResult =
        makeRenderPresentationSettingsRecord(invalidForEncoding);
    require(
        !recordResult.complete() &&
            !recordResult.record.has_value(),
        "invalid settings produced a semantic record");
    requireIssue(
        recordResult.issue,
        RenderPresentationSettingsIssueKind::unsupportedVisualProfile,
        "invalid record input reported the wrong issue");
}

void testInvalidBaseIsRejectedBeforeOverrides() {
    auto base = RenderPresentationSettings{};
    base.renderScalePercent =
        std::numeric_limits<float>::quiet_NaN();
    auto result = resolveRenderPresentationSettings(
        base,
        {
            .renderScalePercent = 100.0F,
            .scenePresentation =
                ScenePresentationMode::originalFourByThree,
            .visualProfile = VisualProfile::enhanced,
            .diagnosticsOverlayEnabled = true,
            .verticalFovAdjustmentDegrees = 0.0F,
        });
    require(
        !result.accepted() &&
            std::isnan(result.settings.renderScalePercent) &&
            result.settings.scenePresentation ==
                base.scenePresentation &&
            result.settings.visualProfile == base.visualProfile &&
            result.settings.diagnosticsOverlayEnabled ==
                base.diagnosticsOverlayEnabled &&
            result.settings.verticalFovAdjustmentDegrees ==
                base.verticalFovAdjustmentDegrees,
        "override repaired or partially changed an invalid base");
    requireIssue(
        result.issue,
        RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
        "invalid base scale reported the wrong issue");

    base = {};
    base.scenePresentation =
        static_cast<ScenePresentationMode>(0xFFU);
    result = resolveRenderPresentationSettings(
        base,
        {
            .scenePresentation =
                ScenePresentationMode::widescreenHorPlus,
        });
    require(
        !result.accepted() &&
            result.settings.scenePresentation ==
                base.scenePresentation,
        "override repaired a forged base presentation");
    requireIssue(
        result.issue,
        RenderPresentationSettingsIssueKind::
            unsupportedScenePresentation,
        "forged base presentation reported the wrong issue");

    base = {};
    base.visualProfile = static_cast<VisualProfile>(0xFFU);
    result = resolveRenderPresentationSettings(
        base,
        {.visualProfile = VisualProfile::classic});
    require(
        !result.accepted() &&
            result.settings.visualProfile == base.visualProfile,
        "override repaired a forged base visual profile");
    requireIssue(
        result.issue,
        RenderPresentationSettingsIssueKind::unsupportedVisualProfile,
        "forged base visual profile reported the wrong issue");
}

void testPartialOverridesPreserveOtherValues() {
    const RenderPresentationSettings base{
        .renderScalePercent = 75.0F,
        .scenePresentation =
            ScenePresentationMode::originalFourByThree,
        .visualProfile = VisualProfile::enhanced,
        .diagnosticsOverlayEnabled = true,
        .verticalFovAdjustmentDegrees = 15.0F,
    };

    const auto scaleOnly = resolveRenderPresentationSettings(
        base,
        {.renderScalePercent = 150.0F});
    require(
        scaleOnly.accepted() &&
            scaleOnly.settings ==
                RenderPresentationSettings{
                    .renderScalePercent = 150.0F,
                    .scenePresentation =
                        ScenePresentationMode::originalFourByThree,
                    .visualProfile = VisualProfile::enhanced,
                    .diagnosticsOverlayEnabled = true,
                    .verticalFovAdjustmentDegrees = 15.0F,
                },
        "partial scale override replaced unrelated settings");

    const auto diagnosticsOnly =
        resolveRenderPresentationSettings(
            base,
            {.diagnosticsOverlayEnabled = false});
    require(
        diagnosticsOnly.accepted() &&
            diagnosticsOnly.settings.renderScalePercent == 75.0F &&
            diagnosticsOnly.settings.scenePresentation ==
                ScenePresentationMode::originalFourByThree &&
            diagnosticsOnly.settings.visualProfile ==
                VisualProfile::enhanced &&
            !diagnosticsOnly.settings.diagnosticsOverlayEnabled &&
            diagnosticsOnly.settings.verticalFovAdjustmentDegrees ==
                15.0F,
        "partial diagnostics override replaced unrelated settings");

    const auto fovOnly = resolveRenderPresentationSettings(
        base,
        {.verticalFovAdjustmentDegrees = 20.0F});
    require(
        fovOnly.accepted() &&
            fovOnly.settings.renderScalePercent == 75.0F &&
            fovOnly.settings.scenePresentation ==
                ScenePresentationMode::originalFourByThree &&
            fovOnly.settings.visualProfile ==
                VisualProfile::enhanced &&
            fovOnly.settings.diagnosticsOverlayEnabled &&
            fovOnly.settings.verticalFovAdjustmentDegrees == 20.0F,
        "partial vertical-FOV override replaced unrelated settings");

    const auto empty =
        resolveRenderPresentationSettings(base, {});
    require(
        empty.accepted() && empty.settings == base,
        "empty override changed settings");
}

void testSettingsDelta() {
    const RenderPresentationSettings base;
    const auto noChange =
        diffRenderPresentationSettings(base, base);
    require(
        noChange.complete() && noChange.delta.has_value() &&
            *noChange.delta == RenderPresentationSettingsDelta{} &&
            !noChange.delta->anyChanged(),
        "identical settings produced a delta");

    auto candidate = base;
    candidate.renderScalePercent = 150.0F;
    auto result = diffRenderPresentationSettings(base, candidate);
    require(
        result.complete() && result.delta.has_value() &&
            *result.delta ==
                RenderPresentationSettingsDelta{
                    .scaleTargetsChanged = true,
                    .layoutChanged = true,
                    .diagnosticsChanged = false,
                    .visualProfileChanged = false,
                },
        "render-scale delta changed the wrong domains");

    candidate = base;
    candidate.scenePresentation =
        ScenePresentationMode::originalFourByThree;
    result = diffRenderPresentationSettings(base, candidate);
    require(
        result.complete() && result.delta.has_value() &&
            *result.delta ==
                RenderPresentationSettingsDelta{
                    .scaleTargetsChanged = false,
                    .layoutChanged = true,
                    .diagnosticsChanged = false,
                    .visualProfileChanged = false,
                },
        "scene-presentation delta changed the wrong domains");

    candidate = base;
    candidate.verticalFovAdjustmentDegrees = 12.0F;
    result = diffRenderPresentationSettings(base, candidate);
    require(
        result.complete() && result.delta.has_value() &&
            *result.delta ==
                RenderPresentationSettingsDelta{
                    .scaleTargetsChanged = false,
                    .layoutChanged = true,
                    .diagnosticsChanged = false,
                    .visualProfileChanged = false,
                },
        "vertical-FOV delta changed the wrong domains");

    candidate = base;
    candidate.diagnosticsOverlayEnabled = true;
    result = diffRenderPresentationSettings(base, candidate);
    require(
        result.complete() && result.delta.has_value() &&
            *result.delta ==
                RenderPresentationSettingsDelta{
                    .scaleTargetsChanged = false,
                    .layoutChanged = false,
                    .diagnosticsChanged = true,
                    .visualProfileChanged = false,
                },
        "diagnostics delta changed the wrong domains");

    candidate = base;
    candidate.visualProfile = VisualProfile::enhanced;
    result = diffRenderPresentationSettings(base, candidate);
    require(
        result.complete() && result.delta.has_value() &&
            *result.delta ==
                RenderPresentationSettingsDelta{
                    .scaleTargetsChanged = false,
                    .layoutChanged = false,
                    .diagnosticsChanged = false,
                    .visualProfileChanged = true,
                },
        "visual-profile delta changed the wrong domains");

    candidate = {
        .renderScalePercent = 50.0F,
        .scenePresentation =
            ScenePresentationMode::originalFourByThree,
        .visualProfile = VisualProfile::enhanced,
        .diagnosticsOverlayEnabled = true,
        .verticalFovAdjustmentDegrees = 25.0F,
    };
    const auto combined =
        diffRenderPresentationSettings(base, candidate);
    require(
        combined.complete() && combined.delta.has_value() &&
            *combined.delta ==
                RenderPresentationSettingsDelta{
                    .scaleTargetsChanged = true,
                    .layoutChanged = true,
                    .diagnosticsChanged = true,
                    .visualProfileChanged = true,
                } &&
            combined.delta->anyChanged(),
        "combined settings delta omitted a changed domain");
}

void testInvalidSettingsProduceNoDelta() {
    const RenderPresentationSettings valid;

    auto invalid = valid;
    invalid.renderScalePercent =
        std::numeric_limits<float>::quiet_NaN();
    auto result = diffRenderPresentationSettings(invalid, valid);
    require(
        !result.complete() && !result.delta.has_value(),
        "invalid previous settings produced a delta");
    requireIssue(
        result.issue,
        RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
        "invalid previous scale reported the wrong issue");

    result = diffRenderPresentationSettings(valid, invalid);
    require(
        !result.complete() && !result.delta.has_value(),
        "invalid candidate settings produced a delta");
    requireIssue(
        result.issue,
        RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
        "invalid candidate scale reported the wrong issue");

    invalid = valid;
    invalid.verticalFovAdjustmentDegrees =
        std::numeric_limits<float>::quiet_NaN();
    result = diffRenderPresentationSettings(valid, invalid);
    require(
        !result.complete() && !result.delta.has_value(),
        "invalid vertical-FOV adjustment produced a delta");
    requireIssue(
        result.issue,
        RenderPresentationSettingsIssueKind::
            nonFiniteVerticalFovAdjustment,
        "invalid vertical-FOV delta reported the wrong issue");

    invalid = valid;
    invalid.scenePresentation =
        static_cast<ScenePresentationMode>(0xFFU);
    result = diffRenderPresentationSettings(invalid, valid);
    require(
        !result.complete() && !result.delta.has_value(),
        "forged previous presentation produced a delta");
    requireIssue(
        result.issue,
        RenderPresentationSettingsIssueKind::
            unsupportedScenePresentation,
        "forged previous presentation reported the wrong issue");

    invalid = valid;
    invalid.visualProfile = static_cast<VisualProfile>(0xFFU);
    result = diffRenderPresentationSettings(valid, invalid);
    require(
        !result.complete() && !result.delta.has_value(),
        "forged candidate visual profile produced a delta");
    requireIssue(
        result.issue,
        RenderPresentationSettingsIssueKind::unsupportedVisualProfile,
        "forged candidate visual profile reported the wrong issue");
}

[[nodiscard]] NativeRenderLayoutConfig layoutConfig(
    const RenderPresentationSettings& settings) {
    return {
        .outputExtent = {1920U, 1080U},
        .renderScalePercent = settings.renderScalePercent,
        .scenePresentation = settings.scenePresentation,
        .verticalFovAdjustmentDegrees =
            settings.verticalFovAdjustmentDegrees,
    };
}

void testDiagnosticsAndProfileAreLayoutOrthogonal() {
    const RenderPresentationSettings base{
        .renderScalePercent = 75.0F,
        .scenePresentation =
            ScenePresentationMode::originalFourByThree,
        .visualProfile = VisualProfile::classic,
        .diagnosticsOverlayEnabled = false,
        .verticalFovAdjustmentDegrees = 0.0F,
    };
    auto changed = base;
    changed.visualProfile = VisualProfile::enhanced;
    changed.diagnosticsOverlayEnabled = true;

    const auto baseConfig = layoutConfig(base);
    const auto changedConfig = layoutConfig(changed);
    require(
        baseConfig.renderScalePercent ==
                changedConfig.renderScalePercent &&
            baseConfig.scenePresentation ==
                changedConfig.scenePresentation &&
            baseConfig.verticalFovAdjustmentDegrees ==
                changedConfig.verticalFovAdjustmentDegrees &&
            baseConfig.outputExtent == changedConfig.outputExtent,
        "diagnostics or visual profile leaked into layout config");

    const auto baseLayout = buildNativeRenderLayout(baseConfig);
    const auto changedLayout =
        buildNativeRenderLayout(changedConfig);
    require(
        baseLayout.complete() && changedLayout.complete(),
        "orthogonal settings produced an invalid layout");
    require(
        baseLayout.layout->outputExtent() ==
                changedLayout.layout->outputExtent() &&
            baseLayout.layout->renderTargetExtent() ==
                changedLayout.layout->renderTargetExtent() &&
            baseLayout.layout->renderScalePercent() ==
                changedLayout.layout->renderScalePercent() &&
            baseLayout.layout->scenePresentation() ==
                changedLayout.layout->scenePresentation() &&
            baseLayout.layout->sceneViewportInRenderTarget() ==
                changedLayout.layout->
                    sceneViewportInRenderTarget() &&
            baseLayout.layout->cameraLogicalExtent() ==
                changedLayout.layout->cameraLogicalExtent() &&
            baseLayout.layout->verticalFovDegrees() ==
                changedLayout.layout->verticalFovDegrees() &&
            baseLayout.layout->horizontalFovDegrees() ==
                changedLayout.layout->horizontalFovDegrees() &&
            baseLayout.layout->uiViewportInOutput() ==
                changedLayout.layout->uiViewportInOutput(),
        "diagnostics or visual profile changed layout policy");
}

void testFovSettingChangesOnlyProjectionLayout() {
    const RenderPresentationSettings base{
        .renderScalePercent = 150.0F,
        .scenePresentation =
            ScenePresentationMode::widescreenHorPlus,
        .visualProfile = VisualProfile::classic,
        .diagnosticsOverlayEnabled = false,
        .verticalFovAdjustmentDegrees = 0.0F,
    };
    auto changed = base;
    changed.verticalFovAdjustmentDegrees = 20.0F;

    const auto baseLayout =
        buildNativeRenderLayout(layoutConfig(base));
    const auto changedLayout =
        buildNativeRenderLayout(layoutConfig(changed));
    require(
        baseLayout.complete() && changedLayout.complete(),
        "valid vertical-FOV setting produced an invalid layout");
    require(
        baseLayout.layout->outputExtent() ==
                changedLayout.layout->outputExtent() &&
            baseLayout.layout->renderTargetExtent() ==
                changedLayout.layout->renderTargetExtent() &&
            baseLayout.layout->sceneViewportInRenderTarget() ==
                changedLayout.layout->
                    sceneViewportInRenderTarget() &&
            baseLayout.layout->uiViewportInOutput() ==
                changedLayout.layout->uiViewportInOutput(),
        "vertical-FOV setting changed raster targets, viewport, or UI");
    require(
        changedLayout.layout->verticalFovAdjustmentDegrees() ==
                20.0F &&
            changedLayout.layout->verticalFovDegrees() >
                baseLayout.layout->verticalFovDegrees() &&
            changedLayout.layout->horizontalFovDegrees() >
                baseLayout.layout->horizontalFovDegrees() &&
            changedLayout.layout->cameraLogicalExtent().height >
                baseLayout.layout->cameraLogicalExtent().height,
        "vertical-FOV setting did not widen the projection");
}

void testStorageNeutralRoundTrip() {
    const RenderPresentationSettings settings{
        .renderScalePercent = 87.5F,
        .scenePresentation =
            ScenePresentationMode::originalFourByThree,
        .visualProfile = VisualProfile::enhanced,
        .diagnosticsOverlayEnabled = true,
        .verticalFovAdjustmentDegrees = 12.0F,
    };
    const auto recordResult =
        makeRenderPresentationSettingsRecord(settings);
    require(
        recordResult.complete() &&
            recordResult.record.has_value() &&
            recordResult.record->schemaVersion ==
                renderPresentationSettingsRecordSchemaVersion &&
            recordResult.record->renderScalePercent == 87.5F &&
            recordResult.record->scenePresentation == 1U &&
            recordResult.record->visualProfile == 1U &&
            recordResult.record->diagnosticsOverlayEnabled == 1U &&
            recordResult.record->verticalFovAdjustmentDegrees ==
                12.0F,
        "settings did not produce the versioned semantic record");

    const auto restored =
        renderPresentationSettingsFromRecord(
            *recordResult.record);
    require(
        restored.complete() && restored.settings == settings,
        "versioned settings record did not round-trip");
}

void testInvalidStorageRecordsFailClosed() {
    const RenderPresentationSettingsRecord baseline;

    for (const std::uint32_t schema : std::array{
             0U,
             renderPresentationSettingsRecordSchemaVersion + 1U}) {
        auto record = baseline;
        record.schemaVersion = schema;
        const auto restored =
            renderPresentationSettingsFromRecord(record);
        require(
            !restored.complete() &&
                !restored.settings.has_value(),
            "unknown settings schema produced settings");
        requireIssue(
            restored.issue,
            RenderPresentationSettingsIssueKind::unsupportedSchema,
            "unknown settings schema reported the wrong issue");
    }

    auto record = baseline;
    record.diagnosticsOverlayEnabled = 2U;
    auto restored =
        renderPresentationSettingsFromRecord(record);
    require(
        !restored.complete() && !restored.settings.has_value(),
        "invalid stored bool produced settings");
    requireIssue(
        restored.issue,
        RenderPresentationSettingsIssueKind::
            invalidStoredDiagnosticsValue,
        "invalid stored bool reported the wrong issue");

    record = baseline;
    record.scenePresentation = 2U;
    restored = renderPresentationSettingsFromRecord(record);
    require(
        !restored.complete() && !restored.settings.has_value(),
        "invalid stored presentation produced settings");
    requireIssue(
        restored.issue,
        RenderPresentationSettingsIssueKind::
            unsupportedScenePresentation,
        "invalid stored presentation reported the wrong issue");

    record = baseline;
    record.visualProfile = 2U;
    restored = renderPresentationSettingsFromRecord(record);
    require(
        !restored.complete() && !restored.settings.has_value(),
        "invalid stored visual profile produced settings");
    requireIssue(
        restored.issue,
        RenderPresentationSettingsIssueKind::unsupportedVisualProfile,
        "invalid stored visual profile reported the wrong issue");

    struct StoredScaleCase final {
        float value;
        RenderPresentationSettingsIssueKind issue;
    };
    const std::array storedScaleCases{
        StoredScaleCase{
            std::numeric_limits<float>::quiet_NaN(),
            RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
        },
        StoredScaleCase{
            std::numeric_limits<float>::infinity(),
            RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
        },
        StoredScaleCase{
            49.0F,
            RenderPresentationSettingsIssueKind::renderScaleOutOfRange,
        },
        StoredScaleCase{
            201.0F,
            RenderPresentationSettingsIssueKind::renderScaleOutOfRange,
        },
    };
    for (const auto& test : storedScaleCases) {
        record = baseline;
        record.renderScalePercent = test.value;
        restored = renderPresentationSettingsFromRecord(record);
        require(
            !restored.complete() && !restored.settings.has_value(),
            "invalid stored render scale produced settings");
        requireIssue(
            restored.issue, test.issue,
            "invalid stored render scale reported the wrong issue");
    }

    struct StoredFovCase final {
        float value;
        RenderPresentationSettingsIssueKind issue;
    };
    const std::array storedFovCases{
        StoredFovCase{
            std::numeric_limits<float>::quiet_NaN(),
            RenderPresentationSettingsIssueKind::
                nonFiniteVerticalFovAdjustment,
        },
        StoredFovCase{
            -1.0F,
            RenderPresentationSettingsIssueKind::
                verticalFovAdjustmentOutOfRange,
        },
        StoredFovCase{
            26.0F,
            RenderPresentationSettingsIssueKind::
                verticalFovAdjustmentOutOfRange,
        },
    };
    for (const auto& test : storedFovCases) {
        record = baseline;
        record.verticalFovAdjustmentDegrees = test.value;
        restored = renderPresentationSettingsFromRecord(record);
        require(
            !restored.complete() && !restored.settings.has_value(),
            "invalid stored vertical-FOV adjustment produced settings");
        requireIssue(
            restored.issue, test.issue,
            "invalid stored vertical-FOV adjustment reported the wrong issue");
    }
}

} // namespace

int main() {
    try {
        testDefaultsAndBoundaries();
        testInvalidSettingsAndAtomicResolve();
        testInvalidBaseIsRejectedBeforeOverrides();
        testPartialOverridesPreserveOtherValues();
        testSettingsDelta();
        testInvalidSettingsProduceNoDelta();
        testDiagnosticsAndProfileAreLayoutOrthogonal();
        testFovSettingChangesOnlyProjectionLayout();
        testStorageNeutralRoundTrip();
        testInvalidStorageRecordsFailClosed();
        std::cout << "Render presentation settings tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Render presentation settings tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
