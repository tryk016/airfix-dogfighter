#include "AirfixSdlInputAdapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace airfix::windows {
namespace {

constexpr std::int32_t mouseQ15PerLogicalPixel = 2048;
constexpr Sint16 triggerActuation =
    airfix::input::controllerTriggerActuationQ15;

[[nodiscard]] airfix::input::Q15 signedAxis(const Sint16 value,
                                            const bool inverted) noexcept {
  std::int32_t widened = value;
  widened = std::max(widened, static_cast<std::int32_t>(airfix::input::q15Min));
  if (inverted) {
    widened = -widened;
  }
  return airfix::input::clampQ15(widened);
}

[[nodiscard]] bool isSyntheticMouse(const SDL_MouseID mouse) noexcept {
  return mouse == SDL_TOUCH_MOUSEID || mouse == SDL_PEN_MOUSEID;
}

} // namespace

airfix::input::ControlId
sdlKeyboardControl(const SDL_Scancode scancode) noexcept {
  const auto value = static_cast<std::int32_t>(scancode);
  if (value <= static_cast<std::int32_t>(SDL_SCANCODE_UNKNOWN) ||
      value > static_cast<std::int32_t>(
                  std::numeric_limits<std::uint16_t>::max())) {
    return {};
  }
  return {static_cast<std::uint16_t>(value)};
}

airfix::input::ControlId sdlMouseControl(const Uint8 button) noexcept {
  using namespace airfix::input::controls::mouse;
  switch (button) {
  case SDL_BUTTON_LEFT:
    return leftButton;
  case SDL_BUTTON_RIGHT:
    return rightButton;
  case SDL_BUTTON_MIDDLE:
    return middleButton;
  case SDL_BUTTON_X1:
    return extraButtonOne;
  case SDL_BUTTON_X2:
    return extraButtonTwo;
  default:
    return {};
  }
}

airfix::input::Q15 sdlMouseMotionValue(const float relativeMotion) noexcept {
  if (!std::isfinite(relativeMotion)) {
    return airfix::input::q15Zero;
  }
  const double scaled =
      std::round(static_cast<double>(relativeMotion) *
                 static_cast<double>(mouseQ15PerLogicalPixel));
  const double bounded =
      std::clamp(scaled, static_cast<double>(airfix::input::q15Min),
                 static_cast<double>(airfix::input::q15One));
  return static_cast<airfix::input::Q15>(bounded);
}

airfix::input::Q15 sdlGamepadAxisValue(const SDL_GamepadAxis axis,
                                       const Sint16 value) noexcept {
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTY:
  case SDL_GAMEPAD_AXIS_RIGHTY:
    return signedAxis(value, true);
  case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
  case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
    return airfix::input::clampQ15(std::max<std::int32_t>(0, value));
  case SDL_GAMEPAD_AXIS_LEFTX:
  case SDL_GAMEPAD_AXIS_RIGHTX:
    return signedAxis(value, false);
  case SDL_GAMEPAD_AXIS_INVALID:
  case SDL_GAMEPAD_AXIS_COUNT:
    break;
  }
  return airfix::input::q15Zero;
}

airfix::input::DesktopControllerAxis
sdlGamepadStickAxis(const SDL_GamepadAxis axis) noexcept {
  using airfix::input::DesktopControllerAxis;
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTX:
    return DesktopControllerAxis::bank;
  case SDL_GAMEPAD_AXIS_LEFTY:
    return DesktopControllerAxis::pitch;
  case SDL_GAMEPAD_AXIS_RIGHTX:
    return DesktopControllerAxis::lookX;
  case SDL_GAMEPAD_AXIS_RIGHTY:
    return DesktopControllerAxis::lookY;
  case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
  case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
  case SDL_GAMEPAD_AXIS_INVALID:
  case SDL_GAMEPAD_AXIS_COUNT:
    break;
  }
  return DesktopControllerAxis::count;
}

