#include "airfix/settings/ControllerInputBindingPickerModel.hpp"

#include <algorithm>
#include <iterator>

namespace airfix::settings {
namespace {

[[nodiscard]] ControllerInputBindingPickerResult pickerResult(
    const ControllerInputBindingPickerStatus status,
    std::optional<input::ControllerDigitalGameplayAction> action = std::nullopt,
    std::optional<input::ControlId> control = std::nullopt,
    std::optional<input::ControllerDigitalGameplayAction> conflictingAction =
        std::nullopt,
    std::optional<input::ControllerInputProfileIssue> issue =
        std::nullopt) noexcept {
  return {
      .status = status,
      .action = action,
      .control = control,
      .conflictingAction = conflictingAction,
      .issue = issue,
  };
}

[[nodiscard]] ControllerInputBindingPickerStatus
pickerStatus(const ControllerInputProfileBindingRemapStatus status) noexcept {
  switch (status) {
  case ControllerInputProfileBindingRemapStatus::accepted:
    return ControllerInputBindingPickerStatus::accepted;
  case ControllerInputProfileBindingRemapStatus::saveInProgress:
    return ControllerInputBindingPickerStatus::saveInProgress;
  case ControllerInputProfileBindingRemapStatus::invalidAction:
    return ControllerInputBindingPickerStatus::invalidAction;
  case ControllerInputProfileBindingRemapStatus::actionUnavailable:
    return ControllerInputBindingPickerStatus::actionUnavailable;
  case ControllerInputProfileBindingRemapStatus::invalidControl:
  case ControllerInputProfileBindingRemapStatus::invalidResolution:
    return ControllerInputBindingPickerStatus::invalidControl;
  case ControllerInputProfileBindingRemapStatus::conflict:
    return ControllerInputBindingPickerStatus::conflict;
  case ControllerInputProfileBindingRemapStatus::protectedConflict:
    return ControllerInputBindingPickerStatus::protectedConflict;
  case ControllerInputProfileBindingRemapStatus::invalidProfile:
    return ControllerInputBindingPickerStatus::invalidProfile;
  }
  return ControllerInputBindingPickerStatus::invalidProfile;
}

} // namespace

ControllerInputBindingPickerResult ControllerInputBindingPickerModel::begin(
    const ControllerInputProfileMenuModel &profile,
    const input::ControllerDigitalGameplayAction action) noexcept {
  close();
  if (profile.phase() == ControllerInputProfileMenuPhase::saving) {
    return pickerResult(ControllerInputBindingPickerStatus::saveInProgress);
  }
  if (input::controllerDigitalGameplayActionDescriptor(action) == nullptr) {
    return pickerResult(ControllerInputBindingPickerStatus::invalidAction,
                        action);
  }

  const auto binding = profile.draftDigitalGameplayBinding(action);
  if (!binding.editable()) {
    return pickerResult(ControllerInputBindingPickerStatus::actionUnavailable,
                        action);
  }
  const auto *record = profile.draftBinding(binding.bindingIndex);
  if (record == nullptr) {
    return pickerResult(ControllerInputBindingPickerStatus::invalidProfile,
                        action);
  }

  const auto controls = input::controllerAssignableControlCatalog();
  const auto found = std::find_if(
      controls.begin(), controls.end(),
      [record](const input::ControllerAssignableControlDescriptor &descriptor) {
        return descriptor.control == record->control;
      });
  if (found == controls.end()) {
    return pickerResult(ControllerInputBindingPickerStatus::invalidControl,
                        action, record->control);
  }

  phase_ = ControllerInputBindingPickerPhase::choosingControl;
  selectedAction_ = action;
  selectedControlIndex_ =
      static_cast<std::size_t>(std::distance(controls.begin(), found));
  return pickerResult(ControllerInputBindingPickerStatus::opened, action,
                      found->control);
}

bool ControllerInputBindingPickerModel::selectControlIndex(
    const std::size_t index) noexcept {
  if (phase_ != ControllerInputBindingPickerPhase::choosingControl ||
      index >= input::controllerAssignableControlCatalog().size()) {
    return false;
  }
  selectedControlIndex_ = index;
  return true;
}

void ControllerInputBindingPickerModel::moveControlSelection(
    const std::int32_t direction) noexcept {
  const auto count = input::controllerAssignableControlCatalog().size();
  if (phase_ != ControllerInputBindingPickerPhase::choosingControl ||
      direction == 0 || count == 0U || selectedControlIndex_ >= count) {
    return;
  }
  const auto current = static_cast<std::int32_t>(selectedControlIndex_);
  const auto maximum = static_cast<std::int32_t>(count - 1U);
  selectedControlIndex_ = static_cast<std::size_t>(
      std::clamp(current + (direction < 0 ? -1 : 1), 0, maximum));
}

ControllerInputBindingPickerResult
ControllerInputBindingPickerModel::applySelection(
    ControllerInputProfileMenuModel &profile) noexcept {
  if (phase_ != ControllerInputBindingPickerPhase::choosingControl) {
    return pickerResult(ControllerInputBindingPickerStatus::invalidPhase);
  }
  return apply(profile,
               ControllerInputProfileBindingConflictResolution::cancel);
}

ControllerInputBindingPickerResult
ControllerInputBindingPickerModel::confirmSwap(
    ControllerInputProfileMenuModel &profile) noexcept {
  if (phase_ != ControllerInputBindingPickerPhase::confirmingSwap) {
    return pickerResult(ControllerInputBindingPickerStatus::invalidPhase);
  }
  return apply(profile, ControllerInputProfileBindingConflictResolution::swap);
}

bool ControllerInputBindingPickerModel::cancelSwapConfirmation() noexcept {
  if (phase_ != ControllerInputBindingPickerPhase::confirmingSwap) {
    return false;
  }
  phase_ = ControllerInputBindingPickerPhase::choosingControl;
  conflictingAction_.reset();
  return true;
}

void ControllerInputBindingPickerModel::close() noexcept {
  phase_ = ControllerInputBindingPickerPhase::closed;
  selectedAction_.reset();
  selectedControlIndex_ = input::controllerInputProfileNoIndex;
  conflictingAction_.reset();
}

std::optional<input::ControlId>
ControllerInputBindingPickerModel::selectedControl() const noexcept {
  const auto controls = input::controllerAssignableControlCatalog();
  if (phase_ == ControllerInputBindingPickerPhase::closed ||
      selectedControlIndex_ >= controls.size()) {
    return std::nullopt;
  }
  return controls[selectedControlIndex_].control;
}

ControllerInputBindingPickerResult ControllerInputBindingPickerModel::apply(
    ControllerInputProfileMenuModel &profile,
    const ControllerInputProfileBindingConflictResolution resolution) noexcept {
  const auto control = selectedControl();
  if (!selectedAction_.has_value() || !control.has_value()) {
    return pickerResult(ControllerInputBindingPickerStatus::invalidPhase);
  }

  const auto action = *selectedAction_;
  const auto remap =
      profile.rebindDigitalGameplayAction(action, *control, resolution);
  ControllerInputBindingPickerResult result{
      .status = pickerStatus(remap.status),
      .action = action,
      .control = *control,
      .conflictingAction = remap.conflictingAction,
      .issue = remap.issue,
  };

  if (remap.accepted()) {
    close();
    return result;
  }
  if (remap.canSwap()) {
    phase_ = ControllerInputBindingPickerPhase::confirmingSwap;
    conflictingAction_ = remap.conflictingAction;
    return result;
  }

  phase_ = ControllerInputBindingPickerPhase::choosingControl;
  conflictingAction_.reset();
  return result;
}

} // namespace airfix::settings
