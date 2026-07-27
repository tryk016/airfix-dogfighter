#include "airfix/assets/MissionWorldRoomDrawPlan.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <variant>

namespace airfix::assets {
namespace {

struct MaterialMatch {
    std::size_t index{};
    bool ambiguous{};
};

void addIssue(
    MissionWorldRoomDrawPlan& result,
    const MissionWorldRoomDrawPlanIssueKind kind,
    const std::optional<std::size_t> sourceIndex = std::nullopt,
    const std::optional<std::size_t> contributorIndex = std::nullopt,
    const std::optional<std::size_t> physicalRoomIndex = std::nullopt,
    const std::optional<std::size_t> placedNodeIndex = std::nullopt,
    const std::optional<std::size_t> physicalMeshIndex = std::nullopt,
    const std::optional<std::uint32_t> materialReference = std::nullopt,
    const std::optional<std::uint32_t> roomReference = std::nullopt,
    const std::optional<PlacedSceneIssueKind> placedSceneIssue =
        std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .sourceIndex = sourceIndex,
        .contributorIndex = contributorIndex,
        .physicalRoomIndex = physicalRoomIndex,
        .placedNodeIndex = placedNodeIndex,
        .physicalMeshIndex = physicalMeshIndex,
        .materialReference = materialReference,
        .roomReference = roomReference,
        .placedSceneIssue = placedSceneIssue,
    });
}

void failClosed(MissionWorldRoomDrawPlan& result) {
    result.worldRoomIndex.reset();
    result.meshes.clear();
    result.placedNodes.clear();
    result.materials.clear();
    result.textures.clear();
}

[[nodiscard]] bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool validateEnabledRoomSections(
    const CcfMetadata& ccf,
    std::size_t& aggregateRoomSections,
    const std::size_t maximumRoomSections) noexcept {
    std::size_t nextSectionCount = 0U;
    if (!checkedAdd(
            aggregateRoomSections,
            ccf.roomSections.size(),
            nextSectionCount) ||
        nextSectionCount > maximumRoomSections) {
        return false;
    }

    std::size_t expectedFirstRoom = 0U;
    for (const auto& section : ccf.roomSections) {
        if (section.firstPhysicalRoomIndex != expectedFirstRoom ||
            section.physicalRoomCount >
                ccf.rooms.size() - expectedFirstRoom ||
            (section.firstDirectChildIsRoom &&
             section.physicalRoomCount == 0U)) {
            return false;
        }
        for (std::size_t localRoomIndex = 0U;
             localRoomIndex < section.physicalRoomCount;
             ++localRoomIndex) {
            const auto physicalRoomIndex =
                expectedFirstRoom + localRoomIndex;
            const bool expectedPrimary =
                section.firstDirectChildIsRoom &&
                localRoomIndex == 0U;
            if (ccf.rooms[physicalRoomIndex].primaryBinding !=
                expectedPrimary) {
                return false;
            }
        }
        expectedFirstRoom += section.physicalRoomCount;
    }
    if (expectedFirstRoom != ccf.rooms.size()) {
        return false;
    }
    aggregateRoomSections = nextSectionCount;
    return true;
}

[[nodiscard]] bool isRoomOnlyPlacedSceneIssue(
    const PlacedSceneIssueKind kind) noexcept {
    return kind == PlacedSceneIssueKind::duplicateRoomReference ||
        kind == PlacedSceneIssueKind::ambiguousRoomReference ||
        kind ==
            PlacedSceneIssueKind::ambiguousPortalRoomReference;
}

