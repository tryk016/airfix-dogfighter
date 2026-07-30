#include "airfix/input/ControllerInputProfile.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using airfix::input::AnalogAxis;
using airfix::input::Binding;
using airfix::input::BindingTable;
using airfix::input::BindingTargetKind;
using airfix::input::ContextMask;
using airfix::input::ControlId;
using airfix::input::ControllerAxisElement;
using airfix::input::ControllerBindingRecord;
using airfix::input::ControllerInputProfileIssueKind;
using airfix::input::controllerInputProfileNoIndex;
using airfix::input::ControllerInputProfileRecord;
using airfix::input::controllerProfileBindingCapacity;
using airfix::input::ControllerResponseCurve;
using airfix::input::DigitalAction;
using airfix::input::gameplayContext;
using airfix::input::makeDefaultBindings;
using airfix::input::makeDefaultControllerInputProfileRecord;
using airfix::input::menuContext;
using airfix::input::PhysicalEventKind;
using airfix::input::Q15;
using airfix::input::q15Min;
using airfix::input::q15One;
using airfix::input::q15Zero;
using airfix::input::resolveControllerInputProfile;
using airfix::input::SourceKind;
using airfix::input::transformControllerAxis;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireIssue(
    const ControllerInputProfileRecord &record,
    const ControllerInputProfileIssueKind expected,
    const std::optional<std::size_t> expectedIndex = std::nullopt) {
  const auto result = resolveControllerInputProfile(record);
  require(!result.complete(), "invalid profile unexpectedly resolved");
  require(!result.profile.has_value(),
          "invalid profile returned a partial resolved value");
  require(result.issue.has_value(), "invalid profile omitted its issue");
  require(result.issue->kind == expected, "profile returned wrong issue");
  if (expectedIndex.has_value()) {
    require(result.issue->index == *expectedIndex,
            "profile issue returned wrong index");
  }
}

[[nodiscard]] bool sameBinding(const Binding &left,
                               const Binding &right) noexcept {
  return left.sourceKind == right.sourceKind && left.control == right.control &&
         left.physicalKind == right.physicalKind &&
         left.targetKind == right.targetKind && left.target == right.target &&
         left.contexts == right.contexts && left.scale == right.scale &&
         left.meaningfulThreshold == right.meaningfulThreshold &&
         left.blocksNeutralGate == right.blocksNeutralGate;
}

[[nodiscard]] Binding
asBinding(const ControllerBindingRecord &record) noexcept {
  return {
      record.sourceKind,
      record.control,
      record.physicalKind,
      record.targetKind,
      record.target,
      record.contexts,
      record.scale,
      record.meaningfulThreshold,
      record.blocksNeutralGate != 0U,
  };
}

[[nodiscard]] bool containsBinding(const BindingTable &table,
                                   const Binding &expected) noexcept {
  for (const auto &candidate : table) {
    if (sameBinding(candidate, expected)) {
      return true;
    }
  }
  return false;
}

template <typename Predicate>
void removeBindings(ControllerInputProfileRecord &record, Predicate predicate) {
  std::size_t destination = 0U;
  const auto originalCount = static_cast<std::size_t>(record.bindingCount);
  for (std::size_t source = 0U; source < originalCount; ++source) {
    if (!predicate(record.bindings[source])) {
      record.bindings[destination] = record.bindings[source];
      ++destination;
    }
  }
  for (std::size_t index = destination; index < originalCount; ++index) {
    record.bindings[index] = {};
  }
  record.bindingCount = static_cast<std::uint8_t>(destination);
}

[[nodiscard]] std::size_t
findBinding(const ControllerInputProfileRecord &record, const ControlId control,
            const ContextMask requiredContext) {
  for (std::size_t index = 0U; index < record.bindingCount; ++index) {
    const auto &candidate = record.bindings[index];
    if (candidate.control == control &&
        (candidate.contexts & requiredContext) != 0U) {
      return index;
    }
  }
  throw std::runtime_error("expected default binding was not found");
}

