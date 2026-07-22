#include "airfix/package/AfPackInstaller.hpp"

#include "airfix/io/DurableFile.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace airfix::afpack {
namespace {

static_assert(std::is_nothrow_move_constructible_v<InstallResult>);

[[noreturn]] void cancelled() {
    throw InstallCancelled{};
}

void checkCancellation(const std::stop_token stopToken) {
    if (stopToken.stop_requested()) {
        cancelled();
    }
}

void validateArguments(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& contentRoot,
    const std::string_view transactionId,
    const InstallLimits& limits) {
    if (sourcePath.empty() || sourcePath.extension() != ".afpack") {
        throw std::invalid_argument("installer source must be a nonempty .afpack path");
    }
    if (contentRoot.empty()) {
        throw std::invalid_argument("installer content root must not be empty");
    }
    if (transactionId.size() != 36U) {
        throw std::invalid_argument("installer transaction id is not a canonical UUID");
    }
    for (std::size_t index = 0U; index < transactionId.size(); ++index) {
        const bool hyphen = index == 8U || index == 13U || index == 18U || index == 23U;
        const char character = transactionId[index];
        if ((hyphen && character != '-') ||
            (!hyphen && !((character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f')))) {
            throw std::invalid_argument("installer transaction id is not a canonical UUID");
        }
    }
    if (limits.maxPackBytes == 0U ||
        limits.maxPackBytes > limits.validation.afpack.maxArchiveSize) {
        throw std::invalid_argument(
            "installer pack limit must be nonzero and not exceed the validation limit");
    }
    if (limits.ioBufferBytes == 0U ||
        limits.ioBufferBytes > kMaximumInstallIoBufferBytes) {
        throw std::invalid_argument("installer I/O buffer size is outside limits");
    }
    if (limits.validation.ioBufferBytes == 0U ||
        limits.validation.ioBufferBytes > kMaximumValidationIoBufferBytes) {
        throw std::invalid_argument("installer validation I/O buffer size is outside limits");
    }
    if (limits.validation.maxManifestBytes == 0U ||
        limits.validation.maxManifestBytes >
            static_cast<std::uint64_t>(limits.validation.manifest.maxInputBytes)) {
        throw std::invalid_argument("installer manifest limits are inconsistent");
    }
}

void report(
    const InstallProgressCallback& callback,
    const std::stop_token stopToken,
    const InstallProgress& progress) {
    checkCancellation(stopToken);
    if (progress.completedBytes > progress.totalBytes ||
        progress.entriesCompleted > progress.entryCount) {
        throw std::logic_error("installer progress invariant failed");
    }
    if (callback) {
        callback(progress);
    }
    checkCancellation(stopToken);
}

void reportCommittedProgressNoexcept(
    const InstallProgressCallback& callback,
    const InstallProgress& progress) noexcept {
    if (progress.completedBytes > progress.totalBytes ||
        progress.entriesCompleted > progress.entryCount) {
        return;
    }
    if (callback) {
        try {
            callback(progress);
        }
        catch (...) {
            // Once the commit phase starts, observer failures cannot turn a
            // completed activation into an ambiguous caller-visible failure.
        }
    }
}

void reportSimple(
    const InstallProgressCallback& callback,
    const std::stop_token stopToken,
    const InstallPhase phase) {
    report(callback, stopToken, {
        .phase = phase,
        .validationPhase = std::nullopt,
    });
}

void ensurePrivateDirectory(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (!error && status.type() != std::filesystem::file_type::not_found) {
        if (!std::filesystem::is_directory(status) ||
            std::filesystem::is_symlink(status)) {
            throw std::runtime_error(
                "installer path is not a private directory: " + path.string());
        }
        return;
    }
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::runtime_error(
            "cannot inspect installer directory " + path.string() + ": " +
            error.message());
    }
    error.clear();
    if (!std::filesystem::create_directory(path, error)) {
        if (!error) {
            throw std::runtime_error(
                "installer directory appeared concurrently: " + path.string());
        }
        throw std::runtime_error(
            "cannot create installer directory " + path.string() + ": " +
            error.message());
    }
}

void requirePrivateParentDirectory(const std::filesystem::path& path) {
    const auto parent = path.parent_path().empty() ?
        std::filesystem::path(".") : path.parent_path();
    std::error_code error;
    const auto status = std::filesystem::symlink_status(parent, error);
    if (error || !std::filesystem::is_directory(status) ||
        std::filesystem::is_symlink(status)) {
        throw std::runtime_error(
            "installer content-root parent must already be a private directory: " +
            parent.string());
    }
}

[[nodiscard]] std::uint64_t streamSize(
    std::istream& input,
    const std::filesystem::path& path,
    const std::string_view purpose) {
    input.clear();
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error(
            "cannot determine " + std::string(purpose) + " size: " + path.string());
    }
    return static_cast<std::uint64_t>(end);
}

[[nodiscard]] std::ifstream openExternalSource(
    const std::filesystem::path& sourcePath,
    const std::uint64_t maxPackBytes,
    std::uint64_t& size) {
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(sourcePath, statusError);
    if (statusError || !std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
        throw std::runtime_error(
            "installer source is not a regular file: " + sourcePath.string());
    }
    std::ifstream input(sourcePath, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open installer source: " + sourcePath.string());
    }
    size = streamSize(input, sourcePath, "installer source");
    if (size > maxPackBytes) {
        throw std::runtime_error("installer source exceeds the configured pack limit");
    }
    input.seekg(0, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek installer source: " + sourcePath.string());
    }
    return input;
}

void removeOwnedFile(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

void removeOwnedFileChecked(const std::filesystem::path& path) {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error || !removed) {
        throw std::runtime_error(
            "cannot remove owned installer staging file: " + path.string());
    }
}

[[nodiscard]] crypto::Sha256Digest copySource(
    std::ifstream& input,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& stagingPath,
    const std::uint64_t sourceSize,
    const InstallLimits& limits,
    const std::stop_token stopToken,
    const InstallProgressCallback& progress) {
    std::ofstream output(stagingPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open reserved installer staging file");
    }
    std::vector<std::uint8_t> buffer(limits.ioBufferBytes);
    crypto::Sha256 copiedHash;
    std::uint64_t copied = 0U;
    report(progress, stopToken, {
        .phase = InstallPhase::copyingSource,
        .completedBytes = 0U,
        .totalBytes = sourceSize,
        .validationPhase = std::nullopt,
    });
    while (copied != sourceSize) {
        checkCancellation(stopToken);
        const auto chunkSize = static_cast<std::size_t>(std::min<std::uint64_t>(
            sourceSize - copied, buffer.size()));
        if (chunkSize > static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max())) {
            throw std::runtime_error("installer copy chunk exceeds stream limits");
        }
        input.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(chunkSize));
        if (input.gcount() != static_cast<std::streamsize>(chunkSize)) {
            throw std::runtime_error(
                "installer source shrank while copying: " + sourcePath.string());
        }
        output.write(
            reinterpret_cast<const char*>(buffer.data()),
            static_cast<std::streamsize>(chunkSize));
        if (!output) {
            throw std::runtime_error("cannot write installer staging file");
        }
        copiedHash.update(std::span<const std::uint8_t>(buffer).first(chunkSize));
        copied += chunkSize;
        report(progress, stopToken, {
            .phase = InstallPhase::copyingSource,
            .completedBytes = copied,
            .totalBytes = sourceSize,
            .validationPhase = std::nullopt,
        });
    }

