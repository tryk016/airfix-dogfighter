#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
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
  rejected,
  commitUnknown,
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
[[nodiscard]] AirfixWindowsContentImportResult
importAirfixWindowsContent(const std::filesystem::path &sourcePath,
                           const std::filesystem::path &contentRoot);

namespace testing {

using AirfixWindowsContentInstallOperation =
    std::function<AirfixWindowsContentImportResult(
        const std::filesystem::path &, const std::filesystem::path &,
        std::string_view)>;

// Injects only the final installer operation. Production callers must use
// importAirfixWindowsContent().
[[nodiscard]] AirfixWindowsContentImportResult
importAirfixWindowsContentWithOperation(
    const std::filesystem::path &sourcePath,
    const std::filesystem::path &contentRoot, std::string_view transactionId,
    const AirfixWindowsContentInstallOperation &operation);

} // namespace testing

} // namespace airfix::windows