void testControllerControlTraitsAreCompleteAndTyped() {
  using namespace airfix::input::controls::controller;
  constexpr std::array analogAxes{
      leftStickX,
      leftStickY,
      rightStickX,
      rightStickY,
  };
  for (const auto control : analogAxes) {
    const auto traits = airfix::input::controllerControlTraits(control);
    require(traits.has_value() &&
                traits->physicalKind == PhysicalEventKind::analog &&
                !traits->binaryTrigger,
            "stick axis has the wrong controller traits");
  }

  constexpr std::array triggers{rightTrigger, leftTrigger};
  for (const auto control : triggers) {
    const auto traits = airfix::input::controllerControlTraits(control);
    require(traits.has_value() &&
                traits->physicalKind == PhysicalEventKind::analog &&
                traits->binaryTrigger,
            "trigger has the wrong controller traits");
  }

  constexpr std::array buttons{
      dpadUp,      dpadDown,      rightShoulder,   leftShoulder,
      faceLeft,    faceTop,       rightStickClick, menu,
      facePrimary, faceSecondary, dpadLeft,        dpadRight,
  };
  for (const auto control : buttons) {
    const auto traits = airfix::input::controllerControlTraits(control);
    require(traits.has_value() &&
                traits->physicalKind == PhysicalEventKind::digital &&
                !traits->binaryTrigger,
            "button has the wrong controller traits");
  }
  require(!airfix::input::controllerControlTraits(airfix::input::ControlId{0U})
                  .has_value() &&
              !airfix::input::controllerControlTraits(
                   airfix::input::ControlId{65000U})
                   .has_value(),
          "forged controller control acquired transport traits");
}

void testDefaultProfileAndCompilation() {
  const auto record = makeDefaultControllerInputProfileRecord();
  const auto resolved = resolveControllerInputProfile(record);
  require(resolved.complete(), "default controller profile was invalid");
  require(resolved.profile->schemaVersion() ==
              airfix::input::controllerInputProfileRecordSchemaVersion,
          "resolved default schema changed");
  require(resolved.profile->bindings().size() == record.bindingCount,
          "resolved default binding count changed");
  require(resolved.profile->record() == record,
          "resolved profile changed its semantic record");

  const auto compiled =
      airfix::input::compileControllerInputBindings(*resolved.profile);
  require(compiled.complete(), "default profile did not compile");

  std::size_t expectedNonControllerCount = 0U;
  for (const auto &candidate : makeDefaultBindings()) {
    if (candidate.sourceKind != SourceKind::controller) {
      ++expectedNonControllerCount;
      require(containsBinding(*compiled.bindings, candidate),
              "compile lost a default non-controller binding");
    }
  }
  require(compiled.bindings->size() ==
              expectedNonControllerCount + record.bindingCount,
          "compiled table has an unexpected binding count");
  for (std::size_t index = 0U; index < record.bindingCount; ++index) {
    require(
        containsBinding(*compiled.bindings, asBinding(record.bindings[index])),
        "compile lost a resolved controller binding");
  }
}

void testDefaultAxisTransformIsFullQ15Identity() {
  const auto resolved =
      resolveControllerInputProfile(makeDefaultControllerInputProfileRecord());
  require(resolved.complete(), "identity profile did not resolve");

  for (std::int32_t raw = q15Min; raw <= q15One; ++raw) {
    for (std::uint8_t axis = 0U;
         axis < static_cast<std::uint8_t>(ControllerAxisElement::count);
         ++axis) {
      const auto transformed = transformControllerAxis(
          static_cast<Q15>(raw), static_cast<ControllerAxisElement>(axis),
          *resolved.profile);
      require(transformed.has_value() && *transformed == raw,
              "default calibration was not exact Q15 identity");
    }
  }

  const auto invalidRaw = transformControllerAxis(
      static_cast<Q15>(-32768), ControllerAxisElement::leftStickX,
      *resolved.profile);
  require(!invalidRaw.has_value(), "-32768 was accepted");
  const auto invalidAxis = transformControllerAxis(
      1, ControllerAxisElement::count, *resolved.profile);
  require(!invalidAxis.has_value(), "forged axis enum was accepted");
}

