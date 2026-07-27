#pragma once

#include "airfix/assets/MissionWorldSpatialArena.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::assets {

struct MissionWorldSpatialLineTraceOptions {
    // PhLine defaults this legacy +0x37 flag to false. When enabled, the
    // polygon inclusion test may accept reverse-facing geometry and reports
    // that fact in the hit.
    bool allowReverseFacing{false};
};

struct MissionWorldSpatialLineHit {
    float fraction{};
    CcfVector3 point{};
    CcfVector3 normal{};
    std::size_t ownerWorldRoomIndex{};
    std::size_t treeIndex{};
    std::size_t nodeIndex{};
    std::size_t polygonIndex{};
    std::optional<std::size_t> portalWorldRoomIndex;
    bool reverseFacing{};

    friend bool operator==(
        const MissionWorldSpatialLineHit&,
        const MissionWorldSpatialLineHit&) = default;
};

enum class MissionWorldSpatialLineTraceStatus : std::uint8_t {
    noHit,
    hit,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    traversalDepthExceeded,
};

struct MissionWorldSpatialLineTraceResult {
    MissionWorldSpatialLineTraceStatus status{
        MissionWorldSpatialLineTraceStatus::noHit};
    std::optional<MissionWorldSpatialLineHit> hit;

    [[nodiscard]] bool valid() const noexcept {
        return status == MissionWorldSpatialLineTraceStatus::noHit ||
            status == MissionWorldSpatialLineTraceStatus::hit;
    }
};

// Traces one source-world segment against either the static or portal trees
// retained for a runtime room. The operation is read-only, allocation-free,
// noexcept, near-first, and keeps the first polygon encountered at an exact
// fraction tie, matching the legacy prepend-list traversal.
//
// The returned fraction intentionally is not clamped: the original epsilon
// endpoint gate can admit a crossing just outside the current recursion
// interval. A normal finite opposite-side crossing remains in [0, 1].
[[nodiscard]] MissionWorldSpatialLineTraceResult
traceMissionWorldSpatialLine(
    const MissionWorldSpatialArena& arena,
    std::size_t worldRoomIndex,
    CcfBspTreeKind treeKind,
    const CcfVector3& start,
    const CcfVector3& end,
    const MissionWorldSpatialLineTraceOptions& options = {}) noexcept;

enum class MissionWorldPortalTraceStatus : std::uint8_t {
    noTransition,
    transition,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    traversalDepthExceeded,
    transitionLimitExceeded,
};

struct MissionWorldPortalTraceResult {
    MissionWorldPortalTraceStatus status{
        MissionWorldPortalTraceStatus::noTransition};
    // Present when a nearest portal polygon was hit, including when its
    // mesh/type/visibility gate blocks the room transition.
    std::optional<MissionWorldSpatialLineHit> hit;
    std::optional<std::size_t> targetWorldRoomIndex;
    std::size_t transitionCount{};

    [[nodiscard]] bool valid() const noexcept {
        return status == MissionWorldPortalTraceStatus::noTransition ||
            status == MissionWorldPortalTraceStatus::transition;
    }
};

struct MissionWorldPortalTraceOptions {
    // The original recursively follows without a cycle guard. Portable code
    // requires an explicit bound so a self/cyclic portal cannot hang a frame.
    // Values above kMissionWorldSpatialMaximumPortalTransitions are rejected
    // as invalid input rather than disabling the safety ceiling.
    std::size_t maximumTransitions{64U};
};

// Reconstructs the default PhLine/CcRoom::TracePortals path on this game
// build: one-sided nearest portal-BSP hit, mesh selectionFlagB enabled,
// portalType exactly zero, and object rawFlag/visibility nonzero. Self-portals
// are followed like the original and terminate only through the portable
// transition bound. CcRoom suppresses a completed chain with no net room
// change. The legacy third float argument is unused and absent from this API.
[[nodiscard]] MissionWorldPortalTraceResult
traceMissionWorldPortalTransition(
    const MissionWorldSpatialArena& arena,
    std::size_t worldRoomIndex,
    const CcfVector3& start,
    const CcfVector3& end,
    const MissionWorldPortalTraceOptions& options = {}) noexcept;

} // namespace airfix::assets
