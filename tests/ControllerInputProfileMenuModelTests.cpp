#include "airfix/input/ControllerInputBatchBridge.hpp"
#include "airfix/input/ControllerInputRuntimeConfiguration.hpp"
#include "airfix/settings/ControllerInputProfileCodec.hpp"
#include "airfix/settings/ControllerInputProfileMenuModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace {

using airfix::input::ControllerAxisCalibrationRecord;
using airfix::input::ControllerAxisElement;
using airfix::input::ControllerDigitalGameplayAction;
using airfix::input::ControllerDigitalGameplayBindingStatus;
using airfix::input::ControllerInputBatch;
using airfix::input::ControllerInputBatchBridge;
using airfix::input::ControllerInputEmission;
using airfix::input::ControllerInputProfileIssueKind;
using airfix::input::ControllerInputProfileRecord;
using airfix::input::ControllerResponseCurve;
using airfix::input::ControllerSample;
using airfix::input::DigitalAction;
using airfix::input::gameplayContext;
using airfix::input::makeDefaultControllerInputProfileRecord;
using airfix::input::PhysicalEventKind;
using airfix::input::q15Min;
using airfix::input::resolveControllerInputProfile;
using airfix::input::transformControllerAxisForTransport;
using airfix::settings::ControllerInputProfileBindingConflictResolution;
using airfix::settings::ControllerInputProfileBindingRemapStatus;
using airfix::settings::ControllerInputProfileMenuCapabilities;
using airfix::settings::ControllerInputProfileMenuEditStatus;
using airfix::settings::ControllerInputProfileMenuModel;
using airfix::settings::ControllerInputProfileMenuPhase;
using airfix::settings::ControllerInputProfileMenuSaveTicket;

using OutputBuffer =
    std::array<ControllerInputEmission,
               ControllerInputBatchBridge::maximumEmissionCount>;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] constexpr std::size_t
axisIndex(const ControllerAxisElement axis) noexcept {
  return static_cast<std::size_t>(axis);
}

[[nodiscard]] ControllerInputProfileRecord remappedRecord() {
  using namespace airfix::input::controls::controller;
  auto record = makeDefaultControllerInputProfileRecord();
  bool changed = false;
  for (std::size_t index = 0U; index < record.bindingCount; ++index) {
    auto &binding = record.bindings[index];
    if (binding.control == faceLeft &&
        (binding.contexts & gameplayContext) != 0U) {
      binding.target =
          static_cast<std::uint8_t>(DigitalAction::combatWeaponNext);
      changed = true;
      break;
    }
  }
  require(changed, "remap fixture did not find the gameplay face binding");
  require(resolveControllerInputProfile(record).complete(),
          "remap fixture is invalid");
  return record;
}

[[nodiscard]] ControllerInputProfileMenuModel
model(const ControllerInputProfileRecord &active,
      const ControllerInputProfileRecord &persisted,
      const ControllerInputProfileMenuCapabilities capabilities = {},
      const std::uint64_t initialSerial = 0U) {
  auto created = ControllerInputProfileMenuModel::create(
      active, persisted, capabilities, initialSerial);
  require(created.has_value(), "valid menu model was not created");
  return *created;
}

[[nodiscard]] ControllerInputProfileMenuModel
defaultModel(const ControllerInputProfileMenuCapabilities capabilities = {},
             const std::uint64_t initialSerial = 0U) {
  const auto record = makeDefaultControllerInputProfileRecord();
  return model(record, record, capabilities, initialSerial);
}

void requireBindingsUnchanged(const ControllerInputProfileRecord &expected,
                              const ControllerInputProfileRecord &actual,
                              const std::string_view context) {
  require(actual.schemaVersion == expected.schemaVersion,
          std::string(context) + " changed schema");
  require(actual.bindingCount == expected.bindingCount,
          std::string(context) + " changed binding count");
  require(actual.bindings == expected.bindings,
          std::string(context) + " changed binding storage");
}

