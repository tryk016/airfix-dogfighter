#pragma once

#include "airfix/crypto/Sha256.hpp"
#include "airfix/package/AfPackActiveRecord.hpp"
#include "airfix/package/AfPackManifest.hpp"
#include "airfix/package/AfPackValidation.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string_view>

namespace airfix::afpack {

inline constexpr std::size_t kDefaultInstallIoBufferBytes = 64U * 1024U;
inline constexpr std::size_t kMaximumInstallIoBufferBytes = 1024U * 1024U;

struct InstallLimits {
    ValidationLimits validation{};
    std::uint64_t maxPackBytes{512U * 1024U * 1024U};
    std::size_t ioBufferBytes{kDefaultInstallIoBufferBytes};
};

enum class InstallPhase : std::uint8_t {
    preparingDirectories,
    copyingSource,
    hashingStagedPack,
    validatingStagedPack,
    confirmingStagedPack,
    publishingPack,
    checkingExistingPack,
    validatingExistingPack,
    confirmingExistingPack,
    readingActiveRecord,
    writingActiveRecord,
    committingActiveRecord,
    complete,
};

struct InstallProgress {
    InstallPhase phase{};
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
    std::size_t entriesCompleted{};
    std::size_t entryCount{};
    std::optional<ValidationPhase> validationPhase;
};

using InstallProgressCallback = std::function<void(const InstallProgress&)>;

class InstallCancelled final : public std::runtime_error {
public:
    InstallCancelled();
};

class InstallCommitUnknown final : public std::runtime_error {
public:
    explicit InstallCommitUnknown(ActiveRecord requestedActive);

    [[nodiscard]] const ActiveRecord& requestedActive() const noexcept {
        return requestedActive_;
    }

private:
    ActiveRecord requestedActive_;
};

struct InstallResult {
    Manifest manifest;
    crypto::Sha256Digest digest{};
    std::uint64_t size{};
    std::filesystem::path finalPath;
    ActiveRecord active;
    bool reusedExisting{};
    bool activeChanged{};
};

// Exactly one caller may mutate a contentRoot at a time. contentRoot, staging,
// packs and the active record must be private to that caller for the complete
// transaction. All child names are derived internally from a canonical UUID or
// a full SHA-256 digest; no archive-provided path is used on the host filesystem.
[[nodiscard]] InstallResult installPack(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& contentRoot,
    std::string_view transactionId,
    const InstallLimits& limits = {},
    std::stop_token stopToken = {},
    InstallProgressCallback progress = {});

namespace testing {

using ActiveCommitHook = void (*)(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& activePath,
    void* context);

using RetryDurabilityHook = void (*)(
    const std::filesystem::path& activePath,
    const std::filesystem::path& contentRoot,
    void* context);

struct InstallHooks {
    ActiveCommitHook commitActive{};
    RetryDurabilityHook retryDurability{};
    void* context{};
};

// Deterministic fault injection for transaction-boundary tests. Production
// callers must use installPack(). Hooks are passed directly; no global mutable
// test state is used.
[[nodiscard]] InstallResult installPackWithHooks(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& contentRoot,
    std::string_view transactionId,
    const InstallHooks& hooks,
    const InstallLimits& limits = {},
    std::stop_token stopToken = {},
    InstallProgressCallback progress = {});

} // namespace testing

} // namespace airfix::afpack
