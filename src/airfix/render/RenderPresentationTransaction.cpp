#include "airfix/render/RenderPresentationTransaction.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace airfix::render {
namespace {

[[nodiscard]] constexpr RenderPresentationTransactionIssue issue(
    const RenderPresentationTransactionIssueKind kind) noexcept {
    return {
        .kind = kind,
        .settingsIssue = std::nullopt,
        .layoutIssue = std::nullopt,
    };
}

[[nodiscard]] bool minimumAccountedBytes(
    const RenderTargetPixelExtent extent,
    std::size_t& bytes) noexcept {
    constexpr std::size_t bytesPerColorAndDepthPixel = 8U;
    const auto width = static_cast<std::size_t>(extent.width);
    const auto height = static_cast<std::size_t>(extent.height);
    if (width == 0U || height == 0U ||
        width > std::numeric_limits<std::size_t>::max() / height) {
        return false;
    }
    const auto pixels = width * height;
    if (pixels >
        std::numeric_limits<std::size_t>::max() /
            bytesPerColorAndDepthPixel) {
        return false;
    }
    bytes = pixels * bytesPerColorAndDepthPixel;
    return true;
}

[[nodiscard]] bool usesScaledTarget(
    const RenderPresentationSettings& settings) noexcept {
    return settings.renderScalePercent !=
        native_render_policy::defaultRenderScalePercent;
}

[[nodiscard]] bool validPreparedState(
    const PreparedRenderPresentationState& prepared) noexcept {
    if (!prepared.surface().complete() ||
        prepared.revision() == 0U ||
        prepared.baseRevision() ==
            std::numeric_limits<std::uint64_t>::max() ||
        prepared.revision() != prepared.baseRevision() + 1U ||
        validateRenderPresentationSettings(prepared.settings()).has_value()) {
        return false;
    }

    const auto layout = buildNativeRenderLayout({
        .outputExtent = prepared.surface().outputExtent,
        .renderScalePercent =
            prepared.settings().renderScalePercent,
        .scenePresentation =
            prepared.settings().scenePresentation,
    });
    if (!layout.complete() ||
        layout.layout->renderTargetExtent() !=
            prepared.targetExtent()) {
        return false;
    }

    if (!usesScaledTarget(prepared.settings())) {
        return !prepared.targetBundle().has_value();
    }
    if (!prepared.targetBundle().has_value() ||
        !prepared.targetBundle()->complete()) {
        return false;
    }
    std::size_t minimumBytes = 0U;
    return minimumAccountedBytes(
               prepared.targetExtent(), minimumBytes) &&
        prepared.targetBundle()->accountedBytes() >= minimumBytes;
}

} // namespace

RenderPresentationTargetBundle::RenderPresentationTargetBundle(
    std::shared_ptr<const void> owner,
    const void* const colorIdentity,
    const void* const depthIdentity,
    const std::size_t accountedBytes) noexcept
    : owner_(std::move(owner)),
      colorIdentity_(colorIdentity),
      depthIdentity_(depthIdentity),
      accountedBytes_(accountedBytes) {}

bool RenderPresentationTargetBundle::empty() const noexcept {
    return owner_ == nullptr &&
        colorIdentity_ == nullptr &&
        depthIdentity_ == nullptr &&
        accountedBytes_ == 0U;
}

bool RenderPresentationTargetBundle::complete() const noexcept {
    return owner_ != nullptr &&
        colorIdentity_ != nullptr &&
        depthIdentity_ != nullptr &&
        colorIdentity_ != depthIdentity_ &&
        accountedBytes_ != 0U;
}

const std::shared_ptr<const void>&
RenderPresentationTargetBundle::owner() const noexcept {
    return owner_;
}

const void*
RenderPresentationTargetBundle::colorIdentity() const noexcept {
    return colorIdentity_;
}

const void*
RenderPresentationTargetBundle::depthIdentity() const noexcept {
    return depthIdentity_;
}

std::size_t
RenderPresentationTargetBundle::accountedBytes() const noexcept {
    return accountedBytes_;
}

RenderPresentationActiveState::RenderPresentationActiveState(
    RenderPresentationSettings settings,
    RenderPresentationSurfaceStamp surface,
    const std::uint64_t revision,
    const RenderTargetPixelExtent targetExtent,
    std::optional<RenderPresentationTargetBundle> targetBundle) noexcept
    : settings_(settings),
      surface_(surface),
      revision_(revision),
      targetExtent_(targetExtent),
      targetBundle_(std::move(targetBundle)) {}

const RenderPresentationSettings&
RenderPresentationActiveState::settings() const noexcept {
    return settings_;
}

const RenderPresentationSurfaceStamp&
RenderPresentationActiveState::surface() const noexcept {
    return surface_;
}

std::uint64_t
RenderPresentationActiveState::revision() const noexcept {
    return revision_;
}

RenderTargetPixelExtent
RenderPresentationActiveState::targetExtent() const noexcept {
    return targetExtent_;
}

