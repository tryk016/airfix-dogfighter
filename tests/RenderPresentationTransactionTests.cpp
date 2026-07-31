#include "airfix/render/RenderPresentationTransaction.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace airfix::render;

static_assert(noexcept(
    std::declval<const RenderPresentationTransaction&>()
        .captureForPreparation()));

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireIssue(
    const std::optional<RenderPresentationTransactionIssue>& issue,
    const RenderPresentationTransactionIssueKind expected,
    const char* const context) {
    require(
        issue.has_value() && issue->kind == expected,
        std::string(context) + " reported the wrong issue");
}

void requirePrepareIssue(
    const PrepareRenderPresentationStateResult& result,
    const RenderPresentationTransactionIssueKind expected,
    const char* const context) {
    require(
        !result.complete() && !result.prepared.has_value(),
        std::string(context) + " unexpectedly prepared a candidate");
    requireIssue(result.issue, expected, context);
}

struct IdentityToken final {
    std::uint64_t value{};
};

struct OwnerTracker final {
    std::size_t liveOwners{};
    std::size_t liveAccountedBytes{};
    std::size_t constructions{};
    std::size_t destructions{};
};

struct FakeTargetOwner final {
    FakeTargetOwner(
        OwnerTracker& ownerTracker,
        const std::size_t bytes,
        const std::uint64_t serial) noexcept
        : tracker(&ownerTracker),
          accountedBytes(bytes),
          color{serial * 2U},
          depth{serial * 2U + 1U} {
        ++tracker->liveOwners;
        tracker->liveAccountedBytes += accountedBytes;
        ++tracker->constructions;
    }

    ~FakeTargetOwner() {
        --tracker->liveOwners;
        tracker->liveAccountedBytes -= accountedBytes;
        ++tracker->destructions;
    }

    OwnerTracker* tracker;
    std::size_t accountedBytes;
    IdentityToken color;
    IdentityToken depth;
};

enum class FakeFactoryResult {
    complete,
    emptyAfterLateOwner,
    missingDepth,
    sameIdentities,
    underAccounted,
};

struct FakeTargetFactory final {
    OwnerTracker tracker;
    FakeFactoryResult result{FakeFactoryResult::complete};
    std::uint32_t remainingLateFailures{};
    std::size_t attempts{};
    std::uint64_t nextSerial{1U};
    const void* lastDeviceIdentity{};
    RenderTargetPixelExtent lastExtent{};
    std::size_t lastMinimumAccountedBytes{};

    [[nodiscard]] RenderPresentationTargetFactory seam() noexcept {
        return {
            .callback = prepare,
            .context = this,
        };
    }

    static RenderPresentationTargetBundle prepare(
        void* const context,
        const RenderPresentationSurfaceStamp& surface,
        const RenderTargetPixelExtent targetExtent,
        const std::size_t minimumAccountedBytes) noexcept {
        auto& factory = *static_cast<FakeTargetFactory*>(context);
        ++factory.attempts;
        factory.lastDeviceIdentity = surface.deviceIdentity;
        factory.lastExtent = targetExtent;
        factory.lastMinimumAccountedBytes = minimumAccountedBytes;
        try {
            const auto reportedBytes =
                factory.result == FakeFactoryResult::underAccounted
                ? minimumAccountedBytes - 1U
                : minimumAccountedBytes;
            auto owner = std::make_shared<FakeTargetOwner>(
                factory.tracker,
                reportedBytes,
                factory.nextSerial++);

            if (factory.remainingLateFailures != 0U) {
                --factory.remainingLateFailures;
                return {};
            }
            if (factory.result ==
                FakeFactoryResult::emptyAfterLateOwner) {
                return {};
            }

            std::shared_ptr<const void> opaqueOwner = owner;
            const void* const color = &owner->color;
            const void* depth = &owner->depth;
            if (factory.result == FakeFactoryResult::missingDepth) {
                depth = nullptr;
            } else if (
                factory.result == FakeFactoryResult::sameIdentities) {
                depth = color;
            }
            return {
                std::move(opaqueOwner),
                color,
                depth,
                reportedBytes,
            };
        } catch (...) {
            return {};
        }
    }
};

