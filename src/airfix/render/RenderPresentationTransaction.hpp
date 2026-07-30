#pragma once

#include "airfix/render/RenderPresentationSettings.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace airfix::render {

// Opaque identities are observed only for equality. Platform adapters remain
// responsible for retaining the view, device, and resources they identify.
struct RenderPresentationSurfaceStamp final {
    const void* viewIdentity{};
    const void* deviceIdentity{};
    OutputPixelExtent outputExtent{};
    std::uint64_t generation{};

    [[nodiscard]] constexpr bool complete() const noexcept {
        return viewIdentity != nullptr &&
            deviceIdentity != nullptr &&
            outputExtent.width != 0U &&
            outputExtent.height != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const RenderPresentationSurfaceStamp&,
        const RenderPresentationSurfaceStamp&) noexcept = default;
};

// Copying a bundle is the in-flight submission contract: the opaque owner and
// its accounting token remain alive until the last copy is destroyed.
class RenderPresentationTargetBundle final {
public:
    RenderPresentationTargetBundle() noexcept = default;

    RenderPresentationTargetBundle(
        std::shared_ptr<const void> owner,
        const void* colorIdentity,
        const void* depthIdentity,
        std::size_t accountedBytes) noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] const std::shared_ptr<const void>& owner() const noexcept;
    [[nodiscard]] const void* colorIdentity() const noexcept;
    [[nodiscard]] const void* depthIdentity() const noexcept;
    [[nodiscard]] std::size_t accountedBytes() const noexcept;

private:
    std::shared_ptr<const void> owner_;
    const void* colorIdentity_{};
    const void* depthIdentity_{};
    std::size_t accountedBytes_{};
};

// The function-pointer seam has no type-erased callable allocation. The
// callback may allocate platform resources, but must return without throwing.
struct RenderPresentationTargetFactory final {
    using Callback = RenderPresentationTargetBundle (*)(
        void* context,
        const RenderPresentationSurfaceStamp& surface,
        RenderTargetPixelExtent targetExtent,
        std::size_t minimumAccountedBytes) noexcept;

    Callback callback{};
    void* context{};

    [[nodiscard]] RenderPresentationTargetBundle prepare(
        const RenderPresentationSurfaceStamp& surface,
        RenderTargetPixelExtent targetExtent,
        std::size_t minimumAccountedBytes) const noexcept {
        return callback == nullptr
            ? RenderPresentationTargetBundle{}
            : callback(
                  context,
                  surface,
                  targetExtent,
                  minimumAccountedBytes);
    }
};

enum class RenderPresentationTransactionIssueKind : std::uint8_t {
    invalidSettings,
    invalidSurfaceIdentity,
    surfaceExtentNotPositive,
    invalidLayout,
    revisionOverflow,
    targetAccountingOverflow,
    targetFactoryUnavailable,
    targetPreparationFailed,
    incompleteTargetBundle,
    insufficientTargetAccounting,
    invalidPreparedState,
    staleActiveRevision,
    staleSurfaceView,
    staleSurfaceDevice,
    staleSurfaceExtent,
    staleSurfaceGeneration,
};

struct RenderPresentationTransactionIssue final {
    RenderPresentationTransactionIssueKind kind{
        RenderPresentationTransactionIssueKind::invalidSettings};
    std::optional<RenderPresentationSettingsIssueKind> settingsIssue;
    std::optional<NativeRenderLayoutBuildIssueKind> layoutIssue;
};

class RenderPresentationActiveState final {
public:
    RenderPresentationActiveState(
        const RenderPresentationActiveState&) noexcept = default;
    RenderPresentationActiveState(
        RenderPresentationActiveState&&) noexcept = default;
    RenderPresentationActiveState& operator=(
        const RenderPresentationActiveState&) noexcept = default;
    RenderPresentationActiveState& operator=(
        RenderPresentationActiveState&&) noexcept = default;

    [[nodiscard]] const RenderPresentationSettings& settings() const noexcept;
    [[nodiscard]] const RenderPresentationSurfaceStamp& surface() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] RenderTargetPixelExtent targetExtent() const noexcept;
    [[nodiscard]] const std::optional<RenderPresentationTargetBundle>&
    targetBundle() const noexcept;

