#include "airfix/content/VerifiedContentSession.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace airfix::content {
namespace {

inline constexpr std::string_view kManifestPath = "manifest.json";
inline constexpr std::string_view kSourceArchivePath = "source/Resource.up";

[[noreturn]] void cancelled() {
    throw VerifiedContentCancelled{};
}

void checkCancellation(const std::stop_token stopToken) {
    if (stopToken.stop_requested()) {
        cancelled();
    }
}

void validateLimits(const VerifiedContentLimits& limits) {
    if (limits.maxPackBytes == 0U) {
        throw std::invalid_argument(
            "verified content pack size limit must be nonzero");
    }
    if (limits.maxManifestBytes == 0U ||
        limits.maxManifestBytes >
            static_cast<std::uint64_t>(limits.manifest.maxInputBytes)) {
        throw std::invalid_argument(
            "verified content manifest read limit is outside parser limits");
    }
    if (limits.ioBufferBytes == 0U ||
        limits.ioBufferBytes > kMaximumVerifiedContentIoBufferBytes) {
        throw std::invalid_argument(
            "verified content I/O buffer size is outside limits");
    }
}

void report(
    const VerifiedContentProgressCallback& callback,
    const std::stop_token stopToken,
    const VerifiedContentPhase phase,
    const std::uint64_t completedBytes,
    const std::uint64_t totalBytes) {
    checkCancellation(stopToken);
    if (completedBytes > totalBytes) {
        throw std::logic_error("verified content progress invariant failed");
    }
    if (callback) {
        callback({
            .phase = phase,
            .completedBytes = completedBytes,
            .totalBytes = totalBytes,
        });
    }
    checkCancellation(stopToken);
}

[[nodiscard]] std::uint64_t physicalSize(
    std::istream& input,
    const std::string_view sourceLabel) {
    input.clear();
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw VerifiedContentError(
            "cannot determine verified content size: " + std::string(sourceLabel));
    }
    return static_cast<std::uint64_t>(end);
}

[[nodiscard]] crypto::Sha256Digest hashStream(
    std::istream& input,
    const std::uint64_t size,
    const std::size_t ioBufferBytes,
    const std::string_view sourceLabel,
    const std::stop_token stopToken,
    const VerifiedContentProgressCallback& progress) {
    if (size > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        throw VerifiedContentError(
            "verified content size exceeds stream limits: " +
            std::string(sourceLabel));
    }

    std::vector<std::uint8_t> buffer(ioBufferBytes);
    crypto::Sha256 hash;
    std::uint64_t completedBytes = 0U;

    input.clear();
    input.seekg(0, std::ios::beg);
    if (!input) {
        throw VerifiedContentError(
            "cannot seek verified content stream: " + std::string(sourceLabel));
    }

    report(progress, stopToken, VerifiedContentPhase::hashingPack,
        completedBytes, size);
    while (completedBytes != size) {
        checkCancellation(stopToken);
        const auto chunkSize = static_cast<std::size_t>(std::min<std::uint64_t>(
            size - completedBytes, buffer.size()));
        if (chunkSize > static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max())) {
            throw VerifiedContentError(
                "verified content I/O chunk exceeds stream limits: " +
                std::string(sourceLabel));
        }
        if (!input.read(
                reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(chunkSize))) {
            throw VerifiedContentError(
                "verified content shrank while hashing: " +
                std::string(sourceLabel));
        }
        hash.update(std::span<const std::uint8_t>(buffer).first(chunkSize));
        completedBytes += chunkSize;
        report(progress, stopToken, VerifiedContentPhase::hashingPack,
            completedBytes, size);
    }
    checkCancellation(stopToken);
    return hash.finish();
}

[[nodiscard]] std::size_t requireEntry(
    const std::span<const afpack::Entry> entries,
    const std::string_view path,
    const afpack::EntryKind kind,
    const std::string_view description) {
    std::size_t result = entries.size();
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        if (entries[index].path == path && entries[index].kind == kind) {
            result = index;
            ++count;
        }
    }
    if (count != 1U) {
        throw VerifiedContentError(
            "verified content requires exactly one " + std::string(description));
    }
    return result;
}

} // namespace

VerifiedContentCancelled::VerifiedContentCancelled()
    : std::runtime_error("verified content session opening cancelled") {}

VerifiedContentSession::VerifiedContentSession(
    std::unique_ptr<std::ifstream> input,
    std::string sourceLabel,
    ContentRevision revision,
    afpack::Pack pack,
    afpack::Manifest manifest,
    udsp::Archive sourceArchive,
    const std::size_t sourcePackEntryIndex) noexcept
    : input_(std::move(input)),
      sourceLabel_(std::move(sourceLabel)),
      revision_(std::move(revision)),
      pack_(std::move(pack)),
      manifest_(std::move(manifest)),
      sourceArchive_(std::move(sourceArchive)),
      sourcePackEntryIndex_(sourcePackEntryIndex) {}