void testCreationRejectsInvalidRecordsAndExposesBoundedState() {
  const auto active = makeDefaultControllerInputProfileRecord();
  auto persisted = remappedRecord();
  persisted.axes[axisIndex(ControllerAxisElement::rightStickY)]
      .sensitivityPermille = 1500U;

  auto subject = model(active, persisted);
  require(subject.activeRecord() == active &&
              subject.persistedRecord() == persisted &&
              subject.draftRecord() == persisted,
          "created model did not retain its exact three snapshots");
  require(subject.resolvedDraftProfile().record() == persisted,
          "created model exposed the wrong resolved draft");
  require(subject.restartRequired(),
          "persisted/active difference did not require restart");
  require(!subject.dirty() && !subject.repairRequired() &&
              subject.persistenceAvailable() && !subject.canSave() &&
              subject.canCancel() &&
              subject.phase() == ControllerInputProfileMenuPhase::idle,
          "created model has the wrong idle capabilities");

  const auto *axis =
      subject.draftAxisCalibration(ControllerAxisElement::rightStickY);
  require(axis != nullptr && axis->sensitivityPermille == 1500U,
          "axis accessor did not expose the selected draft calibration");
  require(subject.draftAxisCalibration(ControllerAxisElement::count) == nullptr,
          "forged axis accessor did not fail closed");

  auto invalidActive = active;
  invalidActive.schemaVersion = 0U;
  require(!ControllerInputProfileMenuModel::create(invalidActive, persisted)
               .has_value(),
          "invalid active record created a menu model");

  auto invalidPersisted = persisted;
  invalidPersisted.axes[0U].outerSaturationQ15 = 0U;
  require(!ControllerInputProfileMenuModel::create(active, invalidPersisted)
               .has_value(),
          "invalid persisted record created a menu model");
}

void testTypedEditsCoverAllAxesAndPreserveBindings() {
  const auto record = remappedRecord();
  auto subject = model(record, record);

  constexpr std::array axes{
      ControllerAxisElement::leftStickX,
      ControllerAxisElement::leftStickY,
      ControllerAxisElement::rightStickX,
      ControllerAxisElement::rightStickY,
  };
  for (std::size_t index = 0U; index < axes.size(); ++index) {
    const auto axis = axes[index];
    const auto inner = static_cast<std::uint16_t>(5000U + index);
    const auto outer = static_cast<std::uint16_t>(30000U - index);
    const auto sensitivity = static_cast<std::uint16_t>(
        airfix::input::controllerAxisMinimumSensitivityPermille + index * 250U);
    const auto curve = static_cast<ControllerResponseCurve>(
        index % static_cast<std::size_t>(ControllerResponseCurve::count));
    require(subject.setInnerDeadzoneQ15(axis, inner).accepted(),
            "valid inner-deadzone edit failed");
    require(subject.setOuterSaturationQ15(axis, outer).accepted(),
            "valid outer-saturation edit failed");
    require(subject.setSensitivityPermille(axis, sensitivity).accepted(),
            "valid sensitivity edit failed");
    require(subject.setResponseCurve(axis, curve).accepted(),
            "valid response-curve edit failed");
    require(subject.setInverted(axis, (index & 1U) != 0U).accepted(),
            "valid inversion edit failed");

    const auto *calibration = subject.draftAxisCalibration(axis);
    require(calibration != nullptr && calibration->innerDeadzoneQ15 == inner &&
                calibration->outerSaturationQ15 == outer &&
                calibration->sensitivityPermille == sensitivity &&
                calibration->responseCurve == curve &&
                calibration->inverted ==
                    static_cast<std::uint8_t>((index & 1U) != 0U),
            "typed edits produced the wrong selected-axis snapshot");
    require(subject.resolvedDraftProfile().record() == subject.draftRecord(),
            "typed edit left the resolved draft stale");
    requireBindingsUnchanged(record, subject.draftRecord(), "typed edit");
  }
  require(subject.dirty() && subject.canSave(),
          "valid typed edits did not produce a saveable draft");
}

