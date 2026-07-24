#pragma once

#include "airfix/render/LegacyGeometry.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace airfix::render {

// A complete absolute runtime-world transform. No simulation, aircraft, or
// authored-parent semantics are implied by this transport type.
struct DynamicInstancePoseOverride {
    std::uint32_t instanceIndex{};
    Mat3 modelLinear{};
    Vec3 modelTranslation{};

    [[nodiscard]] friend constexpr bool operator==(
        const DynamicInstancePoseOverride&,
        const DynamicInstancePoseOverride&) = default;
};

struct ResolvedInstancePose {
    Mat3 modelLinear{};
    Vec3 modelTranslation{};

    [[nodiscard]] friend constexpr bool operator==(
        const ResolvedInstancePose&,
        const ResolvedInstancePose&) = default;
};

struct DynamicInstancePoseLimits {
    std::uint32_t maximumInstances{1'000'000U};
    std::size_t maximumOverrides{65'536U};
    // Counts only the contiguous override payload:
    // override count * sizeof(DynamicInstancePoseOverride).
    std::size_t maximumFrameBytes{8U * 1024U * 1024U};

    [[nodiscard]] friend constexpr bool operator==(
        const DynamicInstancePoseLimits&,
        const DynamicInstancePoseLimits&) = default;
};

struct DynamicInstancePoseFrameView {
    std::uint64_t simulationStep{};
    std::span<const DynamicInstancePoseOverride> overrides;
};

enum class DynamicInstancePosePublishResult : std::uint8_t {
    published,
    wrongOrStaleScene,
    simulationStepNotIncreasing,
    overrideLimitExceeded,
    frameByteLimitExceeded,
    instanceIndexOutOfRange,
    overridesNotStrictlyIncreasing,
    nonFiniteTransform,
    generationExhausted,
    busy,
};

namespace detail {
struct ScenePoseExchangeStorage;
}

class ScenePoseExchange;

// An opaque, move-only identity minted by exactly one ScenePoseExchange.
// It may be passed between the exchange's SPSC producer and consumer.
// Starting another scene or destroying the exchange makes older handles
// stale, even when another scene describes identical content. The private
// shared identity prevents allocator address reuse from reviving a handle.
class ScenePoseHandle final {
public:
    ScenePoseHandle(ScenePoseHandle&& other) noexcept;
    ScenePoseHandle& operator=(ScenePoseHandle&& other) noexcept;
    ~ScenePoseHandle() = default;

    ScenePoseHandle(const ScenePoseHandle&) = delete;
    ScenePoseHandle& operator=(const ScenePoseHandle&) = delete;

    [[nodiscard]] bool valid() const noexcept;

private:
    friend class ScenePoseExchange;

    ScenePoseHandle(
        std::shared_ptr<detail::ScenePoseExchangeStorage> storage,
        std::uint64_t sceneGeneration) noexcept;

    std::shared_ptr<detail::ScenePoseExchangeStorage> storage_;
    std::uint64_t sceneGeneration_{};
};

// A stable read-only view of one published slot. The lease is move-only and
// releases its slot on destruction. A lease may safely outlive the exchange;
// its private shared state keeps the slot alive. A span returned by overrides
// is valid only while that exact lease remains active and has not been moved.
class DynamicInstancePoseLease final {
public:
    DynamicInstancePoseLease(
        DynamicInstancePoseLease&& other) noexcept;
    DynamicInstancePoseLease& operator=(
        DynamicInstancePoseLease&& other) noexcept;
    ~DynamicInstancePoseLease();

    DynamicInstancePoseLease(
        const DynamicInstancePoseLease&) = delete;
    DynamicInstancePoseLease& operator=(
        const DynamicInstancePoseLease&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t sceneGeneration() const noexcept;
    [[nodiscard]] std::uint64_t publicationGeneration() const noexcept;
    [[nodiscard]] std::uint64_t simulationStep() const noexcept;
    [[nodiscard]] std::span<const DynamicInstancePoseOverride>
        overrides() const noexcept;

    // Returns an exact dynamic override when present; otherwise returns the
    // caller-supplied immutable authored transform.
    [[nodiscard]] ResolvedInstancePose resolve(
        std::uint32_t instanceIndex,
        const Mat3& authoredLinear,
        const Vec3& authoredTranslation) const noexcept;

    void reset() noexcept;

private:
    friend class ScenePoseExchange;

    DynamicInstancePoseLease(
        std::shared_ptr<detail::ScenePoseExchangeStorage> storage,
        std::uint8_t slotIndex) noexcept;

    std::shared_ptr<detail::ScenePoseExchangeStorage> storage_;
    std::uint8_t slotIndex_{};
};

// A bounded two-slot single-producer/single-consumer exchange. Construction
// performs all slot allocation. Publish/acquire do not allocate, use an
// explicit mutex, or wait; they perform bounded atomic attempts and report
// contention. A producer receives busy rather than overwriting a leased slot.
//
// tryBeginScene is a lifecycle operation: call it only after stopping the
// producer and consumer and releasing every lease. It checks for retained
// leases but intentionally does not synchronize concurrent scene rebinding.
// Destruction likewise requires producer/consumer quiescence, although an
// already acquired lease remains readable and releasable after destruction.
// Scene/publication generations never wrap: tryBeginScene returns empty when
// its identity domain is exhausted, and publish returns generationExhausted.
// A saturated lease counter makes acquire return empty.
class ScenePoseExchange final {
public:
    explicit ScenePoseExchange(
        DynamicInstancePoseLimits limits = {});
    ~ScenePoseExchange();

    ScenePoseExchange(const ScenePoseExchange&) = delete;
    ScenePoseExchange& operator=(const ScenePoseExchange&) = delete;
    ScenePoseExchange(ScenePoseExchange&&) = delete;
    ScenePoseExchange& operator=(ScenePoseExchange&&) = delete;

    [[nodiscard]] std::optional<ScenePoseHandle> tryBeginScene(
        std::uint32_t instanceCount) noexcept;

    [[nodiscard]] DynamicInstancePosePublishResult tryPublish(
        const ScenePoseHandle& scene,
        DynamicInstancePoseFrameView frame) noexcept;

    // An empty optional means there is no frame for this exact scene, the
    // scene handle is wrong/stale, or the reader lost a publication race and
    // should try again on its next render iteration.
    [[nodiscard]] std::optional<DynamicInstancePoseLease> tryAcquire(
        const ScenePoseHandle& scene) noexcept;

    [[nodiscard]] DynamicInstancePoseLimits limits() const noexcept;

private:
    std::shared_ptr<detail::ScenePoseExchangeStorage> storage_;
};

} // namespace airfix::render
