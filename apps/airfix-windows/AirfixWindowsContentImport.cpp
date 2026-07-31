#include "AirfixWindowsContentImport.hpp"

#include "airfix/package/AfPackInstaller.hpp"
#include "airfix/package/AfPackRecovery.hpp"

#include <array>
#include <objbase.h>
#include <utility>
#include <windows.h>

namespace airfix::windows {
namespace {

[[nodiscard]] constexpr std::string_view
errorMessage(const AirfixWindowsContentImportErrorCategory category) noexcept {
  switch (category) {
  case AirfixWindowsContentImportErrorCategory::transactionUnavailable:
    return "private AFPACK import could not start";
  case AirfixWindowsContentImportErrorCategory::busy:
    return "another private AFPACK import is already in progress";
  case AirfixWindowsContentImportErrorCategory::cancelled:
    return "private AFPACK operation was cancelled";
  case AirfixWindowsContentImportErrorCategory::rejected:
    return "private AFPACK import was rejected; active content was not changed";
  case AirfixWindowsContentImportErrorCategory::commitUnknown:
    return "private AFPACK import commit is uncertain; restart and run "
           "--validate-installed-content";
  }
  return "private AFPACK import failed";
}

[[nodiscard]] constexpr AirfixWindowsContentOperationPhase
operationPhase(const airfix::afpack::InstallPhase phase) noexcept {
  using Input = airfix::afpack::InstallPhase;
  using Output = AirfixWindowsContentOperationPhase;
  switch (phase) {
  case Input::preparingDirectories:
  case Input::copyingSource:
    return Output::copying;
  case Input::hashingStagedPack:
  case Input::validatingStagedPack:
  case Input::confirmingStagedPack:
  case Input::publishingPack:
  case Input::checkingExistingPack:
  case Input::validatingExistingPack:
  case Input::confirmingExistingPack:
    return Output::authenticating;
  case Input::readingActiveRecord:
  case Input::writingActiveRecord:
  case Input::committingActiveRecord:
    return Output::activating;
  case Input::complete:
    return Output::complete;
  }
  return Output::authenticating;
}

[[nodiscard]] constexpr AirfixWindowsContentOperationPhase
operationPhase(const airfix::afpack::RecoveryPhase phase) noexcept {
  using Input = airfix::afpack::RecoveryPhase;
  using Output = AirfixWindowsContentOperationPhase;
  switch (phase) {
  case Input::readingActiveRecord:
  case Input::hashingCurrent:
  case Input::validatingCurrent:
  case Input::hashingPrevious:
  case Input::validatingPrevious:
    return Output::checking;
  case Input::verifyingRollbackPack:
  case Input::checkingStaleActive:
  case Input::writingRollbackRecord:
  case Input::checkingFinalStaleActive:
  case Input::committingRollbackRecord:
    return Output::restoring;
  case Input::complete:
    return Output::complete;
  }
  return Output::checking;
}

void report(const AirfixWindowsContentOperationProgressCallback &callback,
            const airfix::afpack::InstallProgress &progress) {
  if (callback) {
    callback({
        .phase = operationPhase(progress.phase),
        .completedBytes = progress.completedBytes,
        .totalBytes = progress.totalBytes,
    });
  }
}

void report(const AirfixWindowsContentOperationProgressCallback &callback,
            const airfix::afpack::RecoveryProgress &progress) {
  if (callback) {
    callback({
        .phase = operationPhase(progress.phase),
        .completedBytes = progress.completedBytes,
        .totalBytes = progress.totalBytes,
    });
  }
}

[[nodiscard]] constexpr AirfixWindowsInstalledContentStatus
installedStatus(const airfix::afpack::ActiveContentStatus status) noexcept {
  using Input = airfix::afpack::ActiveContentStatus;
  using Output = AirfixWindowsInstalledContentStatus;
  switch (status) {
  case Input::noContent:
    return Output::noContent;
  case Input::ready:
    return Output::ready;
  case Input::rollbackAvailable:
    return Output::rollbackAvailable;
  case Input::unusable:
  case Input::malformedActive:
    return Output::unusable;
  case Input::unavailable:
    return Output::unavailable;
  }
  return Output::unavailable;
}

class ContentImportMutex final {
public:
  ContentImportMutex() {
    handle_ = CreateMutexW(nullptr, FALSE,
                           L"Local\\AirfixDogfighter.ContentImport.v1");
    if (handle_ == nullptr) {
      throw AirfixWindowsContentImportError(
          AirfixWindowsContentImportErrorCategory::transactionUnavailable);
    }
    const DWORD wait = WaitForSingleObject(handle_, 0U);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
      acquired_ = true;
      return;
    }
    (void)CloseHandle(handle_);
    handle_ = nullptr;
    throw AirfixWindowsContentImportError(
        wait == WAIT_TIMEOUT
            ? AirfixWindowsContentImportErrorCategory::busy
            : AirfixWindowsContentImportErrorCategory::transactionUnavailable);
  }

  ~ContentImportMutex() {
    if (acquired_) {
      (void)ReleaseMutex(handle_);
    }
    if (handle_ != nullptr) {
      (void)CloseHandle(handle_);
    }
  }