void requireInvalidEditAtomic(
    ControllerInputProfileMenuModel &subject,
    const airfix::settings::ControllerInputProfileMenuEditResult &result,
    const ControllerInputProfileRecord &before,
    const ControllerInputProfileIssueKind expectedIssue,
    const std::string_view context) {
  require(result.status ==
                  ControllerInputProfileMenuEditStatus::invalidProfile &&
              result.issue.has_value() && result.issue->kind == expectedIssue,
          std::string(context) + " returned the wrong issue");
  require(subject.draftRecord() == before &&
              subject.resolvedDraftProfile().record() == before,
          std::string(context) + " partially changed the draft");
}

void testInvalidEditsAreAtomicAndLimitsArePublic() {
  static_assert(airfix::input::controllerAxisMinimumSensitivityPermille ==
                250U);
  static_assert(airfix::input::controllerAxisMaximumSensitivityPermille ==
                2000U);

  auto subject = defaultModel();
  const auto axis = ControllerAxisElement::leftStickX;
  auto before = subject.draftRecord();
  requireInvalidEditAtomic(
      subject,
      subject.setInnerDeadzoneQ15(
          axis, static_cast<std::uint16_t>(airfix::input::q15One + 1)),
      before, ControllerInputProfileIssueKind::invalidInnerDeadzone,
      "oversized inner deadzone");

  before = subject.draftRecord();
  requireInvalidEditAtomic(
      subject, subject.setOuterSaturationQ15(axis, 0U), before,
      ControllerInputProfileIssueKind::invalidOuterSaturation,
      "zero outer saturation");

  require(subject.setInnerDeadzoneQ15(axis, 5000U).accepted(),
          "relational outer fixture failed");
  before = subject.draftRecord();
  requireInvalidEditAtomic(
      subject, subject.setOuterSaturationQ15(axis, 5000U), before,
      ControllerInputProfileIssueKind::invalidOuterSaturation,
      "outer saturation equal to inner deadzone");

  before = subject.draftRecord();
  requireInvalidEditAtomic(
      subject,
      subject.setSensitivityPermille(
          axis,
          static_cast<std::uint16_t>(
              airfix::input::controllerAxisMinimumSensitivityPermille - 1U)),
      before, ControllerInputProfileIssueKind::invalidSensitivity,
      "below-minimum sensitivity");

  before = subject.draftRecord();
  requireInvalidEditAtomic(
      subject,
      subject.setSensitivityPermille(
          axis,
          static_cast<std::uint16_t>(
              airfix::input::controllerAxisMaximumSensitivityPermille + 1U)),
      before, ControllerInputProfileIssueKind::invalidSensitivity,
      "above-maximum sensitivity");

  before = subject.draftRecord();
  requireInvalidEditAtomic(
      subject, subject.setResponseCurve(axis, ControllerResponseCurve::count),
      before, ControllerInputProfileIssueKind::invalidResponseCurve,
      "forged response curve");

  const auto forgedAxis = static_cast<ControllerAxisElement>(0xFFU);
  before = subject.draftRecord();
  const auto forged = subject.setInverted(forgedAxis, true);
  require(forged.status == ControllerInputProfileMenuEditStatus::invalidAxis &&
              !forged.issue.has_value() && subject.draftRecord() == before,
          "forged axis did not fail atomically");
}

void testResetAndCancelChangeOnlyCalibration() {
  auto persisted = remappedRecord();
  persisted.axes[0U] = {
      .innerDeadzoneQ15 = 6000U,
      .outerSaturationQ15 = 28000U,
      .sensitivityPermille = 1250U,
      .responseCurve = ControllerResponseCurve::squared,
      .inverted = 1U,
  };
  auto subject = model(persisted, persisted);
  require(
      subject.setSensitivityPermille(ControllerAxisElement::leftStickY, 1750U)
          .accepted(),
      "reset fixture edit failed");
  require(subject.resetAxis(ControllerAxisElement::leftStickX).accepted(),
          "single-axis reset failed");
  require(subject.draftRecord().axes[0U] == ControllerAxisCalibrationRecord{} &&
              subject.draftRecord().axes[1U].sensitivityPermille == 1750U,
          "single-axis reset changed the wrong axes");
  requireBindingsUnchanged(persisted, subject.draftRecord(),
                           "single-axis reset");

  require(subject.resetAllAxes().accepted(), "all-axis reset failed");
  for (const auto &axis : subject.draftRecord().axes) {
    require(axis == ControllerAxisCalibrationRecord{},
            "all-axis reset retained a calibration value");
  }
  requireBindingsUnchanged(persisted, subject.draftRecord(), "all-axis reset");

  require(subject.cancelDraft(), "idle cancel failed");
  require(subject.draftRecord() == persisted && !subject.dirty() &&
              subject.resolvedDraftProfile().record() == persisted,
          "cancel did not restore the exact persisted record");
}