const std::optional<RenderPresentationTargetBundle>&
RenderPresentationActiveState::targetBundle() const noexcept {
    return targetBundle_;
}

PreparedRenderPresentationState::PreparedRenderPresentationState(
    RenderPresentationSettings settings,
    RenderPresentationSurfaceStamp surface,
    const std::uint64_t baseRevision,
    const std::uint64_t revision,
    const RenderTargetPixelExtent targetExtent,
    std::optional<RenderPresentationTargetBundle> targetBundle) noexcept
    : settings_(settings),
      surface_(surface),
      baseRevision_(baseRevision),
      revision_(revision),
      targetExtent_(targetExtent),
      targetBundle_(std::move(targetBundle)) {}

const RenderPresentationSettings&
PreparedRenderPresentationState::settings() const noexcept {
    return settings_;
}

const RenderPresentationSurfaceStamp&
PreparedRenderPresentationState::surface() const noexcept {
    return surface_;
}

std::uint64_t
PreparedRenderPresentationState::baseRevision() const noexcept {
    return baseRevision_;
}

std::uint64_t
PreparedRenderPresentationState::revision() const noexcept {
    return revision_;
}

RenderTargetPixelExtent
PreparedRenderPresentationState::targetExtent() const noexcept {
    return targetExtent_;
}

const std::optional<RenderPresentationTargetBundle>&
PreparedRenderPresentationState::targetBundle() const noexcept {
    return targetBundle_;
}

const RenderPresentationActiveState*
RenderPresentationTransaction::activeState() const noexcept {
    return active_.has_value() ? &*active_ : nullptr;
}

RenderPresentationTransaction
RenderPresentationTransaction::captureForPreparation() const noexcept {
    return *this;
}

PrepareRenderPresentationStateResult
RenderPresentationTransaction::prepare(
    const RenderPresentationSettings& candidate,
    const RenderPresentationSurfaceStamp& surface,
    const RenderPresentationTargetFactory targetFactory) const noexcept {
    const auto settingsValidation =
        validateRenderPresentationSettings(candidate);
    if (settingsValidation.has_value()) {
        return {
            .prepared = std::nullopt,
            .issue =
                RenderPresentationTransactionIssue{
                    .kind =
                        RenderPresentationTransactionIssueKind::
                            invalidSettings,
                    .settingsIssue = settingsValidation->kind,
                    .layoutIssue = std::nullopt,
                },
        };
    }
    if (surface.viewIdentity == nullptr ||
        surface.deviceIdentity == nullptr) {
        return {
            .prepared = std::nullopt,
            .issue =
                issue(
                    RenderPresentationTransactionIssueKind::
                        invalidSurfaceIdentity),
        };
    }
    if (surface.outputExtent.width == 0U ||
        surface.outputExtent.height == 0U) {
        return {
            .prepared = std::nullopt,
            .issue =
                issue(
                    RenderPresentationTransactionIssueKind::
                        surfaceExtentNotPositive),
        };
    }

    const auto layout = buildNativeRenderLayout({
        .outputExtent = surface.outputExtent,
        .renderScalePercent = candidate.renderScalePercent,
        .scenePresentation = candidate.scenePresentation,
    });
    if (!layout.complete()) {
        return {
            .prepared = std::nullopt,
            .issue =
                RenderPresentationTransactionIssue{
                    .kind =
                        RenderPresentationTransactionIssueKind::
                            invalidLayout,
                    .settingsIssue = std::nullopt,
                    .layoutIssue = layout.issue->kind,
                },
        };
    }

    const std::uint64_t baseRevision =
        active_.has_value() ? active_->revision() : 0U;
    if (baseRevision == std::numeric_limits<std::uint64_t>::max()) {
        return {
            .prepared = std::nullopt,
            .issue =
                issue(
                    RenderPresentationTransactionIssueKind::
                        revisionOverflow),
        };
    }

    const auto targetExtent =
        layout.layout->renderTargetExtent();
    std::optional<RenderPresentationTargetBundle> targetBundle;
    if (usesScaledTarget(candidate)) {
        std::size_t minimumBytes = 0U;
        if (!minimumAccountedBytes(targetExtent, minimumBytes)) {
            return {
                .prepared = std::nullopt,
                .issue =
                    issue(
                        RenderPresentationTransactionIssueKind::
                            targetAccountingOverflow),
            };
        }

        const bool canReuse =
            active_.has_value() &&
            active_->surface().deviceIdentity ==
                surface.deviceIdentity &&
            active_->targetExtent() == targetExtent &&
            active_->targetBundle().has_value() &&
            active_->targetBundle()->complete() &&
            active_->targetBundle()->accountedBytes() >=
                minimumBytes;
        if (canReuse) {
            targetBundle = *active_->targetBundle();
        } else {
            if (targetFactory.callback == nullptr) {
                return {
                    .prepared = std::nullopt,
                    .issue =
                        issue(
                            RenderPresentationTransactionIssueKind::
                                targetFactoryUnavailable),
                };
            }
            auto preparedBundle = targetFactory.prepare(
                surface, targetExtent, minimumBytes);
            if (preparedBundle.empty()) {
                return {
                    .prepared = std::nullopt,
                    .issue =
                        issue(
                            RenderPresentationTransactionIssueKind::
                                targetPreparationFailed),
                };
            }
            if (!preparedBundle.complete()) {
                return {
                    .prepared = std::nullopt,
                    .issue =
                        issue(
                            RenderPresentationTransactionIssueKind::
                                incompleteTargetBundle),
                };
            }
            if (preparedBundle.accountedBytes() < minimumBytes) {
                return {
                    .prepared = std::nullopt,
                    .issue =
                        issue(
                            RenderPresentationTransactionIssueKind::
                                insufficientTargetAccounting),
                };
            }
            targetBundle = std::move(preparedBundle);
        }
    }

    PreparedRenderPresentationState prepared{
        candidate,
        surface,
        baseRevision,
        baseRevision + 1U,
        targetExtent,
        std::move(targetBundle),
    };
    return {
        .prepared = std::move(prepared),
        .issue = std::nullopt,
    };
}

