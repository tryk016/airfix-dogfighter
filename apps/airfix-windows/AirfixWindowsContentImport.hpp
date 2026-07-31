#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace airfix::windows {

struct AirfixWindowsContentImportResult final {
  std::uint64_t generation{};
  std::uint64_t size{};
  bool reusedExisting{};
  bool activeChanged{};

  [[nodiscard]] friend bool
  operator==(const AirfixWindowsContentImportResult &,
             const AirfixWindowsContentImportResult &) = default;
};

enum class AirfixWindowsContentImportErrorCategory : std::uint8_t {
  transactionUnavailable,
  busy,
  cancelled,
  rejected,
  commitUnknown,
};

enum class AirfixWindowsInstalledContentStatus : std::uint8_t {
  noContent,
  ready,
  rollbackAvailable,
  unusable,
  unavailable,
};

enum class AirfixWindowsContentOperationPhase : std::uint8_t {
  checking,
  copying,
  authenticating,
  activating,
  restoring,
  complete,
};

struct AirfixWindowsContentOperationProgress final {
  AirfixWindowsContentOperationPhase phase{
      AirfixWindowsContentOperationPhase::checking};
  std::uint64_t completedBytes{};
  std::uint64_t totalBytes{};

  [[nodiscard]] friend bool
  operator==(const AirfixWindowsContentOperationProgress &,
             const AirfixWindowsContentOperationProgress &) = default;
};

using AirfixWindowsContentOperationProgressCallback =
    std::function<void(const AirfixWindowsContentOperationProgress &)>;

struct AirfixWindowsContentRollbackResult final {
  std::uint64_t generation{};

  [[nodiscard]] friend bool
  operator==(const AirfixWindowsContentRollbackResult &,
             const AirfixWindowsContentRollbackResult &) = default;
};

class AirfixWindowsContentImportError final : public std::runtime_error {
public:
  explicit AirfixWindowsContentImportError(
      AirfixWindowsContentImportErrorCategory category);

  [[nodiscard]] AirfixWindowsContentImportErrorCategory
  category() const noexcept {
    return category_;
  }

private:
  AirfixWindowsContentImportErrorCategory category_;
};

// Creates a canonical lowercase UUID without embedding a path, timestamp, or
// machine identifier in the transaction name.
[[nodiscard]] std::string makeAirfixWindowsContentTransactionId();

// Copies, authenticates, and atomically activates an owner-selected package in
// the app-private content root. Underlying filesystem diagnostics are never
// allowed across this product boundary because they can contain private paths.
[[nodiscard]] AirfixWindowsContentImportResult importAirfixWindowsContent(
    const std::filesystem::path &sourcePath,
    const std::filesystem::path &contentRoot, std::stop_token stopToken = {},
    AirfixWindowsContentOperationProgressCallback progress = {});

// Inspects only the app-private root and reduces every path-bearing recovery
// diagnostic to a small product status. A valid previous generation is
// reported separately so the UI can offer an explicit rollback.
[[nodiscard]] AirfixWindowsInstalledContentStatus inspectAirfixWindowsContent(
    const std::filesystem::path &contentRoot, std::stop_token stopToken = {},
    AirfixWindowsContentOperationProgressCallback progress = {});

// Re-inspects and commits only an authenticated rollback generation while
// holding the same per-user writer mutex as import. The source paths,
// checksums, and active-record details never cross this boundary.
[[nodiscard]] AirfixWindowsContentRollbackResult rollbackAirfixWindowsContent(
    const std::filesystem::path &contentRoot, std::stop_token stopToken = {},
    AirfixWindowsContentOperationProgressCallback progress = {});

namespace testing {

using AirfixWindowsContentInstallOperation =
    std::function<AirfixWindowsContentImportResult(
        const std::filesystem::path &, const std::filesystem::path &,
        std::string_view, std::stop_token,
        const AirfixWindowsContentOperationProgressCallback &)>;

// Injects only the final installer operation. Production callers must use
// importAirfixWindowsContent().
[[nodiscard]] AirfixWindowsContentImportResult
importAirfixWindowsContentWithOperation(
    const std::filesystem::path &sourcePath,
    const std::filesystem::path &contentRoot, std::string_view transactionId,
    std::stop_token stopToken,
    AirfixWindowsContentOperationProgressCallback progress,
    const AirfixWindowsContentInstallOperation &operation);

} // namespace testing

} // namespace airfix::windows
