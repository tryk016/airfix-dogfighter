#include "airfix/package/AfPackValidation.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace airfix::afpack {
namespace {

[[noreturn]] void cancelled() {
    throw ValidationCancelled{};
}

void checkCancellation(const std::stop_token stopToken) {
    if (stopToken.stop_requested()) {
        cancelled();
    }
}

void validateLimits(const ValidationLimits& limits) {
    if (limits.maxManifestBytes == 0U) {
        throw std::invalid_argument("AFPACK manifest read limit must be nonzero");
    }
    if (limits.maxManifestBytes >
        static_cast<std::uint64_t>(limits.manifest.maxInputBytes)) {
        throw std::invalid_argument(
            "AFPACK manifest read limit must not exceed the manifest parser input limit");
    }
    if (limits.ioBufferBytes == 0U ||
        limits.ioBufferBytes > kMaximumValidationIoBufferBytes) {
        throw std::invalid_argument("AFPACK validation I/O buffer size is outside limits");
    }
}

void report(
    const ValidationProgressCallback& callback,
    const std::stop_token stopToken,
    const ValidationPhase phase,
    const std::uint64_t completedBytes,
    const std::uint64_t totalBytes,
    const std::size_t entriesCompleted,
    const std::size_t entryCount) {
    checkCancellation(stopToken);
    if (completedBytes > totalBytes || entriesCompleted > entryCount) {
        throw std::logic_error("AFPACK validation progress invariant failed");
    }
    if (callback) {
        callback({
            .phase = phase,
            .completedBytes = completedBytes,
            .totalBytes = totalBytes,
            .entriesCompleted = entriesCompleted,
            .entryCount = entryCount,
        });
    }
    checkCancellation(stopToken);
}

[[nodiscard]] std::uint64_t physicalSize(
    std::istream& input,
    const std::filesystem::path& path) {
    input.clear();
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw ParseError("cannot determine staged AFPACK size: " + path.string());
    }
    return static_cast<std::uint64_t>(end);
}

void requireEntryRange(
    const Entry& entry,
    const std::uint64_t archiveSize) {
    if (entry.dataOffset > archiveSize ||
        entry.storedSize > archiveSize - entry.dataOffset) {
        throw ParseError("AFPACK validation entry range is outside the pack: " + entry.path);
    }
    if (entry.dataOffset > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        throw ParseError("AFPACK validation entry offset exceeds stream limits: " + entry.path);
    }
}

[[nodiscard]] std::uint64_t totalPayloadBytes(const std::span<const Entry> entries) {
    std::uint64_t total = 0U;
    for (const auto& entry : entries) {
        if (entry.storedSize > std::numeric_limits<std::uint64_t>::max() - total) {
            throw ParseError("AFPACK validation payload byte count overflows");
        }
        total += entry.storedSize;
    }
    return total;
}

[[nodiscard]] crypto::Sha256Digest hashPrefix(
    std::istream& input,
    const std::uint64_t byteCount,
    const ValidationLimits& limits,
    const std::stop_token stopToken,
    const std::filesystem::path& path) {
    if (byteCount > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        throw ParseError("AFPACK metadata range exceeds stream limits: " + path.string());
    }
    std::vector<std::uint8_t> buffer(limits.ioBufferBytes);
    crypto::Sha256 hash;
    auto remaining = byteCount;
    input.clear();
    input.seekg(0, std::ios::beg);
    if (!input) {
        throw ParseError("cannot seek staged AFPACK metadata: " + path.string());
    }
    while (remaining != 0U) {
        checkCancellation(stopToken);
        const auto chunkSize = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        if (chunkSize > static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max())) {
            throw ParseError("AFPACK metadata I/O chunk exceeds stream limits: " +
                path.string());
        }
        if (!input.read(
                reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(chunkSize))) {
            throw ParseError("cannot read staged AFPACK metadata: " + path.string());
        }
        hash.update(std::span<const std::uint8_t>(buffer).first(chunkSize));
        remaining -= chunkSize;
    }
    checkCancellation(stopToken);
    return hash.finish();
}

void hashPayloads(
    std::istream& input,
    const std::span<const Entry> entries,
    const std::uint64_t archiveSize,
    const ValidationLimits& limits,
    const std::stop_token stopToken,
    const ValidationProgressCallback& progress,
    const std::uint64_t totalBytes,
    const ValidationPhase phase) {
    std::vector<std::uint8_t> buffer(limits.ioBufferBytes);
    std::uint64_t completedBytes = 0U;
    report(progress, stopToken, phase,
        completedBytes, totalBytes, 0U, entries.size());

    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        checkCancellation(stopToken);
        requireEntryRange(entry, archiveSize);

        input.clear();
        input.seekg(static_cast<std::streamoff>(entry.dataOffset), std::ios::beg);
        if (!input) {
            throw ParseError("cannot seek staged AFPACK entry: " + entry.path);
        }

        crypto::Sha256 hash;
        auto remaining = entry.storedSize;
        while (remaining != 0U) {
            checkCancellation(stopToken);
            const auto chunkSize = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            if (chunkSize > static_cast<std::size_t>(
                    std::numeric_limits<std::streamsize>::max())) {
                throw ParseError(
                    "AFPACK validation I/O chunk exceeds stream limits: " + entry.path);
            }
            if (!input.read(
                    reinterpret_cast<char*>(buffer.data()),
                    static_cast<std::streamsize>(chunkSize))) {
                throw ParseError("cannot read staged AFPACK entry: " + entry.path);
            }
            hash.update(std::span<const std::uint8_t>(buffer).first(chunkSize));
            remaining -= chunkSize;
            completedBytes += chunkSize;
            report(progress, stopToken, phase,
                completedBytes, totalBytes, index, entries.size());
        }
        if (hash.finish() != entry.sha256) {
            if (phase == ValidationPhase::confirmingPayloads) {
                throw ParseError("AFPACK payload changed during validation: " + entry.path);
            }
            throw ParseError("AFPACK SHA-256 mismatch: " + entry.path);
        }
        report(progress, stopToken, phase,
            completedBytes, totalBytes, index + 1U, entries.size());
    }
}

