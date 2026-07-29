#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/content/MissionWorldRoomLoaderDetail.hpp"
#include "airfix/content/MissionWorldRoomPublication.hpp"
#include "airfix/content/PlayerSpawnPoseBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace airfix::content {
namespace {

class SessionIdentityChanged final {};

struct PlannedCcf {
    std::size_t archiveFileIndex{};
    std::size_t firstSourceIndex{};
    std::uint64_t sourceFootprintBytes{};
};

struct CachedCcf {
    std::size_t archiveFileIndex{};
    std::uint64_t sourceFootprintBytes{};
    assets::CcfMetadata metadata;
};

struct PinnedMissionWorldRoomManifestInput {
    ContentRevision revision;
    MissionArchiveEntryIdentity setupEntry;
    std::vector<MissionCcfLoadDescriptor> descriptors;
    std::vector<assets::MissionStartPosition> startPositions;
    bool worldHasBackdrop{};
    std::size_t objectEntryCount{};
    std::vector<std::size_t> objectDefinitionIndices;
    std::uint64_t setupSourceFootprintBytes{};
    std::uint64_t plannedCcfSourceFootprintBytes{};
    std::uint64_t plannedPlayerVisualCcfSourceFootprintBytes{};
    std::uint64_t plannedTotalCcfSourceFootprintBytes{};
    std::optional<MissionPlayerVisualDescriptor> playerVisual;
};

[[nodiscard]] MissionWorldRoomLoadIssue
makeIssue(const MissionWorldRoomLoadIssueKind kind) noexcept {
    return {
        .kind = kind,
        .sourceIndex = std::nullopt,
        .sourceFileIndex = std::nullopt,
        .physicalRoomIndex = std::nullopt,
        .worldRoomIndex = std::nullopt,
        .startPositionIndex = std::nullopt,
        .textureAssetId = std::nullopt,
        .catalogIssue = std::nullopt,
        .spatialArenaIssue = std::nullopt,
        .placedCollisionAssemblyIssue = std::nullopt,
        .placedCollisionSceneIssue = std::nullopt,
        .roomSceneIssue = std::nullopt,
        .startIssue = std::nullopt,
        .textureBindingIssue = std::nullopt,
        .texturePreparationIssue = std::nullopt,
        .drawAssemblyIssue = std::nullopt,
        .submissionIssue = std::nullopt,
        .playerTextureBindingIssue = std::nullopt,
        .playerVisualAssemblyIssue = std::nullopt,
        .playerCollisionAssemblyIssue = std::nullopt,
        .playerSceneAssemblyIssue = std::nullopt,
        .publicationIssue = std::nullopt,
    };
}

void addIssue(MissionWorldRoomLoadResult &result,
              MissionWorldRoomLoadIssue issue) {
    result.room.reset();
    result.issues.push_back(std::move(issue));
}

void addIssue(MissionWorldRoomLoadResult &result,
              const MissionWorldRoomLoadIssueKind kind) {
    addIssue(result, makeIssue(kind));
}

using detail::checkedMissionWorldRoomByteAdd;
using detail::checkedMissionWorldRoomByteProduct;

[[nodiscard]] bool accountCount(std::uint64_t &total, const std::size_t count,
                                const std::size_t elementSize) noexcept {
    std::uint64_t bytes = 0U;
    return checkedMissionWorldRoomByteProduct(count, elementSize, bytes) &&
           checkedMissionWorldRoomByteAdd(total, bytes);
}

template <typename T>
[[nodiscard]] bool accountVector(std::uint64_t &total,
                                 const std::vector<T> &values) noexcept {
    return accountCount(total, values.size(), sizeof(T));
}

[[nodiscard]] bool accountString(std::uint64_t &total,
                                 const std::string &value) noexcept {
    return checkedMissionWorldRoomByteAdd(total, value.size());
}

[[nodiscard]] bool
accountOptionalString(std::uint64_t &total,
                      const std::optional<std::string> &value) noexcept {
    return !value.has_value() || accountString(total, *value);
}

[[nodiscard]] std::uint64_t
sourceAllocationFootprint(const udsp::FileEntry &entry) noexcept {
    return static_cast<std::uint64_t>(entry.storedSize) +
           (entry.isCompressed()
                ? static_cast<std::uint64_t>(entry.unpackedSize)
                : 0U);
}

[[nodiscard]] std::optional<std::string>
canonicalArchiveLogicalPath(const udsp::Archive &archive,
                            const std::size_t directoryIndex,
                            const std::size_t fileIndex) {
    if (fileIndex >= archive.files().size() ||
        directoryIndex >= archive.directories().size()) {
        return std::nullopt;
    }
    std::string result = archive.directories()[directoryIndex].path;
    if (!result.empty()) {
        result.push_back('\\');
    }
    result += archive.files()[fileIndex].name;
    return result;
}

[[nodiscard]] bool
accountPlayerDescriptor(std::uint64_t &total,
                        const MissionPlayerVisualDescriptor &descriptor)
    noexcept {
    return accountString(total, descriptor.objectDefinitionSource.logicalPath) &&
           accountString(total, descriptor.modelCcfSource.logicalPath) &&
           accountString(total, descriptor.blueprintSelector) &&
           accountOptionalString(total, descriptor.textureRoot);
}

[[nodiscard]] render::ConvertedNodeTransform
actorWorldFrom(const simulation::PlayerSpawnPose &pose) noexcept {
    const auto vectorAt = [](const std::array<float, 3U> &value) {
        return render::Vec3{value[0], value[1], value[2]};
    };
    return {
        .linear =
            {
                .columns =
                    {
                        vectorAt(pose.runtimeWorldRotationColumns[0]),
                        vectorAt(pose.runtimeWorldRotationColumns[1]),
                        vectorAt(pose.runtimeWorldRotationColumns[2]),
                    },
            },
        .translation = vectorAt(pose.runtimeWorldPosition),
        .rawScalar = 1.0F,
    };
}

[[nodiscard]] bool
accountChunkChildren(std::uint64_t &total,
                     std::vector<const assets::CcfChunk *> &stack) {
    while (!stack.empty()) {
        const auto *chunk = stack.back();
        stack.pop_back();
        if (!accountVector(total, chunk->directChildren)) {
            return false;
        }
        for (const auto &child : chunk->directChildren) {
            stack.push_back(&child);
        }
    }
    return true;
}

[[nodiscard]] bool
accountChunkVector(std::uint64_t &total,
                   const std::vector<assets::CcfChunk> &chunks,
                   std::vector<const assets::CcfChunk *> &stack) {
    if (!accountVector(total, chunks)) {
        return false;
    }
    for (const auto &chunk : chunks) {
        stack.push_back(&chunk);
    }
    return accountChunkChildren(total, stack);
}

[[nodiscard]] bool
accountBspTrees(std::uint64_t &total,
                const std::vector<assets::CcfBspTreeMetadata> &trees,
                std::vector<const assets::CcfChunk *> &chunkStack) {
    if (!accountVector(total, trees)) {
        return false;
    }
    for (const auto &tree : trees) {
        if (!accountVector(total, tree.nodes) ||
            !accountVector(total, tree.polygons)) {
            return false;
        }
        for (const auto &node : tree.nodes) {
            if (!accountVector(total, node.polygonIndices) ||
                !accountChunkVector(total, node.trailingChildren, chunkStack)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t>
retainedCcfMetadataBytes(const assets::CcfMetadata &ccf) {
    std::uint64_t total = sizeof(assets::CcfMetadata);
    std::vector<const assets::CcfChunk *> chunkStack;

    if (!accountChunkVector(total, ccf.topLevelChunks, chunkStack) ||
        !accountVector(total, ccf.roomSections) ||
        !accountVector(total, ccf.rooms) ||
        !accountVector(total, ccf.materials) ||
        !accountVector(total, ccf.meshes) ||
        !accountVector(total, ccf.blueprints) ||
        !accountVector(total, ccf.placedNodes)) {
        return std::nullopt;
    }

    for (const auto &room : ccf.rooms) {
        if (!accountString(total, room.name) ||
            !accountString(total, room.prefix) ||
            !accountBspTrees(total, room.staticBspTrees, chunkStack) ||
            !accountBspTrees(total, room.portalBspTrees, chunkStack) ||
            !accountChunkVector(total, room.directChildren, chunkStack)) {
            return std::nullopt;
        }
    }

    for (const auto &material : ccf.materials) {
        if (!accountString(total, material.name) ||
            !accountString(total, material.prefix) ||
            !accountOptionalString(total, material.primaryTexture) ||
            !accountOptionalString(total, material.secondaryTexture) ||
            !accountOptionalString(total, material.environmentTexture)) {
            return std::nullopt;
        }
    }

    for (const auto &mesh : ccf.meshes) {
        if (!accountString(total, mesh.name) ||
            !accountString(total, mesh.prefix) ||
            !accountVector(total, mesh.vertices) ||
            !accountVector(total, mesh.triangles)) {
            return std::nullopt;
        }
        for (const auto &triangle : mesh.triangles) {
            if (triangle.paint.has_value() &&
                !accountVector(total, triangle.paint->colors)) {
                return std::nullopt;
            }
        }
    }

    for (const auto &blueprint : ccf.blueprints) {
        if (!accountString(total, blueprint.name) ||
            !accountString(total, blueprint.prefix)) {
            return std::nullopt;
        }
    }

    for (const auto &node : ccf.placedNodes) {
        if (!accountString(total, node.name) ||
            !accountString(total, node.prefix) ||
            !accountChunkVector(total, node.directChildren, chunkStack)) {
            return std::nullopt;
        }
        bool dynamicOk = true;
        std::visit(
            [&](const auto &data) {
                using T = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<T,
                                             assets::CcfPlacedObjectMetadata>) {
                    if (data.bsp4101.has_value()) {
                        chunkStack.push_back(&*data.bsp4101);
                        dynamicOk = accountChunkChildren(total, chunkStack);
                    }
                    if (dynamicOk) {
                        dynamicOk = accountBspTrees(
                            total, data.dynamicBspTrees, chunkStack);
                    }
                } else if constexpr (std::is_same_v<
                                         T, assets::CcfPlacedLightMetadata>) {
                    if (data.property4310.has_value() &&
                        !accountOptionalString(total,
                                               data.property4310->texture)) {
                        dynamicOk = false;
                        return;
                    }
                    if (data.property4320.has_value() &&
                        data.property4320->textures.has_value()) {
                        for (const auto &texture :
                             *data.property4320->textures) {
                            if (!accountString(total, texture)) {
                                dynamicOk = false;
                                return;
                            }
                        }
                    }
                }
            },
            node.data);
        if (!dynamicOk) {
            return std::nullopt;
        }
    }
    return total;
}

[[nodiscard]] bool
addModelPublishedBytes(std::uint64_t &total,
                       const render::DrawModelPayload &model) noexcept {
    if (!accountVector(total, model.meshes) ||
        !accountVector(total, model.instances)) {
        return false;
    }
    for (const auto &mesh : model.meshes) {
        if (!accountVector(total, mesh.vertices) ||
            !accountVector(total, mesh.indices) ||
            !accountVector(total, mesh.materials) ||
            !accountVector(total, mesh.ranges)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
finiteStart(const assets::MissionStartPosition &start) noexcept {
    for (const auto value : start.position) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    for (const auto value : start.axisRotation) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
validateLimits(const MissionWorldRoomLoadLimits &limits) noexcept {
    return limits.maximumCcfSourceBytes != 0U &&
           limits.maximumTextureSourceBytes != 0U &&
           limits.maximumPublishedCpuBytes != 0U &&
           limits.maximumPlayerObjectLogicalPathBytes != 0U &&
           limits.maximumPlayerBlueprintSelectorBytes != 0U &&
           limits.maximumPlayerTextureRootBytes != 0U &&
           limits.textureBindings.maximumCcfLogicalPathBytes != 0U &&
           limits.gtiPerTexture.maximumSourceBytes != 0U;
}

void requireExpectedSession(
    const VerifiedContentSession &session,
    const VerifiedContentTransactionIdentity &expectedIdentity,
    const ContentRevision &expectedRevision) {
    if (session.transactionIdentity() != expectedIdentity ||
        session.revision() != expectedRevision) {
        throw SessionIdentityChanged{};
    }
}

[[nodiscard]] bool report(MissionWorldRoomLoadResult &result,
                          const std::stop_token stopToken,
                          const MissionWorldRoomLoadProgressCallback &callback,
                          const MissionWorldRoomLoadPhase phase,
                          const std::size_t completedItems,
                          const std::size_t totalItems) {
    if (stopToken.stop_requested()) {
        addIssue(result, MissionWorldRoomLoadIssueKind::cancelled);
        return false;
    }
    if (completedItems > totalItems) {
        addIssue(result, MissionWorldRoomLoadIssueKind::internalFailure);
        return false;
    }
    if (callback) {
        try {
            callback({
                .phase = phase,
                .completedItems = completedItems,
                .totalItems = totalItems,
            });
        } catch (const SessionIdentityChanged &) {
            throw;
        } catch (const std::bad_alloc &) {
            throw;
        } catch (...) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::progressCallbackFailure);
            return false;
        }
    }
    if (stopToken.stop_requested()) {
        addIssue(result, MissionWorldRoomLoadIssueKind::cancelled);
        return false;
    }
    return true;
}

[[nodiscard]] std::uint64_t remaining(const std::uint64_t limit,
                                      const std::uint64_t used) noexcept {
    return used <= limit ? limit - used : 0U;
}

[[nodiscard]] std::size_t clampToSize(const std::uint64_t value) noexcept {
    if constexpr (std::numeric_limits<std::size_t>::max() <
                  std::numeric_limits<std::uint64_t>::max()) {
        if (value > std::numeric_limits<std::size_t>::max()) {
            return std::numeric_limits<std::size_t>::max();
        }
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] render::GtiUploadDataIssueKind
mapPlanIssue(const render::GtiUploadIssueKind kind) noexcept {
    switch (kind) {
    case render::GtiUploadIssueKind::limitExceeded:
        return render::GtiUploadDataIssueKind::limitExceeded;
    case render::GtiUploadIssueKind::integerOverflow:
        return render::GtiUploadDataIssueKind::integerOverflow;
    default:
        return render::GtiUploadDataIssueKind::planFailure;
    }
}

[[nodiscard]] bool sameUploadPlan(const render::GtiUploadPlan &left,
                                  const render::GtiUploadPlan &right) noexcept {
    return left.request == right.request &&
           left.variantIndex == right.variantIndex &&
           left.format == right.format && left.checksum == right.checksum &&
           left.mipPolicy == right.mipPolicy &&
           left.uploadLevels == right.uploadLevels &&
           left.allocatedMipCount == right.allocatedMipCount &&
           left.uploadedMipCount == right.uploadedMipCount &&
           left.decodedRgbaBytes == right.decodedRgbaBytes &&
           left.uploadRgbaBytes == right.uploadRgbaBytes &&
           left.residentRgbaBytes == right.residentRgbaBytes;
}

void addTextureIssue(MissionWorldRoomLoadResult &result,
                     const MissionWorldRoomLoadIssueKind kind,
                     const render::TextureImportRequest &request,
                     const std::optional<render::GtiUploadDataIssueKind>
                         upstream = std::nullopt) {
    auto issue = makeIssue(kind);
    issue.sourceFileIndex = request.archiveFileIndex;
    issue.textureAssetId = request.assetId;
    issue.texturePreparationIssue = upstream;
    addIssue(result, std::move(issue));
}

[[nodiscard]] bool accountPublished(std::uint64_t &total,
                                    const std::size_t count,
                                    const std::size_t elementSize,
                                    const std::uint64_t limit) noexcept {
    return accountCount(total, count, elementSize) && total <= limit;
}

} // namespace

MissionWorldRoomLoadResult
loadMissionWorldRoom(VerifiedContentSession &session,
                     const MissionLoadManifest &externalManifest,
                     const MissionWorldRoomLoadRequest &externalRequest,
                     const MissionWorldRoomLoadLimits &externalLimits,
                     const std::stop_token stopToken,
                     MissionWorldRoomLoadProgressCallback progress) {
    MissionWorldRoomLoadResult result;
    try {
        const auto expectedIdentity = session.transactionIdentity();
        const ContentRevision expectedRevision = session.revision();
        if (!expectedIdentity.valid()) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::sessionIdentityChanged);
            return result;
        }

        if (!externalManifest.valid()) {
            addIssue(result, MissionWorldRoomLoadIssueKind::invalidManifest);
            return result;
        }
        if (!externalManifest.belongsTo(session)) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::manifestRevisionMismatch);
            return result;
        }

        const MissionWorldRoomLoadLimits limits = externalLimits;
        if (!validateLimits(limits)) {
            addIssue(result, MissionWorldRoomLoadIssueKind::invalidLimits);
            return result;
        }
        std::size_t initialRootBytes = 0U;
        for (const auto *component :
             {&externalRequest.initialRootName.name,
              &externalRequest.initialRootName.prefix}) {
            if (component->has_value() &&
                (*component)->size() >
                    limits.catalog.maximumNameComponentBytes) {
                auto issue =
                    makeIssue(MissionWorldRoomLoadIssueKind::catalogFailure);
                issue.catalogIssue = assets::MissionWorldRoomBuildIssueKind::
                    nameComponentLimitExceeded;
                addIssue(result, std::move(issue));
                return result;
            }
            if (component->has_value()) {
                if ((*component)->size() >
                    std::numeric_limits<std::size_t>::max() -
                        initialRootBytes) {
                    addIssue(result,
                             MissionWorldRoomLoadIssueKind::integerOverflow);
                    return result;
                }
                initialRootBytes += (*component)->size();
            }
        }
        if (initialRootBytes > limits.catalog.maximumRetainedNameBytes) {
            auto issue =
                makeIssue(MissionWorldRoomLoadIssueKind::catalogFailure);
            issue.catalogIssue = assets::MissionWorldRoomBuildIssueKind::
                retainedNameLimitExceeded;
            addIssue(result, std::move(issue));
            return result;
        }

        const auto &externalStartPositions = externalManifest.startPositions();
        if (externalStartPositions.size() >
            assets::legacyMissionStartCapacity) {
            auto issue = makeIssue(
                MissionWorldRoomLoadIssueKind::startResolutionFailure);
            issue.startPositionIndex = assets::legacyMissionStartCapacity;
            issue.startIssue =
                assets::MissionWorldStartIssueKind::startPositionLimitExceeded;
            addIssue(result, std::move(issue));
            return result;
        }
        for (std::size_t index = 0U;
             index < externalStartPositions.size(); ++index) {
            if (externalStartPositions[index].roomName.size() >
                limits.starts.maximumNameComponentBytes) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::startResolutionFailure);
                issue.startPositionIndex = index;
                issue.startIssue = assets::MissionWorldStartIssueKind::
                    nameComponentLimitExceeded;
                addIssue(result, std::move(issue));
                return result;
            }
        }

        const auto &externalDescriptors = externalManifest.ccfLoads();
        const auto &externalPlayerVisual =
            externalManifest.playerVisual();
        const auto externalObjectEntryCount =
            externalManifest.objectEntries().size();
        const auto &externalObjectDefinitionIndices =
            externalManifest.objectDefinitionIndices();
        const auto playerSemanticSourceCount =
            externalPlayerVisual.has_value() ? 1U : 0U;
        if (externalDescriptors.size() > limits.maximumCcfSources ||
            playerSemanticSourceCount >
                limits.maximumCcfSources - externalDescriptors.size()) {
            addIssue(
                result,
                MissionWorldRoomLoadIssueKind::ccfSourceCountLimitExceeded);
            return result;
        }
        if (externalObjectEntryCount > limits.maximumCcfSources ||
            externalObjectDefinitionIndices.size() >
                limits.maximumCcfSources) {
            addIssue(result, MissionWorldRoomLoadIssueKind::invalidManifest);
            return result;
        }
        for (std::size_t index = 0U; index < externalDescriptors.size();
             ++index) {
            const auto &descriptor = externalDescriptors[index];
            if (descriptor.source.logicalPath.size() >
                    limits.textureBindings.maximumCcfLogicalPathBytes ||
                (descriptor.textureRoot.has_value() &&
                 descriptor.textureRoot->size() >
                     limits.textureBindings.textureEntriesPerSource
                         .maximumLogicalPathBytes)) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::invalidCcfDescriptor);
                issue.sourceIndex = index;
                issue.sourceFileIndex = descriptor.source.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
        }
        if (externalPlayerVisual.has_value()) {
            const auto &player = *externalPlayerVisual;
            const auto invalidDescriptor = [&] {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        invalidPlayerVisualDescriptor);
            };
            if (!player.valid() ||
                player.objectDefinitionSource.logicalPath.size() >
                    limits.maximumPlayerObjectLogicalPathBytes ||
                player.modelCcfSource.logicalPath.size() >
                    limits.textureBindings.maximumCcfLogicalPathBytes ||
                player.blueprintSelector.size() >
                    limits.maximumPlayerBlueprintSelectorBytes ||
                (player.textureRoot.has_value() &&
                 player.textureRoot->size() >
                     limits.maximumPlayerTextureRootBytes) ||
                !udsp::isLogicalPathValid(
                    player.objectDefinitionSource.logicalPath,
                    limits.maximumPlayerObjectLogicalPathBytes) ||
                !udsp::isLogicalPathValid(
                    player.modelCcfSource.logicalPath,
                    limits.textureBindings.maximumCcfLogicalPathBytes)) {
                invalidDescriptor();
                return result;
            }
            const auto &initialArchive = session.sourceArchive();
            const auto validateIdentity =
                [&](const MissionArchiveEntryIdentity &identity,
                    const std::size_t logicalPathLimit,
                    const std::uint64_t expectedFootprint) {
                    if (identity.archiveFileIndex >=
                        initialArchive.files().size()) {
                        return false;
                    }
                    udsp::FileLookupResult lookup;
                    try {
                        lookup = initialArchive.lookup(
                            identity.logicalPath, logicalPathLimit);
                    } catch (const std::bad_alloc &) {
                        throw;
                    } catch (...) {
                        return false;
                    }
                    const auto canonical =
                        canonicalArchiveLogicalPath(
                            initialArchive,
                            lookup.directoryIndex,
                            lookup.fileIndex);
                    return lookup.status == udsp::LookupStatus::unique &&
                           lookup.fileIndex ==
                               identity.archiveFileIndex &&
                           canonical.has_value() &&
                           *canonical == identity.logicalPath &&
                           sourceAllocationFootprint(
                               initialArchive
                                   .files()[lookup.fileIndex]) ==
                               expectedFootprint;
                };
            if (!validateIdentity(
                    player.objectDefinitionSource,
                    limits.maximumPlayerObjectLogicalPathBytes,
                    player
                        .objectDefinitionSourceAllocationFootprintBytes) ||
                !validateIdentity(
                    player.modelCcfSource,
                    limits.textureBindings.maximumCcfLogicalPathBytes,
                    player.modelCcfSourceAllocationFootprintBytes) ||
                externalManifest
                        .plannedPlayerVisualCcfSourceFootprintBytes() !=
                    player.modelCcfSourceAllocationFootprintBytes) {
                invalidDescriptor();
                return result;
            }
            if (player.modelCcfSourceAllocationFootprintBytes >
                limits.maximumCcfSourceBytes) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::ccfSourceLimitExceeded);
                issue.sourceIndex = externalDescriptors.size();
                issue.sourceFileIndex =
                    player.modelCcfSource.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
            auto plannedTotal =
                externalManifest.plannedCcfSourceFootprintBytes();
            if (!checkedMissionWorldRoomByteAdd(
                    plannedTotal,
                    player.modelCcfSourceAllocationFootprintBytes) ||
                plannedTotal !=
                    externalManifest
                        .plannedTotalCcfSourceFootprintBytes()) {
                invalidDescriptor();
                return result;
            }
        } else if (
            externalManifest
                    .plannedPlayerVisualCcfSourceFootprintBytes() != 0U ||
            externalManifest.plannedTotalCcfSourceFootprintBytes() !=
                externalManifest.plannedCcfSourceFootprintBytes()) {
            addIssue(
                result,
                MissionWorldRoomLoadIssueKind::
                    invalidPlayerVisualDescriptor);
            return result;
        }

        const MissionWorldRoomLoadRequest request = externalRequest;
        PinnedMissionWorldRoomManifestInput pinnedManifest{
            .revision = externalManifest.revision(),
            .setupEntry = externalManifest.setupEntry(),
            .descriptors = externalDescriptors,
            .startPositions = externalStartPositions,
            .worldHasBackdrop = externalManifest.world().backdrop.has_value(),
            .objectEntryCount = externalObjectEntryCount,
            .objectDefinitionIndices = externalObjectDefinitionIndices,
            .setupSourceFootprintBytes =
                externalManifest.setupSourceFootprintBytes(),
            .plannedCcfSourceFootprintBytes =
                externalManifest.plannedCcfSourceFootprintBytes(),
            .plannedPlayerVisualCcfSourceFootprintBytes =
                externalManifest
                    .plannedPlayerVisualCcfSourceFootprintBytes(),
            .plannedTotalCcfSourceFootprintBytes =
                externalManifest
                    .plannedTotalCcfSourceFootprintBytes(),
            .playerVisual = externalPlayerVisual,
        };
        if (pinnedManifest.revision != expectedRevision) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::manifestRevisionMismatch);
            return result;
        }
        requireExpectedSession(session, expectedIdentity, expectedRevision);

        MissionWorldRoomLoadProgressCallback guardedProgress =
            [&](const MissionWorldRoomLoadProgress &update) {
                requireExpectedSession(session, expectedIdentity,
                                       expectedRevision);
                if (progress) {
                    try {
                        progress(update);
                    } catch (...) {
                        requireExpectedSession(session, expectedIdentity,
                                               expectedRevision);
                        throw;
                    }
                }
                requireExpectedSession(session, expectedIdentity,
                                       expectedRevision);
            };

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::validatingInput, 0U, 1U)) {
            return result;
        }
        for (std::size_t index = 0U;
             index < pinnedManifest.startPositions.size(); ++index) {
            if (!finiteStart(pinnedManifest.startPositions[index])) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::invalidStartPosition);
                issue.startPositionIndex = index;
                addIssue(result, std::move(issue));
                return result;
            }
        }
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::validatingInput, 1U, 1U)) {
            return result;
        }

        const auto &archive = session.sourceArchive();
        const auto &descriptors = pinnedManifest.descriptors;
        if (descriptors.size() > limits.maximumCcfSources) {
            addIssue(
                result,
                MissionWorldRoomLoadIssueKind::ccfSourceCountLimitExceeded);
            return result;
        }
        if (pinnedManifest.objectEntryCount !=
                pinnedManifest.objectDefinitionIndices.size() ||
            descriptors.empty()) {
            addIssue(result, MissionWorldRoomLoadIssueKind::invalidManifest);
            return result;
        }

        std::size_t expectedSourceCount = 1U;
        if (pinnedManifest.worldHasBackdrop) {
            if (expectedSourceCount ==
                std::numeric_limits<std::size_t>::max()) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            ++expectedSourceCount;
        }
        if (pinnedManifest.objectEntryCount >
            std::numeric_limits<std::size_t>::max() - expectedSourceCount) {
            addIssue(result, MissionWorldRoomLoadIssueKind::integerOverflow);
            return result;
        }
        expectedSourceCount += pinnedManifest.objectEntryCount;
        if (descriptors.size() != expectedSourceCount) {
            addIssue(result, MissionWorldRoomLoadIssueKind::invalidManifest);
            return result;
        }

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::preflightingCcfSources, 0U,
                    descriptors.size())) {
            return result;
        }

        std::unordered_map<std::size_t, std::size_t> cacheSlotByFileIndex;
        cacheSlotByFileIndex.reserve(
            std::min(descriptors.size(), limits.maximumUniqueCcfSources));
        std::vector<PlannedCcf> plannedCcfs;
        plannedCcfs.reserve(
            std::min(descriptors.size(), limits.maximumUniqueCcfSources));
        std::vector<std::size_t> cacheIndexByLoadSource;
        cacheIndexByLoadSource.reserve(descriptors.size());
        std::uint64_t semanticCcfFootprintBytes = 0U;
        std::uint64_t uniqueCcfFootprintBytes = 0U;
        const std::size_t objectSourceBegin =
            pinnedManifest.worldHasBackdrop ? 2U : 1U;

        for (std::size_t sourceIndex = 0U; sourceIndex < descriptors.size();
             ++sourceIndex) {
            const auto &descriptor = descriptors[sourceIndex];
            bool descriptorShapeValid = descriptor.sourceIndex == sourceIndex &&
                                        descriptor.roomSectionEnabled &&
                                        !descriptor.copyPrimaryNameToRoot;
            if (sourceIndex == 0U) {
                descriptorShapeValid =
                    descriptorShapeValid &&
                    descriptor.role == MissionCcfLoadRole::mainWorld &&
                    descriptor.legacyLoadFlags == 0U &&
                    descriptor.placedSceneEnabled &&
                    !descriptor.objectPlacementIndex.has_value() &&
                    !descriptor.uniqueObjectDefinitionIndex.has_value();
            } else if (pinnedManifest.worldHasBackdrop && sourceIndex == 1U) {
                descriptorShapeValid =
                    descriptorShapeValid &&
                    descriptor.role == MissionCcfLoadRole::backdrop &&
                    descriptor.legacyLoadFlags == 0U &&
                    descriptor.placedSceneEnabled &&
                    !descriptor.objectPlacementIndex.has_value() &&
                    !descriptor.uniqueObjectDefinitionIndex.has_value();
            } else {
                const auto placementIndex = sourceIndex - objectSourceBegin;
                descriptorShapeValid =
                    descriptorShapeValid &&
                    placementIndex <
                        pinnedManifest.objectDefinitionIndices.size() &&
                    descriptor.role == MissionCcfLoadRole::objectPlacement &&
                    descriptor.legacyLoadFlags == 0x2000U &&
                    !descriptor.placedSceneEnabled &&
                    descriptor.objectPlacementIndex ==
                        std::optional{placementIndex} &&
                    descriptor.uniqueObjectDefinitionIndex ==
                        std::optional{
                            pinnedManifest
                                .objectDefinitionIndices[placementIndex]};
            }
            if (!descriptorShapeValid ||
                descriptor.source.archiveFileIndex >= archive.files().size() ||
                !udsp::isLogicalPathValid(
                    descriptor.source.logicalPath,
                    limits.textureBindings.maximumCcfLogicalPathBytes)) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::invalidCcfDescriptor);
                issue.sourceIndex = sourceIndex;
                issue.sourceFileIndex = descriptor.source.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }

            udsp::FileLookupResult lookup;
            try {
                lookup = archive.lookup(
                    descriptor.source.logicalPath,
                    limits.textureBindings.maximumCcfLogicalPathBytes);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (...) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::invalidCcfDescriptor);
                issue.sourceIndex = sourceIndex;
                issue.sourceFileIndex = descriptor.source.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
            if (lookup.status != udsp::LookupStatus::unique ||
                lookup.fileIndex != descriptor.source.archiveFileIndex) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::invalidCcfDescriptor);
                issue.sourceIndex = sourceIndex;
                issue.sourceFileIndex = descriptor.source.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }

            const auto footprint =
                sourceAllocationFootprint(archive.files()[lookup.fileIndex]);
            if (footprint != descriptor.sourceAllocationFootprintBytes) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::invalidCcfDescriptor);
                issue.sourceIndex = sourceIndex;
                issue.sourceFileIndex = lookup.fileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
            if (footprint > limits.maximumCcfSourceBytes) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::ccfSourceLimitExceeded);
                issue.sourceIndex = sourceIndex;
                issue.sourceFileIndex = lookup.fileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
            if (!checkedMissionWorldRoomByteAdd(semanticCcfFootprintBytes,
                                                footprint)) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::integerOverflow);
                return result;
            }

            auto found = cacheSlotByFileIndex.find(lookup.fileIndex);
            if (found == cacheSlotByFileIndex.end()) {
                if (plannedCcfs.size() >= limits.maximumUniqueCcfSources) {
                    auto issue =
                        makeIssue(MissionWorldRoomLoadIssueKind::
                                      uniqueCcfSourceCountLimitExceeded);
                    issue.sourceIndex = sourceIndex;
                    issue.sourceFileIndex = lookup.fileIndex;
                    addIssue(result, std::move(issue));
                    return result;
                }
                auto nextUniqueFootprint = uniqueCcfFootprintBytes;
                if (!checkedMissionWorldRoomByteAdd(nextUniqueFootprint,
                                                    footprint)) {
                    addIssue(result,
                             MissionWorldRoomLoadIssueKind::integerOverflow);
                    return result;
                }
                if (nextUniqueFootprint >
                    limits.maximumTotalUniqueCcfSourceBytes) {
                    auto issue = makeIssue(MissionWorldRoomLoadIssueKind::
                                               aggregateCcfSourceLimitExceeded);
                    issue.sourceIndex = sourceIndex;
                    issue.sourceFileIndex = lookup.fileIndex;
                    addIssue(result, std::move(issue));
                    return result;
                }
                const auto slot = plannedCcfs.size();
                plannedCcfs.push_back({
                    .archiveFileIndex = lookup.fileIndex,
                    .firstSourceIndex = sourceIndex,
                    .sourceFootprintBytes = footprint,
                });
                found =
                    cacheSlotByFileIndex.emplace(lookup.fileIndex, slot).first;
                uniqueCcfFootprintBytes = nextUniqueFootprint;
            }
            cacheIndexByLoadSource.push_back(found->second);

            if (!report(result, stopToken, guardedProgress,
                        MissionWorldRoomLoadPhase::preflightingCcfSources,
                        sourceIndex + 1U, descriptors.size())) {
                return result;
            }
        }
        if (semanticCcfFootprintBytes !=
            pinnedManifest.plannedCcfSourceFootprintBytes) {
            addIssue(result, MissionWorldRoomLoadIssueKind::invalidManifest);
            return result;
        }

        std::optional<std::size_t> playerVisualCcfCacheIndex;
        if (pinnedManifest.playerVisual.has_value()) {
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        preflightingPlayerCcfSource,
                    0U,
                    1U)) {
                return result;
            }
            const auto &player = *pinnedManifest.playerVisual;
            const auto fileIndex =
                player.modelCcfSource.archiveFileIndex;
            const auto footprint =
                player.modelCcfSourceAllocationFootprintBytes;
            auto found = cacheSlotByFileIndex.find(fileIndex);
            if (found == cacheSlotByFileIndex.end()) {
                if (plannedCcfs.size() >=
                    limits.maximumUniqueCcfSources) {
                    auto issue = makeIssue(
                        MissionWorldRoomLoadIssueKind::
                            uniqueCcfSourceCountLimitExceeded);
                    issue.sourceIndex = descriptors.size();
                    issue.sourceFileIndex = fileIndex;
                    addIssue(result, std::move(issue));
                    return result;
                }
                auto nextUniqueFootprint =
                    uniqueCcfFootprintBytes;
                if (!checkedMissionWorldRoomByteAdd(
                        nextUniqueFootprint, footprint)) {
                    addIssue(
                        result,
                        MissionWorldRoomLoadIssueKind::integerOverflow);
                    return result;
                }
                if (nextUniqueFootprint >
                    limits.maximumTotalUniqueCcfSourceBytes) {
                    auto issue = makeIssue(
                        MissionWorldRoomLoadIssueKind::
                            aggregateCcfSourceLimitExceeded);
                    issue.sourceIndex = descriptors.size();
                    issue.sourceFileIndex = fileIndex;
                    addIssue(result, std::move(issue));
                    return result;
                }
                const auto slot = plannedCcfs.size();
                plannedCcfs.push_back({
                    .archiveFileIndex = fileIndex,
                    .firstSourceIndex = descriptors.size(),
                    .sourceFootprintBytes = footprint,
                });
                found =
                    cacheSlotByFileIndex.emplace(fileIndex, slot).first;
                uniqueCcfFootprintBytes = nextUniqueFootprint;
            }
            playerVisualCcfCacheIndex = found->second;

            auto semanticTotal = semanticCcfFootprintBytes;
            if (!checkedMissionWorldRoomByteAdd(
                    semanticTotal, footprint) ||
                semanticTotal !=
                    pinnedManifest
                        .plannedTotalCcfSourceFootprintBytes) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        invalidPlayerVisualDescriptor);
                return result;
            }
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        preflightingPlayerCcfSource,
                    1U,
                    1U)) {
                return result;
            }
        }

        std::vector<CachedCcf> cachedCcfs;
        cachedCcfs.reserve(plannedCcfs.size());
        std::uint64_t retainedMetadataBytes = 0U;
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::loadingCcfSources, 0U,
                    plannedCcfs.size())) {
            return result;
        }
        for (std::size_t cacheIndex = 0U; cacheIndex < plannedCcfs.size();
             ++cacheIndex) {
            const auto &planned = plannedCcfs[cacheIndex];
            requireExpectedSession(session, expectedIdentity, expectedRevision);
            std::vector<std::uint8_t> ccfBytes;
            try {
                ccfBytes = session.readSourceFile(planned.archiveFileIndex,
                                                  limits.maximumCcfSourceBytes);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (...) {
                requireExpectedSession(session, expectedIdentity,
                                       expectedRevision);
                auto issue =
                    makeIssue(MissionWorldRoomLoadIssueKind::ccfReadFailure);
                issue.sourceIndex = planned.firstSourceIndex;
                issue.sourceFileIndex = planned.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
            requireExpectedSession(session, expectedIdentity, expectedRevision);
            if (stopToken.stop_requested()) {
                addIssue(result, MissionWorldRoomLoadIssueKind::cancelled);
                return result;
            }

            assets::CcfMetadata ccf;
            try {
                ccf = assets::parseCcf(ccfBytes);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (...) {
                auto issue =
                    makeIssue(MissionWorldRoomLoadIssueKind::ccfParseFailure);
                issue.sourceIndex = planned.firstSourceIndex;
                issue.sourceFileIndex = planned.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
            requireExpectedSession(session, expectedIdentity, expectedRevision);
            if (stopToken.stop_requested()) {
                addIssue(result, MissionWorldRoomLoadIssueKind::cancelled);
                return result;
            }
            std::vector<std::uint8_t>().swap(ccfBytes);
            const auto metadataBytes = retainedCcfMetadataBytes(ccf);
            if (!metadataBytes.has_value() ||
                !checkedMissionWorldRoomByteAdd(retainedMetadataBytes,
                                                *metadataBytes)) {
                auto issue =
                    makeIssue(MissionWorldRoomLoadIssueKind::integerOverflow);
                issue.sourceIndex = planned.firstSourceIndex;
                issue.sourceFileIndex = planned.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
            if (retainedMetadataBytes >
                limits.maximumRetainedCcfMetadataBytesAfterParse) {
                auto issue = makeIssue(MissionWorldRoomLoadIssueKind::
                                           retainedCcfMetadataLimitExceeded);
                issue.sourceIndex = planned.firstSourceIndex;
                issue.sourceFileIndex = planned.archiveFileIndex;
                addIssue(result, std::move(issue));
                return result;
            }
            cachedCcfs.push_back({
                .archiveFileIndex = planned.archiveFileIndex,
                .sourceFootprintBytes = planned.sourceFootprintBytes,
                .metadata = std::move(ccf),
            });
            if (!report(result, stopToken, guardedProgress,
                        MissionWorldRoomLoadPhase::loadingCcfSources,
                        cacheIndex + 1U, plannedCcfs.size())) {
                return result;
            }
        }

        std::vector<assets::MissionCcfRoomLoadSource> loadSources;
        std::vector<render::MissionWorldRoomTextureSource> textureSources;
        loadSources.reserve(descriptors.size());
        textureSources.reserve(descriptors.size());
        for (std::size_t sourceIndex = 0U; sourceIndex < descriptors.size();
             ++sourceIndex) {
            const auto cacheIndex = cacheIndexByLoadSource[sourceIndex];
            if (cacheIndex >= cachedCcfs.size()) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::internalFailure);
                return result;
            }
            const auto &descriptor = descriptors[sourceIndex];
            const auto *ccf = &cachedCcfs[cacheIndex].metadata;
            loadSources.push_back({
                .ccf = ccf,
                .roomSectionEnabled = descriptor.roomSectionEnabled,
                .copyPrimaryNameToRoot = descriptor.copyPrimaryNameToRoot,
                .placedSceneEnabled = descriptor.placedSceneEnabled,
            });
            std::optional<std::string_view> textureRoot;
            if (descriptor.textureRoot.has_value()) {
                textureRoot = *descriptor.textureRoot;
            }
            textureSources.push_back({
                .ccf = ccf,
                .textureRoot = textureRoot,
                .ccfLogicalPath = descriptor.source.logicalPath,
                .ccfArchiveFileIndex = descriptor.source.archiveFileIndex,
            });
        }

        std::optional<assets::ObjectDefinition> playerObjectDefinition;
        const assets::CcfMetadata *playerCcf = nullptr;
        if (pinnedManifest.playerVisual.has_value()) {
            if (!playerVisualCcfCacheIndex.has_value() ||
                *playerVisualCcfCacheIndex >= cachedCcfs.size()) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::internalFailure);
                return result;
            }
            const auto &player = *pinnedManifest.playerVisual;
            playerCcf =
                &cachedCcfs[*playerVisualCcfCacheIndex].metadata;
            playerObjectDefinition = assets::ObjectDefinition{
                .kind = assets::ObjectDefinitionKind::object,
                .type = std::nullopt,
                .category = std::nullopt,
                .name = std::nullopt,
                .nationality = std::nullopt,
                .textureRoot = player.textureRoot,
                .ccfPath =
                    std::optional<std::string>{
                        player.modelCcfSource.logicalPath},
                .meshName =
                    std::optional<std::string>{
                        player.blueprintSelector},
                .gravity = std::nullopt,
                .hidden = false,
                .unknownChunks = {},
            };
        }

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::buildingRoomCatalog, 0U, 1U)) {
            return result;
        }
        auto catalogLimits = limits.catalog;
        catalogLimits.maximumSources =
            std::min(catalogLimits.maximumSources, limits.maximumCcfSources);
        assets::MissionWorldRoomCatalog catalog;
        try {
            catalog = assets::buildMissionWorldRoomCatalog(
                {
                    .initialRootName = request.initialRootName,
                    .sources = loadSources,
                },
                catalogLimits);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (...) {
            addIssue(result, MissionWorldRoomLoadIssueKind::catalogFailure);
            return result;
        }
        if (!catalog.complete()) {
            for (const auto &upstream : catalog.issues) {
                auto issue =
                    makeIssue(MissionWorldRoomLoadIssueKind::catalogFailure);
                issue.sourceIndex = upstream.sourceIndex;
                issue.catalogIssue = upstream.kind;
                addIssue(result, std::move(issue));
            }
            if (catalog.issues.empty()) {
                addIssue(result, MissionWorldRoomLoadIssueKind::catalogFailure);
            }
            return result;
        }
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::buildingRoomCatalog, 1U, 1U)) {
            return result;
        }

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::buildingSpatialArena, 0U,
                    1U)) {
            return result;
        }
        auto spatialLimits = limits.spatialArena;
        spatialLimits.maximumSources = std::min(
            spatialLimits.maximumSources, limits.maximumCcfSources);
        spatialLimits.maximumWorldRooms = std::min(
            spatialLimits.maximumWorldRooms,
            catalogLimits.maximumRuntimeRooms);
        spatialLimits.catalogAuthentication = catalogLimits;
        assets::MissionWorldSpatialArena spatialArena;
        try {
            spatialArena = assets::buildMissionWorldSpatialArena(
                loadSources, catalog, spatialLimits);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (...) {
            addIssue(
                result,
                MissionWorldRoomLoadIssueKind::spatialArenaFailure);
            return result;
        }
        if (!spatialArena.complete()) {
            for (const auto &upstream : spatialArena.issues) {
                if (upstream.kind ==
                    assets::MissionWorldSpatialArenaIssueKind::
                        allocationFailure) {
                    addIssue(
                        result,
                        MissionWorldRoomLoadIssueKind::allocationFailure);
                    return result;
                }
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::spatialArenaFailure);
                issue.sourceIndex = upstream.sourceIndex;
                issue.physicalRoomIndex =
                    upstream.physicalRoomIndex;
                issue.worldRoomIndex = upstream.worldRoomIndex;
                issue.spatialArenaIssue = upstream.kind;
                issue.roomSceneIssue = upstream.roomSceneIssue;
                addIssue(result, std::move(issue));
            }
            if (spatialArena.issues.empty()) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::spatialArenaFailure);
            }
            return result;
        }
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::buildingSpatialArena, 1U,
                    1U)) {
            return result;
        }

        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionWorldRoomLoadPhase::assemblingPlacedCollision,
                0U,
                1U)) {
            return result;
        }
        auto placedCollisionLimits = limits.placedCollision;
        placedCollisionLimits.maximumSources = std::min(
            placedCollisionLimits.maximumSources,
            limits.maximumCcfSources);
        placedCollisionLimits.maximumWorldRooms = std::min(
            placedCollisionLimits.maximumWorldRooms,
            catalogLimits.maximumRuntimeRooms);
        placedCollisionLimits.maximumRetainedBytes = std::min(
            placedCollisionLimits.maximumRetainedBytes,
            limits.maximumPublishedCpuBytes);
        placedCollisionLimits.catalogAuthentication = catalogLimits;
        render::MissionPlacedDynamicBspAssembly placedDynamicCollision;
        try {
            placedDynamicCollision =
                render::buildMissionPlacedDynamicBspAssembly(
                    loadSources,
                    catalog,
                    request.basis,
                    placedCollisionLimits);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (...) {
            addIssue(
                result,
                MissionWorldRoomLoadIssueKind::
                    placedCollisionAssemblyFailure);
            return result;
        }
        if (!placedDynamicCollision.complete()) {
            for (const auto& upstream :
                 placedDynamicCollision.issues) {
                if (upstream.kind ==
                    render::MissionPlacedDynamicBspIssueKind::
                        allocationFailure) {
                    addIssue(
                        result,
                        MissionWorldRoomLoadIssueKind::allocationFailure);
                    return result;
                }
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::
                        placedCollisionAssemblyFailure);
                issue.sourceIndex = upstream.sourceIndex;
                issue.physicalRoomIndex =
                    upstream.physicalRoomIndex;
                issue.worldRoomIndex = upstream.worldRoomIndex;
                issue.placedCollisionAssemblyIssue = upstream.kind;
                issue.placedCollisionSceneIssue =
                    upstream.placedSceneIssue;
                addIssue(result, std::move(issue));
            }
            if (placedDynamicCollision.issues.empty()) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        placedCollisionAssemblyFailure);
            }
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionWorldRoomLoadPhase::assemblingPlacedCollision,
                1U,
                1U)) {
            return result;
        }

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::resolvingStart, 0U, 1U)) {
            return result;
        }
        assets::MissionWorldStartResolution startResolution;
        try {
            startResolution = assets::resolveMissionStartsInWorld(
                pinnedManifest.startPositions, catalog, limits.starts);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (...) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::startResolutionFailure);
            return result;
        }
        if (!startResolution.complete()) {
            for (const auto &upstream : startResolution.issues) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::startResolutionFailure);
                issue.startPositionIndex = upstream.startPositionIndex;
                issue.startIssue = upstream.kind;
                addIssue(result, std::move(issue));
            }
            if (startResolution.issues.empty()) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::startResolutionFailure);
            }
            return result;
        }
        const auto selection = assets::selectMissionWorldStart(
            startResolution, request.requestedStartIndex);
        if (!selection.has_value()) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::startSelectionFailure);
            return result;
        }
        std::optional<assets::MissionStartPosition> selectedStart;
        if (selection->startPositionIndex.has_value()) {
            const auto index = *selection->startPositionIndex;
            if (index >= pinnedManifest.startPositions.size()) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::startSelectionFailure);
                return result;
            }
            selectedStart = pinnedManifest.startPositions[index];
        }
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::resolvingStart, 1U, 1U)) {
            return result;
        }
        const auto playerSpawnPose =
            buildPlayerSpawnPose(*selection, selectedStart, request.basis);
        if (!playerSpawnPose.success()) {
            addIssue(
                result,
                playerSpawnPose.issue ==
                        PlayerSpawnPoseBuildIssue::invalidSelection
                    ? MissionWorldRoomLoadIssueKind::startSelectionFailure
                    : MissionWorldRoomLoadIssueKind::invalidRequest);
            return result;
        }

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::planningTextureBindings, 0U,
                    1U)) {
            return result;
        }
        auto textureBindingLimits = limits.textureBindings;
        textureBindingLimits.maximumSources = std::min(
            textureBindingLimits.maximumSources, limits.maximumCcfSources);
        const bool outerTextureAssetLimitClamped =
            limits.maximumTextureAssets < textureBindingLimits.maximumImports;
        textureBindingLimits.maximumImports = std::min(
            textureBindingLimits.maximumImports, limits.maximumTextureAssets);
        render::MissionWorldRoomTextureBindings binding;
        try {
            binding = render::buildMissionWorldRoomTextureBindings(
                catalog, loadSources, selection->worldRoomIndex, textureSources,
                archive, textureBindingLimits);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (...) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::textureBindingFailure);
            return result;
        }
        if (!binding.complete()) {
            if (outerTextureAssetLimitClamped && binding.issues.size() == 1U &&
                binding.issues.front().kind ==
                    render::MissionWorldRoomTextureBindingIssueKind::
                        limitExceeded &&
                binding.issues.front().sourceIndex.has_value() &&
                binding.issues.front().textureEntryIndex.has_value()) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::textureAssetLimitExceeded);
                return result;
            }
            for (const auto &upstream : binding.issues) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::textureBindingFailure);
                issue.sourceIndex = upstream.sourceIndex;
                issue.textureBindingIssue = upstream.kind;
                addIssue(result, std::move(issue));
            }
            if (binding.issues.empty()) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::textureBindingFailure);
            }
            return result;
        }
        if (binding.materialBindingsBySource.size() != loadSources.size() ||
            binding.worldRoomIndex !=
                std::optional{selection->worldRoomIndex}) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::textureBindingFailure);
            return result;
        }
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::planningTextureBindings, 1U,
                    1U)) {
            return result;
        }

        std::optional<render::PlayerActorTextureBindings>
            playerTextureBindings;
        if (pinnedManifest.playerVisual.has_value()) {
            if (!playerObjectDefinition.has_value() ||
                playerCcf == nullptr) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::internalFailure);
                return result;
            }
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        planningPlayerTextureBindings,
                    0U,
                    1U)) {
                return result;
            }
            auto playerTextureLimits =
                limits.playerTextureBindings;
            playerTextureLimits.maximumBaseImports = std::min(
                playerTextureLimits.maximumBaseImports,
                limits.maximumTextureAssets);
            playerTextureLimits.maximumGlobalImports = std::min(
                playerTextureLimits.maximumGlobalImports,
                limits.maximumTextureAssets);
            playerTextureLimits.textureEntries.maximumLogicalPathBytes =
                std::min(
                    playerTextureLimits.textureEntries
                        .maximumLogicalPathBytes,
                    limits.maximumPlayerTextureRootBytes);
            try {
                requireExpectedSession(
                    session, expectedIdentity, expectedRevision);
                playerTextureBindings =
                    render::buildPlayerActorTextureBindings(
                        binding.imports,
                        *playerObjectDefinition,
                        *playerCcf,
                        archive,
                        playerTextureLimits);
                requireExpectedSession(
                    session, expectedIdentity, expectedRevision);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (const SessionIdentityChanged &) {
                throw;
            } catch (...) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        playerTextureBindingFailure);
                return result;
            }
            if (!playerTextureBindings->complete()) {
                for (const auto &upstream :
                     playerTextureBindings->issues) {
                    auto issue = makeIssue(
                        MissionWorldRoomLoadIssueKind::
                            playerTextureBindingFailure);
                    issue.playerTextureBindingIssue =
                        upstream.kind;
                    issue.sourceFileIndex =
                        upstream.archiveFileIndex;
                    addIssue(result, std::move(issue));
                }
                if (playerTextureBindings->issues.empty()) {
                    addIssue(
                        result,
                        MissionWorldRoomLoadIssueKind::
                            playerTextureBindingFailure);
                }
                return result;
            }
            if (playerTextureBindings->imports.size() <
                    binding.imports.size() ||
                !std::equal(
                    binding.imports.begin(),
                    binding.imports.end(),
                    playerTextureBindings->imports.begin())) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        playerTextureBindingFailure);
                return result;
            }
            binding.imports =
                std::move(playerTextureBindings->imports);
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        planningPlayerTextureBindings,
                    1U,
                    1U)) {
                return result;
            }
        }

        if (binding.imports.size() > limits.maximumTextureAssets) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::textureAssetLimitExceeded);
            return result;
        }
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::preflightingTextures, 0U,
                    binding.imports.size())) {
            return result;
        }
        std::unordered_set<std::size_t> uniqueTextureFiles;
        uniqueTextureFiles.reserve(binding.imports.size());
        std::uint64_t textureSourceFootprintBytes = 0U;
        for (std::size_t index = 0U; index < binding.imports.size(); ++index) {
            const auto &import = binding.imports[index];
            if (static_cast<std::size_t>(import.assetId.value) != index ||
                import.archiveFileIndex >= archive.files().size() ||
                !uniqueTextureFiles.insert(import.archiveFileIndex).second) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::invalidTextureImport);
                issue.sourceFileIndex = import.archiveFileIndex;
                issue.textureAssetId = import.assetId;
                addIssue(result, std::move(issue));
                return result;
            }
            const auto footprint = sourceAllocationFootprint(
                archive.files()[import.archiveFileIndex]);
            if (footprint > limits.maximumTextureSourceBytes ||
                footprint > limits.gtiPerTexture.maximumSourceBytes) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::textureSourceLimitExceeded,
                    import);
                return result;
            }
            if (!checkedMissionWorldRoomByteAdd(textureSourceFootprintBytes,
                                                footprint)) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            if (textureSourceFootprintBytes >
                limits.maximumTotalTextureSourceBytes) {
                addTextureIssue(result,
                                MissionWorldRoomLoadIssueKind::
                                    aggregateTextureSourceLimitExceeded,
                                import);
                return result;
            }
            if (!report(result, stopToken, guardedProgress,
                        MissionWorldRoomLoadPhase::preflightingTextures,
                        index + 1U, binding.imports.size())) {
                return result;
            }
        }

        std::uint64_t publishedCpuBytes = sizeof(LoadedMissionWorldRoom);
        if (!accountString(publishedCpuBytes,
                           pinnedManifest.setupEntry.logicalPath) ||
            (pinnedManifest.playerVisual.has_value() &&
             !accountPlayerDescriptor(
                 publishedCpuBytes,
                 *pinnedManifest.playerVisual)) ||
            !accountPublished(publishedCpuBytes, binding.imports.size(),
                              sizeof(LoadedTextureAsset),
                              limits.maximumPublishedCpuBytes) ||
            !accountPublished(publishedCpuBytes, cacheIndexByLoadSource.size(),
                              sizeof(std::size_t),
                              limits.maximumPublishedCpuBytes) ||
            !checkedMissionWorldRoomByteAdd(
                publishedCpuBytes,
                spatialArena.retainedPayloadBytes) ||
            !checkedMissionWorldRoomByteAdd(
                publishedCpuBytes,
                placedDynamicCollision.retainedPayloadBytes) ||
            publishedCpuBytes > limits.maximumPublishedCpuBytes ||
            (selectedStart.has_value() &&
             (!checkedMissionWorldRoomByteAdd(publishedCpuBytes,
                                              selectedStart->roomName.size()) ||
              publishedCpuBytes > limits.maximumPublishedCpuBytes))) {
            addIssue(
                result,
                publishedCpuBytes > limits.maximumPublishedCpuBytes
                    ? MissionWorldRoomLoadIssueKind::publishedCpuLimitExceeded
                    : MissionWorldRoomLoadIssueKind::integerOverflow);
            return result;
        }

        LoadedMissionWorldRoom candidate{
            .revision = expectedRevision,
            .setupEntry = std::move(pinnedManifest.setupEntry),
            .setupSourceFootprintBytes =
                pinnedManifest.setupSourceFootprintBytes,
            .startSelection = *selection,
            .selectedStart = std::move(selectedStart),
            .runtimeBasis = request.basis,
            .playerSpawnPose = *playerSpawnPose.pose,
            .spatialArena = std::move(spatialArena),
            .placedDynamicCollision =
                std::move(placedDynamicCollision),
            .model = {},
            .meshProvenance = {},
            .instanceProvenance = {},
            .playerVisual = pinnedManifest.playerVisual,
            .playerActorMeshProvenance = {},
            .playerActorInstanceProvenance = {},
            .playerActorBinding = std::nullopt,
            .playerActorCollision = std::nullopt,
            .submission = {},
            .textures = {},
            .semanticCcfSourceCount = descriptors.size(),
            .uniqueCcfSourceCount = cachedCcfs.size(),
            .uniqueCcfSourceFootprintBytes = uniqueCcfFootprintBytes,
            .retainedCcfMetadataBytes = retainedMetadataBytes,
            .retainedSpatialBytes = 0U,
            .textureSourceFootprintBytes = textureSourceFootprintBytes,
            .decodedRgbaBytes = 0U,
            .uploadRgbaBytes = 0U,
            .residentRgbaBytes = 0U,
            .publishedCpuBytes = 0U,
            .ccfCacheIndexByLoadSource = std::move(cacheIndexByLoadSource),
            .playerVisualCcfCacheIndex =
                playerVisualCcfCacheIndex,
        };
        candidate.retainedSpatialBytes =
            candidate.spatialArena.retainedPayloadBytes;
        candidate.textures.reserve(binding.imports.size());

        std::uint64_t decodedRgbaBytes = 0U;
        std::uint64_t uploadRgbaBytes = 0U;
        std::uint64_t residentRgbaBytes = 0U;
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::loadingTextures, 0U,
                    binding.imports.size())) {
            return result;
        }
        for (std::size_t index = 0U; index < binding.imports.size(); ++index) {
            const auto &import = binding.imports[index];
            requireExpectedSession(session, expectedIdentity, expectedRevision);
            std::vector<std::uint8_t> gtiBytes;
            try {
                gtiBytes = session.readSourceFile(
                    import.archiveFileIndex,
                    std::min(limits.maximumTextureSourceBytes,
                             limits.gtiPerTexture.maximumSourceBytes));
            } catch (const std::bad_alloc &) {
                throw;
            } catch (...) {
                requireExpectedSession(session, expectedIdentity,
                                       expectedRevision);
                addTextureIssue(
                    result, MissionWorldRoomLoadIssueKind::textureReadFailure,
                    import);
                return result;
            }
            requireExpectedSession(session, expectedIdentity, expectedRevision);
            if (stopToken.stop_requested()) {
                addIssue(result, MissionWorldRoomLoadIssueKind::cancelled);
                return result;
            }

            auto perAssetLimits = limits.gtiPerTexture;
            perAssetLimits.maximumSourceBytes =
                std::min(perAssetLimits.maximumSourceBytes,
                         limits.maximumTextureSourceBytes);

            assets::GtiMetadata metadata;
            try {
                metadata = assets::parseGti(
                    gtiBytes,
                    assets::GtiParseLimits{
                        .maximumVariants =
                            perAssetLimits.upload.maximumVariants,
                        .maximumMetadataBytes =
                            perAssetLimits.upload.maximumMetadataBytes,
                    });
            } catch (const assets::GtiParseLimitError &) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::texturePreparationFailure,
                    import, render::GtiUploadDataIssueKind::limitExceeded);
                return result;
            } catch (const std::bad_alloc &) {
                throw;
            } catch (...) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::texturePreparationFailure,
                    import, render::GtiUploadDataIssueKind::parseFailure);
                return result;
            }
            requireExpectedSession(session, expectedIdentity, expectedRevision);
            if (stopToken.stop_requested()) {
                addIssue(result, MissionWorldRoomLoadIssueKind::cancelled);
                return result;
            }

            render::GtiUploadDescription description;
            try {
                description = render::describeGtiUpload(import, metadata,
                                                        perAssetLimits.upload);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (...) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::texturePreparationFailure,
                    import, render::GtiUploadDataIssueKind::planFailure);
                return result;
            }
            if (!description.issues.empty()) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::texturePreparationFailure,
                    import, mapPlanIssue(description.issues.front().kind));
                return result;
            }
            if (!description.plan.has_value()) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::texturePreparationFailure,
                    import, render::GtiUploadDataIssueKind::planFailure);
                return result;
            }
            const auto &plannedUpload = *description.plan;
            if (plannedUpload.decodedRgbaBytes >
                remaining(limits.maximumDecodedRgbaBytes, decodedRgbaBytes)) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::decodedRgbaLimitExceeded,
                    import);
                return result;
            }
            if (plannedUpload.uploadRgbaBytes >
                remaining(limits.maximumUploadRgbaBytes, uploadRgbaBytes)) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::uploadRgbaLimitExceeded,
                    import);
                return result;
            }
            if (plannedUpload.residentRgbaBytes >
                remaining(limits.maximumResidentRgbaBytes, residentRgbaBytes)) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::residentRgbaLimitExceeded,
                    import);
                return result;
            }

            auto prospectivePublished = publishedCpuBytes;
            if (!accountPublished(prospectivePublished,
                                  plannedUpload.uploadLevels.size(),
                                  sizeof(render::GtiUploadLevel),
                                  limits.maximumPublishedCpuBytes) ||
                !accountPublished(prospectivePublished,
                                  plannedUpload.uploadLevels.size(),
                                  sizeof(assets::RgbaImage),
                                  limits.maximumPublishedCpuBytes) ||
                !checkedMissionWorldRoomByteAdd(
                    prospectivePublished, plannedUpload.decodedRgbaBytes) ||
                prospectivePublished > limits.maximumPublishedCpuBytes) {
                addTextureIssue(
                    result,
                    prospectivePublished > limits.maximumPublishedCpuBytes
                        ? MissionWorldRoomLoadIssueKind::
                              publishedCpuLimitExceeded
                        : MissionWorldRoomLoadIssueKind::integerOverflow,
                    import);
                return result;
            }
            if (stopToken.stop_requested()) {
                addIssue(result, MissionWorldRoomLoadIssueKind::cancelled);
                return result;
            }

            render::GtiUploadPreparation preparation;
            try {
                preparation =
                    render::prepareGtiUpload(import, gtiBytes, perAssetLimits);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (...) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::texturePreparationFailure,
                    import, render::GtiUploadDataIssueKind::decodeFailure);
                return result;
            }
            std::vector<std::uint8_t>().swap(gtiBytes);
            if (!preparation.success()) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::texturePreparationFailure,
                    import,
                    preparation.issues.empty()
                        ? std::optional{render::GtiUploadDataIssueKind::
                                            decodeFailure}
                        : std::optional{preparation.issues.front().kind});
                return result;
            }
            if (!sameUploadPlan(plannedUpload, *preparation.plan)) {
                addTextureIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::texturePreparationFailure,
                    import, render::GtiUploadDataIssueKind::planMismatch);
                return result;
            }
            const auto &upload = *preparation.plan;
            if (!checkedMissionWorldRoomByteAdd(decodedRgbaBytes,
                                                upload.decodedRgbaBytes) ||
                !checkedMissionWorldRoomByteAdd(uploadRgbaBytes,
                                                upload.uploadRgbaBytes) ||
                !checkedMissionWorldRoomByteAdd(residentRgbaBytes,
                                                upload.residentRgbaBytes)) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            publishedCpuBytes = prospectivePublished;
            candidate.textures.push_back({
                .assetId = import.assetId,
                .sourceFileIndex = import.archiveFileIndex,
                .upload = std::move(*preparation.plan),
                .uploadLevels = std::move(preparation.uploadLevels),
            });
            if (!report(result, stopToken, guardedProgress,
                        MissionWorldRoomLoadPhase::loadingTextures, index + 1U,
                        binding.imports.size())) {
                return result;
            }
        }

        candidate.decodedRgbaBytes = decodedRgbaBytes;
        candidate.uploadRgbaBytes = uploadRgbaBytes;
        candidate.residentRgbaBytes = residentRgbaBytes;

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::assemblingRoom, 0U, 1U)) {
            return result;
        }
        std::vector<render::MissionWorldRoomDrawSource> drawSources;
        drawSources.reserve(loadSources.size());
        for (std::size_t sourceIndex = 0U; sourceIndex < loadSources.size();
             ++sourceIndex) {
            drawSources.push_back({
                .ccf = loadSources[sourceIndex].ccf,
                .materialBindings =
                    binding.materialBindingsBySource[sourceIndex],
            });
        }
        auto drawLimits = limits.draw;
        drawLimits.maximumSources =
            std::min(drawLimits.maximumSources, limits.maximumCcfSources);
        drawLimits.maximumTotalBytes =
            std::min(drawLimits.maximumTotalBytes,
                     clampToSize(remaining(limits.maximumPublishedCpuBytes,
                                           publishedCpuBytes)));
        render::MissionWorldRoomDrawAssembly assembly;
        try {
            assembly = render::buildMissionWorldRoomDrawAssembly(
                catalog, loadSources, selection->worldRoomIndex, drawSources,
                request.basis, request.uvPolicy, drawLimits);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (...) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::drawAssemblyFailure);
            return result;
        }
        if (!assembly.complete()) {
            for (const auto &upstream : assembly.issues) {
                auto issue = makeIssue(
                    MissionWorldRoomLoadIssueKind::drawAssemblyFailure);
                issue.sourceIndex = upstream.sourceIndex;
                issue.drawAssemblyIssue = upstream.kind;
                addIssue(result, std::move(issue));
            }
            if (assembly.issues.empty()) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::drawAssemblyFailure);
            }
            return result;
        }
        if (assembly.meshProvenance.size() != assembly.model.meshes.size() ||
            assembly.instanceProvenance.size() !=
                assembly.model.instances.size() ||
            assembly.worldRoomIndex !=
                std::optional{selection->worldRoomIndex}) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::drawAssemblyFailure);
            return result;
        }
        candidate.meshProvenance = std::move(assembly.meshProvenance);
        candidate.instanceProvenance = std::move(assembly.instanceProvenance);
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::assemblingRoom, 1U, 1U)) {
            return result;
        }

        if (candidate.playerVisual.has_value()) {
            if (!playerObjectDefinition.has_value() ||
                playerCcf == nullptr ||
                !playerTextureBindings.has_value()) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::internalFailure);
                return result;
            }
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        assemblingPlayerVisual,
                    0U,
                    1U)) {
                return result;
            }
            render::PlayerActorVisualDrawAssembly actorVisual;
            auto playerVisualLimits = limits.playerVisual;
            playerVisualLimits.maximumTotalBytes =
                std::min(
                    playerVisualLimits.maximumTotalBytes,
                    clampToSize(remaining(
                        limits.maximumPublishedCpuBytes,
                        publishedCpuBytes)));
            try {
                requireExpectedSession(
                    session, expectedIdentity, expectedRevision);
                actorVisual =
                    render::buildPlayerActorVisualDrawAssembly(
                        *playerObjectDefinition,
                        *playerCcf,
                        playerTextureBindings->materialBindings,
                        request.basis,
                        request.uvPolicy,
                        playerVisualLimits);
                requireExpectedSession(
                    session, expectedIdentity, expectedRevision);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (const SessionIdentityChanged &) {
                throw;
            } catch (...) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        playerVisualAssemblyFailure);
                return result;
            }
            if (!actorVisual.issues.empty()) {
                for (const auto &upstream : actorVisual.issues) {
                    auto issue = makeIssue(
                        MissionWorldRoomLoadIssueKind::
                            playerVisualAssemblyFailure);
                    issue.playerVisualAssemblyIssue =
                        upstream.kind;
                    addIssue(result, std::move(issue));
                }
                return result;
            }
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        assemblingPlayerVisual,
                    1U,
                    1U) ||
                !report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        assemblingPlayerCollision,
                    0U,
                    1U)) {
                return result;
            }

            render::PlayerActorCollisionAssembly actorCollision;
            auto playerCollisionLimits = limits.playerCollision;
            playerCollisionLimits.maximumRetainedBytes =
                std::min(
                    playerCollisionLimits.maximumRetainedBytes,
                    remaining(
                        limits.maximumPublishedCpuBytes,
                        publishedCpuBytes));
            try {
                requireExpectedSession(
                    session, expectedIdentity, expectedRevision);
                actorCollision =
                    render::buildPlayerActorCollisionAssembly(
                        *playerCcf,
                        actorVisual,
                        request.basis,
                        playerCollisionLimits);
                requireExpectedSession(
                    session, expectedIdentity, expectedRevision);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (const SessionIdentityChanged &) {
                throw;
            } catch (...) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        playerCollisionAssemblyFailure);
                return result;
            }
            if (!actorCollision.complete()) {
                for (const auto &upstream : actorCollision.issues) {
                    auto issue = makeIssue(
                        MissionWorldRoomLoadIssueKind::
                            playerCollisionAssemblyFailure);
                    issue.playerCollisionAssemblyIssue =
                        upstream.kind;
                    addIssue(result, std::move(issue));
                }
                if (actorCollision.issues.empty()) {
                    addIssue(
                        result,
                        MissionWorldRoomLoadIssueKind::
                            playerCollisionAssemblyFailure);
                }
                return result;
            }
            candidate.playerActorCollision =
                std::move(actorCollision);
            if (!checkedMissionWorldRoomByteAdd(
                    publishedCpuBytes,
                    candidate.playerActorCollision->
                        retainedPayloadBytes)) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            if (publishedCpuBytes >
                limits.maximumPublishedCpuBytes) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        publishedCpuLimitExceeded);
                return result;
            }
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        assemblingPlayerCollision,
                    1U,
                    1U) ||
                !report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        assemblingPlayerScene,
                    0U,
                    1U)) {
                return result;
            }

            render::PlayerActorSceneAssembly actorScene;
            auto playerSceneLimits = limits.playerScene;
            playerSceneLimits.maximumTotalBytes =
                std::min(
                    playerSceneLimits.maximumTotalBytes,
                    clampToSize(remaining(
                        limits.maximumPublishedCpuBytes,
                        publishedCpuBytes)));
            try {
                requireExpectedSession(
                    session, expectedIdentity, expectedRevision);
                actorScene =
                    render::buildPlayerActorSceneAssembly(
                        std::move(assembly.model),
                        std::move(actorVisual),
                        actorWorldFrom(candidate.playerSpawnPose),
                        playerSceneLimits);
                requireExpectedSession(
                    session, expectedIdentity, expectedRevision);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (const SessionIdentityChanged &) {
                throw;
            } catch (...) {
                addIssue(
                    result,
                    MissionWorldRoomLoadIssueKind::
                        playerSceneAssemblyFailure);
                return result;
            }
            if (!actorScene.complete()) {
                for (const auto &upstream : actorScene.issues) {
                    auto issue = makeIssue(
                        MissionWorldRoomLoadIssueKind::
                            playerSceneAssemblyFailure);
                    issue.playerSceneAssemblyIssue =
                        upstream.kind;
                    addIssue(result, std::move(issue));
                }
                if (actorScene.issues.empty()) {
                    addIssue(
                        result,
                        MissionWorldRoomLoadIssueKind::
                            playerSceneAssemblyFailure);
                }
                return result;
            }
            candidate.model = std::move(actorScene.model);
            candidate.playerActorMeshProvenance =
                std::move(actorScene.actorMeshProvenance);
            candidate.playerActorInstanceProvenance =
                std::move(actorScene.actorInstanceProvenance);
            candidate.playerActorBinding =
                actorScene.actorBinding;
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionWorldRoomLoadPhase::
                        assemblingPlayerScene,
                    1U,
                    1U)) {
                return result;
            }
        } else {
            candidate.model = std::move(assembly.model);
        }

        auto modelPublished = publishedCpuBytes;
        if (!addModelPublishedBytes(modelPublished, candidate.model) ||
            !accountCount(modelPublished, candidate.meshProvenance.size(),
                          sizeof(render::MissionWorldRoomMeshProvenance)) ||
            !accountCount(modelPublished, candidate.instanceProvenance.size(),
                          sizeof(render::MissionWorldRoomInstanceProvenance)) ||
            !accountCount(
                modelPublished,
                candidate.playerActorMeshProvenance.size(),
                sizeof(render::PlayerActorSceneMeshProvenance)) ||
            !accountCount(
                modelPublished,
                candidate.playerActorInstanceProvenance.size(),
                sizeof(render::PlayerActorSceneInstanceProvenance))) {
            addIssue(result, MissionWorldRoomLoadIssueKind::integerOverflow);
            return result;
        }
        if (modelPublished > limits.maximumPublishedCpuBytes) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::publishedCpuLimitExceeded);
            return result;
        }
        publishedCpuBytes = modelPublished;

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::planningSubmission, 0U, 1U)) {
            return result;
        }
        std::size_t commandCount = 0U;
        for (const auto &instance : candidate.model.instances) {
            if (instance.meshSlot >= candidate.model.meshes.size()) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::submissionFailure);
                return result;
            }
            const auto rangeCount =
                candidate.model.meshes[instance.meshSlot].ranges.size();
            if (rangeCount >
                std::numeric_limits<std::size_t>::max() - commandCount) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            commandCount += rangeCount;
        }
        auto submissionPublished = publishedCpuBytes;
        if (!accountCount(submissionPublished, candidate.model.meshes.size(),
                          sizeof(render::DrawMeshUploadMetadata)) ||
            !accountCount(submissionPublished, commandCount,
                          sizeof(render::DrawSubmissionCommand))) {
            addIssue(result, MissionWorldRoomLoadIssueKind::integerOverflow);
            return result;
        }
        if (submissionPublished > limits.maximumPublishedCpuBytes) {
            addIssue(result,
                     MissionWorldRoomLoadIssueKind::publishedCpuLimitExceeded);
            return result;
        }
        render::DrawSubmissionDescription submission;
        try {
            submission = render::buildDrawSubmissionPlan(
                candidate.model, candidate.textures.size(), limits.submission);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (...) {
            addIssue(result, MissionWorldRoomLoadIssueKind::submissionFailure);
            return result;
        }
        if (!submission.plan.has_value() || !submission.issues.empty()) {
            for (const auto &upstream : submission.issues) {
                auto issue =
                    makeIssue(MissionWorldRoomLoadIssueKind::submissionFailure);
                issue.submissionIssue = upstream.kind;
                addIssue(result, std::move(issue));
            }
            if (submission.issues.empty()) {
                addIssue(result,
                         MissionWorldRoomLoadIssueKind::submissionFailure);
            }
            return result;
        }
        publishedCpuBytes = submissionPublished;
        candidate.submission = std::move(*submission.plan);
        candidate.publishedCpuBytes = publishedCpuBytes;
        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::planningSubmission, 1U, 1U)) {
            return result;
        }

        if (candidate.playerVisual.has_value() &&
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionWorldRoomLoadPhase::validatingPublication,
                0U,
                1U)) {
            return result;
        }
        const auto publicationIssue =
            validateMissionWorldRoomPublication(
                candidate, expectedRevision);
        if (publicationIssue.has_value()) {
            auto issue = makeIssue(
                MissionWorldRoomLoadIssueKind::publicationFailure);
            issue.publicationIssue = publicationIssue->kind;
            issue.sourceIndex = publicationIssue->sourceIndex;
            addIssue(result, std::move(issue));
            return result;
        }
        if (candidate.playerVisual.has_value() &&
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionWorldRoomLoadPhase::validatingPublication,
                1U,
                1U)) {
            return result;
        }

        if (!report(result, stopToken, guardedProgress,
                    MissionWorldRoomLoadPhase::complete, 1U, 1U)) {
            return result;
        }
        requireExpectedSession(session, expectedIdentity, expectedRevision);
        result.room = std::move(candidate);
        return result;
    } catch (const SessionIdentityChanged &) {
        result.room.reset();
        result.issues.clear();
        addIssue(result, MissionWorldRoomLoadIssueKind::sessionIdentityChanged);
        return result;
    } catch (const std::bad_alloc &) {
        result.room.reset();
        result.issues.clear();
        addIssue(result, MissionWorldRoomLoadIssueKind::allocationFailure);
        return result;
    } catch (...) {
        result.room.reset();
        result.issues.clear();
        addIssue(result, MissionWorldRoomLoadIssueKind::internalFailure);
        return result;
    }
}

} // namespace airfix::content
