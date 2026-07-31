#pragma once

#include "AirfixWindowsContentImport.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace airfix::windows {

enum class AirfixWindowsContentBootstrapAction : std::uint8_t {
  importPackage,
  rollback,
  retry,
  close,
};

enum class AirfixWindowsContentBootstrapNotice : std::uint8_t {
  imported,
  restored,
  cancelled,
  rejected,
  commitUnknown,
  busy,
  unavailable,
};

enum class AirfixWindowsContentBootstrapResult : std::uint8_t {
  closedWithoutReadyContent,
  closedWithReadyContent,
  failed,
};

// Runs the native, modal private-content manager. Outside the owner-controlled
// OS picker it never echoes a source path, app-private root, checksum, package
// generation, or backend diagnostic. The manager is a pre-game operation and
// performs no live mission reload.
[[nodiscard]] AirfixWindowsContentBootstrapResult
runAirfixWindowsContentBootstrap(const std::filesystem::path &contentRoot);

namespace testing {

struct AirfixWindowsContentBootstrapCallbacks final {
  std::function<AirfixWindowsInstalledContentStatus()> inspect;
  std::function<AirfixWindowsContentBootstrapAction(
      AirfixWindowsInstalledContentStatus)>
      chooseAction;
  std::function<std::optional<std::filesystem::path>()> pickPackage;
  std::function<void(const std::filesystem::path &)> importPackage;
  std::function<void()> rollback;
  std::function<void(AirfixWindowsContentBootstrapNotice)> notify;
};

// Deterministic coordinator used by the native dialogs and synthetic tests.
// Callback invocation is bounded so a faulty presenter cannot keep the
// product in an unbounded retry loop.
[[nodiscard]] AirfixWindowsContentBootstrapResult
runAirfixWindowsContentBootstrapWithCallbacks(
    const AirfixWindowsContentBootstrapCallbacks &callbacks);

// Verifies native dialog classes and the TaskDialog entry point without
// displaying a window or touching private content.
[[nodiscard]] bool nativeAirfixWindowsContentUiAvailable() noexcept;

} // namespace testing

} // namespace airfix::windows
