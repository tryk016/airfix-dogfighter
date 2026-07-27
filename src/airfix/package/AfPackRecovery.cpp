#include "airfix/package/AfPackRecovery.hpp"

#include "airfix/io/DurableFile.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <limits>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace airfix::afpack::detail {

struct ActiveContentLeaseFactory final {
    [[nodiscard]] static ActiveContentLease create(
        std::unique_ptr<std::ifstream> input,
        const std::uint64_t activeGeneration,
        ActivePackReference reference,
        std::filesystem::path path,
        Pack pack,
        Manifest manifest,
        udsp::Archive sourceArchive,
        const std::size_t sourcePackEntryIndex) {
        return ActiveContentLease(
            std::move(input),
            activeGeneration,
            std::move(reference),
            std::move(path),
            std::move(pack),
            std::move(manifest),
            std::move(sourceArchive),
            sourcePackEntryIndex);
    }
};

} // namespace airfix::afpack::detail

namespace airfix::afpack {
namespace {

static_assert(std::is_nothrow_move_constructible_v<RollbackCommitResult>);

class CandidateIoError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CandidateProgressError final {
public:
    explicit CandidateProgressError(std::exception_ptr error) noexcept
        : error_(std::move(error)) {}

    [[noreturn]] void rethrowOriginal() const {
        std::rethrow_exception(error_);
    }

private:
    std::exception_ptr error_;
};

[[noreturn]] void cancelled() {
    throw RecoveryCancelled{};
}

void checkCancellation(const std::stop_token stopToken) {
    if (stopToken.stop_requested()) {
        cancelled();
    }
}

void validateLimits(const RecoveryLimits& limits) {
    if (limits.maxPackBytes == 0U ||
        limits.maxPackBytes > limits.validation.afpack.maxArchiveSize) {
        throw std::invalid_argument(
            "recovery pack limit must be nonzero and not exceed validation limit");
    }
    if (limits.ioBufferBytes == 0U ||
        limits.ioBufferBytes > kMaximumRecoveryIoBufferBytes) {
        throw std::invalid_argument("recovery I/O buffer size is outside limits");
    }
    if (limits.validation.maxManifestBytes == 0U ||
        limits.validation.maxManifestBytes >
            static_cast<std::uint64_t>(limits.validation.manifest.maxInputBytes)) {
        throw std::invalid_argument("recovery manifest limits are inconsistent");
    }
}

void validateTransactionId(const std::string_view transactionId) {
    if (transactionId.size() != 36U) {
        throw std::invalid_argument("rollback transaction id is not a canonical UUID");
    }
    for (std::size_t index = 0U; index < transactionId.size(); ++index) {
        const bool hyphen = index == 8U || index == 13U || index == 18U || index == 23U;
        const char character = transactionId[index];
        if ((hyphen && character != '-') ||
            (!hyphen && !((character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f')))) {
            throw std::invalid_argument("rollback transaction id is not a canonical UUID");
        }
    }
}

void report(
    const RecoveryProgressCallback& callback,
    const std::stop_token stopToken,
    const RecoveryProgress& progress) {
    checkCancellation(stopToken);
    if (progress.completedBytes > progress.totalBytes) {
        throw std::logic_error("recovery progress invariant failed");
    }
    if (callback) {
        callback(progress);
    }
    checkCancellation(stopToken);
}

void reportCommittedNoexcept(
    const RecoveryProgressCallback& callback,
    const RecoveryProgress& progress) noexcept {
    if (progress.completedBytes > progress.totalBytes) {
        return;
    }
    if (callback) {
        try {
            callback(progress);
        }
        catch (...) {
        }
    }
}

void reportCandidate(
    const RecoveryProgressCallback& callback,
    const std::stop_token stopToken,
    const RecoveryProgress& progress) {
    try {
        report(callback, stopToken, progress);
    }
    catch (const RecoveryCancelled&) {
        throw;
    }
    catch (...) {
        throw CandidateProgressError(std::current_exception());
    }
}

[[nodiscard]] std::uint64_t streamSize(
    std::istream& input,
    const std::filesystem::path& path) {
    input.clear();
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw CandidateIoError("cannot determine recovery file size: " + path.string());
    }
    return static_cast<std::uint64_t>(end);
}

struct ActiveRead {
    enum class Status : std::uint8_t {
        found,
        missing,
        malformed,
        ioFailure,
    };

    Status status{Status::found};
    std::optional<ActiveRecord> record;
    std::string diagnostic;
};

[[nodiscard]] ActiveRead readActive(
    const std::filesystem::path& path,
    const std::uint64_t maxPackBytes) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if ((!error && status.type() == std::filesystem::file_type::not_found) ||
        error == std::errc::no_such_file_or_directory) {
        return {
            .status = ActiveRead::Status::missing,
            .record = std::nullopt,
            .diagnostic = {},
        };
    }
    if (error) {
        return {
            .status = ActiveRead::Status::ioFailure,
            .record = std::nullopt,
            .diagnostic = "cannot inspect active AFAC path: " + error.message(),
        };
    }
    if (!std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
        return {
            .status = ActiveRead::Status::malformed,
            .record = std::nullopt,
            .diagnostic = "active AFAC path is not an exact regular file",
        };
    }
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            throw CandidateIoError("cannot open active AFAC record");
        }
        const auto size = streamSize(input, path);
        if (size > kActiveRecordWithPreviousSize) {
            throw ActiveRecordError("active AFAC record exceeds maximum size");
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.seekg(0, std::ios::beg);
        if (!input || (!bytes.empty() && !input.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))) {
            throw CandidateIoError("cannot read active AFAC record exactly");
        }
        char extra{};
        input.read(&extra, 1);
        if (input.gcount() != 0 || !input.eof()) {
            throw CandidateIoError("active AFAC record changed while reading");
        }
        return {
            .status = ActiveRead::Status::found,
            .record = parseActiveRecord(bytes, maxPackBytes),
            .diagnostic = {},
        };
    }
    catch (const ActiveRecordError& exception) {
        return {
            .status = ActiveRead::Status::malformed,
            .record = std::nullopt,
            .diagnostic = exception.what(),
        };
    }
    catch (const CandidateIoError& exception) {
        return {
            .status = ActiveRead::Status::ioFailure,
            .record = std::nullopt,
            .diagnostic = exception.what(),
        };
    }
}

[[nodiscard]] ActiveRecord readActiveStrict(
    const std::filesystem::path& path,
    const std::uint64_t maxPackBytes) {
    const auto read = readActive(path, maxPackBytes);
    if (read.status != ActiveRead::Status::found || !read.record.has_value()) {
        throw StaleActiveContent{};
    }
    return *read.record;
}

[[nodiscard]] std::size_t manifestIndex(const std::span<const Entry> entries) {
    const auto match = std::find_if(entries.begin(), entries.end(), [](const Entry& entry) {
        return entry.kind == EntryKind::manifest && entry.path == "manifest.json";
    });
    if (match == entries.end()) {
        throw ParseError("recovery pack has no manifest.json");
    }
    return static_cast<std::size_t>(std::distance(entries.begin(), match));
}

[[nodiscard]] std::size_t findEntryIndex(
    const std::span<const Entry> entries,
    const ManifestEntry& requested) {
    const auto match = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
        return entry.path == requested.path && entry.kind == requested.kind;
    });
    if (match == entries.end()) {
        throw ManifestError("recovery manifest entry is absent from pack table");
    }
    return static_cast<std::size_t>(std::distance(entries.begin(), match));
}

