#include "airfix/input/ControllerInputProfile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace airfix::input {
namespace {

[[nodiscard]] constexpr ControllerInputProfileIssue
issue(const ControllerInputProfileIssueKind kind,
      const std::size_t index = controllerInputProfileNoIndex) noexcept {
  return {kind, index};
}

[[nodiscard]] constexpr bool
validContexts(const ContextMask contexts) noexcept {
  return contexts != 0U &&
         (contexts & static_cast<ContextMask>(~allContexts)) == 0U;
}

[[nodiscard]] constexpr bool
validControllerControl(const ControlId control) noexcept {
  using namespace controls::controller;
  return control == leftStickX || control == leftStickY ||
         control == rightStickX || control == rightStickY ||
         control == dpadUp || control == dpadDown || control == rightTrigger ||
         control == leftTrigger || control == rightShoulder ||
         control == leftShoulder || control == faceLeft || control == faceTop ||
         control == rightStickClick || control == menu ||
         control == facePrimary || control == faceSecondary ||
         control == dpadLeft || control == dpadRight;
}

[[nodiscard]] constexpr PhysicalEventKind
expectedPhysicalKind(const ControlId control) noexcept {
  using namespace controls::controller;
  if (control == leftStickX || control == leftStickY ||
      control == rightStickX || control == rightStickY ||
      control == rightTrigger || control == leftTrigger) {
    return PhysicalEventKind::analog;
  }
  return PhysicalEventKind::digital;
}

[[nodiscard]] constexpr bool isTrigger(const ControlId control) noexcept {
  using namespace controls::controller;
  return control == rightTrigger || control == leftTrigger;
}

[[nodiscard]] constexpr bool activeIn(const ControllerBindingRecord &binding,
                                      const ContextMask context) noexcept {
  return (binding.contexts & context) != 0U;
}

[[nodiscard]] constexpr bool
targetsDigital(const ControllerBindingRecord &binding,
               const DigitalAction action, const ContextMask context) noexcept {
  return binding.targetKind == BindingTargetKind::digital &&
         binding.target == static_cast<std::uint8_t>(action) &&
         activeIn(binding, context);
}

[[nodiscard]] constexpr bool
targetsAnalog(const ControllerBindingRecord &binding, const AnalogAxis axis,
              const ContextMask context) noexcept {
  return binding.targetKind == BindingTargetKind::analog &&
         binding.target == static_cast<std::uint8_t>(axis) &&
         activeIn(binding, context);
}

struct AnalogDirectionReachability final {
  bool negative{};
  bool positive{};
};

[[nodiscard]] constexpr ControllerBindingRecord
bindingRecord(const Binding &binding) noexcept {
  return {
      binding.sourceKind,
      binding.control,
      binding.physicalKind,
      binding.targetKind,
      binding.target,
      binding.contexts,
      binding.scale,
      binding.meaningfulThreshold,
      static_cast<std::uint8_t>(binding.blocksNeutralGate ? 1U : 0U),
  };
}

[[nodiscard]] constexpr Binding
binding(const ControllerBindingRecord &record) noexcept {
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

[[nodiscard]] constexpr std::uint64_t
divideRoundNearestEven(const std::uint64_t numerator,
                       const std::uint64_t denominator) noexcept {
  const auto quotient = numerator / denominator;
  const auto remainder = numerator % denominator;
  const auto complement = denominator - remainder;
  if (remainder > complement ||
      (remainder == complement && (quotient & 1U) != 0U)) {
    return quotient + 1U;
  }
  return quotient;
}

[[nodiscard]] constexpr std::optional<ControllerAxisElement>
calibratedAxisElement(const ControlId control) noexcept {
  using namespace controls::controller;
  if (control == leftStickX) {
    return ControllerAxisElement::leftStickX;
  }
  if (control == leftStickY) {
    return ControllerAxisElement::leftStickY;
  }
  if (control == rightStickX) {
    return ControllerAxisElement::rightStickX;
  }
  if (control == rightStickY) {
    return ControllerAxisElement::rightStickY;
  }
  return std::nullopt;
}

[[nodiscard]] constexpr std::uint32_t
maximumControllerInputMagnitude(const ControllerInputProfileRecord &record,
                                const ControlId control) noexcept {
  const auto axis = calibratedAxisElement(control);
  if (!axis.has_value()) {
    return static_cast<std::uint16_t>(q15One);
  }
  const auto &calibration = record.axes[static_cast<std::size_t>(*axis)];
  const auto maximum = divideRoundNearestEven(
      static_cast<std::uint64_t>(q15One) * calibration.sensitivityPermille,
      1000U);
  return static_cast<std::uint32_t>(
      std::min<std::uint64_t>(maximum, static_cast<std::uint16_t>(q15One)));
}

[[nodiscard]] constexpr bool
reachableDigitalTarget(const ControllerBindingRecord &binding,
                       const DigitalAction action, const ContextMask context,
                       const ControllerInputProfileRecord &record) noexcept {
  return targetsDigital(binding, action, context) &&
         maximumControllerInputMagnitude(record, binding.control) >=
             static_cast<std::uint16_t>(binding.meaningfulThreshold);
}

[[nodiscard]] constexpr AnalogDirectionReachability
reachableAnalogDirections(const ControllerBindingRecord &binding,
                          const AnalogAxis axis, const ContextMask context,
                          const ControllerInputProfileRecord &record) noexcept {
  if (!targetsAnalog(binding, axis, context)) {
    return {};
  }
  const auto maximumInput =
      maximumControllerInputMagnitude(record, binding.control);
  if (maximumInput <= static_cast<std::uint16_t>(binding.meaningfulThreshold)) {
    return {};
  }
  const auto scale = static_cast<std::int32_t>(binding.scale);
  const auto scaleMagnitude =
      static_cast<std::uint32_t>(scale < 0 ? -scale : scale);
  const auto maximumOutput =
      maximumInput * scaleMagnitude / static_cast<std::uint16_t>(q15One);
  if (maximumOutput < static_cast<std::uint16_t>(uiNavigationActuationQ15)) {
    return {};
  }
  if (calibratedAxisElement(binding.control).has_value()) {
    return {.negative = true, .positive = true};
  }
  return binding.scale < 0
             ? AnalogDirectionReachability{.negative = true, .positive = false}
             : AnalogDirectionReachability{.negative = false, .positive = true};
}

[[nodiscard]] constexpr std::optional<ControllerInputProfileIssue>
validateAxis(const ControllerAxisCalibrationRecord &calibration,
             const std::size_t index) noexcept {
  if (calibration.innerDeadzoneQ15 > static_cast<std::uint16_t>(q15One)) {
    return issue(ControllerInputProfileIssueKind::invalidInnerDeadzone, index);
  }
  if (calibration.outerSaturationQ15 > static_cast<std::uint16_t>(q15One) ||
      calibration.outerSaturationQ15 <= calibration.innerDeadzoneQ15) {
    return issue(ControllerInputProfileIssueKind::invalidOuterSaturation,
                 index);
  }
  if (calibration.sensitivityPermille <
          controllerAxisMinimumSensitivityPermille ||
      calibration.sensitivityPermille >
          controllerAxisMaximumSensitivityPermille) {
    return issue(ControllerInputProfileIssueKind::invalidSensitivity, index);
  }
  if (calibration.responseCurve >= ControllerResponseCurve::count) {
    return issue(ControllerInputProfileIssueKind::invalidResponseCurve, index);
  }
  if (calibration.inverted > 1U) {
    return issue(ControllerInputProfileIssueKind::invalidInversion, index);
  }
  return std::nullopt;
}

[[nodiscard]] constexpr std::optional<ControllerInputProfileIssue>
validateBinding(const ControllerBindingRecord &candidate,
                const std::size_t index) noexcept {
  if (candidate.sourceKind != SourceKind::controller) {
    return issue(ControllerInputProfileIssueKind::nonControllerSource, index);
  }
  if (!validControllerControl(candidate.control)) {
    return issue(ControllerInputProfileIssueKind::invalidControl, index);
  }
  if (candidate.physicalKind >= PhysicalEventKind::count ||
      candidate.physicalKind == PhysicalEventKind::weaponSelection) {
    return issue(ControllerInputProfileIssueKind::invalidPhysicalKind, index);
  }
  if (candidate.physicalKind != expectedPhysicalKind(candidate.control)) {
    return issue(ControllerInputProfileIssueKind::physicalControlKindMismatch,
                 index);
  }
  if (candidate.targetKind >= BindingTargetKind::count ||
      candidate.targetKind == BindingTargetKind::weaponSelection) {
    return issue(ControllerInputProfileIssueKind::invalidTargetKind, index);
  }
  // V1 native adapters deliberately expose triggers as fixed-threshold binary
  // transitions carried in an analog physical event (0 or q15One). Do not
  // accept records that imply continuous trigger transport or a configurable
  // actuation threshold that the platform layer cannot honor.
  if (isTrigger(candidate.control) &&
      (candidate.targetKind != BindingTargetKind::digital ||
       candidate.scale != q15One ||
       candidate.meaningfulThreshold != controllerTriggerActuationQ15)) {
    return issue(ControllerInputProfileIssueKind::triggerRequiresBinaryBinding,
                 index);
  }
  switch (candidate.targetKind) {
  case BindingTargetKind::digital:
    if (candidate.target >= digitalActionCount) {
      return issue(ControllerInputProfileIssueKind::invalidTarget, index);
    }
    if (candidate.scale != q15One) {
      return issue(ControllerInputProfileIssueKind::invalidScale, index);
    }
    if (candidate.meaningfulThreshold <= 0) {
      return issue(ControllerInputProfileIssueKind::invalidMeaningfulThreshold,
                   index);
    }
    break;
  case BindingTargetKind::analog:
    if (candidate.target >= analogAxisCount) {
      return issue(ControllerInputProfileIssueKind::invalidTarget, index);
    }
    if (candidate.scale < q15Min || candidate.scale == 0) {
      return issue(ControllerInputProfileIssueKind::invalidScale, index);
    }
    // InputRouter considers an analog value meaningful only when its magnitude
    // is strictly greater than this threshold. q15One would therefore make
    // every representable input neutral and could fake a recovery binding.
    if (candidate.meaningfulThreshold < 0 ||
        candidate.meaningfulThreshold >= q15One) {
      return issue(ControllerInputProfileIssueKind::invalidMeaningfulThreshold,
                   index);
    }
    break;
  case BindingTargetKind::weaponSelection:
  case BindingTargetKind::count:
    return issue(ControllerInputProfileIssueKind::invalidTargetKind, index);
  }
  if (!validContexts(candidate.contexts)) {
    return issue(ControllerInputProfileIssueKind::invalidContexts, index);
  }
  if (candidate.blocksNeutralGate > 1U) {
    return issue(ControllerInputProfileIssueKind::invalidBlocksNeutralGate,
                 index);
  }
  return std::nullopt;
}

} // namespace

ControllerInputProfileRecord
makeDefaultControllerInputProfileRecord() noexcept {
  ControllerInputProfileRecord record;
  const auto defaults = makeDefaultBindings();
  for (const auto &candidate : defaults) {
    if (candidate.sourceKind != SourceKind::controller) {
      continue;
    }
    if (record.bindingCount >= controllerProfileBindingCapacity) {
      return {};
    }
    record.bindings[record.bindingCount] = bindingRecord(candidate);
    ++record.bindingCount;
  }
  return record;
}

ControllerInputProfileResolveResult resolveControllerInputProfile(
    const ControllerInputProfileRecord &record) noexcept {
  ControllerInputProfileResolveResult result;
  if (record.schemaVersion != controllerInputProfileRecordSchemaVersion) {
    result.issue = issue(ControllerInputProfileIssueKind::unsupportedSchema);
    return result;
  }

  for (std::size_t index = 0U; index < record.axes.size(); ++index) {
    if (const auto axisIssue = validateAxis(record.axes[index], index);
        axisIssue.has_value()) {
      result.issue = axisIssue;
      return result;
    }
  }

  if (record.bindingCount > controllerProfileBindingCapacity) {
    result.issue =
        issue(ControllerInputProfileIssueKind::bindingCountOutOfRange);
    return result;
  }
  const auto activeBindingCount = static_cast<std::size_t>(record.bindingCount);
  for (std::size_t index = 0U; index < activeBindingCount; ++index) {
    if (const auto bindingIssue =
            validateBinding(record.bindings[index], index);
        bindingIssue.has_value()) {
      result.issue = bindingIssue;
      return result;
    }
  }
  for (std::size_t index = activeBindingCount; index < record.bindings.size();
       ++index) {
    if (record.bindings[index] != ControllerBindingRecord{}) {
      result.issue = issue(
          ControllerInputProfileIssueKind::nonCanonicalUnusedBinding, index);
      return result;
    }
  }

  for (std::size_t left = 0U; left < activeBindingCount; ++left) {
    for (std::size_t right = left + 1U; right < activeBindingCount; ++right) {
      const auto &first = record.bindings[left];
      const auto &second = record.bindings[right];
      if (first.control == second.control &&
          first.physicalKind == second.physicalKind &&
          (first.contexts & second.contexts) != 0U) {
        result.issue =
            issue(ControllerInputProfileIssueKind::contextConflict, right);
        return result;
      }
    }
  }

  std::size_t nonControllerBindingCount = 0U;
  for (const auto &candidate : makeDefaultBindings()) {
    if (candidate.sourceKind != SourceKind::controller) {
      ++nonControllerBindingCount;
    }
  }
  if (nonControllerBindingCount + activeBindingCount > BindingTable::capacity) {
    result.issue =
        issue(ControllerInputProfileIssueKind::bindingTableCapacityExceeded);
    return result;
  }

  bool hasPause = false;
  bool hasMenuConfirm = false;
  bool hasMenuCancel = false;
  AnalogDirectionReachability menuNavigateX;
  AnalogDirectionReachability menuNavigateY;
  for (std::size_t index = 0U; index < activeBindingCount; ++index) {
    const auto &candidate = record.bindings[index];
    hasPause = hasPause ||
               reachableDigitalTarget(candidate, DigitalAction::globalPause,
                                      gameplayContext, record);
    hasMenuConfirm = hasMenuConfirm ||
                     reachableDigitalTarget(candidate, DigitalAction::uiConfirm,
                                            menuContext, record);
    hasMenuCancel = hasMenuCancel ||
                    reachableDigitalTarget(candidate, DigitalAction::uiCancel,
                                           menuContext, record);
    const auto xDirections = reachableAnalogDirections(
        candidate, AnalogAxis::uiNavigateX, menuContext, record);
    menuNavigateX.negative = menuNavigateX.negative || xDirections.negative;
    menuNavigateX.positive = menuNavigateX.positive || xDirections.positive;
    const auto yDirections = reachableAnalogDirections(
        candidate, AnalogAxis::uiNavigateY, menuContext, record);
    menuNavigateY.negative = menuNavigateY.negative || yDirections.negative;
    menuNavigateY.positive = menuNavigateY.positive || yDirections.positive;
  }
  if (!hasPause) {
    result.issue = issue(ControllerInputProfileIssueKind::missingPause);
    return result;
  }
  if (!hasMenuConfirm) {
    result.issue = issue(ControllerInputProfileIssueKind::missingMenuConfirm);
    return result;
  }
  if (!hasMenuCancel) {
    result.issue = issue(ControllerInputProfileIssueKind::missingMenuCancel);
    return result;
  }
  if (!menuNavigateX.negative || !menuNavigateX.positive) {
    result.issue = issue(ControllerInputProfileIssueKind::missingMenuNavigateX);
    return result;
  }
  if (!menuNavigateY.negative || !menuNavigateY.positive) {
    result.issue = issue(ControllerInputProfileIssueKind::missingMenuNavigateY);
    return result;
  }

  result.profile = ResolvedControllerInputProfile{record};
  return result;
}

ControllerInputProfileCompileResult compileControllerInputBindings(
    const ResolvedControllerInputProfile &profile) noexcept {
  ControllerInputProfileCompileResult result;
  BindingTable compiled;
  for (const auto &candidate : makeDefaultBindings()) {
    if (candidate.sourceKind == SourceKind::controller) {
      continue;
    }
    if (!compiled.add(candidate)) {
      result.issue =
          issue(ControllerInputProfileIssueKind::bindingCompilationFailed);
      return result;
    }
  }
  std::size_t index = 0U;
  for (const auto &candidate : profile.bindings()) {
    if (!compiled.add(binding(candidate))) {
      result.issue = issue(
          ControllerInputProfileIssueKind::bindingCompilationFailed, index);
      return result;
    }
    ++index;
  }
  result.bindings = compiled;
  return result;
}

std::optional<Q15> transformControllerAxis(
    const Q15 raw, const ControllerAxisElement axis,
    const ResolvedControllerInputProfile &profile) noexcept {
  if (raw < q15Min) {
    return std::nullopt;
  }
  const auto *const calibration = profile.axisCalibration(axis);
  if (calibration == nullptr) {
    return std::nullopt;
  }

  const auto widened = static_cast<std::int32_t>(raw);
  const auto magnitude =
      static_cast<std::uint32_t>(widened < 0 ? -widened : widened);
  if (magnitude <= calibration->innerDeadzoneQ15) {
    return q15Zero;
  }

  std::uint64_t normalized = static_cast<std::uint16_t>(q15One);
  if (magnitude < calibration->outerSaturationQ15) {
    const auto numerator =
        static_cast<std::uint64_t>(magnitude - calibration->innerDeadzoneQ15) *
        static_cast<std::uint16_t>(q15One);
    const auto denominator = static_cast<std::uint64_t>(
        calibration->outerSaturationQ15 - calibration->innerDeadzoneQ15);
    normalized = divideRoundNearestEven(numerator, denominator);
  }

  const auto q15Denominator = static_cast<std::uint64_t>(q15One);
  switch (calibration->responseCurve) {
  case ControllerResponseCurve::linear:
    break;
  case ControllerResponseCurve::squared:
    normalized =
        divideRoundNearestEven(normalized * normalized, q15Denominator);
    break;
  case ControllerResponseCurve::cubic:
    normalized = divideRoundNearestEven(normalized * normalized * normalized,
                                        q15Denominator * q15Denominator);
    break;
  case ControllerResponseCurve::count:
    return std::nullopt;
  }

  auto transformed = divideRoundNearestEven(
      normalized * calibration->sensitivityPermille, 1000U);
  if (transformed > static_cast<std::uint16_t>(q15One)) {
    transformed = static_cast<std::uint16_t>(q15One);
  }
  if (transformed == 0U) {
    return q15Zero;
  }

  bool negative = widened < 0;
  if (calibration->inverted != 0U) {
    negative = !negative;
  }
  const auto signedValue = static_cast<std::int32_t>(transformed);
  return static_cast<Q15>(negative ? -signedValue : signedValue);
}

} // namespace airfix::input