void testRepairCapabilityAllowsCleanSave() {
  const auto record = remappedRecord();
  auto subject = model(record, record,
                       ControllerInputProfileMenuCapabilities{
                           .persistenceAvailable = false,
                           .repairRequired = true,
                       });
  require(subject.repairRequired() && !subject.dirty() && !subject.canSave() &&
              !subject.beginSave().has_value(),
          "unavailable persistence allowed a repair save");

  require(
      subject.setInverted(ControllerAxisElement::rightStickY, true).accepted(),
      "persistence capability incorrectly blocked draft editing");
  require(subject.dirty() && !subject.canSave(),
          "unavailable persistence exposed a dirty save");
  require(subject.cancelDraft() && !subject.dirty(),
          "repair fixture cancel failed");

  subject.setPersistenceAvailable(true);
  require(subject.canSave(), "restored persistence did not enable repair");
  const auto ticket = subject.beginSave();
  require(ticket.has_value() && ticket->candidate == record &&
              ticket->repairsPersistence,
          "clean repair did not issue the exact repair ticket");
  require(subject.finishSaveSuccess(*ticket),
          "clean repair ticket did not finish");
  require(!subject.repairRequired() && !subject.dirty() &&
              !subject.restartRequired() && !subject.canSave(),
          "successful repair left incorrect model state");
}

void testSaveFreezesAndExactCompletionControlsPromotion() {
  const auto active = remappedRecord();
  auto subject = model(active, active);
  require(
      subject.setSensitivityPermille(ControllerAxisElement::rightStickX, 1500U)
          .accepted(),
      "save fixture edit failed");
  const auto draft = subject.draftRecord();
  const auto first = subject.beginSave();
  require(first.has_value() && first->serial == 1U &&
              first->candidate == draft && !first->repairsPersistence,
          "first save did not capture the exact draft");
  require(subject.phase() == ControllerInputProfileMenuPhase::saving &&
              !subject.canSave() && !subject.canCancel(),
          "save did not freeze the model");

  const auto frozen =
      subject.setInverted(static_cast<ControllerAxisElement>(0xFFU), true);
  require(frozen.status ==
                  ControllerInputProfileMenuEditStatus::saveInProgress &&
              subject.draftRecord() == draft && !subject.cancelDraft() &&
              !subject.beginSave().has_value(),
          "in-flight save allowed mutation or a concurrent ticket");

  auto forged = *first;
  ++forged.serial;
  require(!subject.finishSaveSuccess(forged) &&
              !subject.finishSaveFailure(forged),
          "forged serial completed a save");
  forged = *first;
  forged.candidate.axes[0U].inverted = 1U;
  require(!subject.finishSaveSuccess(forged),
          "forged candidate completed a save");
  forged = *first;
  forged.repairsPersistence = true;
  require(!subject.finishSaveSuccess(forged),
          "forged repair intent completed a save");

  require(subject.finishSaveFailure(*first),
          "exact failure ticket was rejected");
  require(subject.dirty() && subject.draftRecord() == draft &&
              subject.persistedRecord() == active && subject.canSave(),
          "failed save did not preserve a retryable draft");

  const auto retry = subject.beginSave();
  require(retry.has_value() && retry->serial == 2U &&
              retry->candidate == first->candidate,
          "retry did not use a new serial for the same candidate");
  require(!subject.finishSaveSuccess(*first) &&
              !subject.finishSaveFailure(*first),
          "stale ticket changed the retry");
  require(subject.finishSaveSuccess(*retry),
          "exact retry success was rejected");
  require(subject.activeRecord() == active &&
              subject.persistedRecord() == draft &&
              subject.draftRecord() == draft && !subject.dirty() &&
              subject.restartRequired(),
          "successful save changed active state or failed to promote durable "
          "state");
  require(!subject.finishSaveSuccess(*retry) &&
              !subject.finishSaveFailure(*retry),
          "duplicate completion was accepted");

  require(
      subject.setSensitivityPermille(ControllerAxisElement::rightStickX, 1000U)
          .accepted(),
      "edit back to active failed");
  const auto restore = subject.beginSave();
  require(restore.has_value() && subject.finishSaveSuccess(*restore),
          "save back to active failed");
  require(!subject.restartRequired() && subject.activeRecord() == active,
          "persisting the active record did not clear restart state");
}