    char extra{};
    input.read(&extra, 1);
    if (input.gcount() != 0) {
        throw std::runtime_error(
            "installer source grew while copying: " + sourcePath.string());
    }
    if (!input.eof()) {
        throw std::runtime_error(
            "cannot confirm installer source size: " + sourcePath.string());
    }
    output.flush();
    if (!output) {
        throw std::runtime_error("cannot flush installer staging stream");
    }
    output.close();
    if (!output) {
        throw std::runtime_error("cannot close installer staging stream");
    }
    io::syncFile(stagingPath);
    checkCancellation(stopToken);
    return copiedHash.finish();
}

[[nodiscard]] crypto::Sha256Digest hashExactFile(
    const std::filesystem::path& path,
    const std::uint64_t expectedSize,
    const std::size_t ioBufferBytes,
    const InstallPhase phase,
    const std::stop_token stopToken,
    const InstallProgressCallback& progress) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open installer file for hashing: " + path.string());
    }
    if (streamSize(input, path, "installer hash input") != expectedSize) {
        throw std::runtime_error("installer hash input size mismatch: " + path.string());
    }
    input.seekg(0, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek installer hash input: " + path.string());
    }
    std::vector<std::uint8_t> buffer(ioBufferBytes);
    crypto::Sha256 hash;
    std::uint64_t completed = 0U;
    report(progress, stopToken, {
        .phase = phase,
        .completedBytes = 0U,
        .totalBytes = expectedSize,
        .validationPhase = std::nullopt,
    });
    while (completed != expectedSize) {
        checkCancellation(stopToken);
        const auto chunkSize = static_cast<std::size_t>(std::min<std::uint64_t>(
            expectedSize - completed, buffer.size()));
        input.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(chunkSize));
        if (input.gcount() != static_cast<std::streamsize>(chunkSize)) {
            throw std::runtime_error("installer hash input shrank: " + path.string());
        }
        hash.update(std::span<const std::uint8_t>(buffer).first(chunkSize));
        completed += chunkSize;
        report(progress, stopToken, {
            .phase = phase,
            .completedBytes = completed,
            .totalBytes = expectedSize,
            .validationPhase = std::nullopt,
        });
    }
    char extra{};
    input.read(&extra, 1);
    if (input.gcount() != 0 || !input.eof()) {
        throw std::runtime_error("installer hash input grew: " + path.string());
    }
    checkCancellation(stopToken);
    return hash.finish();
}