SdlGamepadButtonMapping
sdlGamepadButton(const SDL_GamepadButton button) noexcept {
  using airfix::input::ControllerDigitalControl;
  switch (button) {
  case SDL_GAMEPAD_BUTTON_SOUTH:
    return {ControllerDigitalControl::uiConfirm};
  case SDL_GAMEPAD_BUTTON_EAST:
    return {ControllerDigitalControl::uiCancel};
  case SDL_GAMEPAD_BUTTON_WEST:
    return {ControllerDigitalControl::cameraCycle};
  case SDL_GAMEPAD_BUTTON_NORTH:
    return {ControllerDigitalControl::missionStatus};
  case SDL_GAMEPAD_BUTTON_START:
    return {ControllerDigitalControl::pause};
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    return {ControllerDigitalControl::cameraRecenter};
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    return {ControllerDigitalControl::rearView};
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    return {ControllerDigitalControl::weaponNext};
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return {ControllerDigitalControl::throttleUp};
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return {ControllerDigitalControl::throttleDown};
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return {ControllerDigitalControl::uiPrevious};
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return {ControllerDigitalControl::uiNext};
  case SDL_GAMEPAD_BUTTON_INVALID:
  case SDL_GAMEPAD_BUTTON_BACK:
  case SDL_GAMEPAD_BUTTON_GUIDE:
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
  case SDL_GAMEPAD_BUTTON_MISC1:
  case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
  case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
  case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
  case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
  case SDL_GAMEPAD_BUTTON_TOUCHPAD:
  case SDL_GAMEPAD_BUTTON_MISC2:
  case SDL_GAMEPAD_BUTTON_MISC3:
  case SDL_GAMEPAD_BUTTON_MISC4:
  case SDL_GAMEPAD_BUTTON_MISC5:
  case SDL_GAMEPAD_BUTTON_MISC6:
  case SDL_GAMEPAD_BUTTON_COUNT:
    break;
  }
  return {};
}

bool sdlGamepadTriggerPressed(const Sint16 value) noexcept {
  return value >= triggerActuation;
}

AirfixSdlInputAdapter::AirfixSdlInputAdapter() {
  static_cast<void>(openFirstAvailableGamepad());
}

AirfixSdlInputAdapter::AirfixSdlInputAdapter(
    const airfix::input::ControllerInputRuntimeConfiguration &configuration)
    : bridge_(configuration) {
  static_cast<void>(openFirstAvailableGamepad());
}

AirfixSdlInputAdapter::~AirfixSdlInputAdapter() { closeGamepad(); }

