#include "AirfixWindowsContentBootstrap.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cstdint>
#include <cwchar>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace airfix::windows {
namespace {

constexpr std::size_t maximumCoordinatorInteractions = 64U;
constexpr int importButtonId = 1001;
constexpr int rollbackButtonId = 1002;
constexpr int retryButtonId = 1003;

using TaskDialogIndirectFunction = HRESULT(WINAPI *)(const TASKDIALOGCONFIG *,
                                                     int *, int *, BOOL *);

[[nodiscard]] HRESULT showTaskDialog(const TASKDIALOGCONFIG &configuration,
                                     int *selectedButton) noexcept {
  const HMODULE library = LoadLibraryW(L"comctl32.dll");
  if (library == nullptr) {
    return HRESULT_FROM_WIN32(GetLastError());
  }
  const auto function = reinterpret_cast<TaskDialogIndirectFunction>(
      GetProcAddress(library, "TaskDialogIndirect"));
  if (function == nullptr) {
    const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
    FreeLibrary(library);
    return result;
  }
  const HRESULT result =
      function(&configuration, selectedButton, nullptr, nullptr);
  FreeLibrary(library);
  return result;
}

template <typename Interface> class ComPtr final {
public:
  ComPtr() = default;
  ~ComPtr() {
    if (value_ != nullptr) {
      value_->Release();
    }
  }

  ComPtr(const ComPtr &) = delete;
  ComPtr &operator=(const ComPtr &) = delete;

  [[nodiscard]] Interface *get() const noexcept { return value_; }
  [[nodiscard]] Interface **put() noexcept { return &value_; }
  [[nodiscard]] Interface *operator->() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return value_ != nullptr;
  }

private:
  Interface *value_{};
};

class ComApartment final {
public:
  ComApartment() {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                       COINIT_DISABLE_OLE1DDE);
    if (FAILED(result)) {
      throw std::runtime_error("private content UI is unavailable");
    }
    initialized_ = true;
  }

  ~ComApartment() {
    if (initialized_) {
      CoUninitialize();
    }
  }

  ComApartment(const ComApartment &) = delete;
  ComApartment &operator=(const ComApartment &) = delete;

private:
  bool initialized_{};
};

[[nodiscard]] constexpr bool
contentReady(const AirfixWindowsInstalledContentStatus status) noexcept {
  return status == AirfixWindowsInstalledContentStatus::ready;
}

[[nodiscard]] constexpr AirfixWindowsContentBootstrapNotice
noticeFor(const AirfixWindowsContentImportErrorCategory category) noexcept {
  switch (category) {
  case AirfixWindowsContentImportErrorCategory::transactionUnavailable:
    return AirfixWindowsContentBootstrapNotice::unavailable;
  case AirfixWindowsContentImportErrorCategory::busy:
    return AirfixWindowsContentBootstrapNotice::busy;
  case AirfixWindowsContentImportErrorCategory::cancelled:
    return AirfixWindowsContentBootstrapNotice::cancelled;
  case AirfixWindowsContentImportErrorCategory::rejected:
    return AirfixWindowsContentBootstrapNotice::rejected;
  case AirfixWindowsContentImportErrorCategory::commitUnknown:
    return AirfixWindowsContentBootstrapNotice::commitUnknown;
  }
  return AirfixWindowsContentBootstrapNotice::unavailable;
}

