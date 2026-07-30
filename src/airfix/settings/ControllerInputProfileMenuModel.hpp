#pragma once

#include "airfix/input/ControllerInputBindingCatalog.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::settings {

struct ControllerInputProfileMenuCapabilities final {
  bool persistenceAvailable{true};
  // A recovered backup/default can be valid and identical to the draft while
  // the current file still needs an explicit durable repair.
  bool repairRequired{};

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerInputProfileMenuCapabilities &,
             const ControllerInputProfileMenuCapabilities &) noexcept = default;
};

enum class ControllerInputProfileMenuPhase : std::uint8_t {
  idle,
  saving,
};

enum class ControllerInputProfileMenuEditStatus : std::uint8_t {
  accepted,
  saveInProgress,
  invalidAxis,
  invalidProfile,
};

struct ControllerInputProfileMenuEditResult final {
  ControllerInputProfileMenuEditStatus status{
      ControllerInputProfileMenuEditStatus::accepted};
  std::optional<input::ControllerInputProfileIssue> issue;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == ControllerInputProfileMenuEditStatus::accepted &&
           !issue.has_value();
  }
};

enum class ControllerInputProfileBindingConflictResolution : std::uint8_t {
  cancel = 0,
  swap,
};

enum class ControllerInputProfileBindingRemapStatus : std::uint8_t {
  accepted = 0,
  saveInProgress,
  invalidAction,
  actionUnavailable,
  invalidControl,
  invalidResolution,
  conflict,
  protectedConflict,
  invalidProfile,
};

struct ControllerInputProfileBindingRemapResult final {
  ControllerInputProfileBindingRemapStatus status{
      ControllerInputProfileBindingRemapStatus::accepted};
  std::optional<input::ControllerInputProfileIssue> issue;
  std::optional<input::ControllerDigitalGameplayBindingStatus> bindingStatus;
  std::size_t bindingIndex{input::controllerInputProfileNoIndex};
  std::size_t conflictIndex{input::controllerInputProfileNoIndex};
  std::optional<input::ControllerDigitalGameplayAction> conflictingAction;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == ControllerInputProfileBindingRemapStatus::accepted &&
           !issue.has_value();
  }

  [[nodiscard]] constexpr bool canSwap() const noexcept {
    return status == ControllerInputProfileBindingRemapStatus::conflict &&
           conflictingAction.has_value() &&
           conflictIndex != input::controllerInputProfileNoIndex;
  }
};

struct ControllerInputProfileMenuSaveTicket final {
  std::uint64_t serial{};
  input::ControllerInputProfileRecord candidate;
  bool repairsPersistence{};

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerInputProfileMenuSaveTicket &,
             const ControllerInputProfileMenuSaveTicket &) noexcept = default;
};

// Allocation-free owner-thread state for calibration and bounded digital
// gameplay-binding editing.
//
// active_ is the immutable configuration installed in the running input
// pipeline. Successful saves promote only persisted_/draft_; therefore
// restartRequired() remains true until a later process constructs a new model
// with the saved record as its active value. Every edit re-resolves the
// complete record. Calibration operations preserve the binding table; bounded
// remap operations preserve calibration and normalize only the selected
// binding transport, plus the conflicting supported binding during a swap.
class ControllerInputProfileMenuModel final {
public:
  [[nodiscard]] static std::optional<ControllerInputProfileMenuModel>
  create(const input::ControllerInputProfileRecord &active,
         const input::ControllerInputProfileRecord &persisted,
         ControllerInputProfileMenuCapabilities capabilities = {},
         std::uint64_t initialSerial = 0U) noexcept;

  ControllerInputProfileMenuModel(const ControllerInputProfileMenuModel &) =
      default;
  ControllerInputProfileMenuModel(ControllerInputProfileMenuModel &&) noexcept =
      default;
  ControllerInputProfileMenuModel &
  operator=(const ControllerInputProfileMenuModel &) = default;
  ControllerInputProfileMenuModel &
  operator=(ControllerInputProfileMenuModel &&) noexcept = default;
  ~ControllerInputProfileMenuModel() = default;

  [[nodiscard]] const input::ControllerInputProfileRecord &
  activeRecord() const noexcept {
    return active_;
  }

  [[nodiscard]] const input::ControllerInputProfileRecord &
  persistedRecord() const noexcept {
    return persisted_;
  }

  [[nodiscard]] const input::ControllerInputProfileRecord &
  draftRecord() const noexcept {
    return draft_;
  }

  [[nodiscard]] const input::ResolvedControllerInputProfile &
  resolvedDraftProfile() const noexcept {
    return resolvedDraft_;
  }

