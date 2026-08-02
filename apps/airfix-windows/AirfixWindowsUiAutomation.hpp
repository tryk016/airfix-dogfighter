#pragma once

#include "AirfixWindowsUiSemantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace airfix::windows {

inline constexpr std::size_t airfixWindowsUiAutomationActionCapacity = 32U;

struct AirfixWindowsUiAutomationAction final {
  AirfixWindowsRenderSettingsScreen screen{
      AirfixWindowsRenderSettingsScreen::pause};
  std::uint64_t accessibilityGeneration{1U};
  AirfixWindowsRenderSettingsItem item{AirfixWindowsRenderSettingsItem::count};
  AirfixWindowsAccessibilityAction action{
      AirfixWindowsAccessibilityAction::focus};

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsUiAutomationAction &,
             const AirfixWindowsUiAutomationAction &) noexcept = default;
};

struct AirfixWindowsUiAutomationSharedState;

class AirfixWindowsUiAutomationActionQueue final {
public:
  [[nodiscard]] bool
  tryPush(const AirfixWindowsUiAutomationAction &action) noexcept;
  [[nodiscard]] std::optional<AirfixWindowsUiAutomationAction> pop() noexcept;
  void clear() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  mutable std::mutex mutex_;
  std::array<AirfixWindowsUiAutomationAction,
             airfixWindowsUiAutomationActionCapacity>
      actions_{};
  std::size_t read_{};
  std::size_t write_{};
  std::size_t count_{};
};

// Windows-only owner for the SDL HWND's UI Automation fragment provider.
// COM callers see immutable bounded semantic snapshots and can only enqueue
// bounded typed actions. The SDL owner thread remains solely responsible for
// consuming those actions through AirfixWindowsRenderSettingsPanel. Attach,
// detach, and destruction must occur on that HWND's owning thread.
class AirfixWindowsUiAutomationHost final {
public:
  AirfixWindowsUiAutomationHost() noexcept;
  AirfixWindowsUiAutomationHost(const AirfixWindowsUiAutomationHost &) = delete;
  AirfixWindowsUiAutomationHost &
  operator=(const AirfixWindowsUiAutomationHost &) = delete;
  AirfixWindowsUiAutomationHost(AirfixWindowsUiAutomationHost &&) = delete;
  AirfixWindowsUiAutomationHost &
  operator=(AirfixWindowsUiAutomationHost &&) = delete;
  ~AirfixWindowsUiAutomationHost();

  // nativeWindow must be the HWND owned by the SDL window. The pointer is not
  // retained after detach and no ownership is transferred.
  [[nodiscard]] bool attach(void *nativeWindow) noexcept;
  void detach() noexcept;

  [[nodiscard]] bool attached() const noexcept;

  // Passing nullopt withdraws the panel fragment while leaving the SDL HWND
  // and its ordinary host provider intact.
  [[nodiscard]] bool
  publish(const std::optional<AirfixWindowsUiSemanticTree> &tree) noexcept;

  [[nodiscard]] std::optional<AirfixWindowsUiAutomationAction>
  popAction() noexcept;
  [[nodiscard]] std::size_t pendingActionCount() const noexcept;

private:
  std::shared_ptr<AirfixWindowsUiAutomationSharedState> state_;
  void *subclassContext_{};
  bool ownsComInitialization_{};
};

} // namespace airfix::windows