struct SurfaceIdentities final {
    IdentityToken viewA{1U};
    IdentityToken viewB{2U};
    IdentityToken deviceA{3U};
    IdentityToken deviceB{4U};
};

[[nodiscard]] RenderPresentationSurfaceStamp surface(
    SurfaceIdentities& identities,
    const OutputPixelExtent extent = {800U, 600U},
    const std::uint64_t generation = 1U) noexcept {
    return {
        .viewIdentity = &identities.viewA,
        .deviceIdentity = &identities.deviceA,
        .outputExtent = extent,
        .generation = generation,
    };
}

[[nodiscard]] const RenderPresentationActiveState& requireActive(
    const RenderPresentationTransaction& transaction,
    const char* const context) {
    const auto* const active = transaction.activeState();
    require(active != nullptr, std::string(context) + " has no active state");
    return *active;
}

void commit(
    RenderPresentationTransaction& transaction,
    PrepareRenderPresentationStateResult result,
    const RenderPresentationSurfaceStamp& currentSurface,
    const char* const context) {
    require(
        result.complete(),
        std::string(context) + " did not prepare");
    require(
        !transaction
             .finalValidate(*result.prepared, currentSurface)
             .has_value(),
        std::string(context) + " failed final validation");
    transaction.commitValidated(std::move(*result.prepared));
}

[[nodiscard]] const void* colorIdentity(
    const RenderPresentationActiveState& state) {
    return state.targetBundle().has_value()
        ? state.targetBundle()->colorIdentity()
        : nullptr;
}

[[nodiscard]] const void* depthIdentity(
    const RenderPresentationActiveState& state) {
    return state.targetBundle().has_value()
        ? state.targetBundle()->depthIdentity()
        : nullptr;
}

void testTransitionsAndExactIdentities() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction transaction;
    auto currentSurface = surface(identities);
    RenderPresentationSettings settings;

    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "initial 100 percent state");
    const auto& active100 =
        requireActive(transaction, "initial state");
    require(
        active100.revision() == 1U &&
            active100.targetExtent() ==
                RenderTargetPixelExtent{800U, 600U} &&
            !active100.targetBundle().has_value() &&
            factory.attempts == 0U,
        "100 percent allocated or published a scaled bundle");

    settings.renderScalePercent = 50.0F;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "50 percent state");
    const auto& active50 = requireActive(transaction, "50 percent state");
    const void* const color50 = colorIdentity(active50);
    const void* const depth50 = depthIdentity(active50);
    require(
        active50.targetExtent() ==
                RenderTargetPixelExtent{400U, 300U} &&
            active50.targetBundle()->complete() &&
            color50 != nullptr && depth50 != nullptr &&
            color50 != depth50 &&
            factory.tracker.liveOwners == 1U,
        "50 percent did not publish one complete bundle");

    settings.renderScalePercent = 200.0F;
    auto prepared200 =
        transaction.prepare(settings, currentSurface, factory.seam());
    require(
        prepared200.complete() &&
            prepared200.prepared->targetExtent() ==
                RenderTargetPixelExtent{1600U, 1200U} &&
            prepared200.prepared->targetBundle()->colorIdentity() !=
                color50 &&
            colorIdentity(requireActive(transaction, "pre-200 active")) ==
                color50,
        "200 percent preparation mutated or reused the 50 percent bundle");
    commit(
        transaction,
        std::move(prepared200),
        currentSurface,
        "200 percent state");
    const auto& active200 =
        requireActive(transaction, "200 percent state");
    const void* const color200 = colorIdentity(active200);
    const void* const depth200 = depthIdentity(active200);
    require(
        color200 != color50 && depth200 != depth50 &&
            factory.tracker.liveOwners == 1U,
        "200 percent did not replace the exact target pair");

    settings.renderScalePercent = 100.0F;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "final 100 percent state");
    const auto& final100 = requireActive(transaction, "final state");
    require(
        final100.revision() == 4U &&
            !final100.targetBundle().has_value() &&
            factory.tracker.liveOwners == 0U &&
            factory.attempts == 2U,
        "100 to 50 to 200 to 100 lifecycle leaked a target owner");
}