SdlInputEventResult
AirfixSdlInputAdapter::handleEvent(const SDL_Event &event) noexcept {
  SdlInputEventResult result{};
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP: {
    if (event.key.repeat) {
      return result;
    }
    const auto control = sdlKeyboardControl(event.key.scancode);
    if (!control.valid()) {
      return result;
    }
    result.accepted = bridge_.key(control, event.key.down, event.key.timestamp);
    result.meaningfulInput = result.accepted && event.key.down;
    return result;
  }
  case SDL_EVENT_MOUSE_MOTION: {
    if (isSyntheticMouse(event.motion.which)) {
      return result;
    }
    const auto x = sdlMouseMotionValue(event.motion.xrel);
    const auto y = sdlMouseMotionValue(-event.motion.yrel);
    if (x == airfix::input::q15Zero && y == airfix::input::q15Zero) {
      return result;
    }
    result.accepted = bridge_.mouseMotion(x, y);
    result.meaningfulInput = result.accepted;
    return result;
  }
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    if (isSyntheticMouse(event.button.which)) {
      return result;
    }
    const auto control = sdlMouseControl(event.button.button);
    if (!control.valid()) {
      return result;
    }
    result.accepted =
        bridge_.mouseButton(control, event.button.down, event.button.timestamp);
    result.meaningfulInput = result.accepted && event.button.down;
    return result;
  }
  case SDL_EVENT_MOUSE_WHEEL: {
    if (isSyntheticMouse(event.wheel.which)) {
      return result;
    }
    std::int32_t vertical = event.wheel.integer_y;
    if (vertical == 0 && event.wheel.y != 0.0F) {
      vertical = event.wheel.y > 0.0F ? 1 : -1;
    }
    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
      vertical = -vertical;
    }
    if (vertical == 0) {
      return result;
    }
    const auto control = vertical > 0
                             ? airfix::input::controls::mouse::wheelUp
                             : airfix::input::controls::mouse::wheelDown;
    result.accepted = bridge_.mousePulse(control, event.wheel.timestamp);
    result.meaningfulInput = result.accepted;
    return result;
  }
  case SDL_EVENT_KEYBOARD_REMOVED:
    bridge_.keyboardDisconnected();
    result.accepted = bridge_.healthy();
    return result;
  case SDL_EVENT_MOUSE_REMOVED:
    bridge_.mouseDisconnected();
    result.accepted = bridge_.healthy();
    return result;
  case SDL_EVENT_GAMEPAD_ADDED:
    if (gamepad_ == nullptr) {
      static_cast<void>(openGamepad(event.gdevice.which));
    }
    return result;
  case SDL_EVENT_GAMEPAD_REMOVED:
    if (gamepad_ != nullptr && event.gdevice.which == gamepadId_) {
      result.accepted = bridge_.disconnectController();
      result.controllerDisconnected = true;
      closeGamepad();
      if (result.accepted) {
        static_cast<void>(openFirstAvailableGamepad());
      }
    }
    return result;
  case SDL_EVENT_GAMEPAD_REMAPPED:
    if (gamepad_ != nullptr && event.gdevice.which == gamepadId_) {
      result.accepted = restartControllerGeneration();
    }
    return result;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
    if (gamepad_ == nullptr || event.gaxis.which != gamepadId_) {
      return result;
    }
    const auto axis = static_cast<SDL_GamepadAxis>(event.gaxis.axis);
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
        axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
      const auto control =
          axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
              ? airfix::input::ControllerDigitalControl::primaryTrigger
              : airfix::input::ControllerDigitalControl::secondaryTrigger;
      result.accepted = bridge_.controllerButton(
          control, sdlGamepadTriggerPressed(event.gaxis.value));
    } else {
      const auto mappedAxis = sdlGamepadStickAxis(axis);
      if (mappedAxis == airfix::input::DesktopControllerAxis::count) {
        return result;
      }
      result.accepted = bridge_.controllerAxis(
          mappedAxis, sdlGamepadAxisValue(axis, event.gaxis.value));
    }
    result.meaningfulInput =
        result.accepted &&
        sdlGamepadAxisValue(axis, event.gaxis.value) != airfix::input::q15Zero;
    return result;
  }
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP: {
    if (gamepad_ == nullptr || event.gbutton.which != gamepadId_) {
      return result;
    }
    const auto mapping =
        sdlGamepadButton(static_cast<SDL_GamepadButton>(event.gbutton.button));
    if (!mapping.valid()) {
      return result;
    }
    result.accepted =
        bridge_.controllerButton(mapping.control, event.gbutton.down);
    result.meaningfulInput = result.accepted && event.gbutton.down;
    return result;
  }
  default:
    return result;
  }
}

airfix::input::DesktopInputTick
AirfixSdlInputAdapter::tick(const std::uint64_t simulationTick) noexcept {
  return bridge_.tick(simulationTick);
}

void AirfixSdlInputAdapter::focusLost() noexcept { bridge_.focusLost(); }

bool AirfixSdlInputAdapter::focusGained() noexcept {
  bridge_.focusGained();
  return gamepad_ == nullptr || restartControllerGeneration();
}

bool AirfixSdlInputAdapter::resetForGameplayBoundary() noexcept {
  bridge_.resetForGameplayBoundary();
  return gamepad_ == nullptr || restartControllerGeneration();
}

void AirfixSdlInputAdapter::setContext(
    const airfix::input::InputContext context) noexcept {
  bridge_.setContext(context);
}

