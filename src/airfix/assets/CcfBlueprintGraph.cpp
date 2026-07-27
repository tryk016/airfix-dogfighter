#include "airfix/assets/CcfBlueprintGraph.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace airfix::assets {
namespace {

void addIssue(
    std::vector<BlueprintGraphIssue>& issues,
    const BlueprintGraphIssueKind kind,
    const std::optional<std::size_t> index = std::nullopt,
    const std::optional<std::uint32_t> reference = std::nullopt) {
    issues.push_back({
        .kind = kind,
        .blueprintIndex = index,
        .reference = reference,
    });
}

} // namespace

ResolvedBlueprintGraph resolveBlueprintGraph(
    const CcfMetadata& ccf,
    const BlueprintGraphLimits& limits) {
    ResolvedBlueprintGraph result;
    if (ccf.blueprints.size() > limits.maximumBlueprints) {
        addIssue(result.issues, BlueprintGraphIssueKind::limitExceeded);
        return result;
    }

    result.nodes.reserve(ccf.blueprints.size());
    for (std::size_t index = 0U; index < ccf.blueprints.size(); ++index) {
        result.nodes.push_back({
            .blueprintIndex = index,
            .parentIndex = std::nullopt,
            .childIndices = {},
        });
    }

    struct ReferenceMatch {
        std::size_t index{};
        bool ambiguous{};
    };
    std::unordered_map<std::uint32_t, ReferenceMatch> byReference;
    byReference.reserve(ccf.blueprints.size());
    for (std::size_t index = 0U; index < ccf.blueprints.size(); ++index) {
        const auto reference = ccf.blueprints[index].reference;
        if (reference == 0U) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::invalidReference,
                index,
                reference);
            continue;
        }
        const auto [iterator, inserted] = byReference.emplace(
            reference, ReferenceMatch{.index = index});
        if (!inserted) {
            if (!iterator->second.ambiguous) {
                addIssue(
                    result.issues,
                    BlueprintGraphIssueKind::duplicateReference,
                    iterator->second.index,
                    reference);
            }
            iterator->second.ambiguous = true;
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::duplicateReference,
                index,
                reference);
        }

        const auto& blueprint = ccf.blueprints[index];
        if (blueprint.kind == CcfBlueprintKind::mesh &&
            (!blueprint.meshIndex.has_value() ||
             *blueprint.meshIndex >= ccf.meshes.size())) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::invalidMeshIndex,
                index,
                reference);
        }
    }

    std::size_t edgeCount = 0U;
    for (std::size_t index = 0U; index < ccf.blueprints.size(); ++index) {
        const auto& blueprint = ccf.blueprints[index];
        if (blueprint.parentReference == 0U) {
            result.rootIndices.push_back(index);
            continue;
        }
        if (blueprint.parentReference == blueprint.reference) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::selfParent,
                index,
                blueprint.parentReference);
            continue;
        }
        const auto parent = byReference.find(blueprint.parentReference);
        if (parent == byReference.end() || parent->second.ambiguous) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::missingParent,
                index,
                blueprint.parentReference);
            continue;
        }
        if (edgeCount >= limits.maximumEdges) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::limitExceeded,
                index,
                blueprint.parentReference);
            continue;
        }
        ++edgeCount;
        result.nodes[index].parentIndex = parent->second.index;
        result.nodes[parent->second.index].childIndices.push_back(index);
    }

    // Every node has at most one parent. Following parent chains with three
    // colors detects cycles iteratively and cannot exhaust the process stack.
    std::vector<std::uint8_t> color(result.nodes.size(), 0U);
    std::vector<std::size_t> chain;
    chain.reserve(result.nodes.size());
    for (std::size_t start = 0U; start < result.nodes.size(); ++start) {
        if (color[start] != 0U) {
            continue;
        }
        chain.clear();
        auto current = start;
        bool reachedRoot = false;
        while (color[current] == 0U) {
            color[current] = 1U;
            chain.push_back(current);
            if (!result.nodes[current].parentIndex.has_value()) {
                reachedRoot = true;
                break;
            }
            current = *result.nodes[current].parentIndex;
        }
        if (!reachedRoot && color[current] == 1U) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::cycle,
                current,
                ccf.blueprints[current].reference);
        }
        for (const auto index : chain) {
            color[index] = 2U;
        }
    }

    std::vector<std::pair<std::size_t, std::size_t>> pending;
    pending.reserve(result.nodes.size());
    for (const auto root : result.rootIndices) {
        pending.emplace_back(root, 0U);
    }
    while (!pending.empty()) {
        const auto [index, depth] = pending.back();
        pending.pop_back();
        if (depth > limits.maximumDepth) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::limitExceeded,
                index,
                ccf.blueprints[index].reference);
            break;
        }
        const auto& children = result.nodes[index].childIndices;
        if (!children.empty() && depth == limits.maximumDepth) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::limitExceeded,
                children.front(),
                ccf.blueprints[children.front()].reference);
            break;
        }
        for (const auto child : children) {
            pending.emplace_back(child, depth + 1U);
        }
    }
    return result;
}

ResolvedBlueprintSubtree resolveBlueprintSubtree(
    const CcfMetadata& ccf,
    const std::size_t rootIndex,
    const BlueprintGraphLimits& limits) {
    ResolvedBlueprintSubtree result;
    auto graph = resolveBlueprintGraph(ccf, limits);
    if (!graph.issues.empty()) {
        result.issues = std::move(graph.issues);
        return result;
    }
    if (rootIndex >= graph.nodes.size()) {
        addIssue(
            result.issues,
            BlueprintGraphIssueKind::invalidRoot,
            rootIndex);
        return result;
    }

    std::vector<std::size_t> pending{rootIndex};
    pending.reserve(graph.nodes.size());
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        if (result.blueprintIndices.size() >= limits.maximumSelectedNodes) {
            addIssue(
                result.issues,
                BlueprintGraphIssueKind::limitExceeded,
                index,
                ccf.blueprints[index].reference);
            result.blueprintIndices.clear();
            return result;
        }
        result.blueprintIndices.push_back(index);
        const auto& children = graph.nodes[index].childIndices;
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    return result;
}

} // namespace airfix::assets