void testOrthogonalSettingsReuseBundle() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction transaction;
    const auto currentSurface =
        surface(identities, OutputPixelExtent{800U, 450U});
    RenderPresentationSettings settings;
    settings.renderScalePercent = 50.0F;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "orthogonal baseline");
    const auto oldColor =
        colorIdentity(requireActive(transaction, "orthogonal baseline"));
    const auto oldDepth =
        depthIdentity(requireActive(transaction, "orthogonal baseline"));
    const auto attempts = factory.attempts;

    settings.scenePresentation =
        ScenePresentationMode::originalFourByThree;
    settings.visualProfile = VisualProfile::enhanced;
    settings.diagnosticsOverlayEnabled = true;
    settings.verticalFovAdjustmentDegrees = 20.0F;
    auto prepared =
        transaction.prepare(settings, currentSurface, factory.seam());
    require(
        prepared.complete() &&
            prepared.prepared->targetBundle()->colorIdentity() ==
                oldColor &&
            prepared.prepared->targetBundle()->depthIdentity() ==
                oldDepth &&
            factory.attempts == attempts,
        "orthogonal settings allocated a replacement target bundle");
    commit(
        transaction,
        std::move(prepared),
        currentSurface,
        "orthogonal settings");
    const auto& active = requireActive(transaction, "orthogonal settings");
    const auto layout = buildNativeRenderLayout({
        .outputExtent = currentSurface.outputExtent,
        .renderScalePercent = settings.renderScalePercent,
        .scenePresentation = settings.scenePresentation,
        .verticalFovAdjustmentDegrees =
            settings.verticalFovAdjustmentDegrees,
    });
    require(
        active.settings() == settings &&
            colorIdentity(active) == oldColor &&
            depthIdentity(active) == oldDepth &&
            layout.complete() &&
            layout.layout->sceneViewportInRenderTarget() ==
                RenderTargetPixelRect{50.0F, 0.0F, 300.0F, 225.0F} &&
            layout.layout->verticalFovDegrees() > 73.0F,
        "layout-only settings changed target ownership or omitted FOV");
}

void testLateFailuresRollbackAndRetry() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction transaction;
    const auto currentSurface = surface(identities);
    RenderPresentationSettings settings;
    settings.renderScalePercent = 50.0F;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "late-failure baseline");
    const auto& baseline =
        requireActive(transaction, "late-failure baseline");
    const auto oldSettings = baseline.settings();
    const auto oldColor = colorIdentity(baseline);
    const auto oldDepth = depthIdentity(baseline);
    const auto oldBytes = factory.tracker.liveAccountedBytes;

    settings.renderScalePercent = 200.0F;
    factory.remainingLateFailures = 3U;
    for (std::uint32_t failure = 0U; failure < 3U; ++failure) {
        const auto rejected =
            transaction.prepare(settings, currentSurface, factory.seam());
        requirePrepareIssue(
            rejected,
            RenderPresentationTransactionIssueKind::
                targetPreparationFailed,
            "late target failure");
        const auto& active =
            requireActive(transaction, "late target failure");
        require(
            active.settings() == oldSettings &&
                colorIdentity(active) == oldColor &&
                depthIdentity(active) == oldDepth &&
                factory.tracker.liveOwners == 1U &&
                factory.tracker.liveAccountedBytes == oldBytes,
            "late failure leaked its owner or mutated active state");
    }

    const auto retry =
        transaction.prepare(settings, currentSurface, factory.seam());
    require(
        retry.complete() &&
            retry.prepared->targetBundle()->colorIdentity() != oldColor &&
            factory.tracker.liveOwners == 2U,
        "retry did not retain a complete candidate beside active");
    commit(transaction, retry, currentSurface, "late-failure retry");
    require(
        factory.tracker.liveOwners == 1U &&
            colorIdentity(requireActive(transaction, "retry result")) !=
                oldColor,
        "retry did not atomically replace and release the old owner");
}

