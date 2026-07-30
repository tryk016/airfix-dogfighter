#include "airfix/input/ControllerInputBindingCatalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace airfix::input {
namespace {

constexpr std::array<ControllerDigitalGameplayActionDescriptor,
                     controllerDigitalGameplayActionCount>
    actionCatalog{{
        {
            .action = ControllerDigitalGameplayAction::primaryFire,
            .target = DigitalAction::combatPrimaryFire,
        },
        {
            .action = ControllerDigitalGameplayAction::secondaryFire,
            .target = DigitalAction::combatSecondaryFire,
        },
        {
            .action = ControllerDigitalGameplayAction::weaponNext,
            .target = DigitalAction::combatWeaponNext,
        },
        {
            .action = ControllerDigitalGameplayAction::rearView,
            .target = DigitalAction::cameraRearView,
        },
        {
            .action = ControllerDigitalGameplayAction::cameraCycle,
            .target = DigitalAction::cameraCycle,
        },
        {
            .action = ControllerDigitalGameplayAction::cameraRecenter,
            .target = DigitalAction::cameraRecenter,
        },
        {
            .action = ControllerDigitalGameplayAction::missionStatus,
            .target = DigitalAction::missionStatus,
        },
    }};

constexpr std::array<ControllerAssignableControlDescriptor,
                     controllerAssignableControlCount>
    controlCatalog{{
        {.control = controls::controller::rightTrigger},
        {.control = controls::controller::leftTrigger},
        {.control = controls::controller::rightShoulder},
        {.control = controls::controller::leftShoulder},
        {.control = controls::controller::facePrimary},
        {.control = controls::controller::faceSecondary},
        {.control = controls::controller::faceLeft},
        {.control = controls::controller::faceTop},
        {.control = controls::controller::rightStickClick},
        {.control = controls::controller::dpadUp},
        {.control = controls::controller::dpadDown},
        {.control = controls::controller::dpadLeft},
        {.control = controls::controller::dpadRight},
        {.control = controls::controller::menu},
    }};

[[nodiscard]] constexpr bool
targetsActionInGameplay(const ControllerBindingRecord &binding,
                        const DigitalAction target) noexcept {
  return binding.targetKind == BindingTargetKind::digital &&
         binding.target == static_cast<std::uint8_t>(target) &&
         (binding.contexts & gameplayContext) != 0U;
}

} // namespace

std::span<const ControllerDigitalGameplayActionDescriptor>
controllerDigitalGameplayActionCatalog() noexcept {
  return actionCatalog;
}

std::span<const ControllerAssignableControlDescriptor>
controllerAssignableControlCatalog() noexcept {
  return controlCatalog;
}

const ControllerDigitalGameplayActionDescriptor *
controllerDigitalGameplayActionDescriptor(
    const ControllerDigitalGameplayAction action) noexcept {
  const auto index = static_cast<std::size_t>(action);
  if (index >= actionCatalog.size() || actionCatalog[index].action != action) {
    return nullptr;
  }
  return &actionCatalog[index];
}

const ControllerAssignableControlDescriptor *
controllerAssignableControlDescriptor(const ControlId control) noexcept {
  const auto found = std::find_if(
      controlCatalog.begin(), controlCatalog.end(),
      [control](const ControllerAssignableControlDescriptor &entry) {
        return entry.control == control;
      });
  return found == controlCatalog.end() ? nullptr : &*found;
}

ControllerDigitalGameplayBindingLookup controllerDigitalGameplayBinding(
    const ControllerInputProfileRecord &record,
    const ControllerDigitalGameplayAction action) noexcept {
  const auto *descriptor = controllerDigitalGameplayActionDescriptor(action);
  if (descriptor == nullptr) {
    return {
        .status = ControllerDigitalGameplayBindingStatus::missing,
    };
  }

  std::size_t match = controllerInputProfileNoIndex;
  std::size_t matchCount = 0U;
  const auto count = std::min<std::size_t>(record.bindingCount,
                                           controllerProfileBindingCapacity);
  for (std::size_t index = 0U; index < count; ++index) {
    if (!targetsActionInGameplay(record.bindings[index], descriptor->target)) {
      continue;
    }
    match = index;
    ++matchCount;
  }

  if (matchCount == 0U) {
    return {
        .status = ControllerDigitalGameplayBindingStatus::missing,
    };
  }
  if (matchCount != 1U) {
    return {
        .status = ControllerDigitalGameplayBindingStatus::ambiguous,
    };
  }
  if (record.bindings[match].contexts != gameplayContext) {
    return {
        .status = ControllerDigitalGameplayBindingStatus::unsupportedLayout,
        .bindingIndex = match,
    };
  }
  return {
      .status = ControllerDigitalGameplayBindingStatus::editable,
      .bindingIndex = match,
  };
}

std::optional<ControllerDigitalGameplayAction>
controllerDigitalGameplayActionForBinding(
    const ControllerInputProfileRecord &record,
    const std::size_t bindingIndex) noexcept {
  if (bindingIndex >= record.bindingCount ||
      bindingIndex >= controllerProfileBindingCapacity) {
    return std::nullopt;
  }
  for (const auto &descriptor : actionCatalog) {
    const auto lookup =
        controllerDigitalGameplayBinding(record, descriptor.action);
    if (lookup.editable() && lookup.bindingIndex == bindingIndex) {
      return descriptor.action;
    }
  }
  return std::nullopt;
}

} // namespace airfix::input
