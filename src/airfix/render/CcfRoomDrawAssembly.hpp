#pragma once

#include "airfix/assets/CcfRoomDrawPlan.hpp"
#include "airfix/render/DrawModel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class CcfRoomDrawIssueKind : std::uint8_t {
    planDependency,
    invalidMaterialBinding,
    missingMaterialBinding,
    invalidPlacedNode,
    unsupportedPlacedOrientation,
    invalidTransform,
    geometryFailure,
    drawMeshFailure,
    limitExceeded,
    integerOverflow,
};

struct CcfRoomDrawIssue {
    CcfRoomDrawIssueKind kind{CcfRoomDrawIssueKind::planDependency};
    std::optional<std::size_t> placedNodeIndex;
    std::optional<std::size_t> meshIndex;
    std::optional<std::uint32_t> materialReference;
    std::optional<GeometryErrorCode> geometryError;
    std::optional<DrawMeshErrorCode> drawMeshError;
};

struct CcfRoomDrawLimits {
    assets::CcfRoomDrawPlanLimits plan;
    GeometryLimits geometryPerMesh;
    DrawMeshLimits drawMeshPerMesh;
    std::size_t maximumMeshes{65'536U};
    std::size_t maximumInstances{100'000U};
    std::size_t maximumMaterialBindings{65'536U};
    std::size_t maximumTotalVertices{3'000'000U};
    std::size_t maximumTotalIndices{3'000'000U};
    std::size_t maximumTotalMaterials{65'536U};
    std::size_t maximumTotalRanges{1'000'000U};
    std::size_t maximumTotalBytes{512U * 1024U * 1024U};
};

struct CcfRoomDrawAssembly {
    // The plan remains available for diagnostics. On any assembly issue the
    // public draw model is cleared atomically.
    assets::CcfRoomDrawPlan plan;
    DrawModelPayload model;
    std::vector<CcfRoomDrawIssue> issues;
};

// Builds a conservative draw-all model for the first receiver/root room.
// Selection is based only on each placed object's resolved room; room BSP does
// not hide, duplicate, or reorder geometry. Material bindings are supplied by
// the caller after texture assets receive runtime IDs.
[[nodiscard]] CcfRoomDrawAssembly buildFirstRoomDrawAssembly(
    const assets::CcfMetadata& ccf,
    std::span<const DrawMaterial> materialBindings,
    const BasisTransform& basis = {},
    UvPolicy uvPolicy = UvPolicy::preserveRaw,
    const CcfRoomDrawLimits& limits = {});

} // namespace airfix::render
