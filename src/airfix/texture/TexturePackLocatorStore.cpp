#include "airfix/texture/TexturePackLocatorStore.hpp"

#include "airfix/io/DurableDocumentPair.hpp"

#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace airfix::texture {
namespace {

constexpr io::DurableDocumentPairNames documentNames{
    .current = "texture-pack.aftl",
    .backup = "texture-pack.aftl.backup",
    .currentPrepared = "texture-pack.aftl.partial",
    .backupPrepared = "texture-pack.aftl.backup.partial",
};

struct DocumentRead final {
    TexturePackLocatorFileDiagnostic diagnostic;
    std::optional<TexturePackLocatorRecord> locator;
};

[[nodiscard]] io::DurableDocumentDisposition inspectDocument(
    const std::span<const std::uint8_t> bytes, void*) noexcept {
    try {
        const auto decoded = decodeTexturePackLocator(bytes);
        if (std::holds_alternative<OpaqueFutureTexturePackLocatorRecord>(
                decoded)) {
            return io::DurableDocumentDisposition::futurePreserve;
        }
        return io::DurableDocumentDisposition::valid;
    } catch (...) {
        return io::DurableDocumentDisposition::replaceableInvalid;
    }
}

constexpr io::DurableDocumentInspection documentInspection{
    .maximumBytes = maximumTexturePackLocatorBytes,
    .inspect = inspectDocument,
};

[[nodiscard]] TexturePackLocatorFileStatus statusForRead(
    const io::DurableDocumentFileStatus status) noexcept {
    switch (status) {
    case io::DurableDocumentFileStatus::missing:
        return TexturePackLocatorFileStatus::missing;
    case io::DurableDocumentFileStatus::valid:
        return TexturePackLocatorFileStatus::valid;
    case io::DurableDocumentFileStatus::futurePreserve:
        return TexturePackLocatorFileStatus::futureSchema;
    case io::DurableDocumentFileStatus::replaceableInvalid:
        return TexturePackLocatorFileStatus::malformed;
    case io::DurableDocumentFileStatus::oversized:
        return TexturePackLocatorFileStatus::oversized;
    case io::DurableDocumentFileStatus::wrongTypeOrLinked:
        return TexturePackLocatorFileStatus::wrongTypeOrLinked;
    case io::DurableDocumentFileStatus::ioUnavailable:
        return TexturePackLocatorFileStatus::ioUnavailable;
    }
    return TexturePackLocatorFileStatus::ioUnavailable;
}

[[nodiscard]] DocumentRead readDocument(
    const std::filesystem::path& path) noexcept {
    DocumentRead result;
    try {
        auto read = io::readDurableDocument(path, documentInspection);
        result.diagnostic.status = statusForRead(read.status);
        if (read.exactBytes.empty()) {
            return result;
        }
        const auto decoded = decodeTexturePackLocator(read.exactBytes);
        if (const auto* future =
                std::get_if<OpaqueFutureTexturePackLocatorRecord>(&decoded)) {
            result.diagnostic = {
                .status = TexturePackLocatorFileStatus::futureSchema,
                .schemaVersion = future->schemaVersion,
            };
            return result;
        }
        result.diagnostic = {
            .status = TexturePackLocatorFileStatus::valid,
            .schemaVersion = texturePackLocatorSchemaVersion,
        };
        result.locator = std::get<TexturePackLocatorRecord>(decoded);
        return result;
    } catch (const TexturePackLocatorCodecError& error) {
        result.diagnostic = {
            .status = error.issue() == TexturePackLocatorCodecIssue::tooLarge
                          ? TexturePackLocatorFileStatus::oversized
                          : TexturePackLocatorFileStatus::malformed,
            .schemaVersion = error.schemaVersion() == 0U
                                 ? std::nullopt
                                 : std::optional{error.schemaVersion()},
        };
        return result;
    } catch (...) {
        result.diagnostic.status = TexturePackLocatorFileStatus::ioUnavailable;
        return result;
    }
}

[[nodiscard]] bool blocksPersistence(
    const TexturePackLocatorFileStatus status) noexcept {
    return status == TexturePackLocatorFileStatus::futureSchema ||
           status == TexturePackLocatorFileStatus::wrongTypeOrLinked ||
           status == TexturePackLocatorFileStatus::ioUnavailable;
}

[[noreturn]] void storeFailure(
    const TexturePackLocatorStoreErrorKind kind,
    std::optional<TexturePackLocatorRecord> requested,
    const char* const message) {
    throw TexturePackLocatorStoreError(kind, std::move(requested), message);
}

[[noreturn]] void mapPairFailure(
    const io::DurableDocumentPairError& error,
    const TexturePackLocatorRecord& candidate) {
    switch (error.kind()) {
    case io::DurableDocumentPairErrorKind::invalidDirectory:
        storeFailure(TexturePackLocatorStoreErrorKind::invalidDirectory,
                     std::nullopt,
                     "texture package locator directory is invalid");
    case io::DurableDocumentPairErrorKind::persistenceBlocked:
        storeFailure(TexturePackLocatorStoreErrorKind::persistenceBlocked,
                     candidate,
                     "texture package locator persistence is blocked");
    case io::DurableDocumentPairErrorKind::commitUnknown:
        storeFailure(TexturePackLocatorStoreErrorKind::commitUnknown,
                     candidate,
                     "texture package locator commit outcome is unknown");
    case io::DurableDocumentPairErrorKind::invalidArgument:
    case io::DurableDocumentPairErrorKind::saveFailed:
        storeFailure(TexturePackLocatorStoreErrorKind::saveFailed,
                     error.candidateRelevant()
                         ? std::optional<TexturePackLocatorRecord>{candidate}
                         : std::nullopt,
                     "texture package locator durable I/O failed");
    }
    storeFailure(TexturePackLocatorStoreErrorKind::saveFailed, candidate,
                 "texture package locator durable I/O failed");
}

} // namespace

