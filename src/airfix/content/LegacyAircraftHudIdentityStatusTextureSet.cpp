#include "airfix/content/LegacyAircraftHudIdentityStatusTextureSet.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace airfix::content {
namespace {

constexpr std::string_view catalogDirectory = "Graphics\\Ingame\\HUD";
constexpr std::string_view catalogPrefix = "Ac";
constexpr std::string_view catalogSuffix = ".gti";
constexpr std::string_view catalogLogicalDirectoryPrefix =
    "Graphics\\Ingame\\HUD\\";
constexpr std::array<std::string_view,
                     legacyAircraftHudIdentityStatusFixedTextureCount>
    fixedLogicalPaths{{
        "Graphics\\Ingame\\HUD\\techstar.gti",
        "Graphics\\Ingame\\HUD\\techcross.gti",
    }};

struct TextureSource final {
  LegacyAircraftHudIdentityStatusTextureKind kind{
      LegacyAircraftHudIdentityStatusTextureKind::technologyStar};
  LegacyAircraftHudIdentityStatusTextureArchive archive{
      LegacyAircraftHudIdentityStatusTextureArchive::localization};
  std::size_t fileIndex{};
  std::uint64_t footprint{};
  std::string logicalPath;
  std::string matchKey;
};

class SessionIdentityChanged final {};

[[nodiscard]] constexpr unsigned char
asciiLower(const unsigned char value) noexcept {
  return value >= static_cast<unsigned char>('A') &&
                 value <= static_cast<unsigned char>('Z')
             ? static_cast<unsigned char>(value + ('a' - 'A'))
             : value;
}

[[nodiscard]] bool
asciiEqualInsensitive(const std::string_view left,
                      const std::string_view right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.size(); ++index) {
    if (asciiLower(static_cast<unsigned char>(left[index])) !=
        asciiLower(static_cast<unsigned char>(right[index]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool
asciiStartsWithInsensitive(const std::string_view value,
                           const std::string_view prefix) noexcept {
  return value.size() >= prefix.size() &&
         asciiEqualInsensitive(value.substr(0U, prefix.size()), prefix);
}

[[nodiscard]] bool
asciiEndsWithInsensitive(const std::string_view value,
                         const std::string_view suffix) noexcept {
  return value.size() >= suffix.size() &&
         asciiEqualInsensitive(value.substr(value.size() - suffix.size()),
                               suffix);
}

[[nodiscard]] bool validMatchKey(const std::string_view key,
                                 const std::size_t maximumBytes) noexcept {
  if (key.empty() || key.size() > maximumBytes) {
    return false;
  }
  return std::all_of(key.begin(), key.end(), [](const char character) {
    const auto value = static_cast<unsigned char>(character);
    return (value >= static_cast<unsigned char>('a') &&
            value <= static_cast<unsigned char>('z')) ||
           (value >= static_cast<unsigned char>('A') &&
            value <= static_cast<unsigned char>('Z')) ||
           (value >= static_cast<unsigned char>('0') &&
            value <= static_cast<unsigned char>('9')) ||
           character == '_' || character == '-';
  });
}

[[nodiscard]] bool checkedAdd(std::uint64_t &total,
                              const std::uint64_t addition) noexcept {
  if (addition > std::numeric_limits<std::uint64_t>::max() - total) {
    return false;
  }
  total += addition;
  return true;
}

[[nodiscard]] bool checkedMultiply(const std::uint64_t left,
                                   const std::uint64_t right,
                                   std::uint64_t &result) noexcept {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

[[nodiscard]] std::uint64_t
sourceFootprint(const udsp::FileEntry &entry) noexcept {
  return static_cast<std::uint64_t>(entry.storedSize) +
         (entry.isCompressed() ? static_cast<std::uint64_t>(entry.unpackedSize)
                               : 0U);
}

void addIssue(LegacyAircraftHudIdentityStatusTextureLoadResult &result,
              const LegacyAircraftHudIdentityStatusTextureLoadIssueKind kind,
              const std::optional<LegacyAircraftHudIdentityStatusTextureKind>
                  textureKind = std::nullopt,
              const std::optional<std::size_t> fileIndex = std::nullopt,
              const std::optional<render::GtiUploadDataIssueKind>
                  preparationIssue = std::nullopt) {
  result.textures.reset();
  result.issues.push_back({
      .kind = kind,
      .textureKind = textureKind,
      .archiveFileIndex = fileIndex,
      .preparationIssue = preparationIssue,
  });
}

void requireExpectedSession(const VerifiedContentSession &session,
                            const VerifiedContentTransactionIdentity &identity,
                            const ContentRevision &revision) {
  if (session.transactionIdentity() != identity ||
      session.revision() != revision) {
    throw SessionIdentityChanged{};
  }
}

[[nodiscard]] bool report(
    LegacyAircraftHudIdentityStatusTextureLoadResult &result,
    const VerifiedContentSession &session,
    const VerifiedContentTransactionIdentity &identity,
    const ContentRevision &revision, const std::stop_token stopToken,
    const LegacyAircraftHudIdentityStatusTextureLoadProgressCallback &callback,
    const LegacyAircraftHudIdentityStatusTextureLoadPhase phase,
    const std::size_t completed, const std::size_t total) {
  requireExpectedSession(session, identity, revision);
  if (stopToken.stop_requested()) {
    addIssue(result,
             LegacyAircraftHudIdentityStatusTextureLoadIssueKind::cancelled);
    return false;
  }
  if (completed > total) {
    addIssue(
        result,
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::internalFailure);
    return false;
  }
  if (callback) {
    try {
      callback(
          {.phase = phase, .completedItems = completed, .totalItems = total});
    } catch (const std::bad_alloc &) {
      throw;
    } catch (...) {
      requireExpectedSession(session, identity, revision);
      addIssue(result, LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                           progressCallbackFailure);
      return false;
    }
  }
  requireExpectedSession(session, identity, revision);
  if (stopToken.stop_requested()) {
    addIssue(result,
             LegacyAircraftHudIdentityStatusTextureLoadIssueKind::cancelled);
    return false;
  }
  return true;
}

[[nodiscard]] bool validLimits(
    const LegacyAircraftHudIdentityStatusTextureLoadLimits &limits) noexcept {
  const auto &upload = limits.gti.upload;
  return limits.maximumIconCount != 0U && limits.maximumIconCount <= 4096U &&
         limits.maximumMatchKeyBytes != 0U &&
         limits.maximumMatchKeyBytes <= 4096U &&
         limits.maximumSourceBytes >= 8U &&
         limits.maximumSingleSourceFootprintBytes != 0U &&
         limits.maximumTotalSourceFootprintBytes != 0U &&
         limits.maximumDecodedRgbaBytes != 0U &&
         limits.maximumUploadRgbaBytes != 0U &&
         limits.maximumResidentRgbaBytes != 0U &&
         limits.gti.maximumSourceBytes >= 8U && upload.maximumVariants != 0U &&
         upload.maximumDimension >=
             legacyAircraftHudIdentityStatusTextureWidth &&
         upload.maximumDeclaredMipCount != 0U &&
         upload.maximumAllocatedMipCount != 0U &&
         upload.maximumUploadLevels != 0U &&
         upload.maximumDecodedRgbaBytes != 0U &&
         upload.maximumUploadRgbaBytes != 0U &&
         upload.maximumResidentRgbaBytes != 0U &&
         upload.maximumMetadataBytes != 0U;
}

[[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
expectedDimensions(const LegacyAircraftHudIdentityStatusTextureKind) noexcept {
  return {legacyAircraftHudIdentityStatusTextureWidth,
          legacyAircraftHudIdentityStatusTextureHeight};
}

[[nodiscard]] bool
validTexture(const LoadedLegacyAircraftHudIdentityStatusTexture &texture,
             const std::size_t denseIndex) noexcept {
  const auto [width, height] = expectedDimensions(texture.kind);
  if (texture.textureId.value != denseIndex ||
      texture.sourceFootprintBytes == 0U || texture.logicalPath.empty() ||
      texture.upload.request.assetId != texture.textureId ||
      texture.upload.request.archiveFileIndex != texture.sourceFileIndex ||
      texture.upload.format != legacyAircraftHudIdentityStatusTextureFormat ||
      !render::validTextureSampleSpace(texture.upload.sampleSpace) ||
      texture.upload.uploadedMipCount == 0U ||
      texture.upload.allocatedMipCount < texture.upload.uploadedMipCount ||
      texture.upload.uploadLevels.size() != texture.upload.uploadedMipCount ||
      texture.uploadLevels.size() != texture.upload.uploadedMipCount ||
      texture.upload.uploadLevels.empty() ||
      texture.upload.uploadLevels.front().width != width ||
      texture.upload.uploadLevels.front().height != height) {
    return false;
  }
  if ((texture.upload.mipPolicy == render::GtiMipPolicy::authoredChain &&
       texture.upload.allocatedMipCount != texture.upload.uploadedMipCount) ||
      (texture.upload.mipPolicy == render::GtiMipPolicy::generateFromBase &&
       texture.upload.uploadedMipCount != 1U)) {
    return false;
  }

  std::uint64_t decodedBytes = 0U;
  for (std::size_t levelIndex = 0U; levelIndex < texture.uploadLevels.size();
       ++levelIndex) {
    const auto &level = texture.upload.uploadLevels[levelIndex];
    const auto &image = texture.uploadLevels[levelIndex];
    std::uint64_t rowBytes = 0U;
    std::uint64_t imageBytes = 0U;
    if (levelIndex > std::numeric_limits<std::uint32_t>::max() ||
        !checkedMultiply(image.width, 4U, rowBytes) ||
        !checkedMultiply(rowBytes, image.height, imageBytes) ||
        !checkedAdd(decodedBytes, imageBytes) ||
        level.level != static_cast<std::uint32_t>(levelIndex) ||
        level.width != image.width || level.height != image.height ||
        level.bytesPerRow != rowBytes || level.rgbaBytes != imageBytes ||
        image.pixels.size() != imageBytes) {
      return false;
    }
  }

  std::uint64_t residentBytes = 0U;
  auto residentWidth = texture.upload.uploadLevels.front().width;
  auto residentHeight = texture.upload.uploadLevels.front().height;
  for (std::uint32_t level = 0U; level < texture.upload.allocatedMipCount;
       ++level) {
    std::uint64_t rowBytes = 0U;
    std::uint64_t levelBytes = 0U;
    if (!checkedMultiply(residentWidth, 4U, rowBytes) ||
        !checkedMultiply(rowBytes, residentHeight, levelBytes) ||
        !checkedAdd(residentBytes, levelBytes)) {
      return false;
    }
    residentWidth = std::max(1U, residentWidth >> 1U);
    residentHeight = std::max(1U, residentHeight >> 1U);
  }
  return decodedBytes == texture.upload.decodedRgbaBytes &&
         decodedBytes == texture.upload.uploadRgbaBytes &&
         residentBytes == texture.upload.residentRgbaBytes;
}

[[nodiscard]] bool
appendSource(LegacyAircraftHudIdentityStatusTextureLoadResult &result,
             std::vector<TextureSource> &sources,
             const LegacyAircraftHudIdentityStatusTextureLoadLimits &limits,
             const LegacyAircraftHudIdentityStatusTextureKind kind,
             const LegacyAircraftHudIdentityStatusTextureArchive archive,
             const udsp::Archive &archiveData, const std::size_t fileIndex,
             std::string logicalPath, std::string matchKey,
             std::uint64_t &totalSourceFootprint) {
  if (fileIndex >= archiveData.files().size()) {
    addIssue(
        result,
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::internalFailure,
        kind);
    return false;
  }
  for (const auto &existing : sources) {
    if (existing.archive == archive && existing.fileIndex == fileIndex) {
      addIssue(
          result,
          LegacyAircraftHudIdentityStatusTextureLoadIssueKind::duplicateSource,
          kind, fileIndex);
      return false;
    }
  }
  const auto &entry = archiveData.files()[fileIndex];
  if (entry.unpackedSize > limits.maximumSourceBytes ||
      entry.unpackedSize > limits.gti.maximumSourceBytes) {
    addIssue(result,
             LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                 sourceLimitExceeded,
             kind, fileIndex);
    return false;
  }
  const auto footprint = sourceFootprint(entry);
  if (footprint == 0U || footprint > limits.maximumSingleSourceFootprintBytes) {
    addIssue(result,
             LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                 sourceFootprintLimitExceeded,
             kind, fileIndex);
    return false;
  }
  if (!checkedAdd(totalSourceFootprint, footprint)) {
    addIssue(
        result,
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::integerOverflow,
        kind, fileIndex);
    return false;
  }
  if (totalSourceFootprint > limits.maximumTotalSourceFootprintBytes) {
    addIssue(result,
             LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                 aggregateSourceFootprintLimitExceeded,
             kind, fileIndex);
    return false;
  }
  sources.push_back({
      .kind = kind,
      .archive = archive,
      .fileIndex = fileIndex,
      .footprint = footprint,
      .logicalPath = std::move(logicalPath),
      .matchKey = std::move(matchKey),
  });
  return true;
}

} // namespace

bool LoadedLegacyAircraftHudIdentityStatusTextureSet::valid() const noexcept {
  if (!transactionIdentity.valid() || iconCount == 0U ||
      textures.size() !=
          legacyAircraftHudIdentityStatusFixedTextureCount + iconCount ||
      sourceFootprintBytes == 0U || decodedRgbaBytes == 0U ||
      uploadRgbaBytes == 0U || residentRgbaBytes == 0U) {
    return false;
  }

  std::uint64_t source = 0U;
  std::uint64_t decoded = 0U;
  std::uint64_t upload = 0U;
  std::uint64_t resident = 0U;
  for (std::size_t index = 0U; index < textures.size(); ++index) {
    const auto &texture = textures[index];
    if (!validTexture(texture, index)) {
      return false;
    }
    if (index < legacyAircraftHudIdentityStatusFixedTextureCount) {
      const auto expectedKind =
          index == 0U
              ? LegacyAircraftHudIdentityStatusTextureKind::technologyStar
              : LegacyAircraftHudIdentityStatusTextureKind::technologyCross;
      if (texture.kind != expectedKind ||
          texture.archive !=
              LegacyAircraftHudIdentityStatusTextureArchive::localization ||
          texture.logicalPath != fixedLogicalPaths[index] ||
          !texture.matchKey.empty() || texture.iconIndex != 0U) {
        return false;
      }
    } else {
      const auto expectedIconIndex = static_cast<std::uint32_t>(
          index - legacyAircraftHudIdentityStatusFixedTextureCount);
      if (texture.kind !=
              LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon ||
          texture.archive !=
              LegacyAircraftHudIdentityStatusTextureArchive::source ||
          texture.iconIndex != expectedIconIndex ||
          !validMatchKey(texture.matchKey, 4096U) ||
          !asciiStartsWithInsensitive(texture.logicalPath,
                                      catalogLogicalDirectoryPrefix) ||
          !asciiEndsWithInsensitive(texture.logicalPath, catalogSuffix) ||
          texture.logicalPath.size() <= catalogLogicalDirectoryPrefix.size() +
                                            catalogPrefix.size() +
                                            catalogSuffix.size() ||
          !asciiStartsWithInsensitive(
              std::string_view(texture.logicalPath)
                  .substr(catalogLogicalDirectoryPrefix.size()),
              catalogPrefix) ||
          !asciiEqualInsensitive(
              std::string_view(texture.logicalPath)
                  .substr(catalogLogicalDirectoryPrefix.size(),
                          texture.logicalPath.size() -
                              catalogLogicalDirectoryPrefix.size() -
                              catalogSuffix.size()),
              texture.matchKey)) {
        return false;
      }
      for (std::size_t previous =
               legacyAircraftHudIdentityStatusFixedTextureCount;
           previous < index; ++previous) {
        if (asciiEqualInsensitive(textures[previous].matchKey,
                                  texture.matchKey)) {
          return false;
        }
      }
    }
    for (std::size_t previous = 0U; previous < index; ++previous) {
      if (textures[previous].archive == texture.archive &&
          textures[previous].sourceFileIndex == texture.sourceFileIndex) {
        return false;
      }
    }
    if (!checkedAdd(source, texture.sourceFootprintBytes) ||
        !checkedAdd(decoded, texture.upload.decodedRgbaBytes) ||
        !checkedAdd(upload, texture.upload.uploadRgbaBytes) ||
        !checkedAdd(resident, texture.upload.residentRgbaBytes)) {
      return false;
    }
  }
  return source == sourceFootprintBytes && decoded == decodedRgbaBytes &&
         upload == uploadRgbaBytes && resident == residentRgbaBytes;
}

bool LoadedLegacyAircraftHudIdentityStatusTextureSet::belongsTo(
    const VerifiedContentSession &session) const noexcept {
  return valid() && transactionIdentity == session.transactionIdentity() &&
         revision == session.revision();
}

const LoadedLegacyAircraftHudIdentityStatusTexture *
LoadedLegacyAircraftHudIdentityStatusTextureSet::teamBadge(
    const LegacyAircraftHudIdentityStatusTextureKind kind) const noexcept {
  if (!valid()) {
    return nullptr;
  }
  if (kind == LegacyAircraftHudIdentityStatusTextureKind::technologyStar) {
    return &textures[0U];
  }
  if (kind == LegacyAircraftHudIdentityStatusTextureKind::technologyCross) {
    return &textures[1U];
  }
  return nullptr;
}

const LoadedLegacyAircraftHudIdentityStatusTexture *
LoadedLegacyAircraftHudIdentityStatusTextureSet::icon(
    const std::uint32_t iconIndex) const noexcept {
  const auto offset = legacyAircraftHudIdentityStatusFixedTextureCount +
                      static_cast<std::size_t>(iconIndex);
  if (!valid() || offset >= textures.size()) {
    return nullptr;
  }
  return &textures[offset];
}

const LoadedLegacyAircraftHudIdentityStatusTexture *
LoadedLegacyAircraftHudIdentityStatusTextureSet::findIconForAircraftType(
    const std::string_view aircraftTypeName) const noexcept {
  if (aircraftTypeName.empty() || !valid()) {
    return nullptr;
  }
  for (std::size_t index = legacyAircraftHudIdentityStatusFixedTextureCount;
       index < textures.size(); ++index) {
    if (asciiEqualInsensitive(textures[index].matchKey, aircraftTypeName)) {
      return &textures[index];
    }
  }
  return nullptr;
}

LegacyAircraftHudIdentityStatusTextureLoadResult
loadLegacyAircraftHudIdentityStatusTextures(
    VerifiedContentSession &session,
    const LegacyAircraftHudIdentityStatusTextureLoadLimits &externalLimits,
    const std::stop_token stopToken,
    LegacyAircraftHudIdentityStatusTextureLoadProgressCallback progress) {
  LegacyAircraftHudIdentityStatusTextureLoadResult result;
  try {
    const auto identity = session.transactionIdentity();
    const auto revision = session.revision();
    if (!identity.valid()) {
      addIssue(result, LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                           sessionIdentityChanged);
      return result;
    }
    const auto limits = externalLimits;
    if (!validLimits(limits)) {
      addIssue(
          result,
          LegacyAircraftHudIdentityStatusTextureLoadIssueKind::invalidLimits);
      return result;
    }
    if (!report(
            result, session, identity, revision, stopToken, progress,
            LegacyAircraftHudIdentityStatusTextureLoadPhase::validatingInput,
            0U, legacyAircraftHudIdentityStatusFixedTextureCount)) {
      return result;
    }

    std::vector<TextureSource> sources;
    sources.reserve(legacyAircraftHudIdentityStatusFixedTextureCount +
                    limits.maximumIconCount);
    std::uint64_t totalSourceFootprint = 0U;
    for (std::size_t index = 0U; index < fixedLogicalPaths.size(); ++index) {
      const auto lookup =
          session.localizationArchive().lookup(fixedLogicalPaths[index]);
      const auto kind =
          index == 0U
              ? LegacyAircraftHudIdentityStatusTextureKind::technologyStar
              : LegacyAircraftHudIdentityStatusTextureKind::technologyCross;
      if (lookup.status != udsp::LookupStatus::unique) {
        addIssue(result,
                 lookup.status == udsp::LookupStatus::ambiguous
                     ? LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                           textureAmbiguous
                     : LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                           textureNotFound,
                 kind);
        return result;
      }
      if (!appendSource(
              result, sources, limits, kind,
              LegacyAircraftHudIdentityStatusTextureArchive::localization,
              session.localizationArchive(), lookup.fileIndex,
              std::string(fixedLogicalPaths[index]), {},
              totalSourceFootprint)) {
        return result;
      }
    }

    const auto &sourceArchive = session.sourceArchive();
    std::optional<std::pair<std::size_t, std::size_t>> directoryRange;
    for (const auto &directory : sourceArchive.directories()) {
      if (!asciiEqualInsensitive(directory.path, catalogDirectory)) {
        continue;
      }
      const auto range =
          std::pair{static_cast<std::size_t>(directory.firstFileIndex),
                    static_cast<std::size_t>(directory.fileCount)};
      if (directoryRange.has_value() && *directoryRange != range) {
        addIssue(result, LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                             catalogDirectoryAmbiguous);
        return result;
      }
      directoryRange = range;
    }
    if (!directoryRange.has_value()) {
      addIssue(result, LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                           catalogDirectoryNotFound);
      return result;
    }
    const auto [firstFileIndex, fileCount] = *directoryRange;
    if (firstFileIndex > sourceArchive.files().size() ||
        fileCount > sourceArchive.files().size() - firstFileIndex) {
      addIssue(
          result,
          LegacyAircraftHudIdentityStatusTextureLoadIssueKind::internalFailure);
      return result;
    }
    for (std::size_t offset = 0U; offset < fileCount; ++offset) {
      const auto fileIndex = firstFileIndex + offset;
      const auto &entry = sourceArchive.files()[fileIndex];
      const std::string_view name = entry.name;
      if (!asciiStartsWithInsensitive(name, catalogPrefix) ||
          !asciiEndsWithInsensitive(name, catalogSuffix)) {
        continue;
      }
      if (name.size() <= catalogPrefix.size() + catalogSuffix.size()) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     catalogEntryInvalid,
                 LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon,
                 fileIndex);
        return result;
      }
      const auto key = name.substr(0U, name.size() - catalogSuffix.size());
      if (!validMatchKey(key, limits.maximumMatchKeyBytes)) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     catalogEntryInvalid,
                 LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon,
                 fileIndex);
        return result;
      }
      if (sources.size() - legacyAircraftHudIdentityStatusFixedTextureCount >=
          limits.maximumIconCount) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     catalogLimitExceeded,
                 LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon,
                 fileIndex);
        return result;
      }
      for (std::size_t index = legacyAircraftHudIdentityStatusFixedTextureCount;
           index < sources.size(); ++index) {
        if (asciiEqualInsensitive(sources[index].matchKey, key)) {
          addIssue(result,
                   LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                       duplicateCatalogKey,
                   LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon,
                   fileIndex);
          return result;
        }
      }
      std::string logicalPath(catalogDirectory);
      logicalPath.push_back('\\');
      logicalPath.append(name);
      const auto lookup = sourceArchive.lookup(logicalPath);
      if (lookup.status != udsp::LookupStatus::unique ||
          lookup.fileIndex != fileIndex) {
        addIssue(result,
                 lookup.status == udsp::LookupStatus::ambiguous
                     ? LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                           textureAmbiguous
                     : LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                           catalogEntryInvalid,
                 LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon,
                 fileIndex);
        return result;
      }
      if (!appendSource(
              result, sources, limits,
              LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon,
              LegacyAircraftHudIdentityStatusTextureArchive::source,
              sourceArchive, fileIndex, std::move(logicalPath),
              std::string(key), totalSourceFootprint)) {
        return result;
      }
    }
    if (sources.size() == legacyAircraftHudIdentityStatusFixedTextureCount) {
      addIssue(result,
               LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                   catalogEntryInvalid,
               LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon);
      return result;
    }
    const auto totalItems = sources.size();
    if (!report(
            result, session, identity, revision, stopToken, progress,
            LegacyAircraftHudIdentityStatusTextureLoadPhase::discoveringCatalog,
            totalItems, totalItems)) {
      return result;
    }

    LoadedLegacyAircraftHudIdentityStatusTextureSet candidate{
        .revision = revision,
        .transactionIdentity = identity,
        .textures = {},
        .iconCount =
            totalItems - legacyAircraftHudIdentityStatusFixedTextureCount,
        .sourceFootprintBytes = totalSourceFootprint,
    };
    candidate.textures.reserve(totalItems);
    for (std::size_t index = 0U; index < sources.size(); ++index) {
      const auto &source = sources[index];
      requireExpectedSession(session, identity, revision);

      std::vector<std::uint8_t> bytes;
      try {
        bytes = source.archive ==
                        LegacyAircraftHudIdentityStatusTextureArchive::source
                    ? session.readSourceFile(source.fileIndex,
                                             limits.maximumSourceBytes)
                    : session.readLocalizationFile(source.fileIndex,
                                                   limits.maximumSourceBytes);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        requireExpectedSession(session, identity, revision);
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     sourceReadFailure,
                 source.kind, source.fileIndex);
        return result;
      }
      if (!report(
              result, session, identity, revision, stopToken, progress,
              LegacyAircraftHudIdentityStatusTextureLoadPhase::readingTextures,
              index + 1U, totalItems)) {
        return result;
      }

      auto gtiLimits = limits.gti;
      gtiLimits.maximumSourceBytes =
          std::min(gtiLimits.maximumSourceBytes, limits.maximumSourceBytes);
      gtiLimits.upload.maximumDecodedRgbaBytes =
          std::min(gtiLimits.upload.maximumDecodedRgbaBytes,
                   limits.maximumDecodedRgbaBytes);
      gtiLimits.upload.maximumUploadRgbaBytes =
          std::min(gtiLimits.upload.maximumUploadRgbaBytes,
                   limits.maximumUploadRgbaBytes);
      gtiLimits.upload.maximumResidentRgbaBytes =
          std::min(gtiLimits.upload.maximumResidentRgbaBytes,
                   limits.maximumResidentRgbaBytes);
      const render::TextureImportRequest request{
          .assetId = render::TextureAssetId{static_cast<std::uint32_t>(index)},
          .archiveFileIndex = source.fileIndex,
          .logicalPath = source.logicalPath,
      };
      render::GtiUploadPreparation preparation;
      try {
        auto sourcePreparation =
            render::prepareTextureSource(request, bytes, nullptr, {}, gtiLimits);
        preparation = std::move(sourcePreparation.classicGti);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     texturePreparationFailure,
                 source.kind, source.fileIndex);
        return result;
      }
      std::vector<std::uint8_t>().swap(bytes);
      if (!preparation.success()) {
        if (preparation.issues.empty()) {
          addIssue(result,
                   LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                       texturePreparationFailure,
                   source.kind, source.fileIndex);
        } else {
          for (const auto &upstream : preparation.issues) {
            addIssue(result,
                     LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                         texturePreparationFailure,
                     source.kind, source.fileIndex, upstream.kind);
          }
        }
        return result;
      }

      const auto &uploadPlan = *preparation.plan;
      const auto [expectedWidth, expectedHeight] =
          expectedDimensions(source.kind);
      if (uploadPlan.format != legacyAircraftHudIdentityStatusTextureFormat) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     unexpectedFormat,
                 source.kind, source.fileIndex);
        return result;
      }
      if (uploadPlan.uploadLevels.empty() ||
          uploadPlan.uploadLevels.front().width != expectedWidth ||
          uploadPlan.uploadLevels.front().height != expectedHeight) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     unexpectedDimensions,
                 source.kind, source.fileIndex);
        return result;
      }

      auto decoded = candidate.decodedRgbaBytes;
      auto uploaded = candidate.uploadRgbaBytes;
      auto resident = candidate.residentRgbaBytes;
      if (!checkedAdd(decoded, uploadPlan.decodedRgbaBytes) ||
          !checkedAdd(uploaded, uploadPlan.uploadRgbaBytes) ||
          !checkedAdd(resident, uploadPlan.residentRgbaBytes)) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     integerOverflow,
                 source.kind, source.fileIndex);
        return result;
      }
      if (decoded > limits.maximumDecodedRgbaBytes) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     decodedRgbaLimitExceeded,
                 source.kind, source.fileIndex);
        return result;
      }
      if (uploaded > limits.maximumUploadRgbaBytes) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     uploadRgbaLimitExceeded,
                 source.kind, source.fileIndex);
        return result;
      }
      if (resident > limits.maximumResidentRgbaBytes) {
        addIssue(result,
                 LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                     residentRgbaLimitExceeded,
                 source.kind, source.fileIndex);
        return result;
      }

      candidate.decodedRgbaBytes = decoded;
      candidate.uploadRgbaBytes = uploaded;
      candidate.residentRgbaBytes = resident;
      candidate.textures.push_back({
          .kind = source.kind,
          .archive = source.archive,
          .textureId = request.assetId,
          .iconIndex =
              source.kind ==
                      LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon
                  ? static_cast<std::uint32_t>(
                        index -
                        legacyAircraftHudIdentityStatusFixedTextureCount)
                  : 0U,
          .sourceFileIndex = source.fileIndex,
          .sourceFootprintBytes = source.footprint,
          .logicalPath = source.logicalPath,
          .matchKey = source.matchKey,
          .upload = std::move(*preparation.plan),
          .uploadLevels = std::move(preparation.uploadLevels),
      });
      if (!report(result, session, identity, revision, stopToken, progress,
                  LegacyAircraftHudIdentityStatusTextureLoadPhase::
                      preparingTextures,
                  index + 1U, totalItems)) {
        return result;
      }
    }

    if (!report(result, session, identity, revision, stopToken, progress,
                LegacyAircraftHudIdentityStatusTextureLoadPhase::complete,
                totalItems, totalItems)) {
      return result;
    }
    requireExpectedSession(session, identity, revision);
    if (!candidate.valid()) {
      addIssue(
          result,
          LegacyAircraftHudIdentityStatusTextureLoadIssueKind::internalFailure);
      return result;
    }
    result.textures = std::move(candidate);
    return result;
  } catch (const SessionIdentityChanged &) {
    result.textures.reset();
    result.issues.clear();
    addIssue(result, LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
                         sessionIdentityChanged);
    return result;
  } catch (const std::bad_alloc &) {
    result.textures.reset();
    result.issues.clear();
    addIssue(
        result,
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::allocationFailure);
    return result;
  } catch (...) {
    result.textures.reset();
    result.issues.clear();
    addIssue(
        result,
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::internalFailure);
    return result;
  }
}

} // namespace airfix::content
