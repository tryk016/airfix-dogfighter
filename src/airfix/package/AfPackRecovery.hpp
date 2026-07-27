#pragma once

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/package/AfPackActiveRecord.hpp"
#include "airfix/package/AfPackInstaller.hpp"
#include "airfix/package/AfPackManifest.hpp"
#include "airfix/package/AfPackValidation.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace airfix::content {
class VerifiedContentSession;
}

namespace airfix::testing {
struct AuthenticatedStreamIdentityProbe;
}

namespace airfix::afpack {

namespace detail {
struct ActiveContentLeaseFactory;
}

inline constexpr std::size_t kDefaultRecoveryIoBufferBytes = 64U * 1024U;
inline constexpr std::size_t kMaximumRecoveryIoBufferBytes = 1024U * 1024U;

struct RecoveryLimits {
    ValidationLimits validation{};
    std::uint64_t maxPackBytes{512U * 1024U * 1024U};
    std::size_t ioBufferBytes{kDefaultRecoveryIoBufferBytes};
};

enum class ActiveContentStatus : std::uint8_t {
    noContent,
    ready,
    rollbackAvailable,
    unusable,
    malformedActive,
    unavailable,
};

enum class CandidateRole : std::uint8_t {
    current,
    previous,
};

enum class CandidateStatus : std::uint8_t {
    notInspected,
    valid,
    missing,
    wrongType,
    sizeMismatch,
    digestMismatch,
    invalidPack,
    ioFailure,
};

struct CandidateDiagnostic {
    CandidateStatus status{CandidateStatus::notInspected};
    std::string message;
};

// Move-only proof that one open AFPACK handle was authenticated against AFAC
// and that its metadata and nested source archive were parsed from that exact
// handle. The lease is retained until all runtime reads from the package end.
class ActiveContentLease final {
public:
    ActiveContentLease(const ActiveContentLease&) = delete;
    ActiveContentLease& operator=(const ActiveContentLease&) = delete;
    ActiveContentLease(ActiveContentLease&&) noexcept = default;
    ActiveContentLease& operator=(ActiveContentLease&&) noexcept = default;
    ~ActiveContentLease() = default;

    [[nodiscard]] std::uint64_t activeGeneration() const noexcept {
        return activeGeneration_;
    }
    [[nodiscard]] const ActivePackReference& reference() const noexcept {
        return reference_;
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }
    [[nodiscard]] const Pack& pack() const noexcept { return pack_; }
    [[nodiscard]] const Manifest& manifest() const noexcept { return manifest_; }
    [[nodiscard]] const udsp::Archive& sourceArchive() const noexcept {
        return sourceArchive_;
    }
    [[nodiscard]] std::size_t sourcePackEntryIndex() const noexcept {
        return sourcePackEntryIndex_;
    }

private:
    ActiveContentLease(
        std::unique_ptr<std::ifstream> input,
        std::uint64_t activeGeneration,
        ActivePackReference reference,
        std::filesystem::path path,
        Pack pack,
        Manifest manifest,
        udsp::Archive sourceArchive,
        std::size_t sourcePackEntryIndex) noexcept;

    friend struct detail::ActiveContentLeaseFactory;
    friend class airfix::content::VerifiedContentSession;
    friend struct airfix::testing::AuthenticatedStreamIdentityProbe;