void testAxisCalibrationAndNearestEvenRounding() {
  auto record = makeDefaultControllerInputProfileRecord();
  auto &calibration = record.axes[0U];
  calibration.innerDeadzoneQ15 = 4096U;
  calibration.outerSaturationQ15 = 28672U;

  auto resolved = resolveControllerInputProfile(record);
  require(resolved.complete(), "linear calibrated profile was invalid");
  require(transformControllerAxis(4096, ControllerAxisElement::leftStickX,
                                  *resolved.profile) == q15Zero,
          "inner deadzone boundary was not neutral");
  require(transformControllerAxis(28672, ControllerAxisElement::leftStickX,
                                  *resolved.profile) == q15One,
          "outer saturation boundary did not reach positive Q15");
  require(transformControllerAxis(-28672, ControllerAxisElement::leftStickX,
                                  *resolved.profile) == q15Min,
          "negative outer saturation boundary was asymmetric");

  record.axes[0U].inverted = 1U;
  resolved = resolveControllerInputProfile(record);
  require(resolved.complete(), "inverted profile was invalid");
  const auto inverted = transformControllerAxis(
      10000, ControllerAxisElement::leftStickX, *resolved.profile);
  require(inverted.has_value() && *inverted < 0,
          "axis inversion did not change sign");

  auto halfSensitivity = makeDefaultControllerInputProfileRecord();
  halfSensitivity.axes[0U].sensitivityPermille = 500U;
  resolved = resolveControllerInputProfile(halfSensitivity);
  require(resolved.complete(), "half-sensitivity profile was invalid");
  require(transformControllerAxis(1, ControllerAxisElement::leftStickX,
                                  *resolved.profile) == 0,
          "nearest-even 0.5 did not round to even zero");
  require(transformControllerAxis(3, ControllerAxisElement::leftStickX,
                                  *resolved.profile) == 2,
          "nearest-even 1.5 did not round to even two");

  auto highSensitivity = makeDefaultControllerInputProfileRecord();
  highSensitivity.axes[0U].sensitivityPermille = 2000U;
  resolved = resolveControllerInputProfile(highSensitivity);
  require(resolved.complete(), "high-sensitivity profile was invalid");
  require(transformControllerAxis(20000, ControllerAxisElement::leftStickX,
                                  *resolved.profile) == q15One,
          "sensitivity output did not clamp to Q15");
}

void testResponseCurves() {
  auto record = makeDefaultControllerInputProfileRecord();
  record.axes[0U].responseCurve = ControllerResponseCurve::linear;
  auto resolved = resolveControllerInputProfile(record);
  require(resolved.complete(), "linear response profile was invalid");
  const auto linear = transformControllerAxis(
      16384, ControllerAxisElement::leftStickX, *resolved.profile);

  record.axes[0U].responseCurve = ControllerResponseCurve::squared;
  resolved = resolveControllerInputProfile(record);
  require(resolved.complete(), "squared response profile was invalid");
  const auto squared = transformControllerAxis(
      16384, ControllerAxisElement::leftStickX, *resolved.profile);

  record.axes[0U].responseCurve = ControllerResponseCurve::cubic;
  resolved = resolveControllerInputProfile(record);
  require(resolved.complete(), "cubic response profile was invalid");
  const auto cubic = transformControllerAxis(
      16384, ControllerAxisElement::leftStickX, *resolved.profile);

  require(linear.has_value() && squared.has_value() && cubic.has_value() &&
              *linear > *squared && *squared > *cubic && *cubic > 0,
          "response curves are not monotonic at half range");
}

