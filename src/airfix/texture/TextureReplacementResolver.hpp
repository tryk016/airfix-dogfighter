#pragma once

#include "airfix/crypto/Sha256.hpp"
#include "airfix/texture/PrivateTextureFileStore.hpp"
#include "airfix/texture/TextureHdManifestIndex.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace airfix::texture {

enum class TextureReplacementMatchKind : std::uint8_t {
    logicalPath,
    sourceDigestAlternative,
};

// Every failure is intentionally path-free and checksum-free. Product UI and
// logs may aggregate these values but must not attach source-derived text.
enum class TextureReplacementFallbackReason : std::uint8_t {
    none,
    notConfigured,
    invalidResolverConfiguration,
    invalidLogicalPath,
    recordAbsent,
    sourceDigestMismatch,
    sourceDigestAlternativeNotAllowed,
    staleGeneration,
    unsafeRelativePath,
    fileNotFound,
    unsafeIndirection,
    unsafeFileType,
    multipleLinks,
    encodedByteLimitExceeded,
    changedDuringRead,
    fileIoFailure,
    baseChecksumMismatch,
    resolverFailure,
};

struct TextureReplacementLookupPolicy final {
    // Digest-only matching is disabled unless a future caller has independently
    // proved compatible texture-use classification. Merely sharing bytes is not
    // enough to infer colour or technical-channel semantics.
    bool allowSourceDigestAlternative{};
};

struct TextureReplacementResolverLimits final {
    std::size_t maximumBasePngBytes{std::size_t{512U} * 1024U * 1024U};
};

struct TextureReplacementCandidate final {
    // The record remains owned by the immutable index supplied to the resolver.
    // Its private strings are internal content identities, never diagnostics.
    const TextureHdManifestRecord* record{};
    TextureReplacementMatchKind matchKind{
        TextureReplacementMatchKind::logicalPath};
    std::uint64_t generation{};
    crypto::Sha256Digest sourceGtiSha256{};
    std::vector<std::uint8_t> basePngBytes;
};

struct TextureReplacementResolution final {
    TextureReplacementFallbackReason fallbackReason{
        TextureReplacementFallbackReason::notConfigured};
    bool sourceDigestComputed{};
    crypto::Sha256Digest sourceGtiSha256{};
    TextureReplacementCandidate candidate;

    [[nodiscard]] bool replacementReady() const noexcept {
        return candidate.record != nullptr;
    }
};

// Immutable, non-owning resolver over one accepted-only manifest index and one
// root-confined file capability. Both owners must outlive this object and all
// returned candidates. The common non-zero generation must identify the same
// validated private package snapshot.
class TextureReplacementResolver final {
  public:
    TextureReplacementResolver(
        const TextureHdManifestIndex& index,
        const PrivateTextureFileStore& files, std::uint64_t generation,
        TextureReplacementResolverLimits limits = {}) noexcept;

    [[nodiscard]] TextureReplacementResolution
    resolve(std::string_view logicalPath,
            std::span<const std::uint8_t> sourceGtiBytes,
            TextureReplacementLookupPolicy policy = {}) const noexcept;

    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }

  private:
    const TextureHdManifestIndex* index_{};
    const PrivateTextureFileStore* files_{};
    std::uint64_t generation_{};
    TextureReplacementResolverLimits limits_{};
};

} // namespace airfix::texture