[[nodiscard]] RecoveryPhase hashPhase(const CandidateRole role) noexcept {
    return role == CandidateRole::current ?
        RecoveryPhase::hashingCurrent : RecoveryPhase::hashingPrevious;
}

[[nodiscard]] RecoveryPhase validationPhase(const CandidateRole role) noexcept {
    return role == CandidateRole::current ?
        RecoveryPhase::validatingCurrent : RecoveryPhase::validatingPrevious;
}

struct CandidateResult {
    std::optional<ActiveContentLease> content;
    CandidateDiagnostic diagnostic;
};

[[nodiscard]] CandidateResult inspectCandidate(
    const std::filesystem::path& path,
    const ActivePackReference& reference,
    const std::uint64_t activeGeneration,
    const CandidateRole role,
    const RecoveryLimits& limits,
    const std::stop_token stopToken,
    const RecoveryProgressCallback& progress) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if ((!error && status.type() == std::filesystem::file_type::not_found) ||
        error == std::errc::no_such_file_or_directory) {
        return {
            .content = std::nullopt,
            .diagnostic = {CandidateStatus::missing, "digest-derived pack is missing"},
        };
    }
    if (error) {
        return {
            .content = std::nullopt,
            .diagnostic = {
                CandidateStatus::ioFailure,
                "cannot inspect digest-derived pack: " + error.message(),
            },
        };
    }
    if (!std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
        return {
            .content = std::nullopt,
            .diagnostic = {
                CandidateStatus::wrongType,
                "digest-derived pack is not an exact regular file",
            },
        };
    }

    try {
        auto input = std::make_unique<std::ifstream>(
            path, std::ios::binary | std::ios::ate);
        if (!*input) {
            throw CandidateIoError("cannot open digest-derived pack");
        }
        const auto size = streamSize(*input, path);
        if (size != reference.size) {
            return {
                .content = std::nullopt,
                .diagnostic = {
                    CandidateStatus::sizeMismatch,
                    "digest-derived pack size differs from AFAC",
                },
            };
        }
        input->seekg(0, std::ios::beg);
        if (!*input) {
            throw CandidateIoError("cannot seek digest-derived pack");
        }

        crypto::Sha256 hash;
        std::vector<std::uint8_t> buffer(limits.ioBufferBytes);
        std::uint64_t completed = 0U;
        reportCandidate(progress, stopToken, {
            .phase = hashPhase(role),
            .completedBytes = 0U,
            .totalBytes = size,
            .candidate = role,
        });
        while (completed != size) {
            const auto chunkSize = static_cast<std::size_t>(std::min<std::uint64_t>(
                size - completed, buffer.size()));
            input->read(
                reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(chunkSize));
            if (input->gcount() != static_cast<std::streamsize>(chunkSize)) {
                throw CandidateIoError("digest-derived pack shrank while hashing");
            }
            hash.update(std::span<const std::uint8_t>(buffer).first(chunkSize));
            completed += chunkSize;
            reportCandidate(progress, stopToken, {
                .phase = hashPhase(role),
                .completedBytes = completed,
                .totalBytes = size,
                .candidate = role,
            });
        }
        char extra{};
        input->read(&extra, 1);
        if (input->gcount() != 0 || !input->eof()) {
            throw CandidateIoError("digest-derived pack grew while hashing");
        }
        if (hash.finish() != reference.sha256) {
            return {
                .content = std::nullopt,
                .diagnostic = {
                    CandidateStatus::digestMismatch,
                    "digest-derived pack SHA-256 differs from AFAC",
                },
            };
        }

        reportCandidate(progress, stopToken, {
            .phase = validationPhase(role),
            .completedBytes = size,
            .totalBytes = size,
            .candidate = role,
        });
        auto pack = Pack::open(*input, path, limits.validation.afpack);
        if (pack.archiveSize() != reference.size) {
            throw ParseError("trusted recovery pack size changed before metadata parse");
        }
        const auto entries = std::span<const Entry>(pack.entries());
        const auto manifestBytes = pack.readEntry(
            *input,
            path,
            manifestIndex(entries),
            limits.validation.maxManifestBytes);
        auto manifest = parseManifest(
            manifestBytes, entries, limits.validation.manifest);
        std::optional<udsp::Archive> sourceArchive;
        std::optional<std::size_t> sourcePackEntryIndex;
        for (const auto& manifestEntry : manifest.entries) {
            checkCancellation(stopToken);
            const auto entryIndex = findEntryIndex(entries, manifestEntry);
            const auto& entry = entries[entryIndex];
            auto archive = udsp::Archive::openRegion(
                *input,
                pack.archiveSize(),
                path,
                entry.dataOffset,
                entry.storedSize,
                limits.validation.udsp);
            if (manifestEntry.path == "source/Resource.up" &&
                manifestEntry.kind == EntryKind::sourceArchive) {
                sourceArchive.emplace(std::move(archive));
                sourcePackEntryIndex = entryIndex;
            }
        }
        checkCancellation(stopToken);
        if (!sourceArchive.has_value() || !sourcePackEntryIndex.has_value()) {
            throw ManifestError(
                "recovery manifest has no exact source/Resource.up archive");
        }
        return {
            .content = detail::ActiveContentLeaseFactory::create(
                std::move(input),
                activeGeneration,
                reference,
                path,
                std::move(pack),
                std::move(manifest),
                std::move(*sourceArchive),
                *sourcePackEntryIndex),
            .diagnostic = {CandidateStatus::valid, {}},
        };
    }
    catch (const RecoveryCancelled&) {
        throw;
    }
    catch (const CandidateProgressError& error) {
        error.rethrowOriginal();
    }
    catch (const CandidateIoError& exception) {
        return {
            .content = std::nullopt,
            .diagnostic = {CandidateStatus::ioFailure, exception.what()},
        };
    }
    catch (const ParseError& exception) {
        return {
            .content = std::nullopt,
            .diagnostic = {CandidateStatus::invalidPack, exception.what()},
        };
    }
    catch (const ManifestError& exception) {
        return {
            .content = std::nullopt,
            .diagnostic = {CandidateStatus::invalidPack, exception.what()},
        };
    }
    catch (const udsp::ParseError& exception) {
        return {
            .content = std::nullopt,
            .diagnostic = {CandidateStatus::invalidPack, exception.what()},
        };
    }
}