void testAxisValidationFailsClosed() {
  auto record = makeDefaultControllerInputProfileRecord();
  record.axes[1U].innerDeadzoneQ15 = 32768U;
  requireIssue(record, ControllerInputProfileIssueKind::invalidInnerDeadzone,
               1U);

  record = makeDefaultControllerInputProfileRecord();
  record.axes[2U].outerSaturationQ15 = 32768U;
  requireIssue(record, ControllerInputProfileIssueKind::invalidOuterSaturation,
               2U);

  record = makeDefaultControllerInputProfileRecord();
  record.axes[0U].innerDeadzoneQ15 = 100U;
  record.axes[0U].outerSaturationQ15 = 100U;
  requireIssue(record, ControllerInputProfileIssueKind::invalidOuterSaturation,
               0U);

  record = makeDefaultControllerInputProfileRecord();
  record.axes[3U].sensitivityPermille = 249U;
  requireIssue(record, ControllerInputProfileIssueKind::invalidSensitivity, 3U);
  record.axes[3U].sensitivityPermille = 2001U;
  requireIssue(record, ControllerInputProfileIssueKind::invalidSensitivity, 3U);

  record = makeDefaultControllerInputProfileRecord();
  record.axes[1U].responseCurve = ControllerResponseCurve::count;
  requireIssue(record, ControllerInputProfileIssueKind::invalidResponseCurve,
               1U);

  record = makeDefaultControllerInputProfileRecord();
  record.axes[2U].inverted = 2U;
  requireIssue(record, ControllerInputProfileIssueKind::invalidInversion, 2U);
}

void testBindingFieldValidationFailsClosed() {
  auto record = makeDefaultControllerInputProfileRecord();
  record.schemaVersion = 2U;
  requireIssue(record, ControllerInputProfileIssueKind::unsupportedSchema,
               controllerInputProfileNoIndex);

  record = makeDefaultControllerInputProfileRecord();
  record.bindingCount =
      static_cast<std::uint8_t>(controllerProfileBindingCapacity + 1U);
  requireIssue(record, ControllerInputProfileIssueKind::bindingCountOutOfRange);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[record.bindingCount] = record.bindings[0U];
  requireIssue(record,
               ControllerInputProfileIssueKind::nonCanonicalUnusedBinding,
               record.bindingCount);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].sourceKind = SourceKind::keyboard;
  requireIssue(record, ControllerInputProfileIssueKind::nonControllerSource,
               0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].control = ControlId{65000U};
  requireIssue(record, ControllerInputProfileIssueKind::invalidControl, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].physicalKind = PhysicalEventKind::count;
  requireIssue(record, ControllerInputProfileIssueKind::invalidPhysicalKind,
               0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].physicalKind = PhysicalEventKind::digital;
  requireIssue(
      record, ControllerInputProfileIssueKind::physicalControlKindMismatch, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].targetKind = BindingTargetKind::weaponSelection;
  requireIssue(record, ControllerInputProfileIssueKind::invalidTargetKind, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].target = static_cast<std::uint8_t>(AnalogAxis::count);
  requireIssue(record, ControllerInputProfileIssueKind::invalidTarget, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].contexts = 0U;
  requireIssue(record, ControllerInputProfileIssueKind::invalidContexts, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].scale = 0;
  requireIssue(record, ControllerInputProfileIssueKind::invalidScale, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].scale = static_cast<Q15>(-32768);
  requireIssue(record, ControllerInputProfileIssueKind::invalidScale, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].meaningfulThreshold = -1;
  requireIssue(record,
               ControllerInputProfileIssueKind::invalidMeaningfulThreshold, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].meaningfulThreshold = q15One;
  requireIssue(record,
               ControllerInputProfileIssueKind::invalidMeaningfulThreshold, 0U);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[0U].blocksNeutralGate = 2U;
  requireIssue(record,
               ControllerInputProfileIssueKind::invalidBlocksNeutralGate, 0U);
}

