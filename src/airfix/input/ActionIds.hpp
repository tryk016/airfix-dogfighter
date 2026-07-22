#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace airfix::input {

// Values are part of the replay/input-profile contract. Append new actions;
// never reorder or reuse an existing value.
enum class DigitalAction : std::uint8_t {
    globalPause = 0,
    globalBack = 1,
    uiConfirm = 2,
    uiCancel = 3,
    uiTabPrevious = 4,
    uiTabNext = 5,
    combatPrimaryFire = 6,
    combatSecondaryFire = 7,
    combatWeaponNext = 8,
    cameraCycle = 9,
    cameraRearView = 10,
    cameraRecenter = 11,
    missionStatus = 12,
    count = 13,
};

// Values are part of the replay/input-profile contract. Append new axes;
// never reorder or reuse an existing value.
enum class AnalogAxis : std::uint8_t {
    uiNavigateX = 0,
    uiNavigateY = 1,
    flightBank = 2,
    flightPitch = 3,
    flightThrottleDelta = 4,
    flightThrottleSet = 5,
    cameraLookX = 6,
    cameraLookY = 7,
    count = 8,
};

enum class InputContext : std::uint8_t {
    gameplay = 0,
    menu = 1,
    modal = 2,
    controlEditor = 3,
    count = 4,
};

using ContextMask = std::uint8_t;

[[nodiscard]] constexpr ContextMask contextMask(const InputContext context) noexcept {
    if (static_cast<std::uint8_t>(context) >=
        static_cast<std::uint8_t>(InputContext::count)) {
        return 0U;
    }
    return static_cast<ContextMask>(
        ContextMask{1U} << static_cast<std::uint8_t>(context));
}

inline constexpr ContextMask gameplayContext = contextMask(InputContext::gameplay);
inline constexpr ContextMask menuContext = contextMask(InputContext::menu);
inline constexpr ContextMask modalContext = contextMask(InputContext::modal);
inline constexpr ContextMask controlEditorContext = contextMask(InputContext::controlEditor);
inline constexpr ContextMask allContexts = static_cast<ContextMask>(
    gameplayContext | menuContext | modalContext | controlEditorContext);

[[nodiscard]] constexpr std::size_t toIndex(const DigitalAction action) noexcept {
    return static_cast<std::size_t>(action);
}

[[nodiscard]] constexpr std::size_t toIndex(const AnalogAxis axis) noexcept {
    return static_cast<std::size_t>(axis);
}

inline constexpr std::size_t digitalActionCount = toIndex(DigitalAction::count);
inline constexpr std::size_t analogAxisCount = toIndex(AnalogAxis::count);

[[nodiscard]] constexpr bool isGameplayAction(const DigitalAction action) noexcept {
    return action == DigitalAction::globalPause ||
        toIndex(action) >= toIndex(DigitalAction::combatPrimaryFire);
}

[[nodiscard]] constexpr bool isGameplayAxis(const AnalogAxis axis) noexcept {
    return toIndex(axis) >= toIndex(AnalogAxis::flightBank);
}

static_assert(std::is_same_v<std::underlying_type_t<DigitalAction>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<AnalogAxis>, std::uint8_t>);
static_assert(digitalActionCount <= 64U);

} // namespace airfix::input
