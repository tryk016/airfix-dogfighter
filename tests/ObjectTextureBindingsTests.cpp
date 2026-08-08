#include "airfix/render/ObjectTextureBindings.hpp"
#include "airfix/render/PlayerActorTextureBindings.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using namespace airfix::assets;
using namespace airfix::render;

void appendU32(Bytes &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void writeU32(Bytes &bytes, const std::size_t offset,
              const std::uint32_t value) {
  bytes.at(offset) = static_cast<std::uint8_t>(value);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
  bytes.at(offset + 2U) = static_cast<std::uint8_t>(value >> 16U);
  bytes.at(offset + 3U) = static_cast<std::uint8_t>(value >> 24U);
}

void appendRecord(Bytes &bytes, const std::uint32_t hash,
                  const std::uint32_t nameOffset, const std::uint32_t field08,
                  const std::uint32_t field0C, const std::uint32_t field10,
                  const std::uint32_t field14) {
  appendU32(bytes, hash);
  appendU32(bytes, nameOffset);
  appendU32(bytes, field08);
  appendU32(bytes, field0C);
  appendU32(bytes, field10);
  appendU32(bytes, field14);
}

[[nodiscard]] Bytes makeTextureArchive(std::vector<std::string> fileNames) {
  constexpr std::string_view directory = "Graphics\\Textures";
  std::stable_sort(fileNames.begin(), fileNames.end(),
                   [](const auto &left, const auto &right) {
                     return airfix::udsp::nameHash(left) <
                            airfix::udsp::nameHash(right);
                   });
  Bytes bytes(airfix::udsp::kHeaderSize, 0U);
  const auto directoryOffset = static_cast<std::uint32_t>(bytes.size());
  appendRecord(bytes, airfix::udsp::nameHash(directory), 0U, 0U, 0U,
               static_cast<std::uint32_t>(fileNames.size()), 0U);
  const auto fileOffset = static_cast<std::uint32_t>(bytes.size());
  std::uint32_t nameOffset = static_cast<std::uint32_t>(directory.size() + 1U);
  for (const auto &name : fileNames) {
    appendRecord(bytes, airfix::udsp::nameHash(name), nameOffset, 0U, 0U, 0U,
                 static_cast<std::uint32_t>(airfix::udsp::kHeaderSize));
    nameOffset += static_cast<std::uint32_t>(name.size() + 1U);
  }
  const auto stringOffset = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), directory.begin(), directory.end());
  bytes.push_back(0U);
  for (const auto &name : fileNames) {
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.push_back(0U);
  }
  bytes[0] = 'U';
  bytes[1] = 'D';
  bytes[2] = 'S';
  bytes[3] = 'P';
  writeU32(bytes, 4U, airfix::udsp::kVersion);
  writeU32(bytes, 8U, airfix::udsp::kRecordSize);
  writeU32(bytes, 12U, directoryOffset);
  writeU32(bytes, 16U, static_cast<std::uint32_t>(bytes.size()) - stringOffset);
  writeU32(bytes, 20U, stringOffset);
  writeU32(
      bytes, 24U,
      static_cast<std::uint32_t>(fileNames.size() * airfix::udsp::kRecordSize));
  writeU32(bytes, 28U, fileOffset);
  return bytes;
}

void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] std::size_t fileIndex(const airfix::udsp::Archive &archive,
                                    const std::string_view name) {
  const auto found =
      std::ranges::find(archive.files(), name, &airfix::udsp::FileEntry::name);
  require(found != archive.files().end(), "archive fixture file missing");
  return static_cast<std::size_t>(found - archive.files().begin());
}

[[nodiscard]] ObjectDefinition object(const std::string &selector) {
  ObjectDefinition result;
  result.ccfPath = "Graphics\\Object.ccf";
  result.meshName = selector;
  result.textureRoot = "Graphics\\Textures";
  return result;
}

[[nodiscard]] CcfMetadata
ccf(const std::string &selector, const std::uint32_t materialReference,
    const std::optional<std::string> &primary,
    const std::optional<std::string> &secondary = std::nullopt,
    const std::optional<std::string> &environment = std::nullopt) {
  CcfMetadata result;
  CcfMeshMetadata mesh;
  mesh.name = selector;
  mesh.triangles.push_back(
      CcfMeshTriangleMetadata{.materialReference = materialReference});
  result.meshes.push_back(std::move(mesh));
  result.blueprints.push_back({
      .kind = CcfBlueprintKind::mesh,
      .name = selector,
      .reference = materialReference + 100U,
      .meshIndex = 0U,
  });
  result.materials.push_back({
      .name = selector + "-material",
      .reference = materialReference,
      .primaryTexture = primary,
      .secondaryTexture = secondary,
      .environmentTexture = environment,
  });
  return result;
}