void testSerialExhaustionFailsClosedWithoutWrap() {
  const auto almostMaximum = std::numeric_limits<std::uint64_t>::max() - 1U;
  auto subject = defaultModel({}, almostMaximum);
  require(
      subject.setInverted(ControllerAxisElement::leftStickY, true).accepted(),
      "serial-exhaustion edit failed");
  const auto last = subject.beginSave();
  require(last.has_value() &&
              last->serial == std::numeric_limits<std::uint64_t>::max() &&
              subject.exhausted(),
          "last representable serial was not issued");
  require(subject.finishSaveFailure(*last),
          "last representable ticket could not fail");
  require(subject.dirty() && !subject.canSave() &&
              !subject.beginSave().has_value(),
          "save serial wrapped after exhaustion");

  auto alreadyExhausted =
      defaultModel({}, std::numeric_limits<std::uint64_t>::max());
  require(alreadyExhausted.setInverted(ControllerAxisElement::leftStickY, true)
              .accepted(),
          "already-exhausted model blocked editing");
  require(alreadyExhausted.exhausted() && !alreadyExhausted.canSave() &&
              !alreadyExhausted.beginSave().has_value(),
          "initial maximum serial did not fail closed");
}

void testPreviewHelperMatchesConfiguredRuntimeBridge() {
  auto subject = defaultModel();
  require(
      subject.setInnerDeadzoneQ15(ControllerAxisElement::leftStickX, 5000U)
              .accepted() &&
          subject
              .setOuterSaturationQ15(ControllerAxisElement::leftStickX, 25000U)
              .accepted() &&
          subject
              .setSensitivityPermille(ControllerAxisElement::leftStickX, 1250U)
              .accepted() &&
          subject.setInverted(ControllerAxisElement::leftStickX, true)
              .accepted(),
      "preview calibration fixture failed");

  const auto &profile = subject.resolvedDraftProfile();
  const auto belowPositive = transformControllerAxisForTransport(
      4095, ControllerAxisElement::rightStickX, profile);
  const auto boundaryPositive = transformControllerAxisForTransport(
      4096, ControllerAxisElement::rightStickX, profile);
  const auto belowNegative = transformControllerAxisForTransport(
      -4095, ControllerAxisElement::rightStickY, profile);
  const auto boundaryNegative = transformControllerAxisForTransport(
      -4096, ControllerAxisElement::rightStickY, profile);
  require(belowPositive == 0 && boundaryPositive == 4096 &&
              belowNegative == 0 && boundaryNegative == -4096,
          "preview helper did not preserve the exact 4095/4096 floor");
  require(!transformControllerAxisForTransport(
               static_cast<airfix::input::Q15>(-32768),
               ControllerAxisElement::leftStickX, profile)
                  .has_value() &&
              !transformControllerAxisForTransport(
                   5000, ControllerAxisElement::count, profile)
                   .has_value(),
          "preview helper accepted invalid Q15 or axis input");

  const auto preview = transformControllerAxisForTransport(
      15000, ControllerAxisElement::leftStickX, profile);
  require(preview.has_value() && *preview == -20480,
          "preview helper produced the wrong calibrated value");

  const auto prepared =
      airfix::input::prepareControllerInputRuntimeConfiguration(profile);
  require(prepared.complete(), "preview profile did not prepare for runtime");
  ControllerInputBatchBridge bridge{*prepared.configuration};
  ControllerSample sample{};
  sample.bank = 15000;
  ControllerInputBatch batch{
      .generation = 1U,
      .startingState = {},
      .finalState = sample,
  };
  OutputBuffer output{};
  const auto runtime = bridge.process(batch, output);
  require(runtime.accepted() && runtime.emissionCount != 0U &&
              output[0U].control ==
                  airfix::input::controls::controller::leftStickX &&
              output[0U].kind == PhysicalEventKind::analog &&
              output[0U].value == *preview,
          "configured runtime bridge diverged from the preview helper");
}