  ContentImportMutex(const ContentImportMutex &) = delete;
  ContentImportMutex &operator=(const ContentImportMutex &) = delete;

private:
  HANDLE handle_{};
  bool acquired_{};
};

void appendHex(std::string &output, const std::uint64_t value,
               const std::size_t digitCount) {
  constexpr std::array<char, 16U> digits{
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
  };
  for (std::size_t offset = digitCount; offset > 0U; --offset) {
    const auto shift = static_cast<unsigned>((offset - 1U) * 4U);
    output.push_back(digits[static_cast<std::size_t>((value >> shift) & 0xFU)]);
  }
}

} // namespace

AirfixWindowsContentImportError::AirfixWindowsContentImportError(
    const AirfixWindowsContentImportErrorCategory category)
    : std::runtime_error(std::string(errorMessage(category))),
      category_(category) {}

std::string makeAirfixWindowsContentTransactionId() {
  GUID identifier{};
  if (FAILED(CoCreateGuid(&identifier))) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::transactionUnavailable);
  }

  std::string output;
  output.reserve(36U);
  appendHex(output, identifier.Data1, 8U);
  output.push_back('-');
  appendHex(output, identifier.Data2, 4U);
  output.push_back('-');
  appendHex(output, identifier.Data3, 4U);
  output.push_back('-');
  appendHex(output, identifier.Data4[0], 2U);
  appendHex(output, identifier.Data4[1], 2U);
  output.push_back('-');
  for (std::size_t index = 2U; index < std::size(identifier.Data4); ++index) {
    appendHex(output, identifier.Data4[index], 2U);
  }
  return output;
}

AirfixWindowsContentImportResult importAirfixWindowsContent(
    const std::filesystem::path &sourcePath,
    const std::filesystem::path &contentRoot, const std::stop_token stopToken,
    AirfixWindowsContentOperationProgressCallback progress) {
  const ContentImportMutex importMutex;
  const auto transactionId = makeAirfixWindowsContentTransactionId();
  return testing::importAirfixWindowsContentWithOperation(
      sourcePath, contentRoot, transactionId, stopToken, std::move(progress),
      [](const std::filesystem::path &source, const std::filesystem::path &root,
         const std::string_view transaction, const std::stop_token token,
         const AirfixWindowsContentOperationProgressCallback &callback) {
        const auto installed = airfix::afpack::installPack(
            source, root, transaction, {}, token,
            [&callback](const auto &value) { report(callback, value); });
        return AirfixWindowsContentImportResult{
            .generation = installed.active.generation,
            .size = installed.size,
            .reusedExisting = installed.reusedExisting,
            .activeChanged = installed.activeChanged,
        };
      });
}

AirfixWindowsInstalledContentStatus inspectAirfixWindowsContent(
    const std::filesystem::path &contentRoot, const std::stop_token stopToken,
    AirfixWindowsContentOperationProgressCallback progress) {
  try {
    const auto inspection = airfix::afpack::inspectActiveContent(
        contentRoot, {}, stopToken,
        [&progress](const auto &value) { report(progress, value); });
    return installedStatus(inspection.status());
  } catch (const airfix::afpack::RecoveryCancelled &) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::cancelled);
  } catch (const AirfixWindowsContentImportError &) {
    throw;
  } catch (...) {
    return AirfixWindowsInstalledContentStatus::unavailable;
  }
}

AirfixWindowsContentRollbackResult rollbackAirfixWindowsContent(
    const std::filesystem::path &contentRoot, const std::stop_token stopToken,
    AirfixWindowsContentOperationProgressCallback progress) {
  const ContentImportMutex importMutex;
  try {
    const auto inspection = airfix::afpack::inspectActiveContent(
        contentRoot, {}, stopToken,
        [&progress](const auto &value) { report(progress, value); });
    if (inspection.status() !=
        airfix::afpack::ActiveContentStatus::rollbackAvailable) {
      throw AirfixWindowsContentImportError(
          AirfixWindowsContentImportErrorCategory::rejected);
    }
    const auto transactionId = makeAirfixWindowsContentTransactionId();
    const auto restored = airfix::afpack::commitRollback(
        inspection, transactionId, {}, stopToken,
        [&progress](const auto &value) { report(progress, value); });
    return {.generation = restored.active.generation};
  } catch (const airfix::afpack::RecoveryCancelled &) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::cancelled);
  } catch (const airfix::afpack::InstallCommitUnknown &) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::commitUnknown);
  } catch (const AirfixWindowsContentImportError &) {
    throw;
  } catch (...) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::rejected);
  }
}

namespace testing {

AirfixWindowsContentImportResult importAirfixWindowsContentWithOperation(
    const std::filesystem::path &sourcePath,
    const std::filesystem::path &contentRoot,
    const std::string_view transactionId, const std::stop_token stopToken,
    AirfixWindowsContentOperationProgressCallback progress,
    const AirfixWindowsContentInstallOperation &operation) {
  if (!operation) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::transactionUnavailable);
  }
  try {
    return operation(sourcePath, contentRoot, transactionId, stopToken,
                     progress);
  } catch (const airfix::afpack::InstallCancelled &) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::cancelled);
  } catch (const airfix::afpack::InstallCommitUnknown &) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::commitUnknown);
  } catch (const AirfixWindowsContentImportError &) {
    throw;
  } catch (...) {
    // AfPackInstaller diagnostics are intentionally useful to trusted tools,
    // but some include host paths. The product boundary exposes only this
    // fixed category and keeps the previous active generation untouched.
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::rejected);
  }
}

} // namespace testing

} // namespace airfix::windows
