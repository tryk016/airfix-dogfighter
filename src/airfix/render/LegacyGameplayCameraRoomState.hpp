#pragma once

#include "airfix/render/MissionWorldRuntimeSpatialTrace.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::render {

struct LegacyGameplayCameraRoomState final {
    Vec3 runtimeWorldPosition{};
    std::size_t worldRoomIndex{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyGameplayCameraRoomState&,
        const LegacyGameplayCameraRoomState&) noexcept = default;
};

struct LegacyGameplayCameraRoomUpdateOptions final {
    // This remains bounded by
    // assets::kMissionWorldSpatialMaximumPortalTransitions.
    std::size_t maximumPortalTransitions{64U};
};

enum class LegacyGameplayCameraRoomUpdateStatus : std::uint8_t {
    noTransition,
    transition,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    invalidBasis,
    traversalDepthExceeded,
    transitionLimitExceeded,
    outOfSegmentHit,
};

struct LegacyGameplayCameraRoomUpdateResult final {
    LegacyGameplayCameraRoomUpdateStatus status{
        LegacyGameplayCameraRoomUpdateStatus::noTransition};
    // Present only for a complete valid trace. Applying this value is the
    // caller's later atomic commit; this function never mutates currentState.
    std::optional<LegacyGameplayCameraRoomState> proposedState;
    // Diagnostic only. It may be present for blocked portals and for failures
    // after one or more local hits.
    std::optional<MissionWorldRuntimeSpatialLineHit> diagnosticHit;
    std::size_t transitionCount{};

    [[nodiscard]] bool valid() const noexcept {
        return status ==
                LegacyGameplayCameraRoomUpdateStatus::noTransition ||
            status ==
                LegacyGameplayCameraRoomUpdateStatus::transition;
    }
};

// Proposes the candidate position and the room resolved by strict portal
// tracing. A valid no-transition result keeps the current room; a valid
// transition uses the final target room. Every failure returns no proposed
// state, so a caller can retain its complete prior state without rollback.
//
// This boundary does not resolve static/sphere contacts, change camera
// orientation, or publish to a thread/runtime endpoint. The operation is
// read-only, allocation-free, and noexcept.
[[nodiscard]] LegacyGameplayCameraRoomUpdateResult
proposeLegacyGameplayCameraRoomState(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraRoomState& currentState,
    const Vec3& candidateRuntimeWorldPosition,
    const LegacyGameplayCameraRoomUpdateOptions& options = {}) noexcept;

} // namespace airfix::render
