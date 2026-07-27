#pragma once

#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/WorldCcfTextureResolver.hpp"
#include "airfix/content/LoadedTextureAsset.hpp"
#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/render/CcfRoomDrawAssembly.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/TextureRuntimeData.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace airfix::content {

struct WorldRoomLoadRequest {
    std::string worldLogicalPath;
    // This is a physical CCF room index. World ROOM records and BSP metadata
    // are deliberately not used to infer or reorder this selection.
    std::size_t ccfRoomIndex{};
};

struct WorldRoomLoadLimits {
    assets::DefinitionParseLimits world{};
    assets::WorldCcfTextureResolutionLimits textureResolution{};
    render::TextureBindingPlanLimits textureBindings{};
    render::CcfRoomDrawLimits roomDraw{};
    render::DrawSubmissionLimits submission{};
    render::GtiUploadDataLimits gtiPerTexture{};

    std::size_t maximumWorldSourceBytes{64U * 1024U * 1024U};
    std::size_t maximumCcfSourceBytes{256U * 1024U * 1024U};
    std::size_t maximumTextureSourceBytes{256U * 1024U * 1024U};
    std::uint64_t maximumTotalTextureSourceBytes{512U * 1024U * 1024U};
    std::size_t maximumTextureAssets{4'096U};
    std::uint64_t maximumDecodedRgbaBytes{512U * 1024U * 1024U};
    std::uint64_t maximumUploadRgbaBytes{512U * 1024U * 1024U};
    std::uint64_t maximumResidentRgbaBytes{512U * 1024U * 1024U};
    std::uint64_t maximumPublishedCpuBytes{768U * 1024U * 1024U};
};

enum class WorldRoomLoadPhase : std::uint8_t {
    validatingRequest,
    lookingUpWorld,
    readingWorld,
    parsingWorld,
    lookingUpCcf,
    readingCcf,
    parsingCcf,
    resolvingTextures,
    planningTextureBindings,
    loadingTextures,
    assemblingRoom,
    planningSubmission,
    complete,
};

struct WorldRoomLoadProgress {
    WorldRoomLoadPhase phase{};
    std::size_t completedItems{};
    std::size_t totalItems{};
};

using WorldRoomLoadProgressCallback =
    std::function<void(const WorldRoomLoadProgress&)>;

enum class WorldRoomLoadIssueKind : std::uint8_t {
    cancelled,
    invalidLimits,
    invalidWorldLogicalPath,
    worldNotFound,
    worldAmbiguous,
    worldSourceLimitExceeded,
    worldReadFailure,
    worldParseFailure,
    missingCcfPath,
    invalidCcfPath,
    ccfNotFound,
    ccfAmbiguous,
    ccfSourceLimitExceeded,
    ccfReadFailure,
    ccfParseFailure,
    textureResolutionFailure,
    textureBindingFailure,
    invalidTextureImport,
    textureAssetLimitExceeded,
    textureSourceLimitExceeded,
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

struct WorldRoomLoadIssue {
    WorldRoomLoadIssueKind kind{WorldRoomLoadIssueKind::internalFailure};
    std::optional<std::size_t> sourceFileIndex;
    std::optional<render::TextureAssetId> textureAssetId;
    std::optional<assets::WorldCcfTextureIssueKind> textureResolutionIssue;
    std::optional<render::TextureBindingIssueKind> textureBindingIssue;
    std::optional<render::GtiUploadDataIssueKind> texturePreparationIssue;
    std::optional<render::CcfRoomDrawIssueKind> drawAssemblyIssue;
    std::optional<render::DrawSubmissionIssueKind> submissionIssue;
};

struct LoadedWorldRoom {
    ContentRevision revision;
    render::DrawModelPayload model;
    render::DrawSubmissionPlan submission;
    // Dense order: textures[index].assetId.value == index.
    std::vector<LoadedTextureAsset> textures;
};

struct WorldRoomLoadResult {
    // Published only after the complete immutable-source transaction succeeds.
    std::optional<LoadedWorldRoom> room;
    std::vector<WorldRoomLoadIssue> issues;

    [[nodiscard]] bool success() const noexcept {
        return room.has_value() && issues.empty();
    }
};

// Loads one physical room through the authenticated handle owned by session:
// World -> exact CCFF -> CCF room/material dependencies -> exact GTIs -> draw
// model -> backend-neutral submission. The function never opens sourceLabel or
// any logical path as a host path. It publishes no partial room on failure.
[[nodiscard]] WorldRoomLoadResult loadWorldRoom(
    VerifiedContentSession& session,
    const WorldRoomLoadRequest& request,
    const WorldRoomLoadLimits& limits = {},
    std::stop_token stopToken = {},
    WorldRoomLoadProgressCallback progress = {});

} // namespace airfix::content