void verifyRollbackPackUnchanged(
    const ActiveContentLease& content,
    const RecoveryLimits& limits,
    const std::stop_token stopToken,
    const RecoveryProgressCallback& progress) {
    try {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(content.path(), error);
        if (error || !std::filesystem::is_regular_file(status) ||
            std::filesystem::is_symlink(status)) {
            throw StaleActiveContent{};
        }
        std::ifstream input(content.path(), std::ios::binary | std::ios::ate);
        if (!input ||
            streamSize(input, content.path()) != content.reference().size) {
            throw StaleActiveContent{};
        }
        input.seekg(0, std::ios::beg);
        crypto::Sha256 hash;
        std::vector<std::uint8_t> buffer(limits.ioBufferBytes);
        std::uint64_t completed = 0U;
        report(progress, stopToken, {
            .phase = RecoveryPhase::verifyingRollbackPack,
            .completedBytes = 0U,
            .totalBytes = content.reference().size,
            .candidate = CandidateRole::previous,
        });
        while (completed != content.reference().size) {
            const auto chunkSize = static_cast<std::size_t>(std::min<std::uint64_t>(
                content.reference().size - completed, buffer.size()));
            input.read(
                reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(chunkSize));
            if (input.gcount() != static_cast<std::streamsize>(chunkSize)) {
                throw StaleActiveContent{};
            }
            hash.update(std::span<const std::uint8_t>(buffer).first(chunkSize));
            completed += chunkSize;
            report(progress, stopToken, {
                .phase = RecoveryPhase::verifyingRollbackPack,
                .completedBytes = completed,
                .totalBytes = content.reference().size,
                .candidate = CandidateRole::previous,
            });
        }
        char extra{};
        input.read(&extra, 1);
        if (input.gcount() != 0 || !input.eof() ||
            hash.finish() != content.reference().sha256) {
            throw StaleActiveContent{};
        }
    }
    catch (const CandidateIoError&) {
        throw StaleActiveContent{};
    }
}

