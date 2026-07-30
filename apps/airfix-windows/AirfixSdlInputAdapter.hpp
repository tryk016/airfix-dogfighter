#pragma once

#include "airfix/input/DesktopInputBridge.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace airfix::windows {

struct SdlGamepadButtonMapping final {
  airfix::input::ControllerDigitalControl control{
      airfix::input::ControllerDigitalControl::count};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return control != airfix::input::ControllerDigitalControl::count;
  }
};

struct SdlInputEventResult final {
  bool accepted{true};
  bool meaningfulInput{};
  bool controllerDisconnected{};
};

[[nodiscard]] airfix::input::ControlId
sdlKeyboardControl(SDL_Scancode scancode) noexcept;
[[nodiscard]] airfix::input::ControlId sdlMouseControl(Uint8 button) noexcept;
[[nodiscard]] airfix::input::Q15
sdlMouseMotionValue(float relativeMotion) noexcept;
[[nodiscard]] airfix::input::Q15 sdlGamepadAxisValue(SDL_GamepadAxis axis,
                                                     Sint16 value) noexcept;
[[nodiscard]] airfix::input::DesktopControllerAxis
sdlGamepadStickAxis(SDL_GamepadAxis axis) noexcept;
[[nodiscard]] SdlGamepadButtonMapping
sdlGamepadButton(SDL_GamepadButton button) noexcept;
[[nodiscard]] bool sdlGamepadTriggerPressed(Sint16 value) noexcept;

class AirfixSdlInputAdapter final {
public:
  AirfixSdlInputAdapter();
  explicit AirfixSdlInputAdapter(
      const airfix::input::ControllerInputRuntimeConfiguration &configuration);
  ~AirfixSdlInputAdapter();

  AirfixSdlInputAdapter(const AirfixSdlInputAdapter &) = delete;
  AirfixSdlInputAdapter &operator=(const AirfixSdlInputAdapter &) = delete;
  AirfixSdlInputAdapter(AirfixSdlInputAdapter &&) = delete;
  AirfixSdlInputAdapter &operator=(AirfixSdlInputAdapter &&) = delete;

  [[nodiscard]] SdlInputEventResult
  handleEvent(const SDL_Event &event) noexcept;
  [[nodiscard]] airfix::input::DesktopInputTick
  tick(std::uint64_t simulationTick) noexcept;

  void focusLost() noexcept;
  [[nodiscard]] bool focusGained() noexcept;
  [[nodiscard]] bool resetForGameplayBoundary() noexcept;
  void setContext(airfix::input::InputContext context) noexcept;
  [[nodiscard]] airfix::input::InputContext context() const noexcept;

  [[nodiscard]] bool controllerConnected() const noexcept;
  [[nodiscard]] const char *controllerName() const noexcept;
  [[nodiscard]] const airfix::input::ResolvedControllerInputProfile *
  activeControllerProfile() const noexcept;

private:
  [[nodiscard]] bool openGamepad(SDL_JoystickID instanceId) noexcept;
  [[nodiscard]] bool openFirstAvailableGamepad() noexcept;
  [[nodiscard]] bool restartControllerGeneration() noexcept;
  [[nodiscard]] airfix::input::ControllerSample
  sampleController() const noexcept;
  void closeGamepad() noexcept;

  airfix::input::DesktopInputBridge bridge_{};
  SDL_Gamepad *gamepad_{};
  SDL_JoystickID gamepadId_{};
  std::uint64_t nextControllerGeneration_{1U};
};

} // namespace airfix::windows