[[nodiscard]] AirfixWindowsContentBootstrapAction
showStatusDialog(const AirfixWindowsInstalledContentStatus status) {
  constexpr TASKDIALOG_BUTTON readyButtons[]{{
      importButtonId,
      L"Import a replacement package",
  }};
  constexpr TASKDIALOG_BUTTON rollbackButtons[]{
      {rollbackButtonId, L"Restore the previous verified package"},
      {retryButtonId, L"Check installed content again"},
  };
  constexpr TASKDIALOG_BUTTON importButtons[]{
      {importButtonId, L"Import an AFPACK"},
      {retryButtonId, L"Check installed content again"},
  };
  constexpr TASKDIALOG_BUTTON retryButtons[]{
      {retryButtonId, L"Check installed content again"},
  };

  TASKDIALOGCONFIG configuration{};
  configuration.cbSize = sizeof(configuration);
  configuration.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
  configuration.dwCommonButtons = TDCBF_CLOSE_BUTTON;
  configuration.pszWindowTitle = L"Airfix Dogfighter private content";
  configuration.pszMainIcon = TD_INFORMATION_ICON;

  switch (status) {
  case AirfixWindowsInstalledContentStatus::ready:
    configuration.pszMainInstruction = L"Private content is ready";
    configuration.pszContent =
        L"The active AFPACK passed authentication. You can keep it or import "
        L"a replacement before starting the game.";
    configuration.pButtons = readyButtons;
    configuration.cButtons = static_cast<UINT>(std::size(readyButtons));
    break;
  case AirfixWindowsInstalledContentStatus::rollbackAvailable:
    configuration.pszMainIcon = TD_WARNING_ICON;
    configuration.pszMainInstruction = L"Installed content needs recovery";
    configuration.pszContent =
        L"The active AFPACK did not pass authentication. A previous verified "
        L"generation must be restored first. You can then import a "
        L"replacement. "
        L"Closing now leaves private content unavailable.";
    configuration.pButtons = rollbackButtons;
    configuration.cButtons = static_cast<UINT>(std::size(rollbackButtons));
    break;
  case AirfixWindowsInstalledContentStatus::noContent:
    configuration.pszMainInstruction = L"No private content is installed";
    configuration.pszContent =
        L"Choose an owner-created AFPACK. Original and converted game data "
        L"remain outside the application and repository.";
    configuration.pButtons = importButtons;
    configuration.cButtons = static_cast<UINT>(std::size(importButtons));
    break;
  case AirfixWindowsInstalledContentStatus::unusable:
    configuration.pszMainIcon = TD_WARNING_ICON;
    configuration.pszMainInstruction = L"Installed content is not usable";
    configuration.pszContent =
        L"The active package did not pass authentication. Import a valid "
        L"AFPACK or check again after resolving the storage problem.";
    configuration.pButtons = importButtons;
    configuration.cButtons = static_cast<UINT>(std::size(importButtons));
    break;
  case AirfixWindowsInstalledContentStatus::unavailable:
    configuration.pszMainIcon = TD_WARNING_ICON;
    configuration.pszMainInstruction = L"Private content is unavailable";
    configuration.pszContent =
        L"The private content store could not be checked. Retry after "
        L"resolving the storage problem; no package will be replaced while "
        L"the active record is uncertain.";
    configuration.pButtons = retryButtons;
    configuration.cButtons = static_cast<UINT>(std::size(retryButtons));
    break;
  }

  int selectedButton{};
  if (FAILED(showTaskDialog(configuration, &selectedButton))) {
    throw std::runtime_error("private content status UI is unavailable");
  }
  switch (selectedButton) {
  case importButtonId:
    return AirfixWindowsContentBootstrapAction::importPackage;
  case rollbackButtonId:
    return AirfixWindowsContentBootstrapAction::rollback;
  case retryButtonId:
    return AirfixWindowsContentBootstrapAction::retry;
  default:
    return AirfixWindowsContentBootstrapAction::close;
  }
}