airfix::input::InputContext AirfixSdlInputAdapter::context() const noexcept {
  return bridge_.context();
}

bool AirfixSdlInputAdapter::controllerConnected() const noexcept {
  return gamepad_ != nullptr && bridge_.controllerConnected();
}

const char *AirfixSdlInputAdapter::controllerName() const noexcept {
  if (gamepad_ == nullptr) {
    return nullptr;
  }
  return SDL_GetGamepadName(gamepad_);
}

const airfix::input::ResolvedControllerInputProfile *
AirfixSdlInputAdapter::activeControllerProfile() const noexcept {
  return bridge_.controllerProfile();
}

SdlControllerAxisSnapshot
AirfixSdlInputAdapter::controllerAxisSnapshot() const noexcept {
  const auto sample = sampleController();
  return {
      .rawAxes =
          {
              sample.bank,
              sample.pitch,
              sample.lookX,
              sample.lookY,
          },
      .connected = gamepad_ != nullptr,
  };
}

bool AirfixSdlInputAdapter::openGamepad(
    const SDL_JoystickID instanceId) noexcept {
  if (gamepad_ != nullptr) {
    return true;
  }
  SDL_Gamepad *const candidate = SDL_OpenGamepad(instanceId);
  if (candidate == nullptr) {
    return false;
  }

  gamepad_ = candidate;
  gamepadId_ = SDL_GetGamepadID(candidate);
  if (gamepadId_ == 0U || !restartControllerGeneration()) {
    closeGamepad();
    return false;
  }
  return true;
}

bool AirfixSdlInputAdapter::openFirstAvailableGamepad() noexcept {
  if (gamepad_ != nullptr) {
    return true;
  }

  int count = 0;
  SDL_JoystickID *const gamepads = SDL_GetGamepads(&count);
  if (gamepads == nullptr) {
    return count == 0;
  }

  bool opened = false;
  for (int index = 0; index < count && !opened; ++index) {
    opened = openGamepad(gamepads[index]);
  }
  SDL_free(gamepads);
  return opened || count == 0;
}

bool AirfixSdlInputAdapter::restartControllerGeneration() noexcept {
  if (gamepad_ == nullptr ||
      nextControllerGeneration_ == std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  const std::uint64_t generation = nextControllerGeneration_;
  ++nextControllerGeneration_;
  return bridge_.connectController(generation, sampleController());
}

airfix::input::ControllerSample
AirfixSdlInputAdapter::sampleController() const noexcept {
  airfix::input::ControllerSample sample{};
  if (gamepad_ == nullptr) {
    return sample;
  }

  sample.bank =
      sdlGamepadAxisValue(SDL_GAMEPAD_AXIS_LEFTX,
                          SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX));
  sample.pitch =
      sdlGamepadAxisValue(SDL_GAMEPAD_AXIS_LEFTY,
                          SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY));
  sample.lookX = sdlGamepadAxisValue(
      SDL_GAMEPAD_AXIS_RIGHTX,
      SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX));
  sample.lookY = sdlGamepadAxisValue(
      SDL_GAMEPAD_AXIS_RIGHTY,
      SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY));
  sample.primaryTriggerPressed = sdlGamepadTriggerPressed(
      SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
  sample.secondaryTriggerPressed = sdlGamepadTriggerPressed(
      SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));

  for (std::int32_t button = SDL_GAMEPAD_BUTTON_SOUTH;
       button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
    const auto mapping =
        sdlGamepadButton(static_cast<SDL_GamepadButton>(button));
    if (mapping.valid() &&
        SDL_GetGamepadButton(gamepad_,
                             static_cast<SDL_GamepadButton>(button))) {
      sample.setPressed(mapping.control, true);
    }
  }
  return sample;
}

void AirfixSdlInputAdapter::closeGamepad() noexcept {
  if (gamepad_ != nullptr) {
    SDL_CloseGamepad(gamepad_);
  }
  gamepad_ = nullptr;
  gamepadId_ = 0U;
}

} // namespace airfix::windows
