#include "AirfixWindowsContentBootstrap.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

using airfix::windows::AirfixWindowsContentBootstrapAction;
using airfix::windows::AirfixWindowsContentBootstrapNotice;
using airfix::windows::AirfixWindowsContentBootstrapResult;
using airfix::windows::AirfixWindowsContentImportError;
using airfix::windows::AirfixWindowsContentImportErrorCategory;
using airfix::windows::AirfixWindowsInstalledContentStatus;
using airfix::windows::testing::AirfixWindowsContentBootstrapCallbacks;
using airfix::windows::testing::nativeAirfixWindowsContentUiAvailable;
using airfix::windows::testing::runAirfixWindowsContentBootstrapWithCallbacks;

void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct Fixture final {
  std::vector<AirfixWindowsInstalledContentStatus> inspections{
      AirfixWindowsInstalledContentStatus::noContent};
  std::vector<AirfixWindowsContentBootstrapAction> actions{
      AirfixWindowsContentBootstrapAction::close};
  std::optional<std::filesystem::path> pickedPackage;
  std::vector<AirfixWindowsContentBootstrapNotice> notices;
  std::size_t inspectionIndex{};
  std::size_t actionIndex{};
  std::size_t pickCount{};
  std::size_t importCount{};
  std::size_t rollbackCount{};
  std::optional<std::filesystem::path> importedPackage;
  std::optional<AirfixWindowsContentImportErrorCategory> importError;
  std::optional<AirfixWindowsContentImportErrorCategory> rollbackError;

  [[nodiscard]] AirfixWindowsContentBootstrapCallbacks callbacks() {
    return {
        .inspect =
            [this] {
              const auto index =
                  std::min(inspectionIndex, inspections.size() - 1U);
              ++inspectionIndex;
              return inspections[index];
            },
        .chooseAction =
            [this](const auto) {
              if (actionIndex >= actions.size()) {
                return AirfixWindowsContentBootstrapAction::close;
              }
              return actions[actionIndex++];
            },
        .pickPackage =
            [this] {
              ++pickCount;
              return pickedPackage;
            },
        .importPackage =
            [this](const std::filesystem::path &source) {
              ++importCount;
              importedPackage = source;
              if (importError.has_value()) {
                throw AirfixWindowsContentImportError(*importError);
              }
            },
        .rollback =
            [this] {
              ++rollbackCount;
              if (rollbackError.has_value()) {
                throw AirfixWindowsContentImportError(*rollbackError);
              }
            },
        .notify = [this](const auto notice) { notices.push_back(notice); },
    };
  }
};

void readyContentCanCloseWithoutMutation() {
  Fixture fixture;
  fixture.inspections = {AirfixWindowsInstalledContentStatus::ready};
  const auto result =
      runAirfixWindowsContentBootstrapWithCallbacks(fixture.callbacks());
  require(result == AirfixWindowsContentBootstrapResult::closedWithReadyContent,
          "ready content did not close ready");
  require(fixture.inspectionIndex == 1U && fixture.pickCount == 0U &&
              fixture.importCount == 0U && fixture.rollbackCount == 0U &&
              fixture.notices.empty(),
          "ready close performed an operation");
}

void pickerCancellationReturnsToTheSameState() {
  Fixture fixture;
  fixture.actions = {AirfixWindowsContentBootstrapAction::importPackage,
                     AirfixWindowsContentBootstrapAction::close};
  const auto result =
      runAirfixWindowsContentBootstrapWithCallbacks(fixture.callbacks());
  require(result ==
              AirfixWindowsContentBootstrapResult::closedWithoutReadyContent,
          "picker cancellation changed readiness");
  require(fixture.inspectionIndex == 1U && fixture.pickCount == 1U &&
              fixture.importCount == 0U && fixture.notices.empty(),
          "picker cancellation re-inspected or imported");
}