void testStaleCandidatesPreserveActiveState() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction transaction;
    const auto currentSurface = surface(identities);
    RenderPresentationSettings activeSettings;
    activeSettings.renderScalePercent = 50.0F;
    commit(
        transaction,
        transaction.prepare(
            activeSettings, currentSurface, factory.seam()),
        currentSurface,
        "stale baseline");
    const auto oldColor =
        colorIdentity(requireActive(transaction, "stale baseline"));
    const auto oldDepth =
        depthIdentity(requireActive(transaction, "stale baseline"));
    const auto oldRevision =
        requireActive(transaction, "stale baseline").revision();

    auto candidateSettings = activeSettings;
    candidateSettings.renderScalePercent = 200.0F;
    auto candidate =
        transaction.prepare(
            candidateSettings, currentSurface, factory.seam());
    require(candidate.complete(), "stale candidate was not prepared");

    auto changedSurface = currentSurface;
    changedSurface.viewIdentity = &identities.viewB;
    requireIssue(
        transaction.finalValidate(*candidate.prepared, changedSurface),
        RenderPresentationTransactionIssueKind::staleSurfaceView,
        "stale view");
    changedSurface = currentSurface;
    changedSurface.deviceIdentity = &identities.deviceB;
    requireIssue(
        transaction.finalValidate(*candidate.prepared, changedSurface),
        RenderPresentationTransactionIssueKind::staleSurfaceDevice,
        "stale device");
    changedSurface = currentSurface;
    changedSurface.outputExtent = {801U, 600U};
    requireIssue(
        transaction.finalValidate(*candidate.prepared, changedSurface),
        RenderPresentationTransactionIssueKind::staleSurfaceExtent,
        "stale extent");
    changedSurface = currentSurface;
    ++changedSurface.generation;
    requireIssue(
        transaction.finalValidate(*candidate.prepared, changedSurface),
        RenderPresentationTransactionIssueKind::staleSurfaceGeneration,
        "stale generation");

    auto orthogonal = activeSettings;
    orthogonal.visualProfile = VisualProfile::enhanced;
    commit(
        transaction,
        transaction.prepare(orthogonal, currentSurface, factory.seam()),
        currentSurface,
        "revision competitor");
    requireIssue(
        transaction.finalValidate(*candidate.prepared, currentSurface),
        RenderPresentationTransactionIssueKind::staleActiveRevision,
        "stale revision");
    const auto& active = requireActive(transaction, "stale result");
    require(
        active.revision() == oldRevision + 1U &&
            colorIdentity(active) == oldColor &&
            depthIdentity(active) == oldDepth &&
            active.settings() == orthogonal,
        "stale validation changed the active target pair");
}

void testCapturedPreparationBaselineIsIndependent() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction live;
    const auto currentSurface = surface(identities);
    RenderPresentationSettings baseline;
    baseline.renderScalePercent = 50.0F;
    commit(
        live,
        live.prepare(baseline, currentSurface, factory.seam()),
        currentSurface,
        "capture baseline");

    auto captured = live.captureForPreparation();
    auto candidate = baseline;
    candidate.renderScalePercent = 200.0F;
    auto prepared =
        captured.prepare(candidate, currentSurface, factory.seam());
    require(prepared.complete(),
            "captured transaction did not prepare independently");

    auto competitor = baseline;
    competitor.visualProfile = VisualProfile::enhanced;
    commit(
        live,
        live.prepare(competitor, currentSurface, factory.seam()),
        currentSurface,
        "capture competitor");
    requireIssue(
        live.finalValidate(*prepared.prepared, currentSurface),
        RenderPresentationTransactionIssueKind::staleActiveRevision,
        "captured stale revision");
    require(
        requireActive(live, "captured stale result").settings() ==
            competitor,
        "captured preparation mutated the live transaction");
}

void testDeviceChangeNeverReusesTargetBundle() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction transaction;
    auto currentSurface = surface(identities);
    RenderPresentationSettings settings;
    settings.renderScalePercent = 50.0F;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "device baseline");
    const auto oldColor =
        colorIdentity(requireActive(transaction, "device baseline"));

    currentSurface.deviceIdentity = &identities.deviceB;
    ++currentSurface.generation;
    auto changedDevice =
        transaction.prepare(settings, currentSurface, factory.seam());
    require(
        changedDevice.complete() &&
            changedDevice.prepared->targetBundle()->colorIdentity() !=
                oldColor &&
            factory.attempts == 2U,
        "same-extent device change reused a foreign target bundle");
    commit(
        transaction,
        std::move(changedDevice),
        currentSurface,
        "device change");
}