TexturePackLocatorStoreError::TexturePackLocatorStoreError(
    const TexturePackLocatorStoreErrorKind kind,
    std::optional<TexturePackLocatorRecord> requestedLocator,
    const char* const message)
    : std::runtime_error(message), kind_(kind),
      requestedLocator_(std::move(requestedLocator)) {}

TexturePackLocatorLoadResult loadTexturePackLocator(
    const std::filesystem::path& directory) {
    TexturePackLocatorLoadResult result;
    if (directory.empty() || !directory.is_absolute()) {
        result.current.status = TexturePackLocatorFileStatus::ioUnavailable;
        result.persistenceBlocked = true;
        return result;
    }
    switch (io::inspectDurableDocumentDirectory(directory)) {
    case io::DurableDocumentDirectoryStatus::ready:
        break;
    case io::DurableDocumentDirectoryStatus::missing:
        result.current.status = TexturePackLocatorFileStatus::missing;
        result.backup.status = TexturePackLocatorFileStatus::missing;
        return result;
    case io::DurableDocumentDirectoryStatus::wrongTypeOrLinked:
        result.current.status =
            TexturePackLocatorFileStatus::wrongTypeOrLinked;
        result.persistenceBlocked = true;
        return result;
    case io::DurableDocumentDirectoryStatus::unavailable:
        result.current.status = TexturePackLocatorFileStatus::ioUnavailable;
        result.persistenceBlocked = true;
        return result;
    }

    const auto current = readDocument(directory / documentNames.current);
    result.current = current.diagnostic;
    if (current.locator.has_value()) {
        result.locator = current.locator;
        result.source = TexturePackLocatorLoadSource::current;
        return result;
    }
    if (blocksPersistence(current.diagnostic.status)) {
        result.persistenceBlocked = true;
        return result;
    }

    const auto backup = readDocument(directory / documentNames.backup);
    result.backup = backup.diagnostic;
    if (backup.locator.has_value()) {
        result.locator = backup.locator;
        result.source = TexturePackLocatorLoadSource::backup;
        return result;
    }
    result.persistenceBlocked = blocksPersistence(backup.diagnostic.status);
    return result;
}

TexturePackLocatorSaveResult saveTexturePackLocator(
    const std::filesystem::path& directory,
    const TexturePackLocatorRecord& candidate) {
    std::vector<std::uint8_t> bytes;
    try {
        bytes = encodeTexturePackLocator(candidate);
    } catch (...) {
        storeFailure(TexturePackLocatorStoreErrorKind::invalidLocator,
                     std::nullopt,
                     "texture package locator candidate is invalid");
    }

    try {
        const auto committed = io::commitDurableDocumentPair(
            directory, documentNames, documentInspection, bytes);
        switch (committed.status) {
        case io::DurableDocumentCommitStatus::unchanged:
            return {
                .status = TexturePackLocatorSaveStatus::unchanged,
                .backupRotated = committed.backupRotated,
            };
        case io::DurableDocumentCommitStatus::committed:
            return {
                .status = TexturePackLocatorSaveStatus::committed,
                .backupRotated = committed.backupRotated,
            };
        case io::DurableDocumentCommitStatus::committedAfterReadback:
            return {
                .status =
                    TexturePackLocatorSaveStatus::committedAfterReadback,
                .backupRotated = committed.backupRotated,
            };
        }
    } catch (const io::DurableDocumentPairError& error) {
        mapPairFailure(error, candidate);
    }
    storeFailure(TexturePackLocatorStoreErrorKind::saveFailed, candidate,
                 "texture package locator durable I/O failed");
}

} // namespace airfix::texture
