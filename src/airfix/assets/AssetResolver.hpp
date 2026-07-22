#pragma once

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/CcfBlueprintGraph.hpp"
#include "airfix/assets/LegacyFormats.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace airfix::assets {

enum class BlueprintSelectorStatus : std::uint8_t {
    noSelector,
    notFound,
    unique,
    ambiguous,
};

enum class TextureDependencyRole : std::uint8_t {
    primary,
    secondary,
    environment,
};

enum class DependencyIssueKind : std::uint8_t {
    missingCcfPath,
    blueprintNotFound,
    blueprintAmbiguous,
    invalidMeshIndex,
    materialNotFound,
    materialAmbiguous,
    limitExceeded,
};

struct DependencyIssue {
    DependencyIssueKind kind{DependencyIssueKind::blueprintNotFound};
    std::optional<std::uint32_t> reference;
};

struct TextureDependency {
    TextureDependencyRole role{TextureDependencyRole::primary};
    std::uint32_t materialReference{};
    std::size_t materialIndex{};
    std::string sourceText;
};

struct ObjectDependencyLimits {
    std::size_t maximumBlueprints{100'000U};
    std::size_t maximumMaterials{250'000U};
    std::size_t maximumMaterialReferences{65'536U};
    std::size_t maximumTextureEdges{262'144U};
};

struct ObjectDependencyResolution {
    BlueprintSelectorStatus selectorStatus{BlueprintSelectorStatus::noSelector};
    std::optional<std::size_t> blueprintIndex;
    std::optional<std::size_t> meshIndex;
    std::vector<std::size_t> materialIndices;
    std::vector<TextureDependency> textures;
    std::vector<DependencyIssue> issues;
};

struct SceneMeshDependency {
    std::size_t blueprintIndex{};
    std::size_t meshIndex{};
};

struct ObjectSceneDependencyLimits {
    ObjectDependencyLimits dependencies;
    BlueprintGraphLimits graph;
};

struct ObjectSceneDependencyResolution {
    BlueprintSelectorStatus selectorStatus{BlueprintSelectorStatus::noSelector};
    std::optional<std::size_t> rootBlueprintIndex;
    std::vector<std::size_t> blueprintIndices;
    std::vector<SceneMeshDependency> meshes;
    std::vector<std::size_t> materialIndices;
    std::vector<TextureDependency> textures;
    std::vector<DependencyIssue> issues;
    std::vector<BlueprintGraphIssue> graphIssues;
};

enum class TextureEntryStatus : std::uint8_t {
    missingTextureRoot,
    invalidLogicalPath,
    notFound,
    unique,
    ambiguous,
};

enum class TextureEntryIssueKind : std::uint8_t {
    missingTextureRoot,
    invalidLogicalPath,
    notFound,
    ambiguous,
    limitExceeded,
};

struct TextureEntryIssue {
    TextureEntryIssueKind kind{TextureEntryIssueKind::notFound};
    std::optional<std::size_t> dependencyIndex;
};

struct ResolvedTextureEntry {
    TextureDependencyRole role{TextureDependencyRole::primary};
    std::uint32_t materialReference{};
    std::size_t materialIndex{};
    std::string sourceText;
    TextureEntryStatus status{TextureEntryStatus::notFound};
    std::string logicalPath;
    std::optional<std::size_t> archiveDirectoryIndex;
    std::optional<std::size_t> archiveFileIndex;
    std::optional<std::string> archiveLogicalPath;
};

struct TextureEntryResolutionLimits {
    std::size_t maximumDependencies{262'144U};
    std::size_t maximumLogicalPathBytes{4'096U};
};

struct ObjectTextureEntryResolution {
    std::vector<ResolvedTextureEntry> entries;
    std::vector<TextureEntryIssue> issues;
};

[[nodiscard]] ObjectDependencyResolution resolveObjectDependencies(
    const ObjectDefinition& object,
    const CcfMetadata& ccf,
    const ObjectDependencyLimits& limits = {});

// Resolves the subtree instantiated by CcBlueprint::MakeInstance. Mesh order is
// stable depth-first preorder; sibling and triangle order remain physical.
[[nodiscard]] ObjectSceneDependencyResolution resolveObjectSceneDependencies(
    const ObjectDefinition& object,
    const CcfMetadata& ccf,
    const ObjectSceneDependencyLimits& limits = {});

// Resolves the exact legacy TEXU/source-text join as "root\\source.gti".
// The .gti suffix is always appended; no alternate roots are searched and no
// payload bytes are read.
[[nodiscard]] ObjectTextureEntryResolution resolveObjectTextureEntries(
    const ObjectDefinition& object,
    const ObjectDependencyResolution& dependencies,
    const udsp::Archive& archive,
    const TextureEntryResolutionLimits& limits = {});

[[nodiscard]] ObjectTextureEntryResolution resolveObjectTextureEntries(
    const ObjectDefinition& object,
    const ObjectSceneDependencyResolution& dependencies,
    const udsp::Archive& archive,
    const TextureEntryResolutionLimits& limits = {});

} // namespace airfix::assets