void removeOwnedFile(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

[[nodiscard]] RollbackCommitResult commitRollbackImpl(
    const ActiveContentInspection& inspection,
    const std::string_view transactionId,
    const testing::InstallHooks* const hooks,
    const RecoveryLimits& limits,
    const std::stop_token stopToken,
    RecoveryProgressCallback progress) {
    validateLimits(limits);
    validateTransactionId(transactionId);
    if (inspection.status() != ActiveContentStatus::rollbackAvailable ||
        !inspection.sourceActive().has_value() ||
        !inspection.previous().has_value()) {
        throw std::invalid_argument("rollback requires a verified rollbackAvailable result");
    }
    checkCancellation(stopToken);

    const auto& sourceActive = *inspection.sourceActive();
    const auto& rollbackContent = *inspection.previous();
    const auto activePath = inspection.contentRoot() / "active.afac";
    if (readActiveStrict(activePath, limits.maxPackBytes) != sourceActive) {
        throw StaleActiveContent{};
    }
    verifyRollbackPackUnchanged(
        rollbackContent, limits, stopToken, progress);
    report(progress, stopToken, {
        .phase = RecoveryPhase::checkingStaleActive,
        .candidate = std::nullopt,
    });
    if (readActiveStrict(activePath, limits.maxPackBytes) != sourceActive) {
        throw StaleActiveContent{};
    }

    const auto nextActive = makeNextActive(
        rollbackContent.reference().sha256,
        rollbackContent.reference().size,
        sourceActive,
        limits.maxPackBytes);
    const auto bytes = serializeActiveRecord(nextActive, limits.maxPackBytes);
    const auto temporaryPath = inspection.contentRoot() /
        ("active-" + std::string(transactionId) + ".afac.partial");
    RollbackCommitResult result{
        .active = nextActive,
        .activePath = activePath,
        .currentPackPath = rollbackContent.path(),
    };

    bool temporaryOwned = false;
    try {
        report(progress, stopToken, {
            .phase = RecoveryPhase::writingRollbackRecord,
            .candidate = std::nullopt,
        });
        io::writeFileExclusiveDurable(temporaryPath, bytes);
        temporaryOwned = true;
        checkCancellation(stopToken);
        report(progress, stopToken, {
            .phase = RecoveryPhase::checkingFinalStaleActive,
            .candidate = std::nullopt,
        });
        if (readActiveStrict(activePath, limits.maxPackBytes) != sourceActive) {
            throw StaleActiveContent{};
        }
        checkCancellation(stopToken);

        reportCommittedNoexcept(progress, {
            .phase = RecoveryPhase::committingRollbackRecord,
            .candidate = std::nullopt,
        });
        std::optional<ActiveRecord> rebound;
        bool commitReturned = false;
        try {
            if (hooks != nullptr && hooks->commitActive != nullptr) {
                hooks->commitActive(temporaryPath, activePath, hooks->context);
            }
            else {
                io::replaceFileDurable(temporaryPath, activePath);
            }
            commitReturned = true;
            temporaryOwned = false;
            rebound = readActiveStrict(activePath, limits.maxPackBytes);
        }
        catch (...) {
            const auto failure = std::current_exception();
            try {
                rebound = readActiveStrict(activePath, limits.maxPackBytes);
            }
            catch (...) {
                throw InstallCommitUnknown(nextActive);
            }
            if (!rebound.has_value() || *rebound != nextActive) {
                if (commitReturned) {
                    throw InstallCommitUnknown(nextActive);
                }
                std::rethrow_exception(failure);
            }
            try {
                if (hooks != nullptr && hooks->retryDurability != nullptr) {
                    hooks->retryDurability(
                        activePath, inspection.contentRoot(), hooks->context);
                }
                else {
                    io::syncFile(activePath);
                    io::syncDirectory(inspection.contentRoot());
                }
            }
            catch (...) {
                throw InstallCommitUnknown(nextActive);
            }
            temporaryOwned = false;
        }
        if (!rebound.has_value() || *rebound != nextActive) {
            throw InstallCommitUnknown(nextActive);
        }
        reportCommittedNoexcept(progress, {
            .phase = RecoveryPhase::complete,
            .candidate = std::nullopt,
        });
        return result;
    }
    catch (...) {
        if (temporaryOwned) {
            removeOwnedFile(temporaryPath);
        }
        throw;
    }
}

} // namespace

