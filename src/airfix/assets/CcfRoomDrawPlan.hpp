#pragma once

#include "airfix/assets/AssetResolver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::assets {

enum class CcfRoomDrawPlanIssueKind : std::uint8_t {
    roomIndexOutOfRange,
    // Compatibility name for the original first-room-only entry point.
    noRoom = roomIndexOutOfRange,
    firstRoomNotPrimary,
    unsupportedRoomSectionLayout,
    placedSceneDependency,
    invalidPlacedNode,
    invalidMeshIndex,
    materialNotFound,
    materialAmbiguous,
    limitExceeded,
};

struct CcfRoomDrawPlanIssue {
    CcfRoomDrawPlanIssueKind kind{
        CcfRoomDrawPlanIssueKind::roomIndexOutOfRange};
    std::optional<std::size_t> placedNodeIndex;
    std::optional<std::size_t> meshIndex;
    std::optional<std::uint32_t> reference;
    std::optional<std::size_t> requestedRoomIndex;
};

struct CcfRoomDrawPlanLimits {
    std::size_t maximumPlacedNodes{100'000U};
    std::size_t maximumInstances{100'000U};
    std::size_t maximumUniqueMeshes{100'000U};
    std::size_t maximumMaterials{250'000U};
    std::size_t maximumMaterialReferences{65'536U};
    std::size_t maximumTextureEdges{262'144U};
};

struct CcfRoomDrawPlan {
    std::optional<std::size_t> roomIndex;
    // Physical placed-node order. Each entry is one drawable instance.
    std::vector<std::size_t> placedNodeIndices;
    // Stable first-use mesh slots for the instance list above.
    std::vector<std::size_t> meshIndices;
    // Stable first-use order across mesh slots, then physical triangles.
    std::vector<std::size_t> materialIndices;
    std::vector<TextureDependency> textures;
    std::vector<CcfRoomDrawPlanIssue> issues;
};

// Builds a conservative assets-only draw plan for one explicit physical room.
// The selected room index is never inferred from BSP data. Objects resolved to
// the external receiver fallback belong only to the primary receiver room.
// Any issue clears the room selection and every dependency list.
[[nodiscard]] CcfRoomDrawPlan resolveRoomDrawPlan(
    const CcfMetadata& ccf,
    std::size_t ccfRoomIndex,
    const CcfRoomDrawPlanLimits& limits = {});

// Builds the conservative assets-only draw plan for the first physical room.
// Room BSP metadata is deliberately irrelevant. The placed scene is resolved
// internally from this same immutable CCF so callers cannot supply a stale
// resolution. Any issue clears the room selection and every dependency list.
[[nodiscard]] CcfRoomDrawPlan resolveFirstRoomDrawPlan(
    const CcfMetadata& ccf,
    const CcfRoomDrawPlanLimits& limits = {});

} // namespace airfix::assets
