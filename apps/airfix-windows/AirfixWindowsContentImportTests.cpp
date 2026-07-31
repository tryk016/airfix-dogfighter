#include "AirfixWindowsContentImport.hpp"

#include "airfix/package/AfPackActiveRecord.hpp"
#include "airfix/package/AfPackInstaller.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace {

using airfix::windows::AirfixWindowsContentImportError;
using airfix::windows::AirfixWindowsContentImportErrorCategory;
using airfix::windows::AirfixWindowsContentImportResult;
using airfix::windows::makeAirfixWindowsContentTransactionId;
using airfix::windows::testing::importAirfixWindowsContentWithOperation;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] bool isCanonicalUuid(const std::string_view value) {
  if (value.size() != 36U) {
    return false;
  }
  for (std::size_t index = 0U; index < value.size(); ++index) {
    const bool separator =
        index == 8U || index == 13U || index == 18U || index == 23U;
    if (separator) {
      if (value[index] != '-') {
        return false;
      }
    } else if (!((value[index] >= '0' && value[index] <= '9') ||
                 (value[index] >= 'a' && value[index] <= 'f'))) {
      return false;
    }
  }
  return true;
}

void transactionIdentifiersAreCanonicalAndDistinct() {
  std::array<std::string, 32U> identifiers{};
  for (auto &identifier : identifiers) {
    identifier = makeAirfixWindowsContentTransactionId();
    require(isCanonicalUuid(identifier),
            "Windows transaction identifier is not a canonical UUID");
  }
  for (std::size_t left = 0U; left < identifiers.size(); ++left) {
    for (std::size_t right = left + 1U; right < identifiers.size(); ++right) {
      require(identifiers[left] != identifiers[right],
              "Windows transaction identifiers collided");
    }
  }
}

void operationReceivesExactPrivateInputs() {
  const std::filesystem::path source = "owner-private.afpack";
  const std::filesystem::path root = "application-private-content";
  constexpr std::string_view transaction =
      "01234567-89ab-cdef-0123-456789abcdef";
  constexpr AirfixWindowsContentImportResult expected{
      .generation = 7U,
      .size = 123456U,
      .reusedExisting = true,
      .activeChanged = false,
  };
  bool called = false;
  bool progressCalled = false;
  std::stop_source stopSource;
  const auto result = importAirfixWindowsContentWithOperation(
      source, root, transaction, stopSource.get_token(),
      [&progressCalled](const auto &) { progressCalled = true; },
      [&](const std::filesystem::path &receivedSource,
          const std::filesystem::path &receivedRoot,
          const std::string_view receivedTransaction,
          const std::stop_token receivedStopToken, const auto &progress) {
        called = true;
        require(receivedSource == source, "private source path changed");
        require(receivedRoot == root, "private content root changed");
        require(receivedTransaction == transaction,
                "transaction identifier changed");
        require(receivedStopToken.stop_possible(), "stop token changed");
        progress({});
        return expected;
      });
  require(called && progressCalled && result == expected,
          "successful import operation result changed");
}

void privateDiagnosticsAreRedacted() {
  constexpr std::string_view transaction =
      "01234567-89ab-cdef-0123-456789abcdef";
  constexpr std::string_view privateMarker = "PRIVATE-OWNER-PATH";
  try {
    (void)importAirfixWindowsContentWithOperation(
        std::filesystem::path(privateMarker), "content", transaction, {}, {},
        [](const std::filesystem::path &, const std::filesystem::path &,
           std::string_view, std::stop_token,
           const auto &) -> AirfixWindowsContentImportResult {
          throw std::runtime_error(
              "cannot open C:/PRIVATE-OWNER-PATH/source.afpack");
        });
  } catch (const AirfixWindowsContentImportError &error) {
    require(error.category() ==
                AirfixWindowsContentImportErrorCategory::rejected,
            "ordinary installer rejection had the wrong category");
    require(std::string_view(error.what()).find(privateMarker) ==
                std::string_view::npos,
            "private installer diagnostic crossed the product boundary");
    return;
  }
  throw std::runtime_error("installer rejection was not mapped");
}

void ambiguousCommitIsActionableAndRedacted() {
  constexpr std::string_view transaction =
      "01234567-89ab-cdef-0123-456789abcdef";
  try {
    (void)importAirfixWindowsContentWithOperation(
        "owner.afpack", "content", transaction, {}, {},
        [](const std::filesystem::path &, const std::filesystem::path &,
           std::string_view, std::stop_token,
           const auto &) -> AirfixWindowsContentImportResult {
          throw airfix::afpack::InstallCommitUnknown(
              airfix::afpack::ActiveRecord{});
        });
  } catch (const AirfixWindowsContentImportError &error) {
    require(error.category() ==
                AirfixWindowsContentImportErrorCategory::commitUnknown,
            "ambiguous commit had the wrong category");
    require(
        std::string_view(error.what()).find("--validate-installed-content") !=
            std::string_view::npos,
        "ambiguous commit did not provide a safe recovery action");
    return;
  }
  throw std::runtime_error("ambiguous commit was not mapped");
}

void cancellationHasItsOwnPathFreeCategory() {
  try {
    (void)importAirfixWindowsContentWithOperation(
        "owner.afpack", "content", "01234567-89ab-cdef-0123-456789abcdef", {},
        {},
        [](const std::filesystem::path &, const std::filesystem::path &,
           std::string_view, std::stop_token,
           const auto &) -> AirfixWindowsContentImportResult {
          throw airfix::afpack::InstallCancelled{};
        });
  } catch (const AirfixWindowsContentImportError &error) {
    require(error.category() ==
                AirfixWindowsContentImportErrorCategory::cancelled,
            "cancelled import had the wrong category");
    require(std::string_view(error.what()).find("owner") ==
                std::string_view::npos,
            "cancelled import exposed a private marker");
    return;
  }
  throw std::runtime_error("cancelled import was not mapped");
}

void missingOperationFailsClosed() {
  try {
    (void)importAirfixWindowsContentWithOperation(
        "owner.afpack", "content", "01234567-89ab-cdef-0123-456789abcdef", {},
        {}, {});
  } catch (const AirfixWindowsContentImportError &error) {
    require(error.category() ==
                AirfixWindowsContentImportErrorCategory::transactionUnavailable,
            "missing operation had the wrong category");
    return;
  }
  throw std::runtime_error("missing install operation was accepted");
}

} // namespace

int main() {
  try {
    transactionIdentifiersAreCanonicalAndDistinct();
    operationReceivesExactPrivateInputs();
    privateDiagnosticsAreRedacted();
    ambiguousCommitIsActionableAndRedacted();
    cancellationHasItsOwnPathFreeCategory();
    missingOperationFailsClosed();
    std::cout << "Airfix Windows content import tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Airfix Windows content import tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
