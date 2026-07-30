#include "airfix/input/ControllerInputBindingCatalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::input::BindingTargetKind;
using airfix::input::ControllerDigitalGameplayAction;
using airfix::input::ControllerDigitalGameplayBindingStatus;
using airfix::input::ControllerInputProfileRecord;
using airfix::input::DigitalAction;
using airfix::input::gameplayContext;
using airfix::input::menuContext;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void removeBinding(ControllerInputProfileRecord &record,
                   const std::size_t removeIndex) {
  require(removeIndex < record.bindingCount,
          "remove fixture index is out of range");
  for (std::size_t index = removeIndex + 1U; index < record.bindingCount;
       ++index) {
    record.bindings[index - 1U] = record.bindings[index];
  }
  --record.bindingCount;
  record.bindings[record.bindingCount] = {};
}

void testCatalogsAreBoundedUniqueAndTransportSafe() {
  const auto actions = airfix::input::controllerDigitalGameplayActionCatalog();
  require(actions.size() == airfix::input::controllerDigitalGameplayActionCount,
          "action catalog has the wrong size");
  for (std::size_t left = 0U; left < actions.size(); ++left) {
    require(static_cast<std::size_t>(actions[left].action) == left,
            "action catalog order is not enum-stable");
    require(airfix::input::controllerDigitalGameplayActionDescriptor(
                actions[left].action) == &actions[left],
            "action descriptor lookup returned the wrong entry");
    for (std::size_t right = left + 1U; right < actions.size(); ++right) {
      require(actions[left].action != actions[right].action &&
                  actions[left].target != actions[right].target,
              "action catalog contains a duplicate");
    }
  }
  require(airfix::input::controllerDigitalGameplayActionDescriptor(
              ControllerDigitalGameplayAction::count) == nullptr,
          "forged action descriptor did not fail closed");

  const auto controls = airfix::input::controllerAssignableControlCatalog();
  require(controls.size() == airfix::input::controllerAssignableControlCount,
          "control catalog has the wrong size");
  for (std::size_t left = 0U; left < controls.size(); ++left) {
    const auto traits =
        airfix::input::controllerControlTraits(controls[left].control);
    require(traits.has_value() &&
                (traits->physicalKind ==
                     airfix::input::PhysicalEventKind::digital ||
                 traits->binaryTrigger),
            "assignable catalog admitted a continuous axis");
    require(airfix::input::controllerAssignableControlDescriptor(
                controls[left].control) == &controls[left],
            "control descriptor lookup returned the wrong entry");
    for (std::size_t right = left + 1U; right < controls.size(); ++right) {
      require(controls[left].control != controls[right].control,
              "control catalog contains a duplicate");
    }
  }
  require(airfix::input::controllerAssignableControlDescriptor(
              airfix::input::controls::controller::leftStickX) == nullptr &&
              airfix::input::controllerAssignableControlDescriptor(
                  airfix::input::ControlId{65000U}) == nullptr,
          "continuous or forged control was assignable");
}

void testDefaultActionsAreUniquelyEditable() {
  const auto record = airfix::input::makeDefaultControllerInputProfileRecord();
  for (const auto &descriptor :
       airfix::input::controllerDigitalGameplayActionCatalog()) {
    const auto lookup = airfix::input::controllerDigitalGameplayBinding(
        record, descriptor.action);
    require(lookup.editable() && lookup.bindingIndex < record.bindingCount,
            "default action is not uniquely editable");
    const auto &binding = record.bindings[lookup.bindingIndex];
    require(binding.targetKind == BindingTargetKind::digital &&
                binding.target ==
                    static_cast<std::uint8_t>(descriptor.target) &&
                binding.contexts == gameplayContext,
            "editable lookup returned the wrong binding");
    require(airfix::input::controllerDigitalGameplayActionForBinding(
                record, lookup.bindingIndex) == descriptor.action,
            "reverse action lookup failed");
  }

  std::size_t pauseIndex = airfix::input::controllerInputProfileNoIndex;
  for (std::size_t index = 0U; index < record.bindingCount; ++index) {
    const auto &binding = record.bindings[index];
    if (binding.targetKind == BindingTargetKind::digital &&
        binding.target ==
            static_cast<std::uint8_t>(DigitalAction::globalPause) &&
        (binding.contexts & gameplayContext) != 0U) {
      pauseIndex = index;
      break;
    }
  }
  require(pauseIndex != airfix::input::controllerInputProfileNoIndex &&
              !airfix::input::controllerDigitalGameplayActionForBinding(
                   record, pauseIndex)
                   .has_value(),
          "protected pause binding became editable");
}