[[nodiscard]] bool hasIssue(const ObjectTextureBindings &result,
                            const ObjectTextureBindingIssueKind kind) {
  return std::ranges::any_of(
      result.issues, [&](const auto &issue) { return issue.kind == kind; });
}

void requireAtomicFailure(const ObjectTextureBindings &result,
                          const char *message) {
  require(!result.complete() && result.definitions.empty() &&
              result.definitionBindingIndexBySource.empty() &&
              result.imports.empty() && result.totalBytes == 0U,
          message);
}

void testRepeatedDefinitionUsesOneBinding() {
  const auto archive =
      airfix::udsp::Archive::parse(makeTextureArchive({"shared.gti"}));
  const auto definition = object("shared");
  const auto metadata = ccf("shared", 10U, "shared");
  const std::vector<ObjectTextureBindingSource> sources{
      {.definitionIndex = 7U, .object = &definition, .ccf = &metadata},
      {.definitionIndex = 7U, .object = &definition, .ccf = &metadata},
  };
  const auto result = buildObjectTextureBindings({}, sources, archive);
  require(result.complete() && result.definitions.size() == 1U &&
              result.definitionBindingIndexBySource ==
                  std::vector<std::size_t>{0U, 0U} &&
              result.imports.size() == 1U &&
              result.definitions[0].definitionIndex == 7U &&
              result.definitions[0].materials[0].primary == TextureAssetId{0U},
          "repeated definition was not reused in first-use order");

  const auto single =
      buildObjectTextureBindings({}, std::span{sources.data(), 1U}, archive);
  require(single.complete() &&
              result.totalBytes == single.totalBytes + sizeof(std::size_t),
          "repeated source was not included in output-byte accounting");
  auto limits = ObjectTextureBindingLimits{};
  limits.maximumTotalBytes = result.totalBytes - 1U;
  const auto limited = buildObjectTextureBindings({}, sources, archive, limits);
  requireAtomicFailure(limited,
                       "repeated-source output-byte limit leaked payload");
  require(hasIssue(limited, ObjectTextureBindingIssueKind::limitExceeded),
          "repeated-source output-byte limit lost its typed issue");
}

void testGlobalNamespaceDeduplicatesAndRemapsAllRoles() {
  const auto archive = airfix::udsp::Archive::parse(makeTextureArchive(
      {"base.gti", "shared.gti", "first.gti", "second.gti"}));
  const std::vector<TextureImportRequest> base{{
      .assetId = TextureAssetId{0U},
      .archiveFileIndex = fileIndex(archive, "base.gti"),
      .logicalPath = "Graphics\\Textures\\base.gti",
  }};
  const auto firstObject = object("first");
  const auto secondObject = object("second");
  const auto firstCcf = ccf("first", 10U, "shared", "first", "base");
  // The same local material reference is deliberately independent in this
  // second CCF and resolves to a different secondary texture.
  const auto secondCcf = ccf("second", 10U, "shared", "second", "base");
  const std::vector<ObjectTextureBindingSource> sources{
      {.definitionIndex = 2U, .object = &firstObject, .ccf = &firstCcf},
      {.definitionIndex = 9U, .object = &secondObject, .ccf = &secondCcf},
  };
  const auto result = buildObjectTextureBindings(base, sources, archive);
  require(result.complete() && result.definitions.size() == 2U &&
              result.definitionBindingIndexBySource ==
                  std::vector<std::size_t>{0U, 1U} &&
              result.imports.size() == 4U,
          "batch namespace shape changed");
  require(result.imports[0] == base[0] &&
              result.imports[1].archiveFileIndex ==
                  fileIndex(archive, "shared.gti") &&
              result.imports[2].archiveFileIndex ==
                  fileIndex(archive, "first.gti") &&
              result.imports[3].archiveFileIndex ==
                  fileIndex(archive, "second.gti"),
          "global first-use import order changed");
  const auto &first = result.definitions[0].materials.at(0);
  const auto &second = result.definitions[1].materials.at(0);
  require(first.primary == TextureAssetId{1U} &&
              first.secondary == TextureAssetId{2U} &&
              first.environment == TextureAssetId{0U} &&
              second.primary == TextureAssetId{1U} &&
              second.secondary == TextureAssetId{3U} &&
              second.environment == TextureAssetId{0U},
          "primary/secondary/environment IDs were not globally remapped");
}