std::optional<RenderPresentationTransactionIssue>
RenderPresentationTransaction::finalValidate(
    const PreparedRenderPresentationState& prepared,
    const RenderPresentationSurfaceStamp& currentSurface) const noexcept {
    if (!validPreparedState(prepared)) {
        return issue(
            RenderPresentationTransactionIssueKind::
                invalidPreparedState);
    }

    const std::uint64_t activeRevision =
        active_.has_value() ? active_->revision() : 0U;
    if (prepared.baseRevision() != activeRevision) {
        return issue(
            RenderPresentationTransactionIssueKind::
                staleActiveRevision);
    }
    if (prepared.surface().viewIdentity !=
        currentSurface.viewIdentity) {
        return issue(
            RenderPresentationTransactionIssueKind::
                staleSurfaceView);
    }
    if (prepared.surface().deviceIdentity !=
        currentSurface.deviceIdentity) {
        return issue(
            RenderPresentationTransactionIssueKind::
                staleSurfaceDevice);
    }
    if (prepared.surface().outputExtent !=
        currentSurface.outputExtent) {
        return issue(
            RenderPresentationTransactionIssueKind::
                staleSurfaceExtent);
    }
    if (prepared.surface().generation !=
        currentSurface.generation) {
        return issue(
            RenderPresentationTransactionIssueKind::
                staleSurfaceGeneration);
    }
    return std::nullopt;
}

void RenderPresentationTransaction::commitValidated(
    PreparedRenderPresentationState&& prepared) noexcept {
    active_ = RenderPresentationActiveState{
        prepared.settings_,
        prepared.surface_,
        prepared.revision_,
        prepared.targetExtent_,
        std::move(prepared.targetBundle_),
    };
}

void RenderPresentationRetrySchedule::reset() noexcept {
    delayFramesRemaining_ = 0U;
    automaticFailureCount_ = 0U;
    pending_ = false;
    attemptOutstanding_ = false;
}

void RenderPresentationRetrySchedule::resetForExplicitResize() noexcept {
    reset();
}

void RenderPresentationRetrySchedule::recordExplicitFailure() noexcept {
    reset();
    pending_ = true;
}

bool RenderPresentationRetrySchedule::beginFrameRetry() noexcept {
    if (!pending_ || attemptOutstanding_) {
        return false;
    }
    if (delayFramesRemaining_ != 0U) {
        --delayFramesRemaining_;
        return false;
    }
    attemptOutstanding_ = true;
    return true;
}

void RenderPresentationRetrySchedule::recordAutomaticFailure() noexcept {
    if (!pending_ || !attemptOutstanding_) {
        return;
    }
    attemptOutstanding_ = false;
    constexpr std::uint32_t maximumDelayFrames = 120U;
    constexpr std::uint32_t maximumShift = 7U;
    const auto shift =
        std::min(automaticFailureCount_, maximumShift);
    delayFramesRemaining_ =
        std::min(1U << shift, maximumDelayFrames);
    if (automaticFailureCount_ < maximumShift) {
        ++automaticFailureCount_;
    }
}

void RenderPresentationRetrySchedule::recordSuccess() noexcept {
    reset();
}

void RenderPresentationRetrySchedule::resetForZeroExtent() noexcept {
    reset();
}

bool RenderPresentationRetrySchedule::pending() const noexcept {
    return pending_;
}

bool RenderPresentationRetrySchedule::attemptOutstanding() const noexcept {
    return attemptOutstanding_;
}

std::uint32_t
RenderPresentationRetrySchedule::delayFramesRemaining() const noexcept {
    return delayFramesRemaining_;
}

} // namespace airfix::render