    std::unique_ptr<std::ifstream> input_;
    std::uint64_t activeGeneration_{};
    ActivePackReference reference_;
    std::filesystem::path path_;
    Pack pack_;
    Manifest manifest_;
    udsp::Archive sourceArchive_;
    std::size_t sourcePackEntryIndex_{};
};

enum class RecoveryPhase : std::uint8_t {
    readingActiveRecord,
    hashingCurrent,
    validatingCurrent,
    hashingPrevious,
    validatingPrevious,
    verifyingRollbackPack,
    checkingStaleActive,
    writingRollbackRecord,
    checkingFinalStaleActive,
    committingRollbackRecord,
    complete,
};

struct RecoveryProgress {
    RecoveryPhase phase{};
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
    std::optional<CandidateRole> candidate;
};

using RecoveryProgressCallback = std::function<void(const RecoveryProgress&)>;

class RecoveryCancelled final : public std::runtime_error {
public:
    RecoveryCancelled();
};

class StaleActiveContent final : public std::runtime_error {
public:
    StaleActiveContent();
};

class ActiveContentInspection final {
public:
    ActiveContentInspection(const ActiveContentInspection&) = delete;
    ActiveContentInspection(ActiveContentInspection&&) noexcept = default;
    ActiveContentInspection& operator=(const ActiveContentInspection&) = delete;
    ActiveContentInspection& operator=(ActiveContentInspection&&) noexcept = default;

    [[nodiscard]] ActiveContentStatus status() const noexcept { return status_; }
    // Stable absolute root captured by inspectActiveContent().
    [[nodiscard]] const std::filesystem::path& contentRoot() const noexcept {
        return contentRoot_;
    }
    [[nodiscard]] const std::optional<ActiveRecord>& sourceActive() const noexcept {
        return sourceActive_;
    }
    [[nodiscard]] const std::optional<ActiveContentLease>& current() const noexcept {
        return current_;
    }
    [[nodiscard]] const std::optional<ActiveContentLease>& previous() const noexcept {
        return previous_;
    }
    // Consumes the authenticated current package. Only a ready inspection has
    // such a lease; rollback and all error states are rejected.
    [[nodiscard]] ActiveContentLease takeReadyLease() &&;
    [[nodiscard]] const CandidateDiagnostic& currentDiagnostic() const noexcept {
        return currentDiagnostic_;
    }
    [[nodiscard]] const CandidateDiagnostic& previousDiagnostic() const noexcept {
        return previousDiagnostic_;
    }
    [[nodiscard]] const std::string& activeDiagnostic() const noexcept {
        return activeDiagnostic_;
    }

private:
    ActiveContentInspection() = default;

    friend ActiveContentInspection inspectActiveContent(
        const std::filesystem::path&,
        const RecoveryLimits&,
        std::stop_token,
        RecoveryProgressCallback);

    ActiveContentStatus status_{ActiveContentStatus::noContent};
    std::filesystem::path contentRoot_;
    std::optional<ActiveRecord> sourceActive_;
    std::optional<ActiveContentLease> current_;
    std::optional<ActiveContentLease> previous_;
    CandidateDiagnostic currentDiagnostic_;
    CandidateDiagnostic previousDiagnostic_;
    std::string activeDiagnostic_;
};

struct RollbackCommitResult {
    ActiveRecord active;
    std::filesystem::path activePath;
    std::filesystem::path currentPackPath;
};

// Read-only inspection of a serialized, app-private content root. It never
// scans orphan pack files and derives candidate paths solely from AFAC digests.
[[nodiscard]] ActiveContentInspection inspectActiveContent(
    const std::filesystem::path& contentRoot,
    const RecoveryLimits& limits = {},
    std::stop_token stopToken = {},
    RecoveryProgressCallback progress = {});

// Commits only a previously verified rollbackAvailable inspection. The active
// record is compared with the inspection both before writing the temporary
// record and immediately before the non-cancellable commit boundary.
[[nodiscard]] RollbackCommitResult commitRollback(
    const ActiveContentInspection& inspection,
    std::string_view transactionId,
    const RecoveryLimits& limits = {},
    std::stop_token stopToken = {},
    RecoveryProgressCallback progress = {});

namespace testing {

[[nodiscard]] RollbackCommitResult commitRollbackWithHooks(
    const ActiveContentInspection& inspection,
    std::string_view transactionId,
    const InstallHooks& hooks,
    const RecoveryLimits& limits = {},
    std::stop_token stopToken = {},
    RecoveryProgressCallback progress = {});

} // namespace testing

} // namespace airfix::afpack
