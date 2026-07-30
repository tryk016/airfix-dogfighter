#include "airfix/settings/ControllerInputProfileMenuModel.hpp"

#include <limits>

namespace airfix::settings {
namespace {

[[nodiscard]] constexpr ControllerInputProfileMenuEditResult
editResult(const ControllerInputProfileMenuEditStatus status,
           const std::optional<input::ControllerInputProfileIssue> &issue =
               std::nullopt) noexcept {
  return {
      .status = status,
      .issue = issue,
  };
}

[[nodiscard]] constexpr std::optional<std::size_t>
axisIndex(const input::ControllerAxisElement axis) noexcept {
  const auto index = static_cast<std::size_t>(axis);
  return index < input::controllerProfileAxisCount
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

} // namespace

std::optional<ControllerInputProfileMenuModel>
ControllerInputProfileMenuModel::create(
    const input::ControllerInputProfileRecord &active,
    const input::ControllerInputProfileRecord &persisted,
    const ControllerInputProfileMenuCapabilities capabilities,
    const std::uint64_t initialSerial) noexcept {
  const auto resolvedActive = input::resolveControllerInputProfile(active);
  const auto resolvedPersisted =
      input::resolveControllerInputProfile(persisted);
  if (!resolvedActive.complete() || !resolvedPersisted.complete()) {
    return std::nullopt;
  }
  return ControllerInputProfileMenuModel{active, persisted,
                                         *resolvedPersisted.profile,
                                         capabilities, initialSerial};
}

ControllerInputProfileMenuModel::ControllerInputProfileMenuModel(
    const input::ControllerInputProfileRecord &active,
    const input::ControllerInputProfileRecord &persisted,
    const input::ResolvedControllerInputProfile &resolvedPersisted,
    const ControllerInputProfileMenuCapabilities capabilities,
    const std::uint64_t initialSerial) noexcept
    : active_(active), persisted_(persisted), draft_(persisted),
      resolvedDraft_(resolvedPersisted), capabilities_(capabilities),
      serial_(initialSerial),
      exhausted_(initialSerial == std::numeric_limits<std::uint64_t>::max()) {}

void ControllerInputProfileMenuModel::setPersistenceAvailable(
    const bool available) noexcept {
  capabilities_.persistenceAvailable = available;
}

ControllerInputProfileMenuEditResult
ControllerInputProfileMenuModel::setInnerDeadzoneQ15(
    const input::ControllerAxisElement axis,
    const std::uint16_t value) noexcept {
  if (phase() == ControllerInputProfileMenuPhase::saving) {
    return frozenEditResult();
  }
  const auto index = axisIndex(axis);
  if (!index.has_value()) {
    return editResult(ControllerInputProfileMenuEditStatus::invalidAxis);
  }
  auto calibration = draft_.axes[*index];
  calibration.innerDeadzoneQ15 = value;
  return editAxis(axis, calibration);
}

ControllerInputProfileMenuEditResult
ControllerInputProfileMenuModel::setOuterSaturationQ15(
    const input::ControllerAxisElement axis,
    const std::uint16_t value) noexcept {
  if (phase() == ControllerInputProfileMenuPhase::saving) {
    return frozenEditResult();
  }
  const auto index = axisIndex(axis);
  if (!index.has_value()) {
    return editResult(ControllerInputProfileMenuEditStatus::invalidAxis);
  }
  auto calibration = draft_.axes[*index];
  calibration.outerSaturationQ15 = value;
  return editAxis(axis, calibration);
}

ControllerInputProfileMenuEditResult
ControllerInputProfileMenuModel::setSensitivityPermille(
    const input::ControllerAxisElement axis,
    const std::uint16_t value) noexcept {
  if (phase() == ControllerInputProfileMenuPhase::saving) {
    return frozenEditResult();
  }
  const auto index = axisIndex(axis);
  if (!index.has_value()) {
    return editResult(ControllerInputProfileMenuEditStatus::invalidAxis);
  }
  auto calibration = draft_.axes[*index];
  calibration.sensitivityPermille = value;
  return editAxis(axis, calibration);
}

ControllerInputProfileMenuEditResult
ControllerInputProfileMenuModel::setResponseCurve(
    const input::ControllerAxisElement axis,
    const input::ControllerResponseCurve value) noexcept {
  if (phase() == ControllerInputProfileMenuPhase::saving) {
    return frozenEditResult();
  }
  const auto index = axisIndex(axis);
  if (!index.has_value()) {
    return editResult(ControllerInputProfileMenuEditStatus::invalidAxis);
  }
  auto calibration = draft_.axes[*index];
  calibration.responseCurve = value;
  return editAxis(axis, calibration);
}

ControllerInputProfileMenuEditResult
ControllerInputProfileMenuModel::setInverted(
    const input::ControllerAxisElement axis, const bool value) noexcept {
  if (phase() == ControllerInputProfileMenuPhase::saving) {
    return frozenEditResult();
  }
  const auto index = axisIndex(axis);
  if (!index.has_value()) {
    return editResult(ControllerInputProfileMenuEditStatus::invalidAxis);
  }
  auto calibration = draft_.axes[*index];
  calibration.inverted = static_cast<std::uint8_t>(value ? 1U : 0U);
  return editAxis(axis, calibration);
}

ControllerInputProfileMenuEditResult ControllerInputProfileMenuModel::resetAxis(
    const input::ControllerAxisElement axis) noexcept {
  if (phase() == ControllerInputProfileMenuPhase::saving) {
    return frozenEditResult();
  }
  if (!axisIndex(axis).has_value()) {
    return editResult(ControllerInputProfileMenuEditStatus::invalidAxis);
  }
  return editAxis(axis, input::ControllerAxisCalibrationRecord{});
}

ControllerInputProfileMenuEditResult
ControllerInputProfileMenuModel::resetAllAxes() noexcept {
  if (phase() == ControllerInputProfileMenuPhase::saving) {
    return frozenEditResult();
  }
  auto candidate = draft_;
  candidate.axes = {};
  return editRecord(candidate);
}

bool ControllerInputProfileMenuModel::cancelDraft() noexcept {
  if (!canCancel()) {
    return false;
  }
  const auto resolved = input::resolveControllerInputProfile(persisted_);
  if (!resolved.complete()) {
    return false;
  }
  draft_ = persisted_;
  resolvedDraft_ = *resolved.profile;
  return true;
}

std::optional<ControllerInputProfileMenuSaveTicket>
ControllerInputProfileMenuModel::beginSave() noexcept {
  if (!canSave()) {
    return std::nullopt;
  }
  ++serial_;
  if (serial_ == std::numeric_limits<std::uint64_t>::max()) {
    exhausted_ = true;
  }
  current_ = ControllerInputProfileMenuSaveTicket{
      .serial = serial_,
      .candidate = draft_,
      .repairsPersistence = capabilities_.repairRequired,
  };
  return current_;
}

bool ControllerInputProfileMenuModel::finishSaveSuccess(
    const ControllerInputProfileMenuSaveTicket &ticket) noexcept {
  if (!current_.has_value() || *current_ != ticket) {
    return false;
  }
  persisted_ = ticket.candidate;
  draft_ = ticket.candidate;
  capabilities_.repairRequired = false;
  current_.reset();
  return true;
}

bool ControllerInputProfileMenuModel::finishSaveFailure(
    const ControllerInputProfileMenuSaveTicket &ticket) noexcept {
  if (!current_.has_value() || *current_ != ticket) {
    return false;
  }
  current_.reset();
  return true;
}

ControllerInputProfileMenuEditResult ControllerInputProfileMenuModel::editAxis(
    const input::ControllerAxisElement axis,
    const input::ControllerAxisCalibrationRecord &calibration) noexcept {
  const auto index = axisIndex(axis);
  if (!index.has_value()) {
    return editResult(ControllerInputProfileMenuEditStatus::invalidAxis);
  }
  auto candidate = draft_;
  candidate.axes[*index] = calibration;
  return editRecord(candidate);
}

ControllerInputProfileMenuEditResult
ControllerInputProfileMenuModel::editRecord(
    const input::ControllerInputProfileRecord &candidate) noexcept {
  if (phase() == ControllerInputProfileMenuPhase::saving) {
    return frozenEditResult();
  }
  const auto resolved = input::resolveControllerInputProfile(candidate);
  if (!resolved.complete()) {
    return editResult(ControllerInputProfileMenuEditStatus::invalidProfile,
                      resolved.issue);
  }
  draft_ = candidate;
  resolvedDraft_ = *resolved.profile;
  return editResult(ControllerInputProfileMenuEditStatus::accepted);
}

ControllerInputProfileMenuEditResult
ControllerInputProfileMenuModel::frozenEditResult() const noexcept {
  return editResult(ControllerInputProfileMenuEditStatus::saveInProgress);
}

} // namespace airfix::settings