void testCustomLayoutsFailClosedWithoutRejectingValidProfiles() {
  auto missing = airfix::input::makeDefaultControllerInputProfileRecord();
  const auto mission = airfix::input::controllerDigitalGameplayBinding(
      missing, ControllerDigitalGameplayAction::missionStatus);
  require(mission.editable(), "missing fixture action is not editable");
  removeBinding(missing, mission.bindingIndex);
  require(airfix::input::resolveControllerInputProfile(missing).complete(),
          "missing-action fixture is not a valid custom profile");
  require(airfix::input::controllerDigitalGameplayBinding(
              missing, ControllerDigitalGameplayAction::missionStatus)
                  .status == ControllerDigitalGameplayBindingStatus::missing,
          "missing action was not classified");

  auto ambiguous = airfix::input::makeDefaultControllerInputProfileRecord();
  const auto existing = airfix::input::controllerDigitalGameplayBinding(
      ambiguous, ControllerDigitalGameplayAction::missionStatus);
  require(existing.editable() &&
              ambiguous.bindingCount <
                  airfix::input::controllerProfileBindingCapacity,
          "ambiguous fixture cannot append a binding");
  auto duplicate = ambiguous.bindings[existing.bindingIndex];
  duplicate.control = airfix::input::controls::controller::facePrimary;
  duplicate.physicalKind = airfix::input::PhysicalEventKind::digital;
  ambiguous.bindings[ambiguous.bindingCount] = duplicate;
  ++ambiguous.bindingCount;
  require(airfix::input::resolveControllerInputProfile(ambiguous).complete(),
          "multi-control action fixture is not a valid profile");
  require(airfix::input::controllerDigitalGameplayBinding(
              ambiguous, ControllerDigitalGameplayAction::missionStatus)
                  .status == ControllerDigitalGameplayBindingStatus::ambiguous,
          "multi-control action was not classified as ambiguous");

  auto combined = airfix::input::makeDefaultControllerInputProfileRecord();
  const auto camera = airfix::input::controllerDigitalGameplayBinding(
      combined, ControllerDigitalGameplayAction::cameraCycle);
  require(camera.editable(), "combined-context fixture is not editable");
  combined.bindings[camera.bindingIndex].contexts =
      static_cast<airfix::input::ContextMask>(gameplayContext | menuContext);
  require(airfix::input::resolveControllerInputProfile(combined).complete(),
          "combined-context fixture is not a valid profile");
  const auto unsupported = airfix::input::controllerDigitalGameplayBinding(
      combined, ControllerDigitalGameplayAction::cameraCycle);
  require(unsupported.status ==
                  ControllerDigitalGameplayBindingStatus::unsupportedLayout &&
              unsupported.bindingIndex == camera.bindingIndex,
          "combined-context action was not classified as unsupported");

  require(!airfix::input::controllerDigitalGameplayActionForBinding(
               combined, combined.bindingCount)
               .has_value(),
          "out-of-range binding reverse lookup succeeded");
}

} // namespace

int main() {
  try {
    testCatalogsAreBoundedUniqueAndTransportSafe();
    testDefaultActionsAreUniquelyEditable();
    testCustomLayoutsFailClosedWithoutRejectingValidProfiles();
  } catch (const std::exception &error) {
    std::cerr << "ControllerInputBindingCatalogTests failed: " << error.what()
              << '\n';
    return 1;
  }
  std::cout << "ControllerInputBindingCatalogTests passed\n";
  return 0;
}