private:
    friend class RenderPresentationTransaction;

    RenderPresentationActiveState(
        RenderPresentationSettings settings,
        RenderPresentationSurfaceStamp surface,
        std::uint64_t revision,
        RenderTargetPixelExtent targetExtent,
        std::optional<RenderPresentationTargetBundle> targetBundle) noexcept;

    RenderPresentationSettings settings_;
    RenderPresentationSurfaceStamp surface_;
    std::uint64_t revision_{};
    RenderTargetPixelExtent targetExtent_{};
    std::optional<RenderPresentationTargetBundle> targetBundle_;
};

class PreparedRenderPresentationState final {
public:
    PreparedRenderPresentationState(
        const PreparedRenderPresentationState&) noexcept = default;
    PreparedRenderPresentationState(
        PreparedRenderPresentationState&&) noexcept = default;
    PreparedRenderPresentationState& operator=(
        const PreparedRenderPresentationState&) noexcept = default;
    PreparedRenderPresentationState& operator=(
        PreparedRenderPresentationState&&) noexcept = default;

    [[nodiscard]] const RenderPresentationSettings& settings() const noexcept;
    [[nodiscard]] const RenderPresentationSurfaceStamp& surface() const noexcept;
    [[nodiscard]] std::uint64_t baseRevision() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] RenderTargetPixelExtent targetExtent() const noexcept;
    [[nodiscard]] const std::optional<RenderPresentationTargetBundle>&
    targetBundle() const noexcept;

private:
    friend class RenderPresentationTransaction;

    PreparedRenderPresentationState(
        RenderPresentationSettings settings,
        RenderPresentationSurfaceStamp surface,
        std::uint64_t baseRevision,
        std::uint64_t revision,
        RenderTargetPixelExtent targetExtent,
        std::optional<RenderPresentationTargetBundle> targetBundle) noexcept;

    RenderPresentationSettings settings_;
    RenderPresentationSurfaceStamp surface_;
    std::uint64_t baseRevision_{};
    std::uint64_t revision_{};
    RenderTargetPixelExtent targetExtent_{};
    std::optional<RenderPresentationTargetBundle> targetBundle_;
};

struct PrepareRenderPresentationStateResult final {
    std::optional<PreparedRenderPresentationState> prepared;
    std::optional<RenderPresentationTransactionIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return prepared.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

// Single-executor transaction owner. prepare and finalValidate are read-only;
// commitValidated is a no-fail move publication and requires the caller to
// invoke it immediately after a successful finalValidate, with no callback or
// dispatch between those operations.
class RenderPresentationTransaction final {
public:
    RenderPresentationTransaction() noexcept = default;

    [[nodiscard]] const RenderPresentationActiveState*
    activeState() const noexcept;

    // Captures the exact active baseline for transfer to one serialized
    // resource-preparation worker. The copy retains any reusable target owner;
    // finalValidate must still run against the live owner immediately before
    // commit so revision and surface changes are rejected.
    [[nodiscard]] RenderPresentationTransaction
    captureForPreparation() const noexcept;

    [[nodiscard]] PrepareRenderPresentationStateResult prepare(
        const RenderPresentationSettings& candidate,
        const RenderPresentationSurfaceStamp& surface,
        RenderPresentationTargetFactory targetFactory = {}) const noexcept;

    [[nodiscard]] std::optional<RenderPresentationTransactionIssue>
    finalValidate(
        const PreparedRenderPresentationState& prepared,
        const RenderPresentationSurfaceStamp& currentSurface) const noexcept;

    void commitValidated(
        PreparedRenderPresentationState&& prepared) noexcept;

private:
    std::optional<RenderPresentationActiveState> active_;
};

// Frame-counted retry state. There is no wall clock and at most one retry may
// be begun before recordAutomaticFailure or recordSuccess resolves it.
class RenderPresentationRetrySchedule final {
public:
    void resetForExplicitResize() noexcept;
    void recordExplicitFailure() noexcept;
    [[nodiscard]] bool beginFrameRetry() noexcept;
    void recordAutomaticFailure() noexcept;
    void recordSuccess() noexcept;
    void resetForZeroExtent() noexcept;

    [[nodiscard]] bool pending() const noexcept;
    [[nodiscard]] bool attemptOutstanding() const noexcept;
    [[nodiscard]] std::uint32_t delayFramesRemaining() const noexcept;

private:
    void reset() noexcept;

    std::uint32_t delayFramesRemaining_{};
    std::uint32_t automaticFailureCount_{};
    bool pending_{};
    bool attemptOutstanding_{};
};

} // namespace airfix::render
