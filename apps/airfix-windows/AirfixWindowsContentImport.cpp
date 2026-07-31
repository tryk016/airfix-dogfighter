#include "AirfixWindowsContentImport.hpp"

#include "airfix/package/AfPackInstaller.hpp"

#include <array>
#include <objbase.h>
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
  case AirfixWindowsContentImportErrorCategory::rejected:
    return "private AFPACK import was rejected; active content was not changed";
  case AirfixWindowsContentImportErrorCategory::commitUnknown:
    return "private AFPACK import commit is uncertain; restart and run "
           "--validate-installed-content";
  }
  return "private AFPACK import failed";
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

AirfixWindowsContentImportResult
importAirfixWindowsContent(const std::filesystem::path &sourcePath,
                           const std::filesystem::path &contentRoot) {
  const ContentImportMutex importMutex;
  const auto transactionId = makeAirfixWindowsContentTransactionId();
  return testing::importAirfixWindowsContentWithOperation(
      sourcePath, contentRoot, transactionId,
      [](const std::filesystem::path &source, const std::filesystem::path &root,
         const std::string_view transaction) {
        const auto installed =
            airfix::afpack::installPack(source, root, transaction);
        return AirfixWindowsContentImportResult{
            .generation = installed.active.generation,
            .size = installed.size,
            .reusedExisting = installed.reusedExisting,
            .activeChanged = installed.activeChanged,
        };
      });
}

namespace testing {

AirfixWindowsContentImportResult importAirfixWindowsContentWithOperation(
    const std::filesystem::path &sourcePath,
    const std::filesystem::path &contentRoot,
    const std::string_view transactionId,
    const AirfixWindowsContentInstallOperation &operation) {
  if (!operation) {
    throw AirfixWindowsContentImportError(
        AirfixWindowsContentImportErrorCategory::transactionUnavailable);
  }
  try {
    return operation(sourcePath, contentRoot, transactionId);
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
