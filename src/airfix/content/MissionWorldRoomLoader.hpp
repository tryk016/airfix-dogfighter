#pragma once

#include "airfix/assets/MissionWorldSpatialArena.hpp"
#include "airfix/assets/MissionWorldRooms.hpp"
#include "airfix/content/LoadedTextureAsset.hpp"
#include "airfix/content/MissionLoadManifest.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/MissionWorldRoomDrawAssembly.hpp"
#include "airfix/render/MissionWorldRoomTextureBindings.hpp"
#include "airfix/render/PlayerActorSceneAssembly.hpp"
#include "airfix/render/PlayerActorTextureBindings.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

namespace airfix::content {

enum class MissionWorldRoomPublicationIssueKind : std::uint8_t;

struct MissionWorldRoomLoadRequest {
    assets::CcNameState initialRootName;
    std::uint32_t requestedStartIndex{};
    render::BasisTransform basis;
    render::UvPolicy uvPolicy{render::UvPolicy::preserveRaw};
};

struct MissionWorldRoomLoadLimits {
    assets::MissionWorldRoomBuildLimits catalog{};
    assets::MissionWorldSpatialArenaLimits spatialArena{};
    assets::MissionWorldStartResolutionLimits starts{};
    render::MissionWorldRoomTextureBindingLimits textureBindings{};
    render::MissionWorldRoomDrawLimits draw{};
    render::DrawSubmissionLimits submission{};
    render::GtiUploadDataLimits gtiPerTexture{};
    render::PlayerActorTextureBindingLimits playerTextureBindings{};
    render::ObjectVisualDrawLimits playerVisual{};
    render::PlayerActorSceneLimits playerScene{};