  [[nodiscard]] const input::ControllerAxisCalibrationRecord *
  draftAxisCalibration(input::ControllerAxisElement axis) const noexcept {
    return resolvedDraft_.axisCalibration(axis);
  }

  [[nodiscard]] input::ControllerDigitalGameplayBindingLookup
  draftDigitalGameplayBinding(
      input::ControllerDigitalGameplayAction action) const noexcept {
    return input::controllerDigitalGameplayBinding(draft_, action);
  }

  [[nodiscard]] const input::ControllerBindingRecord *
  draftBinding(std::size_t index) const noexcept {
    return index < draft_.bindingCount ? &draft_.bindings[index] : nullptr;
  }

  [[nodiscard]] ControllerInputProfileMenuPhase phase() const noexcept {
    return current_.has_value() ? ControllerInputProfileMenuPhase::saving
                                : ControllerInputProfileMenuPhase::idle;
  }

  [[nodiscard]] bool dirty() const noexcept { return draft_ != persisted_; }

  [[nodiscard]] bool restartRequired() const noexcept {
    return persisted_ != active_;
  }

  [[nodiscard]] bool persistenceAvailable() const noexcept {
    return capabilities_.persistenceAvailable;
  }

  [[nodiscard]] bool repairRequired() const noexcept {
    return capabilities_.repairRequired;
  }

  [[nodiscard]] bool exhausted() const noexcept { return exhausted_; }

  [[nodiscard]] bool canSave() const noexcept {
    return phase() == ControllerInputProfileMenuPhase::idle &&
           capabilities_.persistenceAvailable &&
           (dirty() || capabilities_.repairRequired) && !exhausted_;
  }

  [[nodiscard]] bool canCancel() const noexcept {
    return phase() == ControllerInputProfileMenuPhase::idle;
  }

  void setPersistenceAvailable(bool available) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  setInnerDeadzoneQ15(input::ControllerAxisElement axis,
                      std::uint16_t value) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  setOuterSaturationQ15(input::ControllerAxisElement axis,
                        std::uint16_t value) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  setSensitivityPermille(input::ControllerAxisElement axis,
                         std::uint16_t value) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  setResponseCurve(input::ControllerAxisElement axis,
                   input::ControllerResponseCurve value) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  setInverted(input::ControllerAxisElement axis, bool value) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  resetAxis(input::ControllerAxisElement axis) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult resetAllAxes() noexcept;

  // Cancel-first action-centric remapping. Swap is considered only when the
  // conflicting binding is another unique supported gameplay action. Raw AFIP
  // transport fields are normalized internally and never accepted from UI.
  [[nodiscard]] ControllerInputProfileBindingRemapResult
  rebindDigitalGameplayAction(
      input::ControllerDigitalGameplayAction action, input::ControlId control,
      ControllerInputProfileBindingConflictResolution resolution =
          ControllerInputProfileBindingConflictResolution::cancel) noexcept;

  // Explicit recovery action: restore the complete default controller binding
  // table while preserving schema and all four calibration records.
  [[nodiscard]] ControllerInputProfileMenuEditResult
  resetAllControllerBindings() noexcept;

  [[nodiscard]] bool cancelDraft() noexcept;

  [[nodiscard]] std::optional<ControllerInputProfileMenuSaveTicket>
  beginSave() noexcept;

  [[nodiscard]] bool finishSaveSuccess(
      const ControllerInputProfileMenuSaveTicket &ticket) noexcept;

  [[nodiscard]] bool finishSaveFailure(
      const ControllerInputProfileMenuSaveTicket &ticket) noexcept;

private:
  ControllerInputProfileMenuModel(
      const input::ControllerInputProfileRecord &active,
      const input::ControllerInputProfileRecord &persisted,
      const input::ResolvedControllerInputProfile &resolvedPersisted,
      ControllerInputProfileMenuCapabilities capabilities,
      std::uint64_t initialSerial) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  editAxis(input::ControllerAxisElement axis,
           const input::ControllerAxisCalibrationRecord &calibration) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  editRecord(const input::ControllerInputProfileRecord &candidate) noexcept;

  [[nodiscard]] ControllerInputProfileMenuEditResult
  frozenEditResult() const noexcept;

  input::ControllerInputProfileRecord active_;
  input::ControllerInputProfileRecord persisted_;
  input::ControllerInputProfileRecord draft_;
  input::ResolvedControllerInputProfile resolvedDraft_;
  ControllerInputProfileMenuCapabilities capabilities_;
  std::optional<ControllerInputProfileMenuSaveTicket> current_;
  std::uint64_t serial_{};
  bool exhausted_{};
};

} // namespace airfix::settings