void testDigitalGameplayMoveAllowsDisjointMenuUse() {
  auto subject = defaultModel();
  require(
      subject.setSensitivityPermille(ControllerAxisElement::rightStickY, 1500U)
          .accepted(),
      "move fixture calibration edit failed");
  const auto before = subject.draftRecord();
  const auto selected = subject.draftDigitalGameplayBinding(
      ControllerDigitalGameplayAction::missionStatus);
  require(selected.editable(), "move fixture action is not editable");

  const auto result = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::missionStatus,
      airfix::input::controls::controller::facePrimary);
  require(result.accepted() && result.bindingIndex == selected.bindingIndex &&
              result.conflictIndex ==
                  airfix::input::controllerInputProfileNoIndex,
          "disjoint-context move failed");
  const auto &after = subject.draftRecord();
  const auto &binding = after.bindings[selected.bindingIndex];
  require(binding.control == airfix::input::controls::controller::facePrimary &&
              binding.physicalKind == PhysicalEventKind::digital &&
              binding.scale == airfix::input::q15One &&
              binding.meaningfulThreshold == 1 &&
              binding.blocksNeutralGate == 1U &&
              binding.target ==
                  static_cast<std::uint8_t>(DigitalAction::missionStatus) &&
              binding.contexts == gameplayContext,
          "button move did not normalize the selected transport");
  require(after.bindingCount == before.bindingCount &&
              after.axes == before.axes &&
              after.axes[axisIndex(ControllerAxisElement::rightStickY)]
                      .sensitivityPermille == 1500U &&
              resolveControllerInputProfile(after).complete(),
          "button move changed calibration/count or produced an invalid draft");
  for (std::size_t index = 0U; index < after.bindingCount; ++index) {
    if (index != selected.bindingIndex) {
      require(after.bindings[index] == before.bindings[index],
              "button move changed an unrelated binding");
    }
  }
}

void testDigitalGameplayConflictIsCancelFirstAndSwapIsAtomic() {
  auto subject = defaultModel();
  const auto before = subject.draftRecord();
  const auto camera = subject.draftDigitalGameplayBinding(
      ControllerDigitalGameplayAction::cameraCycle);
  const auto primary = subject.draftDigitalGameplayBinding(
      ControllerDigitalGameplayAction::primaryFire);
  require(camera.editable() && primary.editable(),
          "swap fixture actions are not editable");

  const auto conflict = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::controls::controller::rightTrigger);
  require(
      conflict.status == ControllerInputProfileBindingRemapStatus::conflict &&
          conflict.canSwap() && conflict.bindingIndex == camera.bindingIndex &&
          conflict.conflictIndex == primary.bindingIndex &&
          conflict.conflictingAction ==
              ControllerDigitalGameplayAction::primaryFire &&
          subject.draftRecord() == before,
      "cancel-first conflict mutated state or hid the swap");

  const auto swapped = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::controls::controller::rightTrigger,
      ControllerInputProfileBindingConflictResolution::swap);
  require(swapped.accepted() && swapped.bindingIndex == camera.bindingIndex &&
              swapped.conflictIndex == primary.bindingIndex,
          "supported conflict did not swap");
  const auto &after = subject.draftRecord();
  const auto &cameraBinding = after.bindings[camera.bindingIndex];
  const auto &primaryBinding = after.bindings[primary.bindingIndex];
  require(cameraBinding.control ==
                  airfix::input::controls::controller::rightTrigger &&
              cameraBinding.physicalKind == PhysicalEventKind::analog &&
              cameraBinding.meaningfulThreshold ==
                  airfix::input::controllerTriggerActuationQ15 &&
              cameraBinding.scale == airfix::input::q15One &&
              primaryBinding.control ==
                  airfix::input::controls::controller::faceLeft &&
              primaryBinding.physicalKind == PhysicalEventKind::digital &&
              primaryBinding.meaningfulThreshold == 1 &&
              primaryBinding.scale == airfix::input::q15One,
          "swap did not normalize trigger/button transports");
  require(after.bindingCount == before.bindingCount &&
              after.axes == before.axes &&
              resolveControllerInputProfile(after).complete(),
          "swap changed calibration/count or produced an invalid draft");
  for (std::size_t index = 0U; index < after.bindingCount; ++index) {
    if (index != camera.bindingIndex && index != primary.bindingIndex) {
      require(after.bindings[index] == before.bindings[index],
              "swap changed an unrelated binding");
    }
  }
}

