#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace airfix::content {
namespace {

constexpr std::string_view logicalPath = "Graphics\\Ingame\\HUD\\digits.gti";
constexpr std::size_t textureIndex = 0U;

class SessionIdentityChanged final {};

[[nodiscard]] constexpr std::optional<std::size_t>
roleIndex(const LegacyAircraftHudRollingDigitsTextureRole role) noexcept {
  const auto index = static_cast<std::size_t>(role);
  return index < legacyAircraftHudRollingDigitsTextureCount
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

[[nodiscard]] LegacyAircraftHudRollingDigitsTextureLoadIssue
makeIssue(const LegacyAircraftHudRollingDigitsTextureLoadIssueKind kind,
          const std::optional<std::size_t> sourceFileIndex = std::nullopt,
          const std::optional<render::GtiUploadDataIssueKind> preparationIssue =
              std::nullopt) noexcept {
  return {
      .kind = kind,
      .sourceFileIndex = sourceFileIndex,
      .preparationIssue = preparationIssue,
  };
}

void addIssue(LegacyAircraftHudRollingDigitsTextureLoadResult &result,
              const LegacyAircraftHudRollingDigitsTextureLoadIssueKind kind,
              const std::optional<std::size_t> sourceFileIndex = std::nullopt,
              const std::optional<render::GtiUploadDataIssueKind>
                  preparationIssue = std::nullopt) {
  result.textures.reset();
  result.issues.push_back(makeIssue(kind, sourceFileIndex, preparationIssue));
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

[[nodiscard]] bool report(
    LegacyAircraftHudRollingDigitsTextureLoadResult &result,
    const VerifiedContentSession &session,
    const VerifiedContentTransactionIdentity &expectedIdentity,
    const ContentRevision &expectedRevision, const std::stop_token stopToken,
    const LegacyAircraftHudRollingDigitsTextureLoadProgressCallback &callback,
    const LegacyAircraftHudRollingDigitsTextureLoadPhase phase,
    const std::size_t completedItems) {
  requireExpectedSession(session, expectedIdentity, expectedRevision);
  if (stopToken.stop_requested()) {
    addIssue(result,
             LegacyAircraftHudRollingDigitsTextureLoadIssueKind::cancelled);
    return false;
  }
  if (completedItems > legacyAircraftHudRollingDigitsTextureCount) {
    addIssue(
        result,
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::internalFailure);
    return false;
  }
  if (callback) {
    try {
      callback({
          .phase = phase,
          .completedItems = completedItems,
          .totalItems = legacyAircraftHudRollingDigitsTextureCount,
      });
    } catch (const std::bad_alloc &) {
      throw;
    } catch (...) {
      requireExpectedSession(session, expectedIdentity, expectedRevision);
      addIssue(result, LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                           progressCallbackFailure);
      return false;
    }
  }
  requireExpectedSession(session, expectedIdentity, expectedRevision);
  if (stopToken.stop_requested()) {
    addIssue(result,
             LegacyAircraftHudRollingDigitsTextureLoadIssueKind::cancelled);
    return false;
  }
  return true;
}

[[nodiscard]] bool validLimits(
    const LegacyAircraftHudRollingDigitsTextureLoadLimits &limits) noexcept {
  const auto &upload = limits.gti.upload;
  return limits.maximumSourceBytes >= 8U &&
         limits.maximumSourceFootprintBytes != 0U &&
         limits.maximumDecodedRgbaBytes != 0U &&
         limits.maximumUploadRgbaBytes != 0U &&
         limits.maximumResidentRgbaBytes != 0U &&
         limits.gti.maximumSourceBytes >= 8U && upload.maximumVariants != 0U &&
         upload.maximumDimension >=
             legacyAircraftHudRollingDigitsTextureHeight &&
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

[[nodiscard]] bool validTexture(
    const LoadedLegacyAircraftHudRollingDigitsTexture &texture) noexcept {
  if (texture.role != LegacyAircraftHudRollingDigitsTextureRole::digits ||
      texture.textureId.value != textureIndex ||
      texture.sourceFootprintBytes == 0U ||
      texture.logicalPath != logicalPath ||
      texture.upload.request.assetId != texture.textureId ||
      texture.upload.request.archiveFileIndex != texture.sourceFileIndex ||
      texture.upload.format != legacyAircraftHudRollingDigitsTextureFormat ||
      texture.upload.uploadedMipCount == 0U ||
      texture.upload.allocatedMipCount < texture.upload.uploadedMipCount ||
      texture.upload.uploadLevels.size() != texture.upload.uploadedMipCount ||
      texture.uploadLevels.size() != texture.upload.uploadedMipCount ||
      texture.upload.uploadLevels.empty() ||
      texture.upload.uploadLevels.front().width !=
          legacyAircraftHudRollingDigitsTextureWidth ||
      texture.upload.uploadLevels.front().height !=
          legacyAircraftHudRollingDigitsTextureHeight) {
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

bool LoadedLegacyAircraftHudRollingDigitsTextureSet::valid() const noexcept {
  const auto &candidate = textures.front();
  return transactionIdentity.valid() && sourceFootprintBytes != 0U &&
         decodedRgbaBytes != 0U && uploadRgbaBytes != 0U &&
         residentRgbaBytes != 0U && validTexture(candidate) &&
         sourceFootprintBytes == candidate.sourceFootprintBytes &&
         decodedRgbaBytes == candidate.upload.decodedRgbaBytes &&
         uploadRgbaBytes == candidate.upload.uploadRgbaBytes &&
         residentRgbaBytes == candidate.upload.residentRgbaBytes;
}

bool LoadedLegacyAircraftHudRollingDigitsTextureSet::belongsTo(
    const VerifiedContentSession &session) const noexcept {
  return valid() && transactionIdentity == session.transactionIdentity() &&
         revision == session.revision();
}

const LoadedLegacyAircraftHudRollingDigitsTexture *
LoadedLegacyAircraftHudRollingDigitsTextureSet::texture(
    const LegacyAircraftHudRollingDigitsTextureRole role) const noexcept {
  const auto index = roleIndex(role);
  if (!index.has_value() || !valid()) {
    return nullptr;
  }
  return &textures[*index];
}

LegacyAircraftHudRollingDigitsTextureLoadResult
loadLegacyAircraftHudRollingDigitsTexture(
    VerifiedContentSession &session,
    const LegacyAircraftHudRollingDigitsTextureLoadLimits &externalLimits,
    const std::stop_token stopToken,
    LegacyAircraftHudRollingDigitsTextureLoadProgressCallback progress) {
  LegacyAircraftHudRollingDigitsTextureLoadResult result;
  try {
    const auto expectedIdentity = session.transactionIdentity();
    const auto expectedRevision = session.revision();
    if (!expectedIdentity.valid()) {
      addIssue(result, LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                           sessionIdentityChanged);
      return result;
    }
    const auto limits = externalLimits;
    if (!validLimits(limits)) {
      addIssue(
          result,
          LegacyAircraftHudRollingDigitsTextureLoadIssueKind::invalidLimits);
      return result;
    }
    if (!report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress,
                LegacyAircraftHudRollingDigitsTextureLoadPhase::validatingInput,
                0U)) {
      return result;
    }

    const auto lookup = session.sourceArchive().lookup(logicalPath);
    if (lookup.status != udsp::LookupStatus::unique) {
      addIssue(result,
               lookup.status == udsp::LookupStatus::ambiguous
                   ? LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                         textureAmbiguous
                   : LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                         textureNotFound);
      return result;
    }
    const auto &entry = session.sourceArchive().files().at(lookup.fileIndex);
    if (entry.unpackedSize > limits.maximumSourceBytes ||
        entry.unpackedSize > limits.gti.maximumSourceBytes) {
      addIssue(result,
               LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                   sourceLimitExceeded,
               lookup.fileIndex);
      return result;
    }
    const auto footprint = sourceFootprint(entry);
    if (footprint == 0U || footprint > limits.maximumSourceFootprintBytes) {
      addIssue(result,
               LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                   sourceFootprintLimitExceeded,
               lookup.fileIndex);
      return result;
    }
    if (!report(
            result, session, expectedIdentity, expectedRevision, stopToken,
            progress,
            LegacyAircraftHudRollingDigitsTextureLoadPhase::lookingUpTexture,
            1U)) {
      return result;
    }

    std::vector<std::uint8_t> bytes;
    try {
      bytes =
          session.readSourceFile(lookup.fileIndex, limits.maximumSourceBytes);
    } catch (const std::bad_alloc &) {
      throw;
    } catch (...) {
      requireExpectedSession(session, expectedIdentity, expectedRevision);
      addIssue(
          result,
          LegacyAircraftHudRollingDigitsTextureLoadIssueKind::sourceReadFailure,
          lookup.fileIndex);
      return result;
    }
    if (!report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress,
                LegacyAircraftHudRollingDigitsTextureLoadPhase::readingTexture,
                1U)) {
      return result;
    }

    auto gtiLimits = limits.gti;
    gtiLimits.maximumSourceBytes =
        std::min(gtiLimits.maximumSourceBytes, limits.maximumSourceBytes);
    gtiLimits.upload.maximumDecodedRgbaBytes =
        std::min(gtiLimits.upload.maximumDecodedRgbaBytes,
                 limits.maximumDecodedRgbaBytes);
    gtiLimits.upload.maximumUploadRgbaBytes = std::min(
        gtiLimits.upload.maximumUploadRgbaBytes, limits.maximumUploadRgbaBytes);
    gtiLimits.upload.maximumResidentRgbaBytes =
        std::min(gtiLimits.upload.maximumResidentRgbaBytes,
                 limits.maximumResidentRgbaBytes);
    const render::TextureImportRequest request{
        .assetId = render::TextureAssetId{0U},
        .archiveFileIndex = lookup.fileIndex,
    };
    auto preparation = render::prepareGtiUpload(request, bytes, gtiLimits);
    std::vector<std::uint8_t>().swap(bytes);
    if (!preparation.success()) {
      if (preparation.issues.empty()) {
        addIssue(result,
                 LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                     texturePreparationFailure,
                 lookup.fileIndex);
      } else {
        for (const auto &upstream : preparation.issues) {
          addIssue(result,
                   LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                       texturePreparationFailure,
                   lookup.fileIndex, upstream.kind);
        }
      }
      return result;
    }

    const auto &upload = *preparation.plan;
    if (upload.format != legacyAircraftHudRollingDigitsTextureFormat) {
      addIssue(
          result,
          LegacyAircraftHudRollingDigitsTextureLoadIssueKind::unexpectedFormat,
          lookup.fileIndex);
      return result;
    }
    if (upload.uploadLevels.empty() ||
        upload.uploadLevels.front().width !=
            legacyAircraftHudRollingDigitsTextureWidth ||
        upload.uploadLevels.front().height !=
            legacyAircraftHudRollingDigitsTextureHeight) {
      addIssue(result,
               LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                   unexpectedDimensions,
               lookup.fileIndex);
      return result;
    }
    if (upload.decodedRgbaBytes > limits.maximumDecodedRgbaBytes) {
      addIssue(result,
               LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                   decodedRgbaLimitExceeded,
               lookup.fileIndex);
      return result;
    }
    if (upload.uploadRgbaBytes > limits.maximumUploadRgbaBytes) {
      addIssue(result,
               LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                   uploadRgbaLimitExceeded,
               lookup.fileIndex);
      return result;
    }
    if (upload.residentRgbaBytes > limits.maximumResidentRgbaBytes) {
      addIssue(result,
               LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                   residentRgbaLimitExceeded,
               lookup.fileIndex);
      return result;
    }
    const auto decodedRgbaBytes = upload.decodedRgbaBytes;
    const auto uploadRgbaBytes = upload.uploadRgbaBytes;
    const auto residentRgbaBytes = upload.residentRgbaBytes;

    LoadedLegacyAircraftHudRollingDigitsTextureSet candidate{
        .revision = expectedRevision,
        .transactionIdentity = expectedIdentity,
        .textures = {{{
            .role = LegacyAircraftHudRollingDigitsTextureRole::digits,
            .textureId = request.assetId,
            .sourceFileIndex = lookup.fileIndex,
            .sourceFootprintBytes = footprint,
            .logicalPath = std::string(logicalPath),
            .upload = std::move(*preparation.plan),
            .uploadLevels = std::move(preparation.uploadLevels),
        }}},
        .sourceFootprintBytes = footprint,
        .decodedRgbaBytes = decodedRgbaBytes,
        .uploadRgbaBytes = uploadRgbaBytes,
        .residentRgbaBytes = residentRgbaBytes,
    };
    if (!report(
            result, session, expectedIdentity, expectedRevision, stopToken,
            progress,
            LegacyAircraftHudRollingDigitsTextureLoadPhase::preparingTexture,
            1U) ||
        !report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress,
                LegacyAircraftHudRollingDigitsTextureLoadPhase::complete, 1U)) {
      return result;
    }
    requireExpectedSession(session, expectedIdentity, expectedRevision);
    if (!candidate.valid()) {
      addIssue(
          result,
          LegacyAircraftHudRollingDigitsTextureLoadIssueKind::internalFailure);
      return result;
    }
    result.textures = std::move(candidate);
    return result;
  } catch (const SessionIdentityChanged &) {
    result.textures.reset();
    result.issues.clear();
    addIssue(result, LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
                         sessionIdentityChanged);
    return result;
  } catch (const std::bad_alloc &) {
    result.textures.reset();
    result.issues.clear();
    addIssue(
        result,
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::allocationFailure);
    return result;
  } catch (...) {
    result.textures.reset();
    result.issues.clear();
    addIssue(
        result,
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::internalFailure);
    return result;
  }
}

} // namespace airfix::content