void testTriggersRequireTheFixedBinaryV1Contract() {
  using namespace airfix::input::controls::controller;

  auto record = makeDefaultControllerInputProfileRecord();
  const auto triggerIndex = findBinding(record, rightTrigger, gameplayContext);
  require(record.bindings[triggerIndex].meaningfulThreshold ==
              airfix::input::controllerTriggerActuationQ15,
          "default trigger does not use the V1 actuation threshold");

  record.bindings[triggerIndex].meaningfulThreshold = 1;
  requireIssue(record,
               ControllerInputProfileIssueKind::triggerRequiresBinaryBinding,
               triggerIndex);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[triggerIndex].targetKind = BindingTargetKind::analog;
  record.bindings[triggerIndex].target =
      static_cast<std::uint8_t>(AnalogAxis::flightThrottleDelta);
  requireIssue(record,
               ControllerInputProfileIssueKind::triggerRequiresBinaryBinding,
               triggerIndex);

  record = makeDefaultControllerInputProfileRecord();
  record.bindings[triggerIndex].scale = q15Min;
  requireIssue(record,
               ControllerInputProfileIssueKind::triggerRequiresBinaryBinding,
               triggerIndex);
}

void testContextConflictsFailClosed() {
  auto record = makeDefaultControllerInputProfileRecord();
  const auto duplicateIndex = static_cast<std::size_t>(record.bindingCount);
  require(duplicateIndex < record.bindings.size(),
          "default profile leaves no conflict-test capacity");
  record.bindings[duplicateIndex] = record.bindings[0U];
  ++record.bindingCount;
  requireIssue(record, ControllerInputProfileIssueKind::contextConflict,
               duplicateIndex);
}

void testRecoveryBindingsAreMandatory() {
  const auto digitalTarget = [](const ControllerBindingRecord &binding,
                                const DigitalAction target,
                                const ContextMask context) {
    return binding.targetKind == BindingTargetKind::digital &&
           binding.target == static_cast<std::uint8_t>(target) &&
           (binding.contexts & context) != 0U;
  };
  const auto analogTarget = [](const ControllerBindingRecord &binding,
                               const AnalogAxis target,
                               const ContextMask context) {
    return binding.targetKind == BindingTargetKind::analog &&
           binding.target == static_cast<std::uint8_t>(target) &&
           (binding.contexts & context) != 0U;
  };

  auto record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return digitalTarget(binding, DigitalAction::globalPause, gameplayContext);
  });
  requireIssue(record, ControllerInputProfileIssueKind::missingPause);

  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return digitalTarget(binding, DigitalAction::uiConfirm, menuContext);
  });
  requireIssue(record, ControllerInputProfileIssueKind::missingMenuConfirm);

  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return digitalTarget(binding, DigitalAction::uiCancel, menuContext);
  });
  requireIssue(record, ControllerInputProfileIssueKind::missingMenuCancel);

  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return analogTarget(binding, AnalogAxis::uiNavigateX, menuContext);
  });
  requireIssue(record, ControllerInputProfileIssueKind::missingMenuNavigateX);

  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return analogTarget(binding, AnalogAxis::uiNavigateY, menuContext);
  });
  requireIssue(record, ControllerInputProfileIssueKind::missingMenuNavigateY);

  using namespace airfix::input::controls::controller;
  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return analogTarget(binding, AnalogAxis::uiNavigateX, menuContext) &&
           binding.control != dpadRight;
  });
  requireIssue(record, ControllerInputProfileIssueKind::missingMenuNavigateX);

  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return analogTarget(binding, AnalogAxis::uiNavigateY, menuContext) &&
           binding.control != dpadUp;
  });
  requireIssue(record, ControllerInputProfileIssueKind::missingMenuNavigateY);

  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return (analogTarget(binding, AnalogAxis::uiNavigateX, menuContext) &&
            binding.control != leftStickX) ||
           (analogTarget(binding, AnalogAxis::uiNavigateY, menuContext) &&
            binding.control != leftStickY);
  });
  record.axes[static_cast<std::size_t>(ControllerAxisElement::leftStickX)]
      .sensitivityPermille = 250U;
  record.axes[static_cast<std::size_t>(ControllerAxisElement::leftStickY)]
      .sensitivityPermille = 250U;
  requireIssue(record, ControllerInputProfileIssueKind::missingMenuNavigateX);

  record.axes[static_cast<std::size_t>(ControllerAxisElement::leftStickX)]
      .sensitivityPermille = 500U;
  record.axes[static_cast<std::size_t>(ControllerAxisElement::leftStickY)]
      .sensitivityPermille = 500U;
  require(resolveControllerInputProfile(record).complete(),
          "boundary-reachable stick-only menu recovery was rejected");

  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return analogTarget(binding, AnalogAxis::uiNavigateX, menuContext) &&
           binding.control != dpadLeft && binding.control != dpadRight;
  });
  record.bindings[findBinding(record, dpadLeft, menuContext)].scale = -1;
  record.bindings[findBinding(record, dpadRight, menuContext)].scale = 1;
  requireIssue(record, ControllerInputProfileIssueKind::missingMenuNavigateX);

  record = makeDefaultControllerInputProfileRecord();
  removeBindings(record, [&](const ControllerBindingRecord &binding) {
    return binding.control == leftStickX &&
           (binding.contexts & gameplayContext) != 0U;
  });
  auto &pause = record.bindings[findBinding(record, menu, gameplayContext)];
  pause.control = leftStickX;
  pause.physicalKind = PhysicalEventKind::analog;
  pause.meaningfulThreshold = 16384;
  record.axes[static_cast<std::size_t>(ControllerAxisElement::leftStickX)]
      .sensitivityPermille = 250U;
  requireIssue(record, ControllerInputProfileIssueKind::missingPause);

  record.axes[static_cast<std::size_t>(ControllerAxisElement::leftStickX)]
      .sensitivityPermille = 500U;
  require(resolveControllerInputProfile(record).complete(),
          "boundary-reachable stick pause was rejected");
}