void testProtectedAndUnsupportedRemapsFailAtomically() {
  auto subject = defaultModel();
  const auto before = subject.draftRecord();

  const auto pauseConflict = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::controls::controller::menu,
      ControllerInputProfileBindingConflictResolution::swap);
  require(pauseConflict.status ==
                  ControllerInputProfileBindingRemapStatus::protectedConflict &&
              pauseConflict.conflictIndex !=
                  airfix::input::controllerInputProfileNoIndex &&
              !pauseConflict.conflictingAction.has_value() &&
              subject.draftRecord() == before,
          "protected pause conflict was swapped or mutated");

  const auto throttleConflict = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::controls::controller::dpadUp);
  require(throttleConflict.status ==
                  ControllerInputProfileBindingRemapStatus::protectedConflict &&
              subject.draftRecord() == before,
          "protected throttle conflict was accepted");

  const auto backConflict = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::controls::controller::faceSecondary,
      ControllerInputProfileBindingConflictResolution::swap);
  require(backConflict.status ==
                  ControllerInputProfileBindingRemapStatus::protectedConflict &&
              subject.draftRecord() == before,
          "protected global-back conflict was swapped or mutated");

  const auto invalidAction = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::count,
      airfix::input::controls::controller::facePrimary);
  require(invalidAction.status ==
                  ControllerInputProfileBindingRemapStatus::invalidAction &&
              subject.draftRecord() == before,
          "forged action changed the draft");

  const auto invalidResolution = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::controls::controller::rightTrigger,
      static_cast<ControllerInputProfileBindingConflictResolution>(255U));
  require(invalidResolution.status ==
                  ControllerInputProfileBindingRemapStatus::invalidResolution &&
              subject.draftRecord() == before,
          "forged conflict resolution changed the draft");

  const auto continuous = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::controls::controller::leftStickX);
  require(continuous.status ==
                  ControllerInputProfileBindingRemapStatus::invalidControl &&
              subject.draftRecord() == before,
          "continuous axis entered the button editor");

  const auto forged = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::ControlId{65000U});
  require(forged.status ==
                  ControllerInputProfileBindingRemapStatus::invalidControl &&
              subject.draftRecord() == before,
          "forged control changed the draft");
}

void testCustomMultiBindingActionIsReportedButNotGuessed() {
  auto record = makeDefaultControllerInputProfileRecord();
  const auto mission = airfix::input::controllerDigitalGameplayBinding(
      record, ControllerDigitalGameplayAction::missionStatus);
  require(mission.editable() &&
              record.bindingCount <
                  airfix::input::controllerProfileBindingCapacity,
          "custom-layout fixture cannot append");
  auto duplicate = record.bindings[mission.bindingIndex];
  duplicate.control = airfix::input::controls::controller::facePrimary;
  duplicate.physicalKind = PhysicalEventKind::digital;
  record.bindings[record.bindingCount] = duplicate;
  ++record.bindingCount;
  require(resolveControllerInputProfile(record).complete(),
          "custom multi-binding fixture is invalid");

  auto subject = model(record, record);
  const auto before = subject.draftRecord();
  const auto result = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::missionStatus,
      airfix::input::controls::controller::dpadLeft);
  require(result.status ==
                  ControllerInputProfileBindingRemapStatus::actionUnavailable &&
              result.bindingStatus ==
                  ControllerDigitalGameplayBindingStatus::ambiguous &&
              subject.draftRecord() == before,
          "custom multi-binding action was guessed or mutated");
}