void showNotice(const AirfixWindowsContentBootstrapNotice notice) noexcept {
  PCWSTR instruction = L"Private content operation finished";
  PCWSTR content = L"The installed-content state will be checked again.";
  PCWSTR icon = TD_INFORMATION_ICON;
  switch (notice) {
  case AirfixWindowsContentBootstrapNotice::imported:
    instruction = L"AFPACK imported successfully";
    content = L"The authenticated package is now active.";
    break;
  case AirfixWindowsContentBootstrapNotice::restored:
    instruction = L"Previous AFPACK restored successfully";
    content = L"The verified previous package is now active.";
    break;
  case AirfixWindowsContentBootstrapNotice::cancelled:
    instruction = L"Operation cancelled";
    content = L"Installed content will be checked before another action.";
    icon = TD_WARNING_ICON;
    break;
  case AirfixWindowsContentBootstrapNotice::rejected:
    instruction = L"Package operation was rejected";
    content =
        L"The package or private store was not valid. The previous active "
        L"generation was retained when its state was known.";
    icon = TD_ERROR_ICON;
    break;
  case AirfixWindowsContentBootstrapNotice::commitUnknown:
    instruction = L"The final commit state is uncertain";
    content = L"Restart the application and check installed content before "
              L"trying another import.";
    icon = TD_WARNING_ICON;
    break;
  case AirfixWindowsContentBootstrapNotice::busy:
    instruction = L"Another content operation is running";
    content = L"Wait for it to finish, then check installed content again.";
    icon = TD_WARNING_ICON;
    break;
  case AirfixWindowsContentBootstrapNotice::unavailable:
    instruction = L"Private content operation is unavailable";
    content = L"No private filesystem path or package detail was exposed.";
    icon = TD_ERROR_ICON;
    break;
  }
  TASKDIALOGCONFIG configuration{};
  configuration.cbSize = sizeof(configuration);
  configuration.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
  configuration.dwCommonButtons = TDCBF_OK_BUTTON;
  configuration.pszWindowTitle = L"Airfix Dogfighter private content";
  configuration.pszMainIcon = icon;
  configuration.pszMainInstruction = instruction;
  configuration.pszContent = content;
  int selected{};
  (void)showTaskDialog(configuration, &selected);
}

[[nodiscard]] std::optional<std::filesystem::path> pickAfPack() {
  ComPtr<IFileOpenDialog> dialog;
  const HRESULT created =
      CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(dialog.put()));
  if (FAILED(created)) {
    throw std::runtime_error("private content picker is unavailable");
  }

  DWORD options{};
  if (FAILED(dialog->GetOptions(&options)) ||
      FAILED(dialog->SetOptions(
          options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
          FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | FOS_NODEREFERENCELINKS))) {
    throw std::runtime_error("private content picker is unavailable");
  }
  constexpr COMDLG_FILTERSPEC filter{L"Airfix Dogfighter package (*.afpack)",
                                     L"*.afpack"};
  if (FAILED(dialog->SetFileTypes(1U, &filter)) ||
      FAILED(dialog->SetFileTypeIndex(1U)) ||
      FAILED(dialog->SetDefaultExtension(L"afpack")) ||
      FAILED(dialog->SetTitle(L"Choose a private Airfix Dogfighter AFPACK"))) {
    throw std::runtime_error("private content picker is unavailable");
  }

  const HRESULT shown = dialog->Show(nullptr);
  if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    return std::nullopt;
  }
  if (FAILED(shown)) {
    throw std::runtime_error("private content picker is unavailable");
  }

  ComPtr<IShellItem> item;
  if (FAILED(dialog->GetResult(item.put()))) {
    throw std::runtime_error("private content picker is unavailable");
  }
  PWSTR rawPath{};
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) ||
      rawPath == nullptr) {
    throw std::runtime_error("private content picker is unavailable");
  }
  const std::filesystem::path selected{rawPath};
  CoTaskMemFree(rawPath);
  if (_wcsicmp(selected.extension().c_str(), L".afpack") != 0) {
    throw std::runtime_error("selected private package has the wrong type");
  }
  return selected;
}

[[nodiscard]] PCWSTR
progressText(const AirfixWindowsContentOperationPhase phase) noexcept {
  switch (phase) {
  case AirfixWindowsContentOperationPhase::checking:
    return L"Checking installed content...";
  case AirfixWindowsContentOperationPhase::copying:
    return L"Copying the selected package into private storage...";
  case AirfixWindowsContentOperationPhase::authenticating:
    return L"Authenticating the package...";
  case AirfixWindowsContentOperationPhase::activating:
    return L"Activating the verified package...";
  case AirfixWindowsContentOperationPhase::restoring:
    return L"Restoring the previous verified package...";
  case AirfixWindowsContentOperationPhase::complete:
    return L"Completing the content operation...";
  }
  return L"Working with private content...";
}

