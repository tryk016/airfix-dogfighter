#pragma once

#include "airfix/settings/ControllerInputProfileMenuModel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::settings {

enum class ControllerInputBindingPickerPhase : std::uint8_t {
  closed = 0,
  choosingControl,
  confirmingSwap,
};

enum class ControllerInputBindingPickerStatus : std::uint8_t {
  opened = 0,
  accepted,
  conflict,
  protectedConflict,
  saveInProgress,
  invalidAction,
  actionUnavailable,
  invalidControl,
  invalidProfile,
  invalidPhase,
};

struct ControllerInputBindingPickerResult final {
  ControllerInputBindingPickerStatus status{
      ControllerInputBindingPickerStatus::invalidPhase};
  std::optional<input::ControllerDigitalGameplayAction> action;
  std::optional<input::ControlId> control;
  std::optional<input::ControllerDigitalGameplayAction> conflictingAction;
  std::optional<input::ControllerInputProfileIssue> issue;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == ControllerInputBindingPickerStatus::accepted &&
           !issue.has_value();
  }

  [[nodiscard]] constexpr bool needsSwapConfirmation() const noexcept {
    return status == ControllerInputBindingPickerStatus::conflict &&
           action.has_value() && control.has_value() &&
           conflictingAction.has_value();
  }
};

// Allocation-free interaction state shared by native text pickers.
//
// The profile menu model remains the sole owner of the complete draft and its
// persistence ticket. This class owns only one selected action/control and an
// optional conflict confirmation. Choosing a control always performs the
// cancel-first remap. A swap is attempted only after an explicit second call,
// which revalidates the current draft instead of trusting stale conflict data.
class ControllerInputBindingPickerModel final {
public:
  [[nodiscard]] ControllerInputBindingPickerResult
  begin(const ControllerInputProfileMenuModel &profile,
        input::ControllerDigitalGameplayAction action) noexcept;

  [[nodiscard]] bool selectControlIndex(std::size_t index) noexcept;
  void moveControlSelection(std::int32_t direction) noexcept;

  [[nodiscard]] ControllerInputBindingPickerResult
  applySelection(ControllerInputProfileMenuModel &profile) noexcept;

  [[nodiscard]] ControllerInputBindingPickerResult
  confirmSwap(ControllerInputProfileMenuModel &profile) noexcept;

  [[nodiscard]] bool cancelSwapConfirmation() noexcept;
  void close() noexcept;

  [[nodiscard]] ControllerInputBindingPickerPhase phase() const noexcept {
    return phase_;
  }

  [[nodiscard]] std::optional<input::ControllerDigitalGameplayAction>
  selectedAction() const noexcept {
    return selectedAction_;
  }

  [[nodiscard]] std::size_t selectedControlIndex() const noexcept {
    return selectedControlIndex_;
  }

  [[nodiscard]] std::optional<input::ControlId>
  selectedControl() const noexcept;

  [[nodiscard]] std::optional<input::ControllerDigitalGameplayAction>
  conflictingAction() const noexcept {
    return conflictingAction_;
  }

private:
  [[nodiscard]] ControllerInputBindingPickerResult
  apply(ControllerInputProfileMenuModel &profile,
        ControllerInputProfileBindingConflictResolution resolution) noexcept;

  ControllerInputBindingPickerPhase phase_{
      ControllerInputBindingPickerPhase::closed};
  std::optional<input::ControllerDigitalGameplayAction> selectedAction_;
  std::size_t selectedControlIndex_{input::controllerInputProfileNoIndex};
  std::optional<input::ControllerDigitalGameplayAction> conflictingAction_;
};

} // namespace airfix::settings
