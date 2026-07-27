#pragma once

#include "airfix/assets/AssetResolver.hpp"
#include "airfix/render/TextureRuntimePlan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class PlayerActorTextureBindingIssueKind : std::uint8_t {
    invalidBaseAssetId,
    duplicateBaseArchiveFileIndex,
    baseArchiveFileIndexOutOfRange,
    sceneDependencyFailure,
    sceneGraphFailure,
    invalidSceneResolution,
    textureEntryFailure,
    textureBindingFailure,
    invalidLocalBinding,
    limitExceeded,
    integerOverflow,
};

struct PlayerActorTextureBindingIssue {
    PlayerActorTextureBindingIssueKind kind{
        PlayerActorTextureBindingIssueKind::sceneDependencyFailure};
    std::optional<std::size_t> baseImportIndex;
    std::optional<std::size_t> archiveFileIndex;
    // Complete upstream diagnostics are retained so callers do not have to
    // infer a cause or reinterpret an index.
    std::optional<assets::DependencyIssue> dependencyIssue;
    std::optional<assets::BlueprintGraphIssue> graphIssue;
    std::optional<assets::TextureEntryIssue> textureEntryIssue;
    std::optional<TextureBindingIssue> textureBindingIssue;
};

struct PlayerActorTextureBindingLimits {
    assets::ObjectSceneDependencyLimits dependencies;
    assets::TextureEntryResolutionLimits textureEntries;
    TextureBindingPlanLimits binding;
    std::size_t maximumBaseImports{262'144U};
    std::size_t maximumActorMaterials{65'536U};
    std::size_t maximumActorTextureEntries{262'144U};
    std::size_t maximumGlobalImports{262'144U};
    // Logical renderer-facing vector payload only (element count * sizeof
    // element). This is not allocator capacity, allocator overhead, or RSS.
    std::size_t maximumTotalBytes{64U * 1024U * 1024U};
};

struct PlayerActorTextureBindings {
    // Material order is the selected actor subtree's stable first-use order.
    std::vector<DrawMaterial> materialBindings;
    // A single dense global namespace. A successful result begins with an
    // exact copy of the caller's base imports.
    std::vector<TextureImportRequest> imports;
    std::size_t totalBytes{};
    std::vector<PlayerActorTextureBindingIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return issues.empty();
    }
};

// Re-resolves the authenticated ObjectDefinition/CCF subtree, resolves only
// the texture paths produced by the canonical asset resolver, creates a local
// texture binding plan, and merges it into an existing dense global texture
// namespace. Archive entries are deduplicated by physical file index in
// first-use order.
//
// The input archive, object definition, and CCF metadata must come from the
// same immutable/authenticated load transaction. This metadata-only builder
// performs no archive payload reads. Every failure clears both
// renderer-facing vectors and totalBytes atomically.
[[nodiscard]] PlayerActorTextureBindings buildPlayerActorTextureBindings(
    std::span<const TextureImportRequest> baseImports,
    const assets::ObjectDefinition& object,
    const assets::CcfMetadata& ccf,
    const udsp::Archive& archive,
    const PlayerActorTextureBindingLimits& limits = {});

} // namespace airfix::render