void successfulImportIsReinspectedBeforeReady() {
  Fixture fixture;
  fixture.inspections = {AirfixWindowsInstalledContentStatus::noContent,
                         AirfixWindowsInstalledContentStatus::ready};
  fixture.actions = {AirfixWindowsContentBootstrapAction::importPackage,
                     AirfixWindowsContentBootstrapAction::close};
  fixture.pickedPackage = std::filesystem::path{L"synthetic-owner.afpack"};
  const auto result =
      runAirfixWindowsContentBootstrapWithCallbacks(fixture.callbacks());
  require(result == AirfixWindowsContentBootstrapResult::closedWithReadyContent,
          "successful import did not close ready");
  require(fixture.inspectionIndex == 2U && fixture.pickCount == 1U &&
              fixture.importCount == 1U &&
              fixture.importedPackage == fixture.pickedPackage,
          "successful import handoff changed");
  require(fixture.notices ==
              std::vector{AirfixWindowsContentBootstrapNotice::imported},
          "successful import notice changed");
}

void rollbackRequiresVerifiedAvailabilityAndReinspection() {
  Fixture fixture;
  fixture.inspections = {AirfixWindowsInstalledContentStatus::rollbackAvailable,
                         AirfixWindowsInstalledContentStatus::ready};
  fixture.actions = {AirfixWindowsContentBootstrapAction::rollback,
                     AirfixWindowsContentBootstrapAction::close};
  const auto result =
      runAirfixWindowsContentBootstrapWithCallbacks(fixture.callbacks());
  require(result ==
                  AirfixWindowsContentBootstrapResult::closedWithReadyContent &&
              fixture.rollbackCount == 1U && fixture.inspectionIndex == 2U,
          "verified rollback flow changed");
  require(fixture.notices ==
              std::vector{AirfixWindowsContentBootstrapNotice::restored},
          "rollback notice changed");

  Fixture declined;
  declined.inspections = {
      AirfixWindowsInstalledContentStatus::rollbackAvailable};
  require(
      runAirfixWindowsContentBootstrapWithCallbacks(declined.callbacks()) ==
              AirfixWindowsContentBootstrapResult::closedWithoutReadyContent &&
          declined.rollbackCount == 0U,
      "verified previous generation was treated as active-ready content");

  for (const auto blockedStatus :
       {AirfixWindowsInstalledContentStatus::rollbackAvailable,
        AirfixWindowsInstalledContentStatus::unavailable}) {
    Fixture blockedImport;
    blockedImport.inspections = {blockedStatus};
    blockedImport.actions = {
        AirfixWindowsContentBootstrapAction::importPackage};
    blockedImport.pickedPackage = std::filesystem::path{L"blocked.afpack"};
    require(runAirfixWindowsContentBootstrapWithCallbacks(
                blockedImport.callbacks()) ==
                    AirfixWindowsContentBootstrapResult::failed &&
                blockedImport.pickCount == 0U &&
                blockedImport.importCount == 0U,
            "uncertain active record admitted a replacement import");
  }

  Fixture invalid;
  invalid.inspections = {AirfixWindowsInstalledContentStatus::ready};
  invalid.actions = {AirfixWindowsContentBootstrapAction::rollback};
  require(runAirfixWindowsContentBootstrapWithCallbacks(invalid.callbacks()) ==
                  AirfixWindowsContentBootstrapResult::failed &&
              invalid.rollbackCount == 0U,
          "unavailable rollback was executed");
}

void typedFailuresAreRedactedAndReinspected() {
  constexpr std::array categories{
      AirfixWindowsContentImportErrorCategory::transactionUnavailable,
      AirfixWindowsContentImportErrorCategory::busy,
      AirfixWindowsContentImportErrorCategory::cancelled,
      AirfixWindowsContentImportErrorCategory::rejected,
      AirfixWindowsContentImportErrorCategory::commitUnknown,
  };
  constexpr std::array expected{
      AirfixWindowsContentBootstrapNotice::unavailable,
      AirfixWindowsContentBootstrapNotice::busy,
      AirfixWindowsContentBootstrapNotice::cancelled,
      AirfixWindowsContentBootstrapNotice::rejected,
      AirfixWindowsContentBootstrapNotice::commitUnknown,
  };
  for (std::size_t index = 0U; index < categories.size(); ++index) {
    Fixture fixture;
    fixture.inspections = {AirfixWindowsInstalledContentStatus::noContent,
                           AirfixWindowsInstalledContentStatus::noContent};
    fixture.actions = {AirfixWindowsContentBootstrapAction::importPackage,
                       AirfixWindowsContentBootstrapAction::close};
    fixture.pickedPackage = std::filesystem::path{L"synthetic.afpack"};
    fixture.importError = categories[index];
    const auto result =
        runAirfixWindowsContentBootstrapWithCallbacks(fixture.callbacks());
    const bool commitUnknown =
        categories[index] ==
        AirfixWindowsContentImportErrorCategory::commitUnknown;
    require(result == (commitUnknown
                           ? AirfixWindowsContentBootstrapResult::failed
                           : AirfixWindowsContentBootstrapResult::
                                 closedWithoutReadyContent) &&
                fixture.importCount == 1U &&
                fixture.inspectionIndex == (commitUnknown ? 1U : 2U) &&
                fixture.notices ==
                    std::vector<AirfixWindowsContentBootstrapNotice>{
                        expected[index]},
            "typed import failure was not safely reduced");
  }
}