[[nodiscard]] ValidatedPack validateForInstall(
    const std::filesystem::path& path,
    const ValidationLimits& limits,
    const InstallPhase installPhase,
    const std::stop_token stopToken,
    const InstallProgressCallback& progress) {
    try {
        return validatePack(
            path,
            limits,
            stopToken,
            [&](const ValidationProgress& validation) {
                report(progress, stopToken, {
                    .phase = installPhase,
                    .completedBytes = validation.completedBytes,
                    .totalBytes = validation.totalBytes,
                    .entriesCompleted = validation.entriesCompleted,
                    .entryCount = validation.entryCount,
                    .validationPhase = validation.phase,
                });
            });
    }
    catch (const ValidationCancelled&) {
        cancelled();
    }
}

[[nodiscard]] std::optional<ActiveRecord> readActiveRecord(
    const std::filesystem::path& path,
    const std::uint64_t maxPackBytes) {
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (!statusError && status.type() == std::filesystem::file_type::not_found) {
        return std::nullopt;
    }
    if (statusError == std::errc::no_such_file_or_directory) {
        return std::nullopt;
    }
    if (statusError || !std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
        throw ActiveRecordError("active AFAC path is not a regular file");
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw ActiveRecordError("cannot open active AFAC record");
    }
    const auto size = streamSize(input, path, "active AFAC record");
    if (size > kActiveRecordWithPreviousSize ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw ActiveRecordError("active AFAC record exceeds its maximum size");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!input || (!bytes.empty() && !input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())))) {
        throw ActiveRecordError("cannot read active AFAC record exactly");
    }
    char extra{};
    input.read(&extra, 1);
    if (input.gcount() != 0 || !input.eof()) {
        throw ActiveRecordError("active AFAC record changed while reading");
    }
    return parseActiveRecord(bytes, maxPackBytes);
}

} // namespace

InstallCancelled::InstallCancelled()
    : std::runtime_error("AFPACK installation cancelled") {}

InstallCommitUnknown::InstallCommitUnknown(ActiveRecord requestedActive)
    : std::runtime_error("AFPACK active-record commit outcome is unknown"),
      requestedActive_(std::move(requestedActive)) {}