class NativeProgressDialog final {
public:
  explicit NativeProgressDialog(PCWSTR title) {
    if (FAILED(CoCreateInstance(CLSID_ProgressDialog, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog_.put())))) {
      throw std::runtime_error("private content progress UI is unavailable");
    }
    dialog_->SetTitle(title);
    dialog_->SetCancelMsg(
        L"Stopping safely. The installed-content state will be checked.",
        nullptr);
    dialog_->SetLine(1U, L"Preparing the private content operation...", FALSE,
                     nullptr);
    dialog_->StartProgressDialog(
        nullptr, nullptr,
        PROGDLG_NORMAL | PROGDLG_AUTOTIME | PROGDLG_NOMINIMIZE, nullptr);
    started_ = true;
    dialog_->SetProgress64(0U, 1U);
  }

  ~NativeProgressDialog() {
    if (started_) {
      dialog_->StopProgressDialog();
    }
  }

  NativeProgressDialog(const NativeProgressDialog &) = delete;
  NativeProgressDialog &operator=(const NativeProgressDialog &) = delete;

  void update(const AirfixWindowsContentOperationProgress &progress,
              std::stop_source &stopSource) noexcept {
    if (progress.phase != phase_) {
      phase_ = progress.phase;
      dialog_->SetLine(1U, progressText(progress.phase), FALSE, nullptr);
    }
    const std::uint64_t total =
        progress.totalBytes == 0U ? 1U : progress.totalBytes;
    const std::uint64_t completed = std::min(progress.completedBytes, total);
    dialog_->SetProgress64(completed, total);
    if (dialog_->HasUserCancelled()) {
      stopSource.request_stop();
    }
  }

private:
  ComPtr<IProgressDialog> dialog_;
  AirfixWindowsContentOperationPhase phase_{
      AirfixWindowsContentOperationPhase::complete};
  bool started_{};
};

template <typename Operation>
[[nodiscard]] auto runWithProgress(PCWSTR title, Operation &&operation) {
  NativeProgressDialog dialog{title};
  std::stop_source stopSource;
  const auto progress =
      [&dialog,
       &stopSource](const AirfixWindowsContentOperationProgress &value) {
        dialog.update(value, stopSource);
      };
  return std::forward<Operation>(operation)(stopSource.get_token(), progress);
}

} // namespace

