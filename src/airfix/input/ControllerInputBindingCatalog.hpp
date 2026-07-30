#pragma once

#include "airfix/input/ControllerInputProfile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::input {

// The first remapping surface deliberately excludes pause/back, menu recovery,
// analog targets, throttle directions, and arbitrary custom bindings.
enum class ControllerDigitalGameplayAction : std::uint8_t {
  primaryFire = 0,
  secondaryFire,
  weaponNext,
  rearView,
  cameraCycle,
  cameraRecenter,
  missionStatus,
  count,
};

struct ControllerDigitalGameplayActionDescriptor final {
  ControllerDigitalGameplayAction action{
      ControllerDigitalGameplayAction::primaryFire};
  DigitalAction target{DigitalAction::combatPrimaryFire};

  [[nodiscard]] friend constexpr bool operator==(
      const ControllerDigitalGameplayActionDescriptor &,
      const ControllerDigitalGameplayActionDescriptor &) noexcept = default;
};

struct ControllerAssignableControlDescriptor final {
  ControlId control{};

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerAssignableControlDescriptor &,
             const ControllerAssignableControlDescriptor &) noexcept = default;
};

enum class ControllerDigitalGameplayBindingStatus : std::uint8_t {
  editable = 0,
  missing,
  ambiguous,
  unsupportedLayout,
};

struct ControllerDigitalGameplayBindingLookup final {
  ControllerDigitalGameplayBindingStatus status{
      ControllerDigitalGameplayBindingStatus::missing};
  std::size_t bindingIndex{controllerInputProfileNoIndex};

  [[nodiscard]] constexpr bool editable() const noexcept {
    return status == ControllerDigitalGameplayBindingStatus::editable &&
           bindingIndex != controllerInputProfileNoIndex;
  }

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerDigitalGameplayBindingLookup &,
             const ControllerDigitalGameplayBindingLookup &) noexcept = default;
};

inline constexpr std::size_t controllerDigitalGameplayActionCount =
    static_cast<std::size_t>(ControllerDigitalGameplayAction::count);
inline constexpr std::size_t controllerAssignableControlCount = 14U;

[[nodiscard]] std::span<const ControllerDigitalGameplayActionDescriptor>
controllerDigitalGameplayActionCatalog() noexcept;

[[nodiscard]] std::span<const ControllerAssignableControlDescriptor>
controllerAssignableControlCatalog() noexcept;

[[nodiscard]] const ControllerDigitalGameplayActionDescriptor *
controllerDigitalGameplayActionDescriptor(
    ControllerDigitalGameplayAction action) noexcept;

[[nodiscard]] const ControllerAssignableControlDescriptor *
controllerAssignableControlDescriptor(ControlId control) noexcept;

// Only one exact gameplay-only binding is editable. Multiple controls may
// legally target the same action, but that custom layout is intentionally
// classified as ambiguous instead of being guessed at by the first editor.
[[nodiscard]] ControllerDigitalGameplayBindingLookup
controllerDigitalGameplayBinding(
    const ControllerInputProfileRecord &record,
    ControllerDigitalGameplayAction action) noexcept;

// Returns an action only when this exact binding is the unique editable
// binding for that action in the complete record.
[[nodiscard]] std::optional<ControllerDigitalGameplayAction>
controllerDigitalGameplayActionForBinding(
    const ControllerInputProfileRecord &record,
    std::size_t bindingIndex) noexcept;

} // namespace airfix::input