void rollbackFailuresRespectTheCommitBoundary() {
  Fixture cancelled;
  cancelled.inspections = {
      AirfixWindowsInstalledContentStatus::rollbackAvailable,
      AirfixWindowsInstalledContentStatus::rollbackAvailable};
  cancelled.actions = {AirfixWindowsContentBootstrapAction::rollback,
                       AirfixWindowsContentBootstrapAction::close};
  cancelled.rollbackError = AirfixWindowsContentImportErrorCategory::cancelled;
  require(
      runAirfixWindowsContentBootstrapWithCallbacks(cancelled.callbacks()) ==
              AirfixWindowsContentBootstrapResult::closedWithoutReadyContent &&
          cancelled.inspectionIndex == 2U &&
          cancelled.notices ==
              std::vector{AirfixWindowsContentBootstrapNotice::cancelled},
      "ordinary rollback failure was not reinspected");

  Fixture unknown;
  unknown.inspections = {
      AirfixWindowsInstalledContentStatus::rollbackAvailable};
  unknown.actions = {AirfixWindowsContentBootstrapAction::rollback};
  unknown.rollbackError =
      AirfixWindowsContentImportErrorCategory::commitUnknown;
  require(
      runAirfixWindowsContentBootstrapWithCallbacks(unknown.callbacks()) ==
              AirfixWindowsContentBootstrapResult::failed &&
          unknown.inspectionIndex == 1U && unknown.rollbackCount == 1U &&
          unknown.notices ==
              std::vector{AirfixWindowsContentBootstrapNotice::commitUnknown},
      "ambiguous rollback commit did not terminate the manager");
}

void retryLoopIsBounded() {
  AirfixWindowsContentBootstrapCallbacks callbacks{
      .inspect = [] { return AirfixWindowsInstalledContentStatus::noContent; },
      .chooseAction =
          [](const auto) { return AirfixWindowsContentBootstrapAction::retry; },
      .pickPackage = [] { return std::optional<std::filesystem::path>{}; },
      .importPackage = [](const auto &) {},
      .rollback = [] {},
      .notify = [](const auto) {},
  };
  require(runAirfixWindowsContentBootstrapWithCallbacks(callbacks) ==
              AirfixWindowsContentBootstrapResult::failed,
          "unbounded presenter was not stopped");
}

void incompleteCallbackSetFailsClosed() {
  AirfixWindowsContentBootstrapCallbacks callbacks;
  require(runAirfixWindowsContentBootstrapWithCallbacks(callbacks) ==
              AirfixWindowsContentBootstrapResult::failed,
          "incomplete callbacks were accepted");
}

} // namespace

int main() {
  try {
    require(nativeAirfixWindowsContentUiAvailable(),
            "native content UI prerequisites are unavailable");
    readyContentCanCloseWithoutMutation();
    pickerCancellationReturnsToTheSameState();
    successfulImportIsReinspectedBeforeReady();
    rollbackRequiresVerifiedAvailabilityAndReinspection();
    typedFailuresAreRedactedAndReinspected();
    rollbackFailuresRespectTheCommitBoundary();
    retryLoopIsBounded();
    incompleteCallbackSetFailsClosed();
    std::cout << "Windows private-content bootstrap tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Windows private-content bootstrap test failure: "
              << error.what() << '\n';
    return 1;
  }
}