namespace testing {

AirfixWindowsContentBootstrapResult
runAirfixWindowsContentBootstrapWithCallbacks(
    const AirfixWindowsContentBootstrapCallbacks &callbacks) {
  if (!callbacks.inspect || !callbacks.chooseAction || !callbacks.pickPackage ||
      !callbacks.importPackage || !callbacks.rollback || !callbacks.notify) {
    return AirfixWindowsContentBootstrapResult::failed;
  }

  const auto inspect = [&callbacks]() {
    try {
      return callbacks.inspect();
    } catch (const AirfixWindowsContentImportError &error) {
      callbacks.notify(noticeFor(error.category()));
    } catch (...) {
      callbacks.notify(AirfixWindowsContentBootstrapNotice::unavailable);
    }
    return AirfixWindowsInstalledContentStatus::unavailable;
  };
  auto status = inspect();

  for (std::size_t interaction = 0U;
       interaction < maximumCoordinatorInteractions; ++interaction) {
    AirfixWindowsContentBootstrapAction action{};
    try {
      action = callbacks.chooseAction(status);
    } catch (...) {
      return AirfixWindowsContentBootstrapResult::failed;
    }
    if (action == AirfixWindowsContentBootstrapAction::close) {
      return contentReady(status)
                 ? AirfixWindowsContentBootstrapResult::closedWithReadyContent
                 : AirfixWindowsContentBootstrapResult::
                       closedWithoutReadyContent;
    }
    if (action == AirfixWindowsContentBootstrapAction::retry) {
      status = inspect();
      continue;
    }
    if (action == AirfixWindowsContentBootstrapAction::rollback) {
      if (status != AirfixWindowsInstalledContentStatus::rollbackAvailable) {
        return AirfixWindowsContentBootstrapResult::failed;
      }
      try {
        callbacks.rollback();
        callbacks.notify(AirfixWindowsContentBootstrapNotice::restored);
      } catch (const AirfixWindowsContentImportError &error) {
        callbacks.notify(noticeFor(error.category()));
        if (error.category() ==
            AirfixWindowsContentImportErrorCategory::commitUnknown) {
          return AirfixWindowsContentBootstrapResult::failed;
        }
      } catch (...) {
        callbacks.notify(AirfixWindowsContentBootstrapNotice::unavailable);
      }
      status = inspect();
      continue;
    }
    if (action != AirfixWindowsContentBootstrapAction::importPackage) {
      return AirfixWindowsContentBootstrapResult::failed;
    }
    if (status == AirfixWindowsInstalledContentStatus::rollbackAvailable ||
        status == AirfixWindowsInstalledContentStatus::unavailable) {
      return AirfixWindowsContentBootstrapResult::failed;
    }

    std::optional<std::filesystem::path> selected;
    try {
      selected = callbacks.pickPackage();
    } catch (...) {
      callbacks.notify(AirfixWindowsContentBootstrapNotice::unavailable);
      status = inspect();
      continue;
    }
    if (!selected.has_value()) {
      continue;
    }
    try {
      callbacks.importPackage(*selected);
      callbacks.notify(AirfixWindowsContentBootstrapNotice::imported);
    } catch (const AirfixWindowsContentImportError &error) {
      callbacks.notify(noticeFor(error.category()));
      if (error.category() ==
          AirfixWindowsContentImportErrorCategory::commitUnknown) {
        return AirfixWindowsContentBootstrapResult::failed;
      }
    } catch (...) {
      callbacks.notify(AirfixWindowsContentBootstrapNotice::unavailable);
    }
    status = inspect();
  }
  return AirfixWindowsContentBootstrapResult::failed;
}

bool nativeAirfixWindowsContentUiAvailable() noexcept {
  try {
    const ComApartment apartment;
    const HMODULE library = LoadLibraryW(L"comctl32.dll");
    if (library == nullptr) {
      return false;
    }
    const bool hasTaskDialog =
        GetProcAddress(library, "TaskDialogIndirect") != nullptr;
    FreeLibrary(library);
    if (!hasTaskDialog) {
      return false;
    }

    ComPtr<IFileOpenDialog> picker;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(picker.put())))) {
      return false;
    }
    ComPtr<IProgressDialog> progress;
    return SUCCEEDED(CoCreateInstance(CLSID_ProgressDialog, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(progress.put())));
  } catch (...) {
    return false;
  }
}

} // namespace testing

AirfixWindowsContentBootstrapResult
runAirfixWindowsContentBootstrap(const std::filesystem::path &contentRoot) {
  try {
    const ComApartment apartment;
    const testing::AirfixWindowsContentBootstrapCallbacks callbacks{
        .inspect =
            [&contentRoot] {
              return runWithProgress(
                  L"Checking Airfix Dogfighter private content",
                  [&contentRoot](const std::stop_token stopToken,
                                 const auto &progress) {
                    return inspectAirfixWindowsContent(contentRoot, stopToken,
                                                       progress);
                  });
            },
        .chooseAction = showStatusDialog,
        .pickPackage = pickAfPack,
        .importPackage =
            [&contentRoot](const std::filesystem::path &source) {
              (void)runWithProgress(
                  L"Importing Airfix Dogfighter private content",
                  [&contentRoot, &source](const std::stop_token stopToken,
                                          const auto &progress) {
                    return importAirfixWindowsContent(source, contentRoot,
                                                      stopToken, progress);
                  });
            },
        .rollback =
            [&contentRoot] {
              (void)runWithProgress(
                  L"Restoring Airfix Dogfighter private content",
                  [&contentRoot](const std::stop_token stopToken,
                                 const auto &progress) {
                    return rollbackAirfixWindowsContent(contentRoot, stopToken,
                                                        progress);
                  });
            },
        .notify = showNotice,
    };
    return testing::runAirfixWindowsContentBootstrapWithCallbacks(callbacks);
  } catch (...) {
    showNotice(AirfixWindowsContentBootstrapNotice::unavailable);
    return AirfixWindowsContentBootstrapResult::failed;
  }
}

} // namespace airfix::windows
