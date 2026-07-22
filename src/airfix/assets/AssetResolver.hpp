#pragma once

#include "airfix/assets/AfChunkContainer.hpp"
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

[[nodiscard]] ObjectDependencyResolution resolveObjectDependencies(
    const ObjectDefinition& object,
    const CcfMetadata& ccf,
    const ObjectDependencyLimits& limits = {});

} // namespace airfix::assets
