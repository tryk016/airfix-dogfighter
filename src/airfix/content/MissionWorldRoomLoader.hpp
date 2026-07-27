#pragma once

#include "airfix/assets/MissionWorldRooms.hpp"
#include "airfix/content/LoadedTextureAsset.hpp"
#include "airfix/content/MissionLoadManifest.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/MissionWorldRoomDrawAssembly.hpp"
#include "airfix/render/MissionWorldRoomTextureBindings.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

namespace airfix::content {

struct MissionWorldRoomLoadRequest {
    assets::CcNameState initialRootName;
    std::uint32_t requestedStartIndex{};
    render::BasisTransform basis;
    render::UvPolicy uvPolicy{render::UvPolicy::preserveRaw};
};

struct MissionWorldRoomLoadLimits {
    assets::MissionWorldRoomBuildLimits catalog{};
    assets::MissionWorldStartResolutionLimits starts{};
    render::MissionWorldRoomTextureBindingLimits textureBindings{};
    render::MissionWorldRoomDrawLimits draw{};
    render::DrawSubmissionLimits submission{};
    render::GtiUploadDataLimits gtiPerTexture{};

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
    std::uint64_t maximumPublishedCpuBytes{768U * 1024U * 1024U};
};

enum class MissionWorldRoomLoadPhase : std::uint8_t {
    validatingInput,
    preflightingCcfSources,
    loadingCcfSources,
    buildingRoomCatalog,
    resolvingStart,
    planningTextureBindings,
    preflightingTextures,
    loadingTextures,
    assemblingRoom,
    planningSubmission,
    complete,
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
};

struct MissionWorldRoomLoadIssue {
    MissionWorldRoomLoadIssueKind kind{
        MissionWorldRoomLoadIssueKind::internalFailure};
    std::optional<std::size_t> sourceIndex;
    std::optional<std::size_t> sourceFileIndex;
    std::optional<std::size_t> startPositionIndex;
    std::optional<render::TextureAssetId> textureAssetId;
    std::optional<assets::MissionWorldRoomBuildIssueKind> catalogIssue;
    std::optional<assets::MissionWorldStartIssueKind> startIssue;
    std::optional<render::MissionWorldRoomTextureBindingIssueKind>
        textureBindingIssue;
    std::optional<render::GtiUploadDataIssueKind> texturePreparationIssue;
    std::optional<render::MissionWorldRoomDrawIssueKind> drawAssemblyIssue;
    std::optional<render::DrawSubmissionIssueKind> submissionIssue;
};

struct LoadedMissionWorldRoom {
    ContentRevision revision;
    MissionArchiveEntryIdentity setupEntry;
    std::uint64_t setupSourceFootprintBytes{};
    assets::MissionWorldStartSelection startSelection;
    std::optional<assets::MissionStartPosition> selectedStart;
    render::BasisTransform runtimeBasis;
    simulation::PlayerSpawnPose playerSpawnPose;
    render::DrawModelPayload model;
    std::vector<render::MissionWorldRoomMeshProvenance> meshProvenance;
    std::vector<render::MissionWorldRoomInstanceProvenance> instanceProvenance;
    render::DrawSubmissionPlan submission;
    // Dense order: textures[index].assetId.value == index.
    std::vector<LoadedTextureAsset> textures;

    // Auditable physical-cache provenance. Each entry is parallel to the
    // manifest's semantic CCF load list and indexes the first-use-ordered
    // unique CCF cache used during this transaction.
    std::size_t semanticCcfSourceCount{};
    std::size_t uniqueCcfSourceCount{};
    std::uint64_t uniqueCcfSourceFootprintBytes{};
    std::uint64_t retainedCcfMetadataBytes{};
    std::uint64_t textureSourceFootprintBytes{};
    std::uint64_t decodedRgbaBytes{};
    std::uint64_t uploadRgbaBytes{};
    std::uint64_t residentRgbaBytes{};
    std::uint64_t publishedCpuBytes{};
    std::vector<std::size_t> ccfCacheIndexByLoadSource;
};

struct MissionWorldRoomLoadResult {
    // Published only after the complete immutable-source transaction succeeds.
    std::optional<LoadedMissionWorldRoom> room;
    std::vector<MissionWorldRoomLoadIssue> issues;

    [[nodiscard]] bool success() const noexcept {
        return room.has_value() && issues.empty();
    }
};

// Loads one start-selected runtime room from the exact ordered CCF source list
// authenticated by manifest. Physical CCF and GTI payloads are read only
// through session. Repeated semantic CCF loads share one physical parse but
// remain distinct catalog/draw sources. No partial result is published.
[[nodiscard]] MissionWorldRoomLoadResult
loadMissionWorldRoom(VerifiedContentSession &session,
                     const MissionLoadManifest &manifest,
                     const MissionWorldRoomLoadRequest &request,
                     const MissionWorldRoomLoadLimits &limits = {},
                     std::stop_token stopToken = {},
                     MissionWorldRoomLoadProgressCallback progress = {});

} // namespace airfix::content