ActiveContentLease::ActiveContentLease(
    std::unique_ptr<std::ifstream> input,
    const std::uint64_t activeGeneration,
    ActivePackReference reference,
    std::filesystem::path path,
    Pack pack,
    Manifest manifest,
    udsp::Archive sourceArchive,
    const std::size_t sourcePackEntryIndex) noexcept
    : input_(std::move(input)),
      activeGeneration_(activeGeneration),
      reference_(std::move(reference)),
      path_(std::move(path)),
      pack_(std::move(pack)),
      manifest_(std::move(manifest)),
      sourceArchive_(std::move(sourceArchive)),
      sourcePackEntryIndex_(sourcePackEntryIndex) {}

RecoveryCancelled::RecoveryCancelled()
    : std::runtime_error("AFPACK recovery cancelled") {}

StaleActiveContent::StaleActiveContent()
    : std::runtime_error("active content changed after recovery inspection") {}

ActiveContentLease ActiveContentInspection::takeReadyLease() && {
    if (status_ != ActiveContentStatus::ready || !current_.has_value()) {
        throw std::logic_error(
            "only a ready recovery inspection owns an active content lease");
    }
    auto lease = std::move(*current_);
    current_.reset();
    return lease;
}

ActiveContentInspection inspectActiveContent(
    const std::filesystem::path& contentRoot,
    const RecoveryLimits& limits,
    const std::stop_token stopToken,
    RecoveryProgressCallback progress) {
    validateLimits(limits);
    if (contentRoot.empty()) {
        throw std::invalid_argument("recovery content root must not be empty");
    }
    std::error_code rootError;
    auto stableContentRoot = std::filesystem::absolute(contentRoot, rootError);
    if (rootError) {
        throw std::system_error(rootError, "cannot resolve recovery content root");
    }
    stableContentRoot = stableContentRoot.lexically_normal();
    report(progress, stopToken, {
        .phase = RecoveryPhase::readingActiveRecord,
        .candidate = std::nullopt,
    });

    ActiveContentInspection inspection;
    inspection.contentRoot_ = stableContentRoot;
    const auto active = readActive(
        stableContentRoot / "active.afac", limits.maxPackBytes);
    if (active.status == ActiveRead::Status::missing) {
        inspection.status_ = ActiveContentStatus::noContent;
        report(progress, stopToken, {
            .phase = RecoveryPhase::complete,
            .candidate = std::nullopt,
        });
        return inspection;
    }
    if (active.status == ActiveRead::Status::ioFailure) {
        inspection.status_ = ActiveContentStatus::unavailable;
        inspection.activeDiagnostic_ = active.diagnostic;
        report(progress, stopToken, {
            .phase = RecoveryPhase::complete,
            .candidate = std::nullopt,
        });
        return inspection;
    }
    if (active.status == ActiveRead::Status::malformed ||
        !active.record.has_value()) {
        inspection.status_ = ActiveContentStatus::malformedActive;
        inspection.activeDiagnostic_ = active.diagnostic;
        report(progress, stopToken, {
            .phase = RecoveryPhase::complete,
            .candidate = std::nullopt,
        });
        return inspection;
    }
    inspection.sourceActive_ = *active.record;
    const auto packs = stableContentRoot / "packs";
    const auto currentPath = packs / packFileName(active.record->current.sha256);
    auto current = inspectCandidate(
        currentPath,
        active.record->current,
        active.record->generation,
        CandidateRole::current,
        limits,
        stopToken,
        progress);
    inspection.currentDiagnostic_ = current.diagnostic;
    if (current.content.has_value()) {
        inspection.status_ = ActiveContentStatus::ready;
        inspection.current_ = std::move(current.content);
        report(progress, stopToken, {
            .phase = RecoveryPhase::complete,
            .candidate = CandidateRole::current,
        });
        return inspection;
    }

    if (current.diagnostic.status == CandidateStatus::ioFailure) {
        inspection.status_ = ActiveContentStatus::unavailable;
        report(progress, stopToken, {
            .phase = RecoveryPhase::complete,
            .candidate = CandidateRole::current,
        });
        return inspection;
    }

    if (!active.record->previous.has_value()) {
        inspection.status_ = ActiveContentStatus::unusable;
        report(progress, stopToken, {
            .phase = RecoveryPhase::complete,
            .candidate = CandidateRole::current,
        });
        return inspection;
    }
    const auto previousPath = packs / packFileName(active.record->previous->sha256);
    auto previous = inspectCandidate(
        previousPath,
        *active.record->previous,
        active.record->generation,
        CandidateRole::previous,
        limits,
        stopToken,
        progress);
    inspection.previousDiagnostic_ = previous.diagnostic;
    if (previous.content.has_value()) {
        inspection.status_ = ActiveContentStatus::rollbackAvailable;
        inspection.previous_ = std::move(previous.content);
    }
    else if (previous.diagnostic.status == CandidateStatus::ioFailure) {
        inspection.status_ = ActiveContentStatus::unavailable;
    }
    else {
        inspection.status_ = ActiveContentStatus::unusable;
    }
    report(progress, stopToken, {
        .phase = RecoveryPhase::complete,
        .candidate = CandidateRole::previous,
    });
    return inspection;
}

RollbackCommitResult commitRollback(
    const ActiveContentInspection& inspection,
    const std::string_view transactionId,
    const RecoveryLimits& limits,
    const std::stop_token stopToken,
    RecoveryProgressCallback progress) {
    return commitRollbackImpl(
        inspection,
        transactionId,
        nullptr,
        limits,
        stopToken,
        std::move(progress));
}

namespace testing {

RollbackCommitResult commitRollbackWithHooks(
    const ActiveContentInspection& inspection,
    const std::string_view transactionId,
    const InstallHooks& hooks,
    const RecoveryLimits& limits,
    const std::stop_token stopToken,
    RecoveryProgressCallback progress) {
    return commitRollbackImpl(
        inspection,
        transactionId,
        &hooks,
        limits,
        stopToken,
        std::move(progress));
}

} // namespace testing

} // namespace airfix::afpack
