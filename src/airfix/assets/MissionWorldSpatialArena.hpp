#pragma once

#include "airfix/assets/CcfRoomScene.hpp"
#include "airfix/assets/MissionWorldRooms.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::assets {

inline constexpr std::size_t kMissionWorldSpatialMaximumTraceDepth =
    1'024U;
inline constexpr std::size_t kMissionWorldSpatialMaximumPortalTransitions =
    256U;

struct MissionWorldSpatialPolygon {
    // Bit-preserved authored source-world values. In particular, faceNormal
    // may contain the documented legacy quiet-NaN sentinel.
    CcfVector3 faceCross{};
    CcfVector3 faceNormal{};
    CcfVector3 point0{};
    CcfVector3 edge01{};
    CcfVector3 edge12{};
    std::uint32_t polygonIndex{};
    std::uint32_t placedObjectReference{};
    // Present only for a portal polygon and already translated from the
    // source-local physical room to MissionWorldRoomCatalog::rooms.
    std::optional<std::size_t> portalWorldRoomIndex;
    // Exact CcObjPolygon::GetPortalType/GetObjectVisible follow gates copied
    // from the resolved mesh and placed object. They remain false/zero for a
    // static polygon.
    bool portalMeshSelectionFlagB{};
    std::uint32_t portalType{};
    bool portalObjectVisible{};

    friend bool operator==(
        const MissionWorldSpatialPolygon&,
        const MissionWorldSpatialPolygon&) = default;
};

struct MissionWorldSpatialNode {
    std::optional<std::size_t> childAIndex;
    std::optional<std::size_t> childBIndex;
    CcfVector3 splitNormal{};
    CcfVector3 pointOnPlane{};
    // Contiguous range in MissionWorldSpatialArena::polygons. Polygon order
    // is the legacy runtime prepend order, opposite the physical CCF order.
    std::size_t firstPolygonIndex{};
    std::size_t polygonCount{};

    friend bool operator==(
        const MissionWorldSpatialNode&,
        const MissionWorldSpatialNode&) = default;
};

struct MissionWorldSpatialTree {
    CcfBspTreeKind kind{CcfBspTreeKind::staticTree};
    std::size_t sourceIndex{};
    std::size_t physicalRoomIndex{};
    std::size_t worldRoomIndex{};
    std::size_t rootNodeIndex{};
    std::size_t firstNodeIndex{};
    std::size_t nodeCount{};

    friend bool operator==(
        const MissionWorldSpatialTree&,
        const MissionWorldSpatialTree&) = default;
};

struct MissionWorldSpatialRoom {
    // Both ranges address MissionWorldSpatialArena::treeReferences. Each range
    // is in legacy runtime prepend order, opposite semantic load/file order.
    std::size_t firstStaticTreeReference{};
    std::size_t staticTreeCount{};
    std::size_t firstPortalTreeReference{};
    std::size_t portalTreeCount{};

    friend bool operator==(
        const MissionWorldSpatialRoom&,
        const MissionWorldSpatialRoom&) = default;
};

enum class MissionWorldSpatialArenaIssueKind : std::uint8_t {
    catalogIncomplete,
    catalogMismatch,
    sourceCountMismatch,
    invalidSourceMetadata,
    unsupportedSourceFlags,
    invalidContributor,
    duplicateContributor,
    missingContributor,
    roomSceneFailure,
    invalidRoomSceneBinding,
    missingPortalWorldRoom,
    invalidSpatialValue,
    limitExceeded,
    retainedByteLimitExceeded,
    integerOverflow,
    allocationFailure,
};

struct MissionWorldSpatialArenaIssue {
    MissionWorldSpatialArenaIssueKind kind{
        MissionWorldSpatialArenaIssueKind::catalogIncomplete};
    std::optional<std::size_t> sourceIndex;
    std::optional<std::size_t> physicalRoomIndex;
    std::optional<std::size_t> worldRoomIndex;
    std::optional<RoomSceneIssueKind> roomSceneIssue;
};

struct MissionWorldSpatialArenaLimits {
    std::size_t maximumSources{65'536U};
    std::size_t maximumWorldRooms{100'000U};
    // Aggregate active semantic source/physical-room pairs. Disabled sources
    // do not allocate reverse-map entries or consume this limit.
    std::size_t maximumPhysicalRooms{262'144U};
    std::size_t maximumTrees{262'144U};
    std::size_t maximumNodes{1'000'000U};
    std::size_t maximumPolygons{2'000'000U};
    // Logical bytes retained by the arena vectors, excluding the arena
    // object's inline vector handles.
    std::uint64_t maximumRetainedBytes{512U * 1024U * 1024U};
    MissionWorldRoomBuildLimits catalogAuthentication{};
    RoomSceneLimits roomScenePerSource{};
};

struct MissionWorldSpatialArena {
    // Parallel to MissionWorldRoomCatalog::rooms.
    std::vector<MissionWorldSpatialRoom> rooms;
    std::vector<std::size_t> treeReferences;
    std::vector<MissionWorldSpatialTree> trees;
    std::vector<MissionWorldSpatialNode> nodes;
    std::vector<MissionWorldSpatialPolygon> polygons;
    std::uint64_t retainedPayloadBytes{};
    std::vector<MissionWorldSpatialArenaIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return issues.empty() && !rooms.empty();
    }
};

// Repackages source-local CCF room BSP into a pointer-free mission-lifetime
// arena. Inputs are semantic load sources, not the deduplicated physical CCF
// cache, because contributor identity and legacy load flags are source-index
// based. The arena retains raw source-world vectors; callers must explicitly
// convert runtime queries into that common source space.
//
// A room-disabled but placed-scene-enabled source currently fails closed:
// legacy root-fallback spatial ownership for that load shape is not proven.
// A placed-scene-disabled source contributes rooms but no collision trees,
// matching the legacy 0x2000 suppression rather than creating ghost contacts.
[[nodiscard]] MissionWorldSpatialArena buildMissionWorldSpatialArena(
    std::span<const MissionCcfRoomLoadSource> sources,
    const MissionWorldRoomCatalog& catalog,
    const MissionWorldSpatialArenaLimits& limits = {});

} // namespace airfix::assets