VerifiedContentSession VerifiedContentSession::open(
    std::unique_ptr<std::ifstream> input,
    std::string sourceLabel,
    ContentRevision trustedRevision,
    const VerifiedContentLimits& limits,
    const std::stop_token stopToken,
    VerifiedContentProgressCallback progress) {
    validateLimits(limits);
    checkCancellation(stopToken);
    if (!input) {
        throw VerifiedContentError("verified content stream is null");
    }
    if (sourceLabel.empty()) {
        sourceLabel = "content stream";
    }
    if (trustedRevision.generation == 0U) {
        throw VerifiedContentError(
            "trusted content revision generation must be positive");
    }
    if (trustedRevision.pack.size > limits.maxPackBytes ||
        trustedRevision.pack.size > limits.afpack.maxArchiveSize) {
        throw VerifiedContentError(
            "trusted content revision exceeds configured pack size limit");
    }

    report(progress, stopToken, VerifiedContentPhase::inspectingStream,
        0U, trustedRevision.pack.size);
    const auto sizeBeforeHash = physicalSize(*input, sourceLabel);
    checkCancellation(stopToken);
    if (sizeBeforeHash != trustedRevision.pack.size) {
        throw VerifiedContentError(
            "verified content size does not match trusted revision: " +
            sourceLabel);
    }
    report(progress, stopToken, VerifiedContentPhase::inspectingStream,
        sizeBeforeHash, trustedRevision.pack.size);

    const auto digest = hashStream(
        *input,
        sizeBeforeHash,
        limits.ioBufferBytes,
        sourceLabel,
        stopToken,
        progress);
    const auto sizeAfterHash = physicalSize(*input, sourceLabel);
    checkCancellation(stopToken);
    if (sizeAfterHash != sizeBeforeHash) {
        throw VerifiedContentError(
            "verified content size changed while hashing: " + sourceLabel);
    }
    if (digest != trustedRevision.pack.sha256) {
        throw VerifiedContentError(
            "verified content SHA-256 does not match trusted revision: " +
            sourceLabel);
    }

    report(progress, stopToken, VerifiedContentPhase::parsingPack,
        sizeBeforeHash, sizeBeforeHash);
    const auto diagnosticPath = std::filesystem::path(sourceLabel);
    auto pack = afpack::Pack::open(*input, diagnosticPath, limits.afpack);
    checkCancellation(stopToken);
    const auto entries = std::span<const afpack::Entry>(pack.entries());
    const auto manifestIndex = requireEntry(
        entries,
        kManifestPath,
        afpack::EntryKind::manifest,
        "manifest.json manifest entry");

    report(progress, stopToken, VerifiedContentPhase::parsingManifest,
        sizeBeforeHash, sizeBeforeHash);
    const auto manifestBytes = pack.readEntry(
        *input, diagnosticPath, manifestIndex, limits.maxManifestBytes);
    auto manifest = afpack::parseManifest(
        manifestBytes, entries, limits.manifest);
    checkCancellation(stopToken);

    const auto sourceIndex = requireEntry(
        entries,
        kSourceArchivePath,
        afpack::EntryKind::sourceArchive,
        "source/Resource.up source-archive entry");
    const auto& sourceEntry = entries[sourceIndex];

    report(progress, stopToken, VerifiedContentPhase::openingSourceArchive,
        sizeBeforeHash, sizeBeforeHash);
    auto sourceArchive = udsp::Archive::openRegion(
        *input,
        pack.archiveSize(),
        std::string_view(sourceLabel),
        sourceEntry.dataOffset,
        sourceEntry.storedSize,
        limits.udsp);
    checkCancellation(stopToken);

    const auto finalSize = physicalSize(*input, sourceLabel);
    checkCancellation(stopToken);
    if (finalSize != sizeBeforeHash) {
        throw VerifiedContentError(
            "verified content size changed while opening the session: " +
            sourceLabel);
    }
    report(progress, stopToken, VerifiedContentPhase::complete,
        sizeBeforeHash, sizeBeforeHash);

    return VerifiedContentSession(
        std::move(input),
        std::move(sourceLabel),
        std::move(trustedRevision),
        std::move(pack),
        std::move(manifest),
        std::move(sourceArchive),
        sourceIndex);
}

std::vector<std::uint8_t> VerifiedContentSession::readSourceFilePrefix(
    const std::size_t fileIndex,
    const std::size_t maximumBytes) {
    return udsp::readFilePrefix(
        *input_,
        revision_.pack.size,
        std::string_view(sourceLabel_),
        sourceArchive_,
        fileIndex,
        maximumBytes);
}

std::vector<std::uint8_t> VerifiedContentSession::readSourceFile(
    const std::size_t fileIndex,
    const std::size_t outputLimit) {
    return udsp::readFile(
        *input_,
        revision_.pack.size,
        std::string_view(sourceLabel_),
        sourceArchive_,
        fileIndex,
        outputLimit);
}

} // namespace airfix::content
