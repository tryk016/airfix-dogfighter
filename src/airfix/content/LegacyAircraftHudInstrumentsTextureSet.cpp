#include "airfix/content/LegacyAircraftHudInstrumentsTextureSet.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace airfix::content {
namespace {

struct TextureSpecification final {
  LegacyAircraftHudInstrumentTextureRole role;
  LegacyAircraftHudInstrumentTextureArchive archive;
  std::string_view logicalPath;
  std::uint32_t width;
  std::uint32_t height;
};

constexpr std::array<TextureSpecification,
                     legacyAircraftHudInstrumentTextureCount>
    textureSpecifications{{
        {
            .role = LegacyAircraftHudInstrumentTextureRole::leftFace,
            .archive = LegacyAircraftHudInstrumentTextureArchive::localization,
            .logicalPath = "Graphics\\Ingame\\HUD\\clock1.gti",
            .width = legacyAircraftHudInstrumentFaceWidth,
            .height = legacyAircraftHudInstrumentFaceHeight,
        },
        {
            .role = LegacyAircraftHudInstrumentTextureRole::rightFace,
            .archive = LegacyAircraftHudInstrumentTextureArchive::localization,
            .logicalPath = "Graphics\\Ingame\\HUD\\clock2.gti",
            .width = legacyAircraftHudInstrumentFaceWidth,
            .height = legacyAircraftHudInstrumentFaceHeight,
        },
        {
            .role = LegacyAircraftHudInstrumentTextureRole::indicator,
            .archive = LegacyAircraftHudInstrumentTextureArchive::source,
            .logicalPath = "Graphics\\Ingame\\HUD\\indicator.gti",
            .width = legacyAircraftHudIndicatorWidth,
            .height = legacyAircraftHudIndicatorHeight,
        },
    }};

class SessionIdentityChanged final {};

[[nodiscard]] constexpr std::optional<std::size_t>
roleIndex(const LegacyAircraftHudInstrumentTextureRole role) noexcept {
  const auto index = static_cast<std::size_t>(role);
  return index < legacyAircraftHudInstrumentTextureCount
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

[[nodiscard]] const udsp::Archive &
archiveFor(const VerifiedContentSession &session,
           const LegacyAircraftHudInstrumentTextureArchive archive) noexcept {
  return archive == LegacyAircraftHudInstrumentTextureArchive::source
             ? session.sourceArchive()
             : session.localizationArchive();
}

[[nodiscard]] std::vector<std::uint8_t>
readFile(VerifiedContentSession &session,
         const LegacyAircraftHudInstrumentTextureArchive archive,
         const std::size_t fileIndex, const std::size_t maximumBytes) {
  return archive == LegacyAircraftHudInstrumentTextureArchive::source
             ? session.readSourceFile(fileIndex, maximumBytes)
             : session.readLocalizationFile(fileIndex, maximumBytes);
}

void addIssue(
    LegacyAircraftHudInstrumentTextureLoadResult &result,
    const LegacyAircraftHudInstrumentTextureLoadIssueKind kind,
    const std::optional<LegacyAircraftHudInstrumentTextureRole> role =
        std::nullopt,
    const std::optional<LegacyAircraftHudInstrumentTextureArchive> archive =
        std::nullopt,
    const std::optional<std::size_t> archiveFileIndex = std::nullopt,
    const std::optional<render::GtiUploadDataIssueKind> preparationIssue =
        std::nullopt) {
  result.textures.reset();
  result.issues.push_back({
      .kind = kind,
      .role = role,
      .archive = archive,
      .archiveFileIndex = archiveFileIndex,
      .preparationIssue = preparationIssue,
  });
}

void addTextureIssue(
    LegacyAircraftHudInstrumentTextureLoadResult &result,
    const LegacyAircraftHudInstrumentTextureLoadIssueKind kind,
    const TextureSpecification &specification,
    const std::optional<std::size_t> archiveFileIndex = std::nullopt,
    const std::optional<render::GtiUploadDataIssueKind> preparationIssue =
        std::nullopt) {
  addIssue(result, kind, specification.role, specification.archive,
           archiveFileIndex, preparationIssue);
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

[[nodiscard]] bool
report(LegacyAircraftHudInstrumentTextureLoadResult &result,
       const VerifiedContentSession &session,
       const VerifiedContentTransactionIdentity &expectedIdentity,
       const ContentRevision &expectedRevision, const std::stop_token stopToken,
       const LegacyAircraftHudInstrumentTextureLoadProgressCallback &callback,
       const LegacyAircraftHudInstrumentTextureLoadPhase phase,
       const std::size_t completedItems) {
  requireExpectedSession(session, expectedIdentity, expectedRevision);
  if (stopToken.stop_requested()) {
    addIssue(result,
             LegacyAircraftHudInstrumentTextureLoadIssueKind::cancelled);
    return false;
  }
  if (completedItems > legacyAircraftHudInstrumentTextureCount) {
    addIssue(result,
             LegacyAircraftHudInstrumentTextureLoadIssueKind::internalFailure);
    return false;
  }
  if (callback) {
    try {
      callback({
          .phase = phase,
          .completedItems = completedItems,
          .totalItems = legacyAircraftHudInstrumentTextureCount,
      });
    } catch (const std::bad_alloc &) {
      throw;
    } catch (...) {
      requireExpectedSession(session, expectedIdentity, expectedRevision);
      addIssue(result, LegacyAircraftHudInstrumentTextureLoadIssueKind::
                           progressCallbackFailure);
      return false;
    }
  }
  requireExpectedSession(session, expectedIdentity, expectedRevision);
  if (stopToken.stop_requested()) {
    addIssue(result,
             LegacyAircraftHudInstrumentTextureLoadIssueKind::cancelled);
    return false;
  }
  return true;
}

[[nodiscard]] bool validLimits(
    const LegacyAircraftHudInstrumentTextureLoadLimits &limits) noexcept {
  const auto &upload = limits.gti.upload;
  return limits.maximumSourceBytes >= 8U &&
         limits.maximumSingleSourceFootprintBytes != 0U &&
         limits.maximumTotalSourceFootprintBytes != 0U &&
         limits.maximumDecodedRgbaBytes != 0U &&
         limits.maximumUploadRgbaBytes != 0U &&
         limits.maximumResidentRgbaBytes != 0U &&
         limits.gti.maximumSourceBytes >= 8U && upload.maximumVariants != 0U &&
         upload.maximumDimension >= legacyAircraftHudInstrumentFaceWidth &&
         upload.maximumDeclaredMipCount != 0U &&
         upload.maximumAllocatedMipCount != 0U &&
         upload.maximumUploadLevels != 0U &&
         upload.maximumDecodedRgbaBytes != 0U &&
         upload.maximumUploadRgbaBytes != 0U &&
         upload.maximumResidentRgbaBytes != 0U &&
         upload.maximumMetadataBytes != 0U;
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

[[nodiscard]] bool
validTexture(const LoadedLegacyAircraftHudInstrumentTexture &texture,
             const TextureSpecification &specification) noexcept {
  const auto expectedIndex = roleIndex(specification.role);
  if (!expectedIndex.has_value() || texture.role != specification.role ||
      texture.archive != specification.archive ||
      texture.textureId.value != *expectedIndex ||
      texture.sourceFootprintBytes == 0U ||
      texture.logicalPath != specification.logicalPath ||
      texture.upload.request.assetId != texture.textureId ||
      texture.upload.request.archiveFileIndex != texture.sourceFileIndex ||
      texture.upload.format != legacyAircraftHudInstrumentTextureFormat ||
      !render::validTextureSampleSpace(texture.upload.sampleSpace) ||
      texture.upload.uploadedMipCount == 0U ||
      texture.upload.allocatedMipCount < texture.upload.uploadedMipCount ||
      texture.upload.uploadLevels.size() != texture.upload.uploadedMipCount ||
      texture.uploadLevels.size() != texture.upload.uploadedMipCount ||
      texture.upload.uploadLevels.empty() ||
      texture.upload.uploadLevels.front().width != specification.width ||
      texture.upload.uploadLevels.front().height != specification.height) {
    return false;
  }
  if ((texture.upload.mipPolicy == render::GtiMipPolicy::authoredChain &&
       texture.upload.allocatedMipCount != texture.upload.uploadedMipCount) ||
      (texture.upload.mipPolicy == render::GtiMipPolicy::generateFromBase &&
       texture.upload.uploadedMipCount != 1U)) {
    return false;
  }

  std::uint64_t decodedBytes = 0U;
  for (std::size_t index = 0U; index < texture.uploadLevels.size(); ++index) {
    const auto &level = texture.upload.uploadLevels[index];
    const auto &image = texture.uploadLevels[index];
    std::uint64_t rowBytes = 0U;
    std::uint64_t imageBytes = 0U;
    if (index > std::numeric_limits<std::uint32_t>::max() ||
        !checkedMultiply(image.width, 4U, rowBytes) ||
        !checkedMultiply(rowBytes, image.height, imageBytes) ||
        !checkedAdd(decodedBytes, imageBytes) ||
        level.level != static_cast<std::uint32_t>(index) ||
        level.width != image.width || level.height != image.height ||
        level.bytesPerRow != rowBytes || level.rgbaBytes != imageBytes ||
        image.pixels.size() != imageBytes) {
      return false;
    }
  }

  std::uint64_t residentBytes = 0U;
  auto width = texture.upload.uploadLevels.front().width;
  auto height = texture.upload.uploadLevels.front().height;
  for (std::uint32_t level = 0U; level < texture.upload.allocatedMipCount;
       ++level) {
    std::uint64_t rowBytes = 0U;
    std::uint64_t levelBytes = 0U;
    if (!checkedMultiply(width, 4U, rowBytes) ||
        !checkedMultiply(rowBytes, height, levelBytes) ||
        !checkedAdd(residentBytes, levelBytes)) {
      return false;
    }
    width = std::max(1U, width >> 1U);
    height = std::max(1U, height >> 1U);
  }
  return decodedBytes == texture.upload.decodedRgbaBytes &&
         decodedBytes == texture.upload.uploadRgbaBytes &&
         residentBytes == texture.upload.residentRgbaBytes;
}

} // namespace

bool LoadedLegacyAircraftHudInstrumentTextureSet::valid() const noexcept {
  if (!transactionIdentity.valid() || sourceFootprintBytes == 0U ||
      decodedRgbaBytes == 0U || uploadRgbaBytes == 0U ||
      residentRgbaBytes == 0U) {
    return false;
  }
  std::uint64_t source = 0U;
  std::uint64_t decoded = 0U;
  std::uint64_t upload = 0U;
  std::uint64_t resident = 0U;
  for (std::size_t index = 0U; index < textures.size(); ++index) {
    const auto &texture = textures[index];
    if (!validTexture(texture, textureSpecifications[index])) {
      return false;
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

bool LoadedLegacyAircraftHudInstrumentTextureSet::belongsTo(
    const VerifiedContentSession &session) const noexcept {
  return valid() && transactionIdentity == session.transactionIdentity() &&
         revision == session.revision();
}

const LoadedLegacyAircraftHudInstrumentTexture *
LoadedLegacyAircraftHudInstrumentTextureSet::texture(
    const LegacyAircraftHudInstrumentTextureRole role) const noexcept {
  const auto index = roleIndex(role);
  if (!index.has_value() || !valid()) {
    return nullptr;
  }
  return &textures[*index];
}

LegacyAircraftHudInstrumentTextureLoadResult
loadLegacyAircraftHudInstrumentTextures(
    VerifiedContentSession &session,
    const LegacyAircraftHudInstrumentTextureLoadLimits &externalLimits,
    const std::stop_token stopToken,
    LegacyAircraftHudInstrumentTextureLoadProgressCallback progress) {
  LegacyAircraftHudInstrumentTextureLoadResult result;
  try {
    const auto expectedIdentity = session.transactionIdentity();
    const auto expectedRevision = session.revision();
    if (!expectedIdentity.valid()) {
      addIssue(result, LegacyAircraftHudInstrumentTextureLoadIssueKind::
                           sessionIdentityChanged);
      return result;
    }
    const auto limits = externalLimits;
    if (!validLimits(limits)) {
      addIssue(result,
               LegacyAircraftHudInstrumentTextureLoadIssueKind::invalidLimits);
      return result;
    }
    if (!report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress,
                LegacyAircraftHudInstrumentTextureLoadPhase::validatingInput,
                0U)) {
      return result;
    }

    std::array<std::size_t, legacyAircraftHudInstrumentTextureCount>
        sourceIndices{};
    std::array<std::uint64_t, legacyAircraftHudInstrumentTextureCount>
        sourceFootprints{};
    std::uint64_t totalSourceFootprint = 0U;
    for (std::size_t index = 0U; index < textureSpecifications.size();
         ++index) {
      const auto &specification = textureSpecifications[index];
      const auto &archive = archiveFor(session, specification.archive);
      const auto lookup = archive.lookup(specification.logicalPath);
      if (lookup.status != udsp::LookupStatus::unique) {
        addTextureIssue(result,
                        lookup.status == udsp::LookupStatus::ambiguous
                            ? LegacyAircraftHudInstrumentTextureLoadIssueKind::
                                  textureAmbiguous
                            : LegacyAircraftHudInstrumentTextureLoadIssueKind::
                                  textureNotFound,
                        specification);
        return result;
      }
      for (std::size_t previous = 0U; previous < index; ++previous) {
        if (textureSpecifications[previous].archive == specification.archive &&
            sourceIndices[previous] == lookup.fileIndex) {
          addTextureIssue(
              result,
              LegacyAircraftHudInstrumentTextureLoadIssueKind::duplicateSource,
              specification, lookup.fileIndex);
          return result;
        }
      }
      const auto &entry = archive.files().at(lookup.fileIndex);
      if (entry.unpackedSize > limits.maximumSourceBytes ||
          entry.unpackedSize > limits.gti.maximumSourceBytes) {
        addTextureIssue(result,
                        LegacyAircraftHudInstrumentTextureLoadIssueKind::
                            sourceLimitExceeded,
                        specification, lookup.fileIndex);
        return result;
      }
      const auto footprint = sourceFootprint(entry);
      if (footprint == 0U ||
          footprint > limits.maximumSingleSourceFootprintBytes) {
        addTextureIssue(result,
                        LegacyAircraftHudInstrumentTextureLoadIssueKind::
                            sourceFootprintLimitExceeded,
                        specification, lookup.fileIndex);
        return result;
      }
      if (!checkedAdd(totalSourceFootprint, footprint)) {
        addIssue(
            result,
            LegacyAircraftHudInstrumentTextureLoadIssueKind::integerOverflow);
        return result;
      }
      if (totalSourceFootprint > limits.maximumTotalSourceFootprintBytes) {
        addIssue(result, LegacyAircraftHudInstrumentTextureLoadIssueKind::
                             aggregateSourceFootprintLimitExceeded);
        return result;
      }
      sourceIndices[index] = lookup.fileIndex;
      sourceFootprints[index] = footprint;
      if (!report(
              result, session, expectedIdentity, expectedRevision, stopToken,
              progress,
              LegacyAircraftHudInstrumentTextureLoadPhase::lookingUpTextures,
              index + 1U)) {
        return result;
      }
    }

    LoadedLegacyAircraftHudInstrumentTextureSet candidate{
        .revision = expectedRevision,
        .transactionIdentity = expectedIdentity,
        .textures = {},
        .sourceFootprintBytes = totalSourceFootprint,
    };
    for (std::size_t index = 0U; index < textureSpecifications.size();
         ++index) {
      const auto &specification = textureSpecifications[index];
      const auto sourceFileIndex = sourceIndices[index];
      requireExpectedSession(session, expectedIdentity, expectedRevision);

      std::vector<std::uint8_t> bytes;
      try {
        bytes = readFile(session, specification.archive, sourceFileIndex,
                         limits.maximumSourceBytes);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        requireExpectedSession(session, expectedIdentity, expectedRevision);
        addTextureIssue(
            result,
            LegacyAircraftHudInstrumentTextureLoadIssueKind::sourceReadFailure,
            specification, sourceFileIndex);
        return result;
      }
      if (!report(result, session, expectedIdentity, expectedRevision,
                  stopToken, progress,
                  LegacyAircraftHudInstrumentTextureLoadPhase::readingTextures,
                  index + 1U)) {
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
          .archiveFileIndex = sourceFileIndex,
          .logicalPath = std::string(specification.logicalPath),
      };
      render::GtiUploadPreparation preparation;
      try {
        auto sourcePreparation =
            render::prepareTextureSource(request, bytes, nullptr, {}, gtiLimits);
        preparation = std::move(sourcePreparation.classicGti);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        addTextureIssue(result,
                        LegacyAircraftHudInstrumentTextureLoadIssueKind::
                            texturePreparationFailure,
                        specification, sourceFileIndex);
        return result;
      }
      std::vector<std::uint8_t>().swap(bytes);
      if (!preparation.success()) {
        if (preparation.issues.empty()) {
          addTextureIssue(result,
                          LegacyAircraftHudInstrumentTextureLoadIssueKind::
                              texturePreparationFailure,
                          specification, sourceFileIndex);
        } else {
          for (const auto &upstream : preparation.issues) {
            addTextureIssue(result,
                            LegacyAircraftHudInstrumentTextureLoadIssueKind::
                                texturePreparationFailure,
                            specification, sourceFileIndex, upstream.kind);
          }
        }
        return result;
      }

      const auto &upload = *preparation.plan;
      if (upload.format != legacyAircraftHudInstrumentTextureFormat) {
        addTextureIssue(
            result,
            LegacyAircraftHudInstrumentTextureLoadIssueKind::unexpectedFormat,
            specification, sourceFileIndex);
        return result;
      }
      if (upload.uploadLevels.empty() ||
          upload.uploadLevels.front().width != specification.width ||
          upload.uploadLevels.front().height != specification.height) {
        addTextureIssue(result,
                        LegacyAircraftHudInstrumentTextureLoadIssueKind::
                            unexpectedDimensions,
                        specification, sourceFileIndex);
        return result;
      }
      auto decoded = candidate.decodedRgbaBytes;
      auto uploadBytes = candidate.uploadRgbaBytes;
      auto resident = candidate.residentRgbaBytes;
      if (!checkedAdd(decoded, upload.decodedRgbaBytes) ||
          !checkedAdd(uploadBytes, upload.uploadRgbaBytes) ||
          !checkedAdd(resident, upload.residentRgbaBytes)) {
        addIssue(
            result,
            LegacyAircraftHudInstrumentTextureLoadIssueKind::integerOverflow);
        return result;
      }
      if (decoded > limits.maximumDecodedRgbaBytes) {
        addIssue(result, LegacyAircraftHudInstrumentTextureLoadIssueKind::
                             decodedRgbaLimitExceeded);
        return result;
      }
      if (uploadBytes > limits.maximumUploadRgbaBytes) {
        addIssue(result, LegacyAircraftHudInstrumentTextureLoadIssueKind::
                             uploadRgbaLimitExceeded);
        return result;
      }
      if (resident > limits.maximumResidentRgbaBytes) {
        addIssue(result, LegacyAircraftHudInstrumentTextureLoadIssueKind::
                             residentRgbaLimitExceeded);
        return result;
      }

      candidate.decodedRgbaBytes = decoded;
      candidate.uploadRgbaBytes = uploadBytes;
      candidate.residentRgbaBytes = resident;
      candidate.textures[index] = {
          .role = specification.role,
          .archive = specification.archive,
          .textureId = request.assetId,
          .sourceFileIndex = sourceFileIndex,
          .sourceFootprintBytes = sourceFootprints[index],
          .logicalPath = std::string(specification.logicalPath),
          .upload = std::move(*preparation.plan),
          .uploadLevels = std::move(preparation.uploadLevels),
      };
      if (!report(
              result, session, expectedIdentity, expectedRevision, stopToken,
              progress,
              LegacyAircraftHudInstrumentTextureLoadPhase::preparingTextures,
              index + 1U)) {
        return result;
      }
    }

    if (!report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress, LegacyAircraftHudInstrumentTextureLoadPhase::complete,
                legacyAircraftHudInstrumentTextureCount)) {
      return result;
    }
    requireExpectedSession(session, expectedIdentity, expectedRevision);
    if (!candidate.valid()) {
      addIssue(
          result,
          LegacyAircraftHudInstrumentTextureLoadIssueKind::internalFailure);
      return result;
    }
    result.textures = std::move(candidate);
    return result;
  } catch (const SessionIdentityChanged &) {
    result.textures.reset();
    result.issues.clear();
    addIssue(result, LegacyAircraftHudInstrumentTextureLoadIssueKind::
                         sessionIdentityChanged);
    return result;
  } catch (const std::bad_alloc &) {
    result.textures.reset();
    result.issues.clear();
    addIssue(
        result,
        LegacyAircraftHudInstrumentTextureLoadIssueKind::allocationFailure);
    return result;
  } catch (...) {
    result.textures.reset();
    result.issues.clear();
    addIssue(result,
             LegacyAircraftHudInstrumentTextureLoadIssueKind::internalFailure);
    return result;
  }
}

} // namespace airfix::content