void testConflictingRepeatAndLimitsFailAtomically() {
  const auto archive = airfix::udsp::Archive::parse(
      makeTextureArchive({"first.gti", "second.gti"}));
  const auto firstObject = object("first");
  const auto secondObject = object("second");
  const auto firstCcf = ccf("first", 10U, "first");
  const auto secondCcf = ccf("second", 10U, "second");

  std::vector<ObjectTextureBindingSource> sources{
      {.definitionIndex = 4U, .object = &firstObject, .ccf = &firstCcf},
      {.definitionIndex = 4U, .object = &secondObject, .ccf = &secondCcf},
  };
  auto result = buildObjectTextureBindings({}, sources, archive);
  requireAtomicFailure(result,
                       "conflicting repeated definition leaked payload");
  require(
      hasIssue(result,
               ObjectTextureBindingIssueKind::conflictingRepeatedDefinition),
      "conflicting repeated definition lost its typed issue");

  sources[1].definitionIndex = 5U;
  auto limits = ObjectTextureBindingLimits{};
  limits.maximumUniqueDefinitions = 1U;
  result = buildObjectTextureBindings({}, sources, archive, limits);
  requireAtomicFailure(result, "unique-definition limit leaked payload");
  require(hasIssue(result, ObjectTextureBindingIssueKind::limitExceeded),
          "unique-definition limit lost its typed issue");

  limits = {};
  limits.maximumGlobalImports = 1U;
  result = buildObjectTextureBindings({}, sources, archive, limits);
  requireAtomicFailure(result, "global import limit leaked payload");
  require(hasIssue(result, ObjectTextureBindingIssueKind::limitExceeded),
          "global import limit lost its typed issue");

  const auto baseline = buildObjectTextureBindings({}, sources, archive);
  require(baseline.complete() && baseline.totalBytes > 0U,
          "baseline batch did not build");
  limits = {};
  limits.maximumTotalBytes = baseline.totalBytes;
  require(buildObjectTextureBindings({}, sources, archive, limits).complete(),
          "exact output-byte limit failed");
  --limits.maximumTotalBytes;
  result = buildObjectTextureBindings({}, sources, archive, limits);
  requireAtomicFailure(result, "one-under output-byte limit leaked payload");
}

void testInvalidBaseAndLateTextureFailureAreAtomic() {
  const auto archive = airfix::udsp::Archive::parse(
      makeTextureArchive({"base.gti", "present.gti"}));
  const auto definition = object("item");
  const auto metadata = ccf("item", 10U, "missing");
  const ObjectTextureBindingSource source{
      .definitionIndex = 1U, .object = &definition, .ccf = &metadata};

  const std::vector<TextureImportRequest> invalidBase{{
      .assetId = TextureAssetId{1U},
      .archiveFileIndex = fileIndex(archive, "base.gti"),
  }};
  auto result =
      buildObjectTextureBindings(invalidBase, std::span{&source, 1U}, archive);
  requireAtomicFailure(result, "invalid base ID leaked payload");
  require(hasIssue(result, ObjectTextureBindingIssueKind::invalidBaseAssetId),
          "invalid base ID lost its typed issue");

  result = buildObjectTextureBindings({}, std::span{&source, 1U}, archive);
  requireAtomicFailure(result, "late missing GTI leaked payload");
  require(hasIssue(result, ObjectTextureBindingIssueKind::textureEntryFailure),
          "late missing GTI lost its typed issue");
}

void testPlayerWrapperPreservesBaseBytePreflight() {
  const auto archive =
      airfix::udsp::Archive::parse(makeTextureArchive({"present.gti"}));
  const auto definition = object("item");
  const auto metadata = ccf("item", 10U, "present");
  const std::vector<TextureImportRequest> invalidBase{{
      .assetId = TextureAssetId{1U},
      .archiveFileIndex = fileIndex(archive, "present.gti"),
  }};
  auto limits = PlayerActorTextureBindingLimits{};
  limits.maximumTotalBytes = 0U;
  const auto result = buildPlayerActorTextureBindings(
      invalidBase, definition, metadata, archive, limits);
  require(!result.complete() && result.materialBindings.empty() &&
              result.imports.empty() && result.totalBytes == 0U &&
              result.issues.size() == 1U &&
              result.issues.front().kind ==
                  PlayerActorTextureBindingIssueKind::limitExceeded,
          "player wrapper no longer checks base byte budget before base IDs");
}

} // namespace

int main() {
  try {
    testRepeatedDefinitionUsesOneBinding();
    testGlobalNamespaceDeduplicatesAndRemapsAllRoles();
    testConflictingRepeatAndLimitsFailAtomically();
    testInvalidBaseAndLateTextureFailureAreAtomic();
    testPlayerWrapperPreservesBaseBytePreflight();
    std::cout << "Object texture bindings tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Object texture bindings tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