[[nodiscard]] bool hasCanonicalTopLevelOrder(
    const CcfMetadata& ccf,
    const MissionWorldRoomDrawPlanLimits& limits,
    const bool roomSectionEnabled,
    const bool placedSceneEnabled) noexcept {
    std::uint8_t lastSectionRank = 0U;
    bool recognizedSectionSeen = false;
    std::size_t roomSectionIndex = 0U;
    std::size_t physicalRoomIndex = 0U;
    std::size_t placedNodeCount = 0U;
    for (const auto& section : ccf.topLevelChunks) {
        std::optional<std::uint8_t> sectionRank;
        if (section.id == 0x1000U) {
            if (roomSectionEnabled) {
                sectionRank = 0U;
            }
            if (roomSectionIndex >= ccf.roomSections.size()) {
                return false;
            }
            const auto& metadata =
                ccf.roomSections[roomSectionIndex];
            const auto directRoomCount =
                static_cast<std::size_t>(std::ranges::count_if(
                    section.directChildren,
                    [](const CcfChunk& child) {
                        return child.id == 0x1100U;
                    }));
            const bool firstDirectChildIsRoom =
                !section.directChildren.empty() &&
                section.directChildren.front().id == 0x1100U;
            if (metadata.offset != section.offset ||
                metadata.firstPhysicalRoomIndex !=
                    physicalRoomIndex ||
                metadata.physicalRoomCount != directRoomCount ||
                metadata.firstDirectChildIsRoom !=
                    firstDirectChildIsRoom) {
                return false;
            }
            for (const auto& child : section.directChildren) {
                if (child.id != 0x1100U) {
                    continue;
                }
                if (physicalRoomIndex >= ccf.rooms.size() ||
                    ccf.rooms[physicalRoomIndex].offset !=
                        child.offset) {
                    return false;
                }
                ++physicalRoomIndex;
            }
            ++roomSectionIndex;
        }
        else if (section.id == 0x2000U) {
            sectionRank = 1U;
        }
        else if (section.id == 0x3000U) {
            sectionRank = 2U;
        }
        else if (section.id == 0x4000U) {
            if (!placedSceneEnabled) {
                continue;
            }
            sectionRank = 3U;
            for (const auto& child : section.directChildren) {
                if (child.id != 0x4100U &&
                    child.id != 0x4200U &&
                    child.id != 0x4300U) {
                    continue;
                }
                if (placedNodeCount >=
                    limits.maximumScannedPlacedNodes) {
                    return false;
                }
                ++placedNodeCount;
            }
        }
        if (sectionRank.has_value()) {
            if (recognizedSectionSeen &&
                *sectionRank < lastSectionRank) {
                return false;
            }
            lastSectionRank = *sectionRank;
            recognizedSectionSeen = true;
        }
    }
    return roomSectionIndex == ccf.roomSections.size() &&
        physicalRoomIndex == ccf.rooms.size() &&
        (!placedSceneEnabled ||
         placedNodeCount == ccf.placedNodes.size());
}

} // namespace

