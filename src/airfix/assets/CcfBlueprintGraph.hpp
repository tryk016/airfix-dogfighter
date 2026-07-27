#pragma once

#include "airfix/assets/LegacyFormats.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::assets {

enum class BlueprintGraphIssueKind : std::uint8_t {
    invalidReference,
    duplicateReference,
    missingParent,
    selfParent,
    cycle,
    invalidMeshIndex,
    invalidRoot,
    limitExceeded,
};

struct BlueprintGraphIssue {
    BlueprintGraphIssueKind kind{BlueprintGraphIssueKind::missingParent};
    std::optional<std::size_t> blueprintIndex;
    std::optional<std::uint32_t> reference;
};

struct ResolvedBlueprintNode {
    std::size_t blueprintIndex{};
    std::optional<std::size_t> parentIndex;
    std::vector<std::size_t> childIndices;
};

struct BlueprintGraphLimits {
    std::size_t maximumBlueprints{100'000U};
    std::size_t maximumEdges{100'000U};
    std::size_t maximumSelectedNodes{100'000U};
    std::size_t maximumDepth{1'024U};
};

struct ResolvedBlueprintGraph {
    // Nodes remain parallel to CcfMetadata::blueprints. Child lists retain
    // physical CCF order.
    std::vector<ResolvedBlueprintNode> nodes;
    std::vector<std::size_t> rootIndices;
    std::vector<BlueprintGraphIssue> issues;
};

struct ResolvedBlueprintSubtree {
    // Stable depth-first preorder beginning with rootIndex.
    std::vector<std::size_t> blueprintIndices;
    std::vector<BlueprintGraphIssue> issues;
};

// Resolves parent references without changing the transforms authored in the
// CCF. The legacy loader attaches nodes only after the whole section is read.
[[nodiscard]] ResolvedBlueprintGraph resolveBlueprintGraph(
    const CcfMetadata& ccf,
    const BlueprintGraphLimits& limits = {});

// Returns one selected component in stable preorder. A malformed graph is
// rejected as a whole so callers never render a silently truncated model.
[[nodiscard]] ResolvedBlueprintSubtree resolveBlueprintSubtree(
    const CcfMetadata& ccf,
    std::size_t rootIndex,
    const BlueprintGraphLimits& limits = {});

} // namespace airfix::assets
