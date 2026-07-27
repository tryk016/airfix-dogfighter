#pragma once

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/assets/MissionWorldRoomDrawPlan.hpp"
#include "airfix/render/TextureRuntimePlan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace airfix::render {

struct MissionWorldRoomTextureSource {
    // CCF metadata and string views must outlive
    // buildMissionWorldRoomTextureBindings.
    const assets::CcfMetadata* ccf{};
    std::optional<std::string_view> textureRoot;
    // Logical UDSP path, never a host filesystem path.
    std::string_view ccfLogicalPath;
    // Source-entry identity supplied by the immutable/authenticated load
    // transaction that produced ccf. This function checks metadata identity;
    // it cannot prove that ccf was parsed from that archive payload.
    std::size_t ccfArchiveFileIndex{};
};

enum class MissionWorldRoomTextureBindingIssueKind : std::uint8_t {
    drawPlanDependency,
    sourceCountMismatch,
    invalidSource,
    invalidCcfLogicalPath,
    ccfNotFound,
    ccfAmbiguous,
    ccfIdentityMismatch,
    textureResolutionDependency,
    textureBindingDependency,
    limitExceeded,
    integerOverflow,
};

struct MissionWorldRoomTextureBindingIssue {
    MissionWorldRoomTextureBindingIssueKind kind{
        MissionWorldRoomTextureBindingIssueKind::drawPlanDependency};
    std::optional<std::size_t> sourceIndex;
    std::optional<std::size_t> textureEntryIndex;
    std::optional<assets::MissionWorldRoomDrawPlanIssueKind> drawPlanIssue;
    std::optional<assets::TextureEntryIssueKind> textureResolutionIssue;
    std::optional<TextureBindingIssueKind> textureBindingIssue;
};

struct MissionWorldRoomTextureBindingLimits {
    assets::MissionWorldRoomDrawPlanLimits drawPlan;
    assets::TextureEntryResolutionLimits textureEntriesPerSource;
    TextureBindingPlanLimits bindingPerSource;
    std::size_t maximumSources{65'536U};
    std::size_t maximumMaterials{65'536U};
    std::size_t maximumTextureEntries{262'144U};
    std::size_t maximumImports{262'144U};
    std::size_t maximumCcfLogicalPathBytes{4'096U};
};

struct MissionWorldRoomTextureBindings {
    std::optional<std::size_t> worldRoomIndex;
    // One vector per exact load source. Equal numeric material references in
    // different sources remain independent.
    std::vector<std::vector<DrawMaterial>> materialBindingsBySource;
    // One dense global TextureAssetId namespace. Archive entries are
    // deduplicated across every source, material, and texture role.
    std::vector<TextureImportRequest> imports;
    std::vector<MissionWorldRoomTextureBindingIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return worldRoomIndex.has_value() && issues.empty();
    }
};

// Replays the canonical source-aware room plan, checks each supplied CCF
// metadata identity against its logical UDSP entry, resolves source-local
// texture roots, and merges the existing per-source binding plans into one
// deterministic global texture-ID namespace. The caller must create each
// CcfMetadata object and its archive-file index in one immutable/authenticated
// load transaction. This metadata-only check does not prove that the CCF
// metadata came from the referenced payload. IDs follow first resolved
// archive-file use in the canonical room plan. The function never reads CCF
// or GTI payloads. Any source, dependency, identity, overflow, or limit failure
// clears all renderer-facing payload vectors atomically.
[[nodiscard]] MissionWorldRoomTextureBindings
buildMissionWorldRoomTextureBindings(
    const assets::MissionWorldRoomCatalog& catalog,
    std::span<const assets::MissionCcfRoomLoadSource> loadSources,
    std::size_t worldRoomIndex,
    std::span<const MissionWorldRoomTextureSource> textureSources,
    const udsp::Archive& archive,
    const MissionWorldRoomTextureBindingLimits& limits = {});

} // namespace airfix::render