void testResizeAndZeroExtent() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction transaction;
    auto currentSurface = surface(identities);
    RenderPresentationSettings settings;
    settings.renderScalePercent = 50.0F;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "resize baseline");
    auto oldColor =
        colorIdentity(requireActive(transaction, "resize baseline"));

    currentSurface.outputExtent = {1000U, 500U};
    ++currentSurface.generation;
    auto resized =
        transaction.prepare(settings, currentSurface, factory.seam());
    require(
        resized.complete() &&
            resized.prepared->targetExtent() ==
                RenderTargetPixelExtent{500U, 250U} &&
            resized.prepared->targetBundle()->colorIdentity() != oldColor,
        "50 percent resize reused an incompatible target");
    commit(transaction, resized, currentSurface, "50 percent resize");
    oldColor =
        colorIdentity(requireActive(transaction, "50 percent resize"));

    settings.renderScalePercent = 200.0F;
    auto resized200 =
        transaction.prepare(settings, currentSurface, factory.seam());
    require(
        resized200.complete() &&
            resized200.prepared->targetExtent() ==
                RenderTargetPixelExtent{2000U, 1000U} &&
            resized200.prepared->targetBundle()->colorIdentity() !=
                oldColor,
        "200 percent resize did not prepare its exact extent");
    commit(transaction, resized200, currentSurface, "200 percent resize");
    const auto finalColor =
        colorIdentity(requireActive(transaction, "200 percent resize"));
    const auto finalSettings =
        requireActive(transaction, "200 percent resize").settings();

    RenderPresentationRetrySchedule retry;
    retry.recordExplicitFailure();
    require(retry.beginFrameRetry(), "zero reset setup did not retry");
    retry.recordAutomaticFailure();
    auto zeroSurface = currentSurface;
    zeroSurface.outputExtent = {};
    const auto zero =
        transaction.prepare(settings, zeroSurface, factory.seam());
    requirePrepareIssue(
        zero,
        RenderPresentationTransactionIssueKind::
            surfaceExtentNotPositive,
        "zero extent");
    retry.resetForZeroExtent();
    require(
        !retry.pending() &&
            requireActive(transaction, "zero extent").settings() ==
                finalSettings &&
            colorIdentity(requireActive(transaction, "zero extent")) ==
                finalColor,
        "zero extent changed active state or retained retry state");

    ++currentSurface.generation;
    retry.resetForExplicitResize();
    auto recovered =
        transaction.prepare(settings, currentSurface, factory.seam());
    require(
        recovered.complete() &&
            recovered.prepared->targetBundle()->colorIdentity() ==
                finalColor,
        "same-extent recovery did not reuse the complete bundle");
    commit(transaction, recovered, currentSurface, "zero recovery");
    retry.recordSuccess();
    require(!retry.pending(), "success did not reset retry state");
}

void testRetryBackoffHasNoHotLoop() {
    RenderPresentationRetrySchedule retry;
    retry.recordExplicitFailure();
    require(
        retry.pending() &&
            retry.delayFramesRemaining() == 0U &&
            retry.beginFrameRetry() &&
            !retry.beginFrameRetry(),
        "explicit failure did not schedule exactly one immediate retry");

    retry.recordAutomaticFailure();
    require(
        retry.delayFramesRemaining() == 1U &&
            !retry.beginFrameRetry() &&
            retry.delayFramesRemaining() == 0U &&
            retry.beginFrameRetry(),
        "first automatic failure did not skip exactly one frame");
    retry.recordAutomaticFailure();
    require(
        retry.delayFramesRemaining() == 2U &&
            !retry.beginFrameRetry() &&
            !retry.beginFrameRetry() &&
            retry.beginFrameRetry(),
        "second automatic failure did not skip exactly two frames");

    constexpr std::array<std::uint32_t, 8U> expectedDelays{
        4U, 8U, 16U, 32U, 64U, 120U, 120U, 120U,
    };
    for (const auto expectedDelay : expectedDelays) {
        retry.recordAutomaticFailure();
        require(
            retry.delayFramesRemaining() == expectedDelay,
            "backoff did not publish the exact saturated sequence");
        while (retry.delayFramesRemaining() != 0U) {
            require(
                !retry.beginFrameRetry(),
                "backoff attempted during a skipped frame");
        }
        require(
            retry.beginFrameRetry(),
            "backoff did not eventually permit a retry");
    }
    retry.recordAutomaticFailure();
    require(
        retry.delayFramesRemaining() == 120U,
        "exponential backoff did not saturate at 120 frames");

    retry.resetForExplicitResize();
    require(
        !retry.pending() && !retry.beginFrameRetry(),
        "explicit resize did not reset pending backoff");
    retry.recordExplicitFailure();
    require(retry.beginFrameRetry(), "success reset setup failed");
    retry.recordSuccess();
    require(
        !retry.pending() && !retry.attemptOutstanding(),
        "success did not reset retry state");
}