    // Room semantic sources plus the optional player visual CCF.
    std::size_t maximumCcfSources{65'536U};
    std::size_t maximumUniqueCcfSources{65'536U};
    std::size_t maximumCcfSourceBytes{256U * 1024U * 1024U};
    std::uint64_t maximumTotalUniqueCcfSourceBytes{1024U * 1024U * 1024U};
    // Post-parse logical accounting of retained CcfMetadata payloads. CCF
    // admission is bounded independently by parseCcf's internal hard caps.
    // This is not a peak-allocation or process-RSS limit.
    std::uint64_t maximumRetainedCcfMetadataBytesAfterParse{512U * 1024U *
                                                            1024U};
    std::size_t maximumTextureAssets{4'096U};
    std::size_t maximumTextureSourceBytes{256U * 1024U * 1024U};
    std::uint64_t maximumTotalTextureSourceBytes{512U * 1024U * 1024U};
    std::uint64_t maximumDecodedRgbaBytes{512U * 1024U * 1024U};
    std::uint64_t maximumUploadRgbaBytes{512U * 1024U * 1024U};
    std::uint64_t maximumResidentRgbaBytes{512U * 1024U * 1024U};
    std::size_t maximumPlayerObjectLogicalPathBytes{4'096U};
    std::size_t maximumPlayerBlueprintSelectorBytes{4'096U};
    std::size_t maximumPlayerTextureRootBytes{4'096U};
    std::uint64_t maximumPublishedCpuBytes{768U * 1024U * 1024U};
};

enum class MissionWorldRoomLoadPhase : std::uint8_t {
    validatingInput,
    preflightingCcfSources,
    loadingCcfSources,
    buildingRoomCatalog,
    buildingSpatialArena,
    resolvingStart,
    planningTextureBindings,
    preflightingTextures,
    loadingTextures,
    assemblingRoom,
    planningSubmission,
    complete,
    planningPlayerTextureBindings,
    assemblingPlayerVisual,
    assemblingPlayerScene,
    validatingPublication,
    preflightingPlayerCcfSource,
};

struct MissionWorldRoomLoadProgress {
    MissionWorldRoomLoadPhase phase{};
    std::size_t completedItems{};
    std::size_t totalItems{};
};

using MissionWorldRoomLoadProgressCallback =
    std::function<void(const MissionWorldRoomLoadProgress &)>;

enum class MissionWorldRoomLoadIssueKind : std::uint8_t {
    cancelled,
    invalidLimits,
    invalidManifest,
    manifestRevisionMismatch,
    sessionIdentityChanged,
    invalidRequest,
    invalidStartPosition,
    invalidCcfDescriptor,
    ccfSourceCountLimitExceeded,
    uniqueCcfSourceCountLimitExceeded,
    ccfSourceLimitExceeded,
    aggregateCcfSourceLimitExceeded,
    ccfReadFailure,
    ccfParseFailure,
    retainedCcfMetadataLimitExceeded,
    catalogFailure,
    spatialArenaFailure,
    startResolutionFailure,
    startSelectionFailure,
    textureBindingFailure,
    invalidTextureImport,
    textureAssetLimitExceeded,
    textureSourceLimitExceeded,
    aggregateTextureSourceLimitExceeded,
    textureReadFailure,
    texturePreparationFailure,
    decodedRgbaLimitExceeded,
    uploadRgbaLimitExceeded,
    residentRgbaLimitExceeded,
    drawAssemblyFailure,
    submissionFailure,
    publishedCpuLimitExceeded,
    integerOverflow,
    allocationFailure,
    progressCallbackFailure,
    internalFailure,
    invalidPlayerVisualDescriptor,
    playerTextureBindingFailure,
    playerVisualAssemblyFailure,
    playerSceneAssemblyFailure,
    publicationFailure,
};

struct MissionWorldRoomLoadIssue {
    MissionWorldRoomLoadIssueKind kind{
        MissionWorldRoomLoadIssueKind::internalFailure};
    std::optional<std::size_t> sourceIndex;
    std::optional<std::size_t> sourceFileIndex;
    std::optional<std::size_t> physicalRoomIndex;
    std::optional<std::size_t> worldRoomIndex;
    std::optional<std::size_t> startPositionIndex;
    std::optional<render::TextureAssetId> textureAssetId;
    std::optional<assets::MissionWorldRoomBuildIssueKind> catalogIssue;
    std::optional<assets::MissionWorldSpatialArenaIssueKind>
        spatialArenaIssue;
    std::optional<assets::RoomSceneIssueKind> roomSceneIssue;
    std::optional<assets::MissionWorldStartIssueKind> startIssue;
    std::optional<render::MissionWorldRoomTextureBindingIssueKind>
        textureBindingIssue;
    std::optional<render::GtiUploadDataIssueKind> texturePreparationIssue;
    std::optional<render::MissionWorldRoomDrawIssueKind> drawAssemblyIssue;
    std::optional<render::DrawSubmissionIssueKind> submissionIssue;
    std::optional<render::PlayerActorTextureBindingIssueKind>
        playerTextureBindingIssue;
    std::optional<render::PlayerActorVisualDrawIssueKind>
        playerVisualAssemblyIssue;
    std::optional<render::PlayerActorSceneIssueKind> playerSceneAssemblyIssue;
    std::optional<MissionWorldRoomPublicationIssueKind> publicationIssue;
};

struct LoadedMissionWorldRoom {
    ContentRevision revision;
    MissionArchiveEntryIdentity setupEntry;
    std::uint64_t setupSourceFootprintBytes{};
    assets::MissionWorldStartSelection startSelection;
    std::optional<assets::MissionStartPosition> selectedStart;
    render::BasisTransform runtimeBasis;
    simulation::PlayerSpawnPose playerSpawnPose;
    // Pointer-free source-world BSP retained for room collision and portal
    // transitions after the parsed CCF cache is destroyed.
    assets::MissionWorldSpatialArena spatialArena;
    render::DrawModelPayload model;
    // Static room provenance remains a stable prefix of the final model.
    std::vector<render::MissionWorldRoomMeshProvenance> meshProvenance;
    std::vector<render::MissionWorldRoomInstanceProvenance> instanceProvenance;
    std::optional<MissionPlayerVisualDescriptor> playerVisual;
    std::vector<render::PlayerActorSceneMeshProvenance>
        playerActorMeshProvenance;
    std::vector<render::PlayerActorSceneInstanceProvenance>
        playerActorInstanceProvenance;
    std::optional<render::PlayerActorSceneBinding> playerActorBinding;
    render::DrawSubmissionPlan submission;
    // Dense order: textures[index].assetId.value == index.
    std::vector<LoadedTextureAsset> textures;

    // Auditable physical-cache provenance. The vector remains parallel to the
    // room-only manifest CCF load list. The optional player index addresses
    // the same first-use-ordered physical cache after all room first uses.
    std::size_t semanticCcfSourceCount{};
    std::size_t uniqueCcfSourceCount{};
    std::uint64_t uniqueCcfSourceFootprintBytes{};
    std::uint64_t retainedCcfMetadataBytes{};
    std::uint64_t retainedSpatialBytes{};
    std::uint64_t textureSourceFootprintBytes{};
    std::uint64_t decodedRgbaBytes{};
    std::uint64_t uploadRgbaBytes{};
    std::uint64_t residentRgbaBytes{};
    std::uint64_t publishedCpuBytes{};
    std::vector<std::size_t> ccfCacheIndexByLoadSource;
    std::optional<std::size_t> playerVisualCcfCacheIndex;
};

struct MissionWorldRoomLoadResult {
    // Published only after the complete immutable-source transaction succeeds.
    std::optional<LoadedMissionWorldRoom> room;
    std::vector<MissionWorldRoomLoadIssue> issues;

    [[nodiscard]] bool success() const noexcept {
        return room.has_value() && issues.empty();
    }
};

// Loads one start-selected runtime room and optional authenticated player
// visual. Physical CCF and GTI payloads are read only through session.
// Repeated room/player CCF identities share one physical parse. The player is
// appended after the static room prefix and receives one final submission
// plan with the room. No partial result is published.
[[nodiscard]] MissionWorldRoomLoadResult
loadMissionWorldRoom(VerifiedContentSession &session,
                     const MissionLoadManifest &manifest,
                     const MissionWorldRoomLoadRequest &request,
                     const MissionWorldRoomLoadLimits &limits = {},
                     std::stop_token stopToken = {},
                     MissionWorldRoomLoadProgressCallback progress = {});

} // namespace airfix::content