void testBindingResetAndSaveFreezeShareTheExistingDraft() {
  auto subject = defaultModel();
  require(
      subject.setInverted(ControllerAxisElement::leftStickY, true).accepted(),
      "reset fixture calibration edit failed");
  const auto moved = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::missionStatus,
      airfix::input::controls::controller::facePrimary);
  require(moved.accepted(), "reset fixture remap failed");
  const auto calibrationBeforeReset = subject.draftRecord().axes;

  require(subject.resetAllControllerBindings().accepted(),
          "reset all controller bindings failed");
  const auto defaults = makeDefaultControllerInputProfileRecord();
  require(subject.draftRecord().bindings == defaults.bindings &&
              subject.draftRecord().bindingCount == defaults.bindingCount &&
              subject.draftRecord().axes == calibrationBeforeReset &&
              subject.draftRecord().axes != defaults.axes,
          "binding reset changed calibration or retained assignments");

  const auto movedAgain = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::missionStatus,
      airfix::input::controls::controller::facePrimary);
  require(movedAgain.accepted(), "save-freeze remap fixture failed");
  const auto frozenDraft = subject.draftRecord();
  const auto ticket = subject.beginSave();
  require(ticket.has_value(), "remapped draft did not begin save");
  const auto encoded =
      airfix::settings::encodeControllerInputProfileDocument(ticket->candidate);
  const auto decoded =
      airfix::settings::decodeControllerInputProfileDocument(encoded);
  require(std::holds_alternative<ControllerInputProfileRecord>(decoded) &&
              std::get<ControllerInputProfileRecord>(decoded) ==
                  ticket->candidate,
          "remapped save ticket did not survive an exact AFIP round trip");
  const auto frozen = subject.rebindDigitalGameplayAction(
      ControllerDigitalGameplayAction::cameraCycle,
      airfix::input::controls::controller::facePrimary);
  require(frozen.status ==
                  ControllerInputProfileBindingRemapStatus::saveInProgress &&
              subject.draftRecord() == frozenDraft &&
              subject.resetAllControllerBindings().status ==
                  ControllerInputProfileMenuEditStatus::saveInProgress,
          "in-flight save allowed binding mutation or reset");
  require(subject.finishSaveFailure(*ticket) && subject.canSave() &&
              subject.draftRecord() == frozenDraft,
          "failed remap save did not retain a retryable complete draft");
}

static_assert(
    std::is_trivially_copyable_v<ControllerInputProfileMenuSaveTicket>);
static_assert(std::is_trivially_copyable_v<
              airfix::settings::ControllerInputProfileBindingRemapResult>);
static_assert(std::is_trivially_copyable_v<ControllerInputProfileMenuModel>);
static_assert(noexcept(ControllerInputProfileMenuModel::create({}, {})));
static_assert(noexcept(transformControllerAxisForTransport(
    0, ControllerAxisElement::leftStickX,
    std::declval<const airfix::input::ResolvedControllerInputProfile &>())));

} // namespace

int main() {
  try {
    testCreationRejectsInvalidRecordsAndExposesBoundedState();
    testTypedEditsCoverAllAxesAndPreserveBindings();
    testInvalidEditsAreAtomicAndLimitsArePublic();
    testResetAndCancelChangeOnlyCalibration();
    testRepairCapabilityAllowsCleanSave();
    testSaveFreezesAndExactCompletionControlsPromotion();
    testSerialExhaustionFailsClosedWithoutWrap();
    testPreviewHelperMatchesConfiguredRuntimeBridge();
    testDigitalGameplayMoveAllowsDisjointMenuUse();
    testDigitalGameplayConflictIsCancelFirstAndSwapIsAtomic();
    testProtectedAndUnsupportedRemapsFailAtomically();
    testCustomMultiBindingActionIsReportedButNotGuessed();
    testBindingResetAndSaveFreezeShareTheExistingDraft();
    std::cout << "controller-input profile menu model tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "controller-input profile menu model test failure: "
              << error.what() << '\n';
    return 1;
  }
}