void testOwnerLifetimeAndInFlightCopies() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction transaction;
    const auto currentSurface = surface(identities);
    RenderPresentationSettings settings;
    settings.renderScalePercent = 50.0F;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "in-flight baseline");
    require(
        factory.tracker.liveOwners == 1U,
        "baseline owner count is wrong");

    {
        const RenderPresentationActiveState inFlight =
            requireActive(transaction, "in-flight baseline");
        const auto oldColor = colorIdentity(inFlight);
        const auto oldBytes = inFlight.targetBundle()->accountedBytes();
        settings.renderScalePercent = 200.0F;
        commit(
            transaction,
            transaction.prepare(
                settings, currentSurface, factory.seam()),
            currentSurface,
            "in-flight replacement");
        require(
            factory.tracker.liveOwners == 2U &&
                factory.tracker.liveAccountedBytes ==
                    oldBytes +
                    requireActive(transaction, "replacement")
                        .targetBundle()
                        ->accountedBytes() &&
                colorIdentity(inFlight) == oldColor &&
                colorIdentity(
                    requireActive(transaction, "replacement")) !=
                    oldColor,
            "in-flight copy did not retain exact old owner and accounting");
    }
    require(
        factory.tracker.liveOwners == 1U,
        "retired owner survived its final in-flight copy");

    const RenderPresentationTargetBundle bundleCopy =
        *requireActive(transaction, "bundle copy").targetBundle();
    settings.renderScalePercent = 100.0F;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "direct state with in-flight bundle");
    require(
        factory.tracker.liveOwners == 1U &&
            bundleCopy.complete(),
        "direct publication prematurely released an in-flight bundle");
}

