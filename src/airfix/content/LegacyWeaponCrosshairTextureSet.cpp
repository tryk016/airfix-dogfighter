#include "airfix/content/LegacyWeaponCrosshairTextureSet.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace airfix::content {
namespace {

struct TextureSpecification final {
  LegacyWeaponCrosshairTextureRole role;
  std::string_view logicalPath;
};

constexpr std::array<TextureSpecification, legacyWeaponCrosshairTextureCount>
    textureSpecifications{{
        {
            .role = LegacyWeaponCrosshairTextureRole::machineGun,
            .logicalPath = "Graphics\\Ingame\\HUD\\MGsight.gti",
        },
        {
            .role = LegacyWeaponCrosshairTextureRole::rocket,
            .logicalPath = "Graphics\\Ingame\\HUD\\ROsight.gti",
        },
        {
            .role = LegacyWeaponCrosshairTextureRole::bomb,
            .logicalPath = "Graphics\\Ingame\\HUD\\BOsight.gti",
        },
    }};

class SessionIdentityChanged final {};

[[nodiscard]] constexpr std::optional<std::size_t>
roleIndex(const LegacyWeaponCrosshairTextureRole role) noexcept {
  const auto index = static_cast<std::size_t>(role);
  return index < legacyWeaponCrosshairTextureCount
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

[[nodiscard]] LegacyWeaponCrosshairTextureLoadIssue
makeIssue(const LegacyWeaponCrosshairTextureLoadIssueKind kind) noexcept {
  return {
      .kind = kind,
      .role = std::nullopt,
      .sourceFileIndex = std::nullopt,
      .preparationIssue = std::nullopt,
  };
}

void addIssue(LegacyWeaponCrosshairTextureLoadResult &result,
              LegacyWeaponCrosshairTextureLoadIssue issue) {
  result.textures.reset();
  result.issues.push_back(std::move(issue));
}

void addIssue(LegacyWeaponCrosshairTextureLoadResult &result,
              const LegacyWeaponCrosshairTextureLoadIssueKind kind) {
  addIssue(result, makeIssue(kind));
}

void addPreparationIssue(
    LegacyWeaponCrosshairTextureLoadResult &result,
    const TextureSpecification &specification,
    const std::size_t sourceFileIndex,
    const render::GtiUploadDataIssueKind preparationIssue) {
  auto issue = makeIssue(
      LegacyWeaponCrosshairTextureLoadIssueKind::texturePreparationFailure);
  issue.role = specification.role;
  issue.sourceFileIndex = sourceFileIndex;
  issue.preparationIssue = preparationIssue;
  addIssue(result, std::move(issue));
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
report(LegacyWeaponCrosshairTextureLoadResult &result,
       const VerifiedContentSession &session,
       const VerifiedContentTransactionIdentity &expectedIdentity,
       const ContentRevision &expectedRevision, const std::stop_token stopToken,
       const LegacyWeaponCrosshairTextureLoadProgressCallback &callback,
       const LegacyWeaponCrosshairTextureLoadPhase phase,
       const std::size_t completedItems, const std::size_t totalItems) {
  requireExpectedSession(session, expectedIdentity, expectedRevision);
  if (stopToken.stop_requested()) {
    addIssue(result, LegacyWeaponCrosshairTextureLoadIssueKind::cancelled);
    return false;
  }
  if (completedItems > totalItems) {
    addIssue(result,
             LegacyWeaponCrosshairTextureLoadIssueKind::internalFailure);
    return false;
  }
  if (callback) {
    try {
      callback({
          .phase = phase,
          .completedItems = completedItems,
          .totalItems = totalItems,
      });
    } catch (const std::bad_alloc &) {
      throw;
    } catch (...) {
      requireExpectedSession(session, expectedIdentity, expectedRevision);
      addIssue(
          result,
          LegacyWeaponCrosshairTextureLoadIssueKind::progressCallbackFailure);
      return false;
    }
  }
  requireExpectedSession(session, expectedIdentity, expectedRevision);
  if (stopToken.stop_requested()) {
    addIssue(result, LegacyWeaponCrosshairTextureLoadIssueKind::cancelled);
    return false;
  }
  return true;
}

[[nodiscard]] bool
validLimits(const LegacyWeaponCrosshairTextureLoadLimits &limits) noexcept {
  const auto &upload = limits.gti.upload;
  return limits.maximumSourceBytes >= 8U &&
         limits.maximumSingleSourceFootprintBytes != 0U &&
         limits.maximumTotalSourceFootprintBytes != 0U &&
         limits.maximumDecodedRgbaBytes != 0U &&
         limits.maximumUploadRgbaBytes != 0U &&
         limits.maximumResidentRgbaBytes != 0U &&
         limits.gti.maximumSourceBytes >= 8U && upload.maximumVariants != 0U &&
         upload.maximumDimension >= legacyWeaponCrosshairTextureWidth &&
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

[[nodiscard]] render::GtiUploadDataIssueKind
mapPlanIssue(const render::GtiUploadIssueKind kind) noexcept {
  switch (kind) {
  case render::GtiUploadIssueKind::limitExceeded:
    return render::GtiUploadDataIssueKind::limitExceeded;
  case render::GtiUploadIssueKind::integerOverflow:
    return render::GtiUploadDataIssueKind::integerOverflow;
  default:
    return render::GtiUploadDataIssueKind::planFailure;
  }
}

[[nodiscard]] bool sameUploadPlan(const render::GtiUploadPlan &left,
                                  const render::GtiUploadPlan &right) noexcept {
  return left.request == right.request &&
         left.variantIndex == right.variantIndex &&
         left.format == right.format && left.checksum == right.checksum &&
         left.mipPolicy == right.mipPolicy &&
         left.uploadLevels == right.uploadLevels &&
         left.allocatedMipCount == right.allocatedMipCount &&
         left.uploadedMipCount == right.uploadedMipCount &&
         left.decodedRgbaBytes == right.decodedRgbaBytes &&
         left.uploadRgbaBytes == right.uploadRgbaBytes &&
         left.residentRgbaBytes == right.residentRgbaBytes;
}

[[nodiscard]] bool
validTexture(const LoadedLegacyWeaponCrosshairTexture &texture,
             const TextureSpecification &specification) noexcept {
  const auto expectedIndex = roleIndex(specification.role);
  if (!expectedIndex.has_value() || texture.role != specification.role ||
      texture.textureId.value != *expectedIndex ||
      texture.sourceFootprintBytes == 0U ||
      texture.logicalPath != specification.logicalPath ||
      texture.upload.request.assetId != texture.textureId ||
      texture.upload.request.archiveFileIndex != texture.sourceFileIndex ||
      texture.upload.format != legacyWeaponCrosshairTextureFormat ||
      texture.upload.uploadedMipCount == 0U ||
      texture.upload.allocatedMipCount < texture.upload.uploadedMipCount ||
      texture.upload.uploadLevels.size() != texture.upload.uploadedMipCount ||
      texture.uploadLevels.size() != texture.upload.uploadedMipCount ||
      texture.upload.uploadLevels.empty() ||
      texture.upload.uploadLevels.front().width !=
          legacyWeaponCrosshairTextureWidth ||
      texture.upload.uploadLevels.front().height !=
          legacyWeaponCrosshairTextureHeight) {
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

} // namespace

bool LoadedLegacyWeaponCrosshairTextureSet::valid() const noexcept {
  if (!transactionIdentity.valid() || sourceFootprintBytes == 0U ||
      decodedRgbaBytes == 0U || uploadRgbaBytes == 0U ||
      residentRgbaBytes == 0U) {
    return false;
  }

  std::uint64_t accountedSource = 0U;
  std::uint64_t accountedDecoded = 0U;
  std::uint64_t accountedUpload = 0U;
  std::uint64_t accountedResident = 0U;
  for (std::size_t index = 0U; index < textures.size(); ++index) {
    const auto &texture = textures[index];
    if (!validTexture(texture, textureSpecifications[index])) {
      return false;
    }
    for (std::size_t previous = 0U; previous < index; ++previous) {
      if (textures[previous].sourceFileIndex == texture.sourceFileIndex) {
        return false;
      }
    }
    if (!checkedAdd(accountedSource, texture.sourceFootprintBytes) ||
        !checkedAdd(accountedDecoded, texture.upload.decodedRgbaBytes) ||
        !checkedAdd(accountedUpload, texture.upload.uploadRgbaBytes) ||
        !checkedAdd(accountedResident, texture.upload.residentRgbaBytes)) {
      return false;
    }
  }
  return accountedSource == sourceFootprintBytes &&
         accountedDecoded == decodedRgbaBytes &&
         accountedUpload == uploadRgbaBytes &&
         accountedResident == residentRgbaBytes;
}

bool LoadedLegacyWeaponCrosshairTextureSet::belongsTo(
    const VerifiedContentSession &session) const noexcept {
  return valid() && transactionIdentity == session.transactionIdentity() &&
         revision == session.revision();
}

const LoadedLegacyWeaponCrosshairTexture *
LoadedLegacyWeaponCrosshairTextureSet::texture(
    const LegacyWeaponCrosshairTextureRole role) const noexcept {
  const auto index = roleIndex(role);
  if (!index.has_value() || !valid()) {
    return nullptr;
  }
  return &textures[*index];
}

LegacyWeaponCrosshairTextureLoadResult loadLegacyWeaponCrosshairTextures(
    VerifiedContentSession &session,
    const LegacyWeaponCrosshairTextureLoadLimits &externalLimits,
    const std::stop_token stopToken,
    LegacyWeaponCrosshairTextureLoadProgressCallback progress) {
  LegacyWeaponCrosshairTextureLoadResult result;
  try {
    const auto expectedIdentity = session.transactionIdentity();
    const ContentRevision expectedRevision = session.revision();
    if (!expectedIdentity.valid()) {
      addIssue(
          result,
          LegacyWeaponCrosshairTextureLoadIssueKind::sessionIdentityChanged);
      return result;
    }

    const LegacyWeaponCrosshairTextureLoadLimits limits = externalLimits;
    if (!validLimits(limits)) {
      addIssue(result,
               LegacyWeaponCrosshairTextureLoadIssueKind::invalidLimits);
      return result;
    }
    if (!report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress,
                LegacyWeaponCrosshairTextureLoadPhase::validatingInput, 0U,
                legacyWeaponCrosshairTextureCount)) {
      return result;
    }

    std::array<std::size_t, legacyWeaponCrosshairTextureCount> sourceIndices{};
    std::array<std::uint64_t, legacyWeaponCrosshairTextureCount>
        sourceFootprints{};
    std::unordered_set<std::size_t> uniqueSources;
    uniqueSources.reserve(legacyWeaponCrosshairTextureCount);
    std::uint64_t totalSourceFootprint = 0U;
    const auto &archive = session.sourceArchive();
    for (std::size_t index = 0U; index < textureSpecifications.size();
         ++index) {
      const auto &specification = textureSpecifications[index];
      const auto lookup = archive.lookup(specification.logicalPath);
      if (lookup.status != udsp::LookupStatus::unique) {
        auto issue = makeIssue(
            lookup.status == udsp::LookupStatus::ambiguous
                ? LegacyWeaponCrosshairTextureLoadIssueKind::textureAmbiguous
                : LegacyWeaponCrosshairTextureLoadIssueKind::textureNotFound);
        issue.role = specification.role;
        addIssue(result, std::move(issue));
        return result;
      }
      if (!uniqueSources.insert(lookup.fileIndex).second) {
        auto issue = makeIssue(
            LegacyWeaponCrosshairTextureLoadIssueKind::duplicateSource);
        issue.role = specification.role;
        issue.sourceFileIndex = lookup.fileIndex;
        addIssue(result, std::move(issue));
        return result;
      }

      sourceIndices[index] = lookup.fileIndex;
      const auto &entry = archive.files().at(lookup.fileIndex);
      if (entry.unpackedSize > limits.maximumSourceBytes ||
          entry.unpackedSize > limits.gti.maximumSourceBytes) {
        auto issue = makeIssue(
            LegacyWeaponCrosshairTextureLoadIssueKind::sourceLimitExceeded);
        issue.role = specification.role;
        issue.sourceFileIndex = lookup.fileIndex;
        addIssue(result, std::move(issue));
        return result;
      }
      sourceFootprints[index] = sourceFootprint(entry);
      if (sourceFootprints[index] > limits.maximumSingleSourceFootprintBytes) {
        auto issue = makeIssue(LegacyWeaponCrosshairTextureLoadIssueKind::
                                   sourceFootprintLimitExceeded);
        issue.role = specification.role;
        issue.sourceFileIndex = lookup.fileIndex;
        addIssue(result, std::move(issue));
        return result;
      }
      if (!checkedAdd(totalSourceFootprint, sourceFootprints[index])) {
        addIssue(result,
                 LegacyWeaponCrosshairTextureLoadIssueKind::integerOverflow);
        return result;
      }
      if (totalSourceFootprint > limits.maximumTotalSourceFootprintBytes) {
        addIssue(result, LegacyWeaponCrosshairTextureLoadIssueKind::
                             aggregateSourceFootprintLimitExceeded);
        return result;
      }
      if (!report(result, session, expectedIdentity, expectedRevision,
                  stopToken, progress,
                  LegacyWeaponCrosshairTextureLoadPhase::lookingUpTextures,
                  index + 1U, legacyWeaponCrosshairTextureCount)) {
        return result;
      }
    }

    LoadedLegacyWeaponCrosshairTextureSet candidate{
        .revision = expectedRevision,
        .transactionIdentity = expectedIdentity,
        .textures = {},
        .sourceFootprintBytes = totalSourceFootprint,
        .decodedRgbaBytes = 0U,
        .uploadRgbaBytes = 0U,
        .residentRgbaBytes = 0U,
    };
    for (std::size_t index = 0U; index < textureSpecifications.size();
         ++index) {
      const auto &specification = textureSpecifications[index];
      const std::size_t sourceFileIndex = sourceIndices[index];
      requireExpectedSession(session, expectedIdentity, expectedRevision);

      std::vector<std::uint8_t> bytes;
      try {
        bytes =
            session.readSourceFile(sourceFileIndex, limits.maximumSourceBytes);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        requireExpectedSession(session, expectedIdentity, expectedRevision);
        auto issue = makeIssue(
            LegacyWeaponCrosshairTextureLoadIssueKind::sourceReadFailure);
        issue.role = specification.role;
        issue.sourceFileIndex = sourceFileIndex;
        addIssue(result, std::move(issue));
        return result;
      }
      requireExpectedSession(session, expectedIdentity, expectedRevision);
      if (!report(result, session, expectedIdentity, expectedRevision,
                  stopToken, progress,
                  LegacyWeaponCrosshairTextureLoadPhase::readingTextures,
                  index + 1U, legacyWeaponCrosshairTextureCount)) {
        return result;
      }

      const render::TextureImportRequest request{
          .assetId = render::TextureAssetId{static_cast<std::uint32_t>(index)},
          .archiveFileIndex = sourceFileIndex,
      };

      auto perTextureLimits = limits.gti;
      perTextureLimits.maximumSourceBytes = std::min(
          perTextureLimits.maximumSourceBytes, limits.maximumSourceBytes);
      assets::GtiMetadata metadata;
      try {
        metadata = assets::parseGti(
            bytes,
            assets::GtiParseLimits{
                .maximumVariants = perTextureLimits.upload.maximumVariants,
                .maximumMetadataBytes =
                    perTextureLimits.upload.maximumMetadataBytes,
            });
      } catch (const std::bad_alloc &) {
        throw;
      } catch (const assets::GtiParseLimitError &) {
        addPreparationIssue(result, specification, sourceFileIndex,
                            render::GtiUploadDataIssueKind::limitExceeded);
        return result;
      } catch (const assets::ParseError &) {
        addPreparationIssue(result, specification, sourceFileIndex,
                            render::GtiUploadDataIssueKind::parseFailure);
        return result;
      } catch (...) {
        addPreparationIssue(result, specification, sourceFileIndex,
                            render::GtiUploadDataIssueKind::parseFailure);
        return result;
      }

      render::GtiUploadDescription description;
      try {
        description = render::describeGtiUpload(request, metadata,
                                                perTextureLimits.upload);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        addPreparationIssue(result, specification, sourceFileIndex,
                            render::GtiUploadDataIssueKind::planFailure);
        return result;
      }
      if (!description.issues.empty()) {
        for (const auto &upstream : description.issues) {
          addPreparationIssue(result, specification, sourceFileIndex,
                              mapPlanIssue(upstream.kind));
        }
        return result;
      }
      if (!description.plan.has_value()) {
        addPreparationIssue(result, specification, sourceFileIndex,
                            render::GtiUploadDataIssueKind::planFailure);
        return result;
      }

      const auto &plannedUpload = *description.plan;
      if (plannedUpload.format != legacyWeaponCrosshairTextureFormat) {
        auto issue = makeIssue(
            LegacyWeaponCrosshairTextureLoadIssueKind::unexpectedFormat);
        issue.role = specification.role;
        issue.sourceFileIndex = sourceFileIndex;
        addIssue(result, std::move(issue));
        return result;
      }
      if (plannedUpload.uploadLevels.empty() ||
          plannedUpload.uploadLevels.front().width !=
              legacyWeaponCrosshairTextureWidth ||
          plannedUpload.uploadLevels.front().height !=
              legacyWeaponCrosshairTextureHeight) {
        auto issue = makeIssue(
            LegacyWeaponCrosshairTextureLoadIssueKind::unexpectedDimensions);
        issue.role = specification.role;
        issue.sourceFileIndex = sourceFileIndex;
        addIssue(result, std::move(issue));
        return result;
      }

      auto prospectiveDecoded = candidate.decodedRgbaBytes;
      auto prospectiveUpload = candidate.uploadRgbaBytes;
      auto prospectiveResident = candidate.residentRgbaBytes;
      if (!checkedAdd(prospectiveDecoded, plannedUpload.decodedRgbaBytes) ||
          !checkedAdd(prospectiveUpload, plannedUpload.uploadRgbaBytes) ||
          !checkedAdd(prospectiveResident, plannedUpload.residentRgbaBytes)) {
        addIssue(result,
                 LegacyWeaponCrosshairTextureLoadIssueKind::integerOverflow);
        return result;
      }
      if (prospectiveDecoded > limits.maximumDecodedRgbaBytes) {
        addIssue(result, LegacyWeaponCrosshairTextureLoadIssueKind::
                             decodedRgbaLimitExceeded);
        return result;
      }
      if (prospectiveUpload > limits.maximumUploadRgbaBytes) {
        addIssue(
            result,
            LegacyWeaponCrosshairTextureLoadIssueKind::uploadRgbaLimitExceeded);
        return result;
      }
      if (prospectiveResident > limits.maximumResidentRgbaBytes) {
        addIssue(result, LegacyWeaponCrosshairTextureLoadIssueKind::
                             residentRgbaLimitExceeded);
        return result;
      }

      render::GtiUploadPreparation preparation;
      try {
        preparation =
            render::prepareGtiUpload(request, bytes, perTextureLimits);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        auto issue = makeIssue(LegacyWeaponCrosshairTextureLoadIssueKind::
                                   texturePreparationFailure);
        issue.role = specification.role;
        issue.sourceFileIndex = sourceFileIndex;
        addIssue(result, std::move(issue));
        return result;
      }
      std::vector<std::uint8_t>().swap(bytes);
      if (!preparation.success()) {
        if (preparation.issues.empty()) {
          auto issue = makeIssue(LegacyWeaponCrosshairTextureLoadIssueKind::
                                     texturePreparationFailure);
          issue.role = specification.role;
          issue.sourceFileIndex = sourceFileIndex;
          addIssue(result, std::move(issue));
        } else {
          for (const auto &upstream : preparation.issues) {
            auto issue = makeIssue(LegacyWeaponCrosshairTextureLoadIssueKind::
                                       texturePreparationFailure);
            issue.role = specification.role;
            issue.sourceFileIndex = sourceFileIndex;
            issue.preparationIssue = upstream.kind;
            addIssue(result, std::move(issue));
          }
        }
        return result;
      }
      if (!sameUploadPlan(plannedUpload, *preparation.plan)) {
        addPreparationIssue(result, specification, sourceFileIndex,
                            render::GtiUploadDataIssueKind::planMismatch);
        return result;
      }

      candidate.decodedRgbaBytes = prospectiveDecoded;
      candidate.uploadRgbaBytes = prospectiveUpload;
      candidate.residentRgbaBytes = prospectiveResident;

      candidate.textures[index] = {
          .role = specification.role,
          .textureId = request.assetId,
          .sourceFileIndex = sourceFileIndex,
          .sourceFootprintBytes = sourceFootprints[index],
          .logicalPath = std::string(specification.logicalPath),
          .upload = std::move(*preparation.plan),
          .uploadLevels = std::move(preparation.uploadLevels),
      };
      if (!report(result, session, expectedIdentity, expectedRevision,
                  stopToken, progress,
                  LegacyWeaponCrosshairTextureLoadPhase::preparingTextures,
                  index + 1U, legacyWeaponCrosshairTextureCount)) {
        return result;
      }
    }

    if (!report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress, LegacyWeaponCrosshairTextureLoadPhase::complete,
                legacyWeaponCrosshairTextureCount,
                legacyWeaponCrosshairTextureCount)) {
      return result;
    }
    requireExpectedSession(session, expectedIdentity, expectedRevision);
    if (!candidate.valid()) {
      addIssue(result,
               LegacyWeaponCrosshairTextureLoadIssueKind::internalFailure);
      return result;
    }
    result.textures = std::move(candidate);
    return result;
  } catch (const SessionIdentityChanged &) {
    result.textures.reset();
    result.issues.clear();
    addIssue(result,
             LegacyWeaponCrosshairTextureLoadIssueKind::sessionIdentityChanged);
    return result;
  } catch (const std::bad_alloc &) {
    result.textures.reset();
    result.issues.clear();
    addIssue(result,
             LegacyWeaponCrosshairTextureLoadIssueKind::allocationFailure);
    return result;
  } catch (...) {
    result.textures.reset();
    result.issues.clear();
    addIssue(result,
             LegacyWeaponCrosshairTextureLoadIssueKind::internalFailure);
    return result;
  }
}

} // namespace airfix::content