namespace {

InstallResult installPackImpl(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& contentRoot,
    const std::string_view transactionId,
    const testing::InstallHooks* const hooks,
    const InstallLimits& limits,
    const std::stop_token stopToken,
    InstallProgressCallback progress) {
    validateArguments(sourcePath, contentRoot, transactionId, limits);
    checkCancellation(stopToken);

    std::uint64_t sourceSize = 0U;
    auto source = openExternalSource(sourcePath, limits.maxPackBytes, sourceSize);

    reportSimple(progress, stopToken, InstallPhase::preparingDirectories);
    requirePrivateParentDirectory(contentRoot);
    const auto contentRootParent = contentRoot.parent_path().empty() ?
        std::filesystem::path(".") : contentRoot.parent_path();
    ensurePrivateDirectory(contentRoot);
    const auto stagingDirectory = contentRoot / "staging";
    const auto packsDirectory = contentRoot / "packs";
    ensurePrivateDirectory(stagingDirectory);
    ensurePrivateDirectory(packsDirectory);
    io::syncDirectory(contentRootParent);
    io::syncDirectory(contentRoot);
    io::syncDirectory(stagingDirectory);
    io::syncDirectory(packsDirectory);

    const std::string transaction(transactionId);
    const auto stagingPath = stagingDirectory /
        ("import-" + transaction + ".afpack.partial");
    const auto activePath = contentRoot / "active.afac";
    const auto activeTemporaryPath = contentRoot /
        ("active-" + transaction + ".afac.partial");

    bool stagingOwned = false;
    bool activeTemporaryOwned = false;
    try {
        io::writeFileExclusiveDurable(stagingPath, {});
        stagingOwned = true;
        const auto copiedDigest = copySource(
            source,
            sourcePath,
            stagingPath,
            sourceSize,
            limits,
            stopToken,
            progress);
        const auto digest = hashExactFile(
            stagingPath,
            sourceSize,
            limits.ioBufferBytes,
            InstallPhase::hashingStagedPack,
            stopToken,
            progress);
        if (digest != copiedDigest) {
            throw std::runtime_error(
                "staged AFPACK differs from the bytes accepted during copy");
        }
        auto validated = validateForInstall(
            stagingPath,
            limits.validation,
            InstallPhase::validatingStagedPack,
            stopToken,
            progress);
        const auto confirmedDigest = hashExactFile(
            stagingPath,
            sourceSize,
            limits.ioBufferBytes,
            InstallPhase::confirmingStagedPack,
            stopToken,
            progress);
        if (confirmedDigest != digest) {
            throw std::runtime_error(
                "staged AFPACK changed after its content digest was computed");
        }

        const auto finalPath = packsDirectory / packFileName(digest);
        reportSimple(progress, stopToken, InstallPhase::publishingPack);
        bool reusedExisting = false;
        try {
            io::renameFileNoReplaceDurable(stagingPath, finalPath);
            stagingOwned = false;
        }
        catch (const io::DurableFileError& error) {
            if (error.kind() != io::DurableFileErrorKind::alreadyExists) {
                throw;
            }
            reusedExisting = true;
            std::error_code statusError;
            const auto finalStatus = std::filesystem::symlink_status(finalPath, statusError);
            if (statusError || !std::filesystem::is_regular_file(finalStatus) ||
                std::filesystem::is_symlink(finalStatus)) {
                throw std::runtime_error(
                    "existing digest-named AFPACK is not a regular file");
            }
            const auto existingDigest = hashExactFile(
                finalPath,
                sourceSize,
                limits.ioBufferBytes,
                InstallPhase::checkingExistingPack,
                stopToken,
                progress);
            if (existingDigest != digest) {
                throw std::runtime_error(
                    "existing digest-named AFPACK does not match its full digest");
            }
            (void)validateForInstall(
                finalPath,
                limits.validation,
                InstallPhase::validatingExistingPack,
                stopToken,
                progress);
            const auto confirmedExistingDigest = hashExactFile(
                finalPath,
                sourceSize,
                limits.ioBufferBytes,
                InstallPhase::confirmingExistingPack,
                stopToken,
                progress);
            if (confirmedExistingDigest != digest) {
                throw std::runtime_error(
                    "existing AFPACK changed after collision validation");
            }
            removeOwnedFileChecked(stagingPath);
            stagingOwned = false;
        }

        reportSimple(progress, stopToken, InstallPhase::readingActiveRecord);
        const auto oldActive = readActiveRecord(activePath, limits.maxPackBytes);
        if (oldActive.has_value() && oldActive->current.sha256 == digest) {
            if (oldActive->current.size != sourceSize) {
                throw ActiveRecordError(
                    "active AFAC size disagrees with the digest-named pack");
            }
            report(progress, stopToken, {
                .phase = InstallPhase::complete,
                .validationPhase = std::nullopt,
            });
            return {
                .manifest = std::move(validated.manifest),
                .digest = digest,
                .size = sourceSize,
                .finalPath = finalPath,
                .active = *oldActive,
                .reusedExisting = reusedExisting,
                .activeChanged = false,
            };
        }

        const auto nextActive = makeNextActive(
            digest, sourceSize, oldActive, limits.maxPackBytes);
        const auto activeBytes = serializeActiveRecord(nextActive, limits.maxPackBytes);
        InstallResult committedResult {
            .manifest = std::move(validated.manifest),
            .digest = digest,
            .size = sourceSize,
            .finalPath = finalPath,
            .active = nextActive,
            .reusedExisting = reusedExisting,
            .activeChanged = true,
        };
        reportSimple(progress, stopToken, InstallPhase::writingActiveRecord);
        io::writeFileExclusiveDurable(activeTemporaryPath, activeBytes);
        activeTemporaryOwned = true;
        checkCancellation(stopToken);

        reportCommittedProgressNoexcept(progress, {
            .phase = InstallPhase::committingActiveRecord,
            .validationPhase = std::nullopt,
        });
        std::optional<ActiveRecord> reboundActive;
        bool commitFunctionReturned = false;
        try {
            if (hooks != nullptr && hooks->commitActive != nullptr) {
                hooks->commitActive(
                    activeTemporaryPath, activePath, hooks->context);
            }
            else {
                io::replaceFileDurable(activeTemporaryPath, activePath);
            }
            commitFunctionReturned = true;
            activeTemporaryOwned = false;
            reboundActive = readActiveRecord(activePath, limits.maxPackBytes);
        }
        catch (...) {
            const auto commitFailure = std::current_exception();
            try {
                reboundActive = readActiveRecord(activePath, limits.maxPackBytes);
            }
            catch (...) {
                throw InstallCommitUnknown(nextActive);
            }
            if (!reboundActive.has_value() || *reboundActive != nextActive) {
                if (commitFunctionReturned) {
                    throw InstallCommitUnknown(nextActive);
                }
                std::rethrow_exception(commitFailure);
            }
            // replaceFileDurable may report a post-rename sync failure. An
            // exact startup-equivalent re-read proves the requested logical
            // state, but the interrupted durability work must still be retried.
            try {
                if (hooks != nullptr && hooks->retryDurability != nullptr) {
                    hooks->retryDurability(activePath, contentRoot, hooks->context);
                }
                else {
                    io::syncFile(activePath);
                    io::syncDirectory(contentRoot);
                }
            }
            catch (...) {
                throw InstallCommitUnknown(nextActive);
            }
            activeTemporaryOwned = false;
        }
        if (!reboundActive.has_value() || *reboundActive != nextActive) {
            throw InstallCommitUnknown(nextActive);
        }
        reportCommittedProgressNoexcept(progress, {
            .phase = InstallPhase::complete,
            .validationPhase = std::nullopt,
        });
        return committedResult;
    }
    catch (...) {
        if (activeTemporaryOwned) {
            removeOwnedFile(activeTemporaryPath);
        }
        if (stagingOwned) {
            removeOwnedFile(stagingPath);
        }
        throw;
    }
}

} // namespace

InstallResult installPack(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& contentRoot,
    const std::string_view transactionId,
    const InstallLimits& limits,
    const std::stop_token stopToken,
    InstallProgressCallback progress) {
    return installPackImpl(
        sourcePath,
        contentRoot,
        transactionId,
        nullptr,
        limits,
        stopToken,
        std::move(progress));
}

namespace testing {

InstallResult installPackWithHooks(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& contentRoot,
    const std::string_view transactionId,
    const InstallHooks& hooks,
    const InstallLimits& limits,
    const std::stop_token stopToken,
    InstallProgressCallback progress) {
    return installPackImpl(
        sourcePath,
        contentRoot,
        transactionId,
        &hooks,
        limits,
        stopToken,
        std::move(progress));
}

} // namespace testing

} // namespace airfix::afpack