void testInvalidInputsAndBundleContracts() {
    SurfaceIdentities identities;
    FakeTargetFactory factory;
    RenderPresentationTransaction transaction;
    auto currentSurface = surface(identities);
    RenderPresentationSettings settings;

    auto invalidSettings = settings;
    invalidSettings.renderScalePercent =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalid =
        transaction.prepare(
            invalidSettings, currentSurface, factory.seam());
    requirePrepareIssue(
        invalid,
        RenderPresentationTransactionIssueKind::invalidSettings,
        "invalid settings");
    require(
        invalid.issue->settingsIssue ==
            RenderPresentationSettingsIssueKind::
                nonFiniteRenderScale,
        "invalid settings lost their typed detail");

    auto invalidIdentity = currentSurface;
    invalidIdentity.deviceIdentity = nullptr;
    requirePrepareIssue(
        transaction.prepare(
            settings, invalidIdentity, factory.seam()),
        RenderPresentationTransactionIssueKind::
            invalidSurfaceIdentity,
        "invalid surface identity");

    auto invalidLayoutSurface = currentSurface;
    invalidLayoutSurface.outputExtent = {
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
    };
    auto oversized = settings;
    oversized.renderScalePercent = 200.0F;
    const auto invalidLayout =
        transaction.prepare(
            oversized, invalidLayoutSurface, factory.seam());
    requirePrepareIssue(
        invalidLayout,
        RenderPresentationTransactionIssueKind::invalidLayout,
        "invalid layout");
    require(
        invalidLayout.issue->layoutIssue ==
            NativeRenderLayoutBuildIssueKind::
                renderTargetExtentOverflow,
        "invalid layout lost its typed detail");

    auto accountingOverflowSurface = currentSurface;
    accountingOverflowSurface.outputExtent = {
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
    };
    auto accountingOverflowSettings = settings;
    accountingOverflowSettings.renderScalePercent = 50.0F;
    const auto attemptsBeforeOverflow = factory.attempts;
    requirePrepareIssue(
        transaction.prepare(
            accountingOverflowSettings,
            accountingOverflowSurface,
            factory.seam()),
        RenderPresentationTransactionIssueKind::
            targetAccountingOverflow,
        "target accounting overflow");
    require(
        factory.attempts == attemptsBeforeOverflow,
        "target accounting overflow consulted the factory");

    settings.renderScalePercent = 50.0F;
    requirePrepareIssue(
        transaction.prepare(settings, currentSurface, {}),
        RenderPresentationTransactionIssueKind::
            targetFactoryUnavailable,
        "missing target factory");

    factory.result = FakeFactoryResult::emptyAfterLateOwner;
    requirePrepareIssue(
        transaction.prepare(
            settings, currentSurface, factory.seam()),
        RenderPresentationTransactionIssueKind::
            targetPreparationFailed,
        "empty target result");
    require(
        factory.tracker.liveOwners == 0U,
        "empty target result leaked its late owner");

    factory.result = FakeFactoryResult::missingDepth;
    requirePrepareIssue(
        transaction.prepare(
            settings, currentSurface, factory.seam()),
        RenderPresentationTransactionIssueKind::
            incompleteTargetBundle,
        "missing depth identity");
    require(
        factory.tracker.liveOwners == 0U,
        "incomplete bundle leaked its owner");

    factory.result = FakeFactoryResult::sameIdentities;
    requirePrepareIssue(
        transaction.prepare(
            settings, currentSurface, factory.seam()),
        RenderPresentationTransactionIssueKind::
            incompleteTargetBundle,
        "aliased target identities");

    factory.result = FakeFactoryResult::underAccounted;
    requirePrepareIssue(
        transaction.prepare(
            settings, currentSurface, factory.seam()),
        RenderPresentationTransactionIssueKind::
            insufficientTargetAccounting,
        "under-accounted bundle");
    require(
        factory.tracker.liveOwners == 0U &&
            factory.lastMinimumAccountedBytes ==
                400U * 300U * 8U,
        "under-accounted bundle did not roll back exact bytes");

    factory.result = FakeFactoryResult::complete;
    settings.renderScalePercent = 100.0F;
    const auto attempts = factory.attempts;
    commit(
        transaction,
        transaction.prepare(settings, currentSurface, factory.seam()),
        currentSurface,
        "direct target contract");
    require(
        factory.attempts == attempts &&
            !requireActive(transaction, "direct target contract")
                 .targetBundle()
                 .has_value(),
        "100 percent consulted the target factory");

    auto prepared =
        transaction.prepare(
            RenderPresentationSettings{
                .renderScalePercent = 50.0F,
            },
            currentSurface,
            factory.seam());
    require(prepared.complete(), "invalid prepared-state setup failed");
    transaction.commitValidated(std::move(*prepared.prepared));
    requireIssue(
        transaction.finalValidate(*prepared.prepared, currentSurface),
        RenderPresentationTransactionIssueKind::
            invalidPreparedState,
        "moved prepared state");
}

} // namespace

int main() {
    try {
        testTransitionsAndExactIdentities();
        testOrthogonalSettingsReuseBundle();
        testLateFailuresRollbackAndRetry();
        testStaleCandidatesPreserveActiveState();
        testCapturedPreparationBaselineIsIndependent();
        testDeviceChangeNeverReusesTargetBundle();
        testResizeAndZeroExtent();
        testRetryBackoffHasNoHotLoop();
        testOwnerLifetimeAndInFlightCopies();
        testInvalidInputsAndBundleContracts();
        std::cout << "Render presentation transaction tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Render presentation transaction tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
