#include "airfix/texture/TextureReplacementResolver.hpp"

#include <utility>

namespace airfix::texture {
namespace {

[[nodiscard]] TextureReplacementFallbackReason
mapFileFailure(const PrivateTextureFileStatus status) noexcept {
    switch (status) {
    case PrivateTextureFileStatus::invalidArgument:
        return TextureReplacementFallbackReason::invalidResolverConfiguration;
    case PrivateTextureFileStatus::invalidRelativePath:
        return TextureReplacementFallbackReason::unsafeRelativePath;
    case PrivateTextureFileStatus::staleGeneration:
        return TextureReplacementFallbackReason::staleGeneration;
    case PrivateTextureFileStatus::notFound:
        return TextureReplacementFallbackReason::fileNotFound;
    case PrivateTextureFileStatus::unsafeIndirection:
        return TextureReplacementFallbackReason::unsafeIndirection;
    case PrivateTextureFileStatus::unsafeType:
        return TextureReplacementFallbackReason::unsafeFileType;
    case PrivateTextureFileStatus::multipleLinks:
        return TextureReplacementFallbackReason::multipleLinks;
    case PrivateTextureFileStatus::sizeLimitExceeded:
        return TextureReplacementFallbackReason::encodedByteLimitExceeded;
    case PrivateTextureFileStatus::changedDuringRead:
        return TextureReplacementFallbackReason::changedDuringRead;
    case PrivateTextureFileStatus::ioFailure:
        return TextureReplacementFallbackReason::fileIoFailure;
    case PrivateTextureFileStatus::ready:
        break;
    }
    return TextureReplacementFallbackReason::resolverFailure;
}

[[nodiscard]] TextureReplacementResolution
fallback(const TextureReplacementFallbackReason reason,
         const crypto::Sha256Digest& sourceDigest,
         const bool sourceDigestComputed) noexcept {
    return {
        .fallbackReason = reason,
        .sourceDigestComputed = sourceDigestComputed,
        .sourceGtiSha256 = sourceDigest,
        .candidate = {},
    };
}

} // namespace

TextureReplacementResolver::TextureReplacementResolver(
    const TextureHdManifestIndex& index, const PrivateTextureFileStore& files,
    const std::uint64_t generation,
    const TextureReplacementResolverLimits limits) noexcept
    : index_(&index), files_(&files), generation_(generation), limits_(limits) {
}

TextureReplacementResolution TextureReplacementResolver::resolve(
    const std::string_view logicalPath,
    const std::span<const std::uint8_t> sourceGtiBytes,
    const TextureReplacementLookupPolicy policy) const noexcept {
    crypto::Sha256Digest sourceDigest{};
    if (index_ == nullptr || files_ == nullptr || generation_ == 0U ||
        limits_.maximumBasePngBytes == 0U) {
        return fallback(
            TextureReplacementFallbackReason::invalidResolverConfiguration,
            sourceDigest, false);
    }
    if (files_->generation() != generation_) {
        return fallback(TextureReplacementFallbackReason::staleGeneration,
                        sourceDigest, false);
    }
    if (!index_->acceptsLogicalPath(logicalPath)) {
        return fallback(TextureReplacementFallbackReason::invalidLogicalPath,
                        sourceDigest, false);
    }

    try {
        sourceDigest = crypto::sha256(sourceGtiBytes);
    } catch (...) {
        return fallback(TextureReplacementFallbackReason::resolverFailure,
                        sourceDigest, false);
    }
    const TextureHdManifestRecord* record{};
    auto matchKind = TextureReplacementMatchKind::logicalPath;
    try {
        record = index_->findByLogicalPath(logicalPath);
    } catch (...) {
        return fallback(TextureReplacementFallbackReason::resolverFailure,
                        sourceDigest, true);
    }

    if (record == nullptr || record->sourceGtiSha256 != sourceDigest) {
        const auto* const digestRecord =
            index_->findBySourceGtiSha256(sourceDigest);
        if (digestRecord == nullptr) {
            return fallback(
                record == nullptr
                    ? TextureReplacementFallbackReason::recordAbsent
                    : TextureReplacementFallbackReason::sourceDigestMismatch,
                sourceDigest, true);
        }
        if (!policy.allowSourceDigestAlternative) {
            return fallback(TextureReplacementFallbackReason::
                                sourceDigestAlternativeNotAllowed,
                            sourceDigest, true);
        }
        record = digestRecord;
        matchKind = TextureReplacementMatchKind::sourceDigestAlternative;
    }

    auto read = files_->readFile(record->baseTextureRelativePath,
                                 limits_.maximumBasePngBytes, generation_);
    if (!read.success()) {
        return fallback(mapFileFailure(read.status), sourceDigest, true);
    }
    crypto::Sha256Digest baseDigest{};
    try {
        baseDigest = crypto::sha256(read.bytes);
    } catch (...) {
        return fallback(TextureReplacementFallbackReason::resolverFailure,
                        sourceDigest, true);
    }
    if (baseDigest != record->outputPngSha256) {
        return fallback(TextureReplacementFallbackReason::baseChecksumMismatch,
                        sourceDigest, true);
    }

    TextureReplacementResolution resolution{
        .fallbackReason = TextureReplacementFallbackReason::none,
        .sourceDigestComputed = true,
        .sourceGtiSha256 = sourceDigest,
        .candidate =
            {
                .record = record,
                .matchKind = matchKind,
                .generation = generation_,
                .sourceGtiSha256 = sourceDigest,
                .basePngBytes = std::move(read.bytes),
            },
    };
    return resolution;
}

} // namespace airfix::texture