MissionWorldRoomDrawPlan resolveMissionWorldRoomDrawPlan(
    const MissionWorldRoomCatalog& catalog,
    const std::span<const MissionCcfRoomLoadSource> sources,
    const std::size_t worldRoomIndex,
    const MissionWorldRoomDrawPlanLimits& limits) {
    MissionWorldRoomDrawPlan result;
    result.sourceCount = sources.size();
    if (!catalog.complete() ||
        catalog.sourceCount !=
            catalog.sourcePhysicalRoomCounts.size()) {
        addIssue(
            result,
            MissionWorldRoomDrawPlanIssueKind::catalogIncomplete);
        return result;
    }
    if (catalog.sourceCount != sources.size()) {
        addIssue(
            result,
            MissionWorldRoomDrawPlanIssueKind::sourceCountMismatch);
        return result;
    }
    if (sources.size() > limits.maximumSources) {
        addIssue(
            result,
            MissionWorldRoomDrawPlanIssueKind::limitExceeded);
        return result;
    }
    if (catalog.rooms.size() > limits.maximumRuntimeRooms) {
        addIssue(
            result,
            MissionWorldRoomDrawPlanIssueKind::limitExceeded);
        return result;
    }
    std::size_t catalogContributorCount = 0U;
    for (const auto& room : catalog.rooms) {
        std::size_t nextContributorCount = 0U;
        if (!checkedAdd(
                catalogContributorCount,
                room.contributors.size(),
                nextContributorCount)) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::integerOverflow);
            return result;
        }
        if (nextContributorCount >
            limits.maximumContributors) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::limitExceeded);
            return result;
        }
        catalogContributorCount = nextContributorCount;
    }
    if (worldRoomIndex >= catalog.rooms.size()) {
        addIssue(
            result,
            MissionWorldRoomDrawPlanIssueKind::
                worldRoomIndexOutOfRange);
        return result;
    }

    std::size_t aggregateRoomSections = 0U;
    std::size_t aggregateTopLevelSections = 0U;
    std::size_t processedPhysicalRooms = 0U;
    std::size_t scannedMeshes = 0U;
    std::size_t scannedPlacedNodes = 0U;
    std::size_t scannedMaterials = 0U;
    for (std::size_t sourceIndex = 0U;
         sourceIndex < sources.size();
         ++sourceIndex) {
        const auto& source = sources[sourceIndex];
        if (source.ccf == nullptr ||
            source.ccf->rooms.size() !=
                catalog.sourcePhysicalRoomCounts[sourceIndex]) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::invalidSource,
                sourceIndex);
            return result;
        }
        std::size_t nextTopLevelSections = 0U;
        if (!checkedAdd(
                aggregateTopLevelSections,
                source.ccf->topLevelChunks.size(),
                nextTopLevelSections)) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::integerOverflow,
                sourceIndex);
            return result;
        }
        if (nextTopLevelSections >
            limits.maximumTopLevelSections) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::limitExceeded,
                sourceIndex);
            return result;
        }
        aggregateTopLevelSections = nextTopLevelSections;
        if (source.roomSectionEnabled) {
            std::size_t nextRoomSections = 0U;
            if (!checkedAdd(
                    aggregateRoomSections,
                    source.ccf->roomSections.size(),
                    nextRoomSections)) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        integerOverflow,
                    sourceIndex);
                return result;
            }
            if (nextRoomSections >
                limits.maximumRoomSections) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        limitExceeded,
                    sourceIndex);
                return result;
            }
            if (!validateEnabledRoomSections(
                    *source.ccf,
                    aggregateRoomSections,
                    limits.maximumRoomSections)) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        invalidRoomSectionLayout,
                    sourceIndex);
                return result;
            }
        }
        if (!hasCanonicalTopLevelOrder(
                *source.ccf,
                limits,
                source.roomSectionEnabled,
                source.placedSceneEnabled)) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::
                    invalidTopLevelOrder,
                sourceIndex);
            return result;
        }
        if (source.roomSectionEnabled ||
            source.placedSceneEnabled) {
            std::size_t nextPhysicalRooms = 0U;
            if (!checkedAdd(
                    processedPhysicalRooms,
                    source.ccf->rooms.size(),
                    nextPhysicalRooms)) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        integerOverflow,
                    sourceIndex);
                return result;
            }
            if (nextPhysicalRooms >
                limits.maximumPhysicalRooms) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        limitExceeded,
                    sourceIndex);
                return result;
            }
            processedPhysicalRooms = nextPhysicalRooms;
        }
        if (!source.placedSceneEnabled) {
            continue;
        }

        std::size_t nextMeshes = 0U;
        std::size_t nextPlaced = 0U;
        std::size_t nextMaterials = 0U;
        if (!checkedAdd(
                scannedMeshes,
                source.ccf->meshes.size(),
                nextMeshes) ||
            !checkedAdd(
                scannedPlacedNodes,
                source.ccf->placedNodes.size(),
                nextPlaced) ||
            !checkedAdd(
                scannedMaterials,
                source.ccf->materials.size(),
                nextMaterials)) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::integerOverflow,
                sourceIndex);
            return result;
        }
        if (nextMeshes > limits.maximumScannedMeshes ||
            nextPlaced > limits.maximumScannedPlacedNodes ||
            nextMaterials > limits.maximumScannedMaterials) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::limitExceeded,
                sourceIndex);
            return result;
        }
        scannedMeshes = nextMeshes;
        scannedPlacedNodes = nextPlaced;
        scannedMaterials = nextMaterials;
    }

    const auto canonicalCatalog = buildMissionWorldRoomCatalog(
        {
            .initialRootName = catalog.initialRootName,
            .sources = sources,
        },
        MissionWorldRoomBuildLimits{
            .maximumSources = limits.maximumSources,
            .maximumRoomSections = limits.maximumRoomSections,
            .maximumContributors = limits.maximumContributors,
            .maximumRuntimeRooms = limits.maximumRuntimeRooms,
        });
    if (!canonicalCatalog.complete() ||
        canonicalCatalog.sourceCount != catalog.sourceCount ||
        canonicalCatalog.sourcePhysicalRoomCounts !=
            catalog.sourcePhysicalRoomCounts ||
        canonicalCatalog.initialRootName !=
            catalog.initialRootName ||
        canonicalCatalog.rooms != catalog.rooms) {
        addIssue(
            result,
            MissionWorldRoomDrawPlanIssueKind::catalogIncomplete);
        return result;
    }

    std::vector<std::vector<std::optional<std::size_t>>>
        runtimeRoomByPhysical(sources.size());
    std::vector<std::vector<std::optional<std::size_t>>>
        selectedContributorByPhysical(sources.size());
    for (std::size_t sourceIndex = 0U;
         sourceIndex < sources.size();
         ++sourceIndex) {
        runtimeRoomByPhysical[sourceIndex].resize(
            sources[sourceIndex].roomSectionEnabled
                ? sources[sourceIndex].ccf->rooms.size()
                : 0U);
        selectedContributorByPhysical[sourceIndex].resize(
            runtimeRoomByPhysical[sourceIndex].size());
    }
    for (std::size_t runtimeRoomIndex = 0U;
         runtimeRoomIndex < catalog.rooms.size();
         ++runtimeRoomIndex) {
        const auto& runtimeRoom = catalog.rooms[runtimeRoomIndex];
        for (std::size_t contributorIndex = 0U;
             contributorIndex < runtimeRoom.contributors.size();
             ++contributorIndex) {
            const auto& contributor =
                runtimeRoom.contributors[contributorIndex];
            auto& assignment =
                runtimeRoomByPhysical[contributor.sourceIndex]
                    [contributor.physicalRoomIndex];
            if (assignment.has_value()) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        invalidContributor,
                    contributor.sourceIndex,
                    contributorIndex,
                    contributor.physicalRoomIndex);
                return result;
            }
            assignment = runtimeRoomIndex;
            if (runtimeRoomIndex == worldRoomIndex) {
                selectedContributorByPhysical[
                    contributor.sourceIndex]
                    [contributor.physicalRoomIndex] =
                        contributorIndex;
            }
        }
    }
    for (std::size_t sourceIndex = 0U;
         sourceIndex < sources.size();
         ++sourceIndex) {
        for (std::size_t physicalRoomIndex = 0U;
             physicalRoomIndex <
                 runtimeRoomByPhysical[sourceIndex].size();
             ++physicalRoomIndex) {
            if (runtimeRoomByPhysical[sourceIndex][physicalRoomIndex]
                    .has_value() !=
                sources[sourceIndex].roomSectionEnabled) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        invalidContributor,
                    sourceIndex,
                    std::nullopt,
                    physicalRoomIndex);
                return result;
            }
        }
    }

    result.worldRoomIndex = worldRoomIndex;
    result.placedNodes.reserve(std::min(
        scannedPlacedNodes, limits.maximumInstances));
    result.meshes.reserve(std::min(
        scannedPlacedNodes, limits.maximumUniqueMeshes));
    std::map<std::pair<std::size_t, std::size_t>, std::size_t>
        meshSlots;

    for (std::size_t sourceIndex = 0U;
         sourceIndex < sources.size();
         ++sourceIndex) {
        const auto& source = sources[sourceIndex];
        if (!source.placedSceneEnabled) {
            continue;
        }
        const auto scene = resolvePlacedScene(
            *source.ccf, limits.placedScenePerSource);
        const auto dependencyIssue = std::find_if(
            scene.issues.begin(),
            scene.issues.end(),
            [](const PlacedSceneIssue& issue) {
                return !isRoomOnlyPlacedSceneIssue(issue.kind);
            });
        if (dependencyIssue != scene.issues.end() ||
            scene.nodes.size() != source.ccf->placedNodes.size()) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::
                    placedSceneDependency,
                sourceIndex,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                dependencyIssue == scene.issues.end()
                    ? std::nullopt
                    : std::optional{dependencyIssue->kind});
            failClosed(result);
            return result;
        }

        struct ActiveRoomReference {
            std::size_t runtimeRoomIndex{};
            std::size_t physicalRoomIndex{};
        };
        std::map<std::uint32_t, ActiveRoomReference>
            activeRoomsByReference;
        std::map<std::size_t, std::uint32_t>
            activeReferenceByRuntimeRoom;
        for (std::size_t physicalRoomIndex = 0U;
             physicalRoomIndex < source.ccf->rooms.size();
             ++physicalRoomIndex) {
            if (!source.roomSectionEnabled) {
                break;
            }
            const auto runtimeRoom =
                runtimeRoomByPhysical[sourceIndex][physicalRoomIndex];
            if (!runtimeRoom.has_value()) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        invalidRoomReferenceMap,
                    sourceIndex,
                    std::nullopt,
                    physicalRoomIndex);
                failClosed(result);
                return result;
            }
            const auto previous =
                activeReferenceByRuntimeRoom.find(*runtimeRoom);
            if (previous != activeReferenceByRuntimeRoom.end()) {
                const auto active =
                    activeRoomsByReference.find(previous->second);
                if (active == activeRoomsByReference.end() ||
                    active->second.runtimeRoomIndex != *runtimeRoom) {
                    addIssue(
                        result,
                        MissionWorldRoomDrawPlanIssueKind::
                            invalidRoomReferenceMap,
                        sourceIndex,
                        std::nullopt,
                        physicalRoomIndex,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        previous->second);
                    failClosed(result);
                    return result;
                }
                activeRoomsByReference.erase(active);
            }

            const auto reference =
                source.ccf->rooms[physicalRoomIndex].reference;
            if (activeRoomsByReference.contains(reference)) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        invalidRoomReferenceMap,
                    sourceIndex,
                    std::nullopt,
                    physicalRoomIndex,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    reference);
                failClosed(result);
                return result;
            }
            activeRoomsByReference.emplace(
                reference,
                ActiveRoomReference{
                    .runtimeRoomIndex = *runtimeRoom,
                    .physicalRoomIndex = physicalRoomIndex,
                });
            activeReferenceByRuntimeRoom[*runtimeRoom] = reference;
        }

        for (std::size_t placedNodeIndex = 0U;
             placedNodeIndex < scene.nodes.size();
             ++placedNodeIndex) {
            const auto& sourceNode =
                source.ccf->placedNodes[placedNodeIndex];
            const auto& node = scene.nodes[placedNodeIndex];
            if (node.placedNodeIndex != placedNodeIndex) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        invalidPlacedNode,
                    sourceIndex,
                    std::nullopt,
                    std::nullopt,
                    placedNodeIndex);
                failClosed(result);
                return result;
            }
            if (sourceNode.kind != CcfPlacedNodeKind::object) {
                continue;
            }
            if (!std::holds_alternative<CcfPlacedObjectMetadata>(
                    sourceNode.data) ||
                !node.instantiated) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        invalidPlacedNode,
                    sourceIndex,
                    std::nullopt,
                    std::nullopt,
                    placedNodeIndex);
                failClosed(result);
                return result;
            }

            std::optional<std::size_t> contributorIndex;
            std::optional<std::size_t> physicalRoomIndex;
            std::size_t targetRuntimeRoomIndex = 0U;
            const auto roomMatch = activeRoomsByReference.find(
                sourceNode.roomReference);
            if (roomMatch != activeRoomsByReference.end()) {
                targetRuntimeRoomIndex =
                    roomMatch->second.runtimeRoomIndex;
                physicalRoomIndex =
                    roomMatch->second.physicalRoomIndex;
                if (targetRuntimeRoomIndex == worldRoomIndex) {
                    contributorIndex =
                        selectedContributorByPhysical[sourceIndex]
                            [*physicalRoomIndex];
                    if (!contributorIndex.has_value()) {
                        addIssue(
                            result,
                            MissionWorldRoomDrawPlanIssueKind::
                                invalidRoomReferenceMap,
                            sourceIndex,
                            std::nullopt,
                            physicalRoomIndex,
                            placedNodeIndex,
                            std::nullopt,
                            std::nullopt,
                            sourceNode.roomReference);
                        failClosed(result);
                        return result;
                    }
                }
            }
            if (targetRuntimeRoomIndex != worldRoomIndex) {
                continue;
            }

            if (node.meshTarget.kind !=
                    PlacedMeshTargetKind::parsedMesh ||
                !node.meshTarget.meshIndex.has_value() ||
                *node.meshTarget.meshIndex >=
                    source.ccf->meshes.size()) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        invalidMeshIndex,
                    sourceIndex,
                    contributorIndex,
                    physicalRoomIndex,
                    placedNodeIndex,
                    node.meshTarget.meshIndex);
                failClosed(result);
                return result;
            }
            if (result.placedNodes.size() >=
                limits.maximumInstances) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        limitExceeded,
                    sourceIndex,
                    contributorIndex,
                    physicalRoomIndex,
                    placedNodeIndex);
                failClosed(result);
                return result;
            }

            const auto meshKey = std::pair{
                sourceIndex, *node.meshTarget.meshIndex};
            auto meshSlot = meshSlots.find(meshKey);
            if (meshSlot == meshSlots.end()) {
                if (result.meshes.size() >=
                    limits.maximumUniqueMeshes) {
                    addIssue(
                        result,
                        MissionWorldRoomDrawPlanIssueKind::
                            limitExceeded,
                        sourceIndex,
                        contributorIndex,
                        physicalRoomIndex,
                        placedNodeIndex,
                        *node.meshTarget.meshIndex);
                    failClosed(result);
                    return result;
                }
                const auto insertedSlot = result.meshes.size();
                result.meshes.push_back({
                    .sourceIndex = sourceIndex,
                    .physicalMeshIndex =
                        *node.meshTarget.meshIndex,
                });
                meshSlot = meshSlots.emplace(
                    meshKey, insertedSlot).first;
            }
            result.placedNodes.push_back({
                .sourceIndex = sourceIndex,
                .placedNodeIndex = placedNodeIndex,
                .meshSlot = meshSlot->second,
                .contributorIndex = contributorIndex,
                .physicalRoomIndex = physicalRoomIndex,
            });
        }
    }

    std::vector<std::unordered_map<std::uint32_t, MaterialMatch>>
        materialsBySource(sources.size());
    for (std::size_t sourceIndex = 0U;
         sourceIndex < sources.size();
         ++sourceIndex) {
        if (!sources[sourceIndex].placedSceneEnabled) {
            continue;
        }
        auto& matches = materialsBySource[sourceIndex];
        matches.reserve(sources[sourceIndex].ccf->materials.size());
        for (std::size_t materialIndex = 0U;
             materialIndex <
                 sources[sourceIndex].ccf->materials.size();
             ++materialIndex) {
            const auto reference =
                sources[sourceIndex]
                    .ccf->materials[materialIndex].reference;
            const auto [iterator, inserted] = matches.emplace(
                reference,
                MaterialMatch{.index = materialIndex});
            if (!inserted) {
                iterator->second.ambiguous = true;
            }
        }
    }

    std::set<std::pair<std::size_t, std::uint32_t>>
        seenMaterialReferences;
    std::size_t retainedSourceTextBytes = 0U;
    for (const auto& plannedMesh : result.meshes) {
        const auto& ccf = *sources[plannedMesh.sourceIndex].ccf;
        if (plannedMesh.physicalMeshIndex >= ccf.meshes.size()) {
            addIssue(
                result,
                MissionWorldRoomDrawPlanIssueKind::invalidMeshIndex,
                plannedMesh.sourceIndex,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                plannedMesh.physicalMeshIndex);
            failClosed(result);
            return result;
        }
        for (const auto& triangle :
             ccf.meshes[plannedMesh.physicalMeshIndex].triangles) {
            const auto materialKey = std::pair{
                plannedMesh.sourceIndex,
                triangle.materialReference};
            if (seenMaterialReferences.contains(materialKey)) {
                continue;
            }
            if (seenMaterialReferences.size() >=
                limits.maximumMaterialReferences) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        limitExceeded,
                    plannedMesh.sourceIndex,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    plannedMesh.physicalMeshIndex,
                    triangle.materialReference);
                failClosed(result);
                return result;
            }
            seenMaterialReferences.insert(materialKey);

            const auto match =
                materialsBySource[plannedMesh.sourceIndex].find(
                    triangle.materialReference);
            if (match ==
                materialsBySource[plannedMesh.sourceIndex].end()) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        materialNotFound,
                    plannedMesh.sourceIndex,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    plannedMesh.physicalMeshIndex,
                    triangle.materialReference);
                failClosed(result);
                return result;
            }
            if (match->second.ambiguous) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        materialAmbiguous,
                    plannedMesh.sourceIndex,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    plannedMesh.physicalMeshIndex,
                    triangle.materialReference);
                failClosed(result);
                return result;
            }

            const auto materialIndex = match->second.index;
            const auto& material = ccf.materials[materialIndex];
            result.materials.push_back({
                .sourceIndex = plannedMesh.sourceIndex,
                .physicalMaterialIndex = materialIndex,
            });
            const auto addTexture = [&](
                const TextureDependencyRole role,
                const std::optional<std::string>& sourceText) {
                if (!sourceText.has_value()) {
                    return true;
                }
                if (result.textures.size() >=
                    limits.maximumTextureEdges) {
                    return false;
                }
                std::size_t nextBytes = 0U;
                if (!checkedAdd(
                        retainedSourceTextBytes,
                        sourceText->size(),
                        nextBytes) ||
                    nextBytes >
                        limits.maximumRetainedSourceTextBytes) {
                    return false;
                }
                retainedSourceTextBytes = nextBytes;
                result.textures.push_back({
                    .sourceIndex = plannedMesh.sourceIndex,
                    .dependency = {
                        .role = role,
                        .materialReference =
                            triangle.materialReference,
                        .materialIndex = materialIndex,
                        .sourceText = *sourceText,
                    },
                });
                return true;
            };
            if (!addTexture(
                    TextureDependencyRole::primary,
                    material.primaryTexture) ||
                !addTexture(
                    TextureDependencyRole::secondary,
                    material.secondaryTexture) ||
                !addTexture(
                    TextureDependencyRole::environment,
                    material.environmentTexture)) {
                addIssue(
                    result,
                    MissionWorldRoomDrawPlanIssueKind::
                        limitExceeded,
                    plannedMesh.sourceIndex,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    plannedMesh.physicalMeshIndex,
                    triangle.materialReference);
                failClosed(result);
                return result;
            }
        }
    }
    return result;
}

} // namespace airfix::assets