[[nodiscard]] std::size_t manifestIndex(const std::span<const Entry> entries) {
    std::size_t result = entries.size();
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        if (entries[index].kind == EntryKind::manifest &&
            entries[index].path == "manifest.json") {
            result = index;
            ++count;
        }
    }
    if (count != 1U) {
        throw ParseError("AFPACK validation requires exactly one manifest.json entry");
    }
    return result;
}

[[nodiscard]] const Entry& findContentEntry(
    const std::span<const Entry> entries,
    const ManifestEntry& manifestEntry) {
    const auto match = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& entry) {
            return entry.path == manifestEntry.path && entry.kind == manifestEntry.kind;
        });
    if (match == entries.end()) {
        throw ManifestError("manifest content entry is absent from the AFPACK table");
    }
    return *match;
}

} // namespace

ValidationCancelled::ValidationCancelled()
    : std::runtime_error("AFPACK validation cancelled") {}

ValidatedPack validatePack(
    const std::filesystem::path& path,
    const ValidationLimits& limits,
    const std::stop_token stopToken,
    ValidationProgressCallback progress) {
    validateLimits(limits);
    report(progress, stopToken, ValidationPhase::openingPack, 0U, 0U, 0U, 0U);

    // Open before parsing metadata and retain this exact handle through every
    // semantic read. A same-size pathname replacement can therefore never
    // splice bytes from two files into one parsed validation result.
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw ParseError("cannot open staged AFPACK for validation: " + path.string());
    }
    auto pack = Pack::open(input, path, limits.afpack);
    checkCancellation(stopToken);
    if (physicalSize(input, path) != pack.archiveSize()) {
        throw ParseError("staged AFPACK physical size changed before validation");
    }
    const auto metadataDigest = hashPrefix(
        input, pack.header().dataOffset, limits, stopToken, path);

    const auto entries = std::span<const Entry>(pack.entries());
    const auto totalBytes = totalPayloadBytes(entries);
    hashPayloads(input, entries, pack.archiveSize(), limits, stopToken,
        progress, totalBytes, ValidationPhase::hashingPayloads);

    report(progress, stopToken, ValidationPhase::parsingManifest,
        totalBytes, totalBytes, entries.size(), entries.size());
    const auto index = manifestIndex(entries);
    const auto manifestBytes = pack.readEntry(
        input, path, index, limits.maxManifestBytes);
    auto manifest = parseManifest(manifestBytes, entries, limits.manifest);

    report(progress, stopToken, ValidationPhase::validatingNestedArchives,
        totalBytes, totalBytes, entries.size(), entries.size());
    for (const auto& manifestEntry : manifest.entries) {
        checkCancellation(stopToken);
        const auto& entry = findContentEntry(entries, manifestEntry);
        (void)udsp::Archive::openRegion(
            input, pack.archiveSize(), path,
            entry.dataOffset, entry.storedSize, limits.udsp);
        checkCancellation(stopToken);
    }

    // Bind the completed result back to the staging pathname. This fresh handle
    // is used only to prove semantic byte-for-byte equivalence with the file
    // parsed above; it never contributes metadata or manifest objects.
    std::ifstream boundInput(path, std::ios::binary | std::ios::ate);
    if (!boundInput || physicalSize(boundInput, path) != pack.archiveSize()) {
        throw ParseError("staged AFPACK pathname changed during validation");
    }
    hashPayloads(boundInput, entries, pack.archiveSize(), limits, stopToken,
        progress, totalBytes, ValidationPhase::confirmingPayloads);
    if (hashPrefix(boundInput, pack.header().dataOffset, limits, stopToken, path) !=
        metadataDigest) {
        throw ParseError("AFPACK metadata changed during validation");
    }

    if (physicalSize(input, path) != pack.archiveSize()) {
        throw ParseError("staged AFPACK physical size changed during validation");
    }
    report(progress, stopToken, ValidationPhase::complete,
        totalBytes, totalBytes, entries.size(), entries.size());
    return {.pack = std::move(pack), .manifest = std::move(manifest)};
}

} // namespace airfix::afpack