void testValidRemapCompilesAtomically() {
  using namespace airfix::input::controls::controller;
  auto record = makeDefaultControllerInputProfileRecord();
  const auto index = findBinding(record, faceLeft, gameplayContext);
  auto &remapped = record.bindings[index];
  require(remapped.targetKind == BindingTargetKind::digital,
          "camera-cycle default was not digital");
  remapped.target = static_cast<std::uint8_t>(DigitalAction::combatWeaponNext);

  const auto resolved = resolveControllerInputProfile(record);
  require(resolved.complete(), "valid remap did not resolve");
  const auto compiled =
      airfix::input::compileControllerInputBindings(*resolved.profile);
  require(compiled.complete(), "valid remap did not compile");
  require(containsBinding(*compiled.bindings, asBinding(remapped)),
          "compiled table omitted the remapped binding");

  for (const auto &candidate : *compiled.bindings) {
    require(candidate.sourceKind != SourceKind::controller ||
                !(candidate.control == faceLeft &&
                  candidate.targetKind == BindingTargetKind::digital &&
                  candidate.target ==
                      static_cast<std::uint8_t>(DigitalAction::cameraCycle) &&
                  (candidate.contexts & gameplayContext) != 0U),
            "compile retained a replaced controller default");
  }
}

} // namespace

int main() {
  try {
    testControllerControlTraitsAreCompleteAndTyped();
    testDefaultProfileAndCompilation();
    testDefaultAxisTransformIsFullQ15Identity();
    testAxisCalibrationAndNearestEvenRounding();
    testResponseCurves();
    testAxisValidationFailsClosed();
    testBindingFieldValidationFailsClosed();
    testTriggersRequireTheFixedBinaryV1Contract();
    testContextConflictsFailClosed();
    testRecoveryBindingsAreMandatory();
    testValidRemapCompilesAtomically();
  } catch (const std::exception &error) {
    std::cerr << "ControllerInputProfileTests failed: " << error.what() << '\n';
    return 1;
  }

  std::cout << "ControllerInputProfileTests passed\n";
  return 0;
}
