#include "AirfixSdlInputAdapter.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::input::ControllerDigitalControl;
using airfix::input::DesktopControllerAxis;
using airfix::input::q15Min;
using airfix::input::q15One;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testKeyboardUsesUsbHidIds() {
  using namespace airfix::input::controls::keyboard;
  require(
      airfix::windows::sdlKeyboardControl(SDL_SCANCODE_W) == w &&
          airfix::windows::sdlKeyboardControl(SDL_SCANCODE_SPACE) == space &&
          airfix::windows::sdlKeyboardControl(SDL_SCANCODE_ESCAPE) == escape &&
          airfix::windows::sdlKeyboardControl(SDL_SCANCODE_UP) == arrowUp,
      "SDL scancodes do not match stable USB HID controls");
  require(!airfix::windows::sdlKeyboardControl(SDL_SCANCODE_UNKNOWN).valid(),
          "unknown SDL scancode was accepted");
}

void testMouseMappingAndScaling() {
  using namespace airfix::input::controls::mouse;
  require(
      airfix::windows::sdlMouseControl(SDL_BUTTON_LEFT) == leftButton &&
          airfix::windows::sdlMouseControl(SDL_BUTTON_RIGHT) == rightButton &&
          airfix::windows::sdlMouseControl(SDL_BUTTON_MIDDLE) == middleButton &&
          airfix::windows::sdlMouseControl(SDL_BUTTON_X1) == extraButtonOne &&
          airfix::windows::sdlMouseControl(SDL_BUTTON_X2) == extraButtonTwo,
      "SDL mouse buttons were mapped incorrectly");
  require(!airfix::windows::sdlMouseControl(0U).valid(),
          "invalid SDL mouse button was accepted");
  require(airfix::windows::sdlMouseMotionValue(1.0F) == 2048 &&
              airfix::windows::sdlMouseMotionValue(-16.0F) == q15Min &&
              airfix::windows::sdlMouseMotionValue(16.0F) == q15One,
          "mouse Q15 scaling or saturation was incorrect");
  require(airfix::windows::sdlMouseMotionValue(
              std::numeric_limits<float>::quiet_NaN()) == 0,
          "non-finite mouse motion was not neutralized");
}

void testGamepadAxes() {
  require(airfix::windows::sdlGamepadStickAxis(SDL_GAMEPAD_AXIS_LEFTX) ==
                  DesktopControllerAxis::bank &&
              airfix::windows::sdlGamepadStickAxis(SDL_GAMEPAD_AXIS_LEFTY) ==
                  DesktopControllerAxis::pitch &&
              airfix::windows::sdlGamepadStickAxis(SDL_GAMEPAD_AXIS_RIGHTX) ==
                  DesktopControllerAxis::lookX &&
              airfix::windows::sdlGamepadStickAxis(SDL_GAMEPAD_AXIS_RIGHTY) ==
                  DesktopControllerAxis::lookY,
          "SDL stick axes were mapped incorrectly");
  require(airfix::windows::sdlGamepadStickAxis(
              SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == DesktopControllerAxis::count,
          "trigger was exposed as a stick axis");
  require(airfix::windows::sdlGamepadAxisValue(SDL_GAMEPAD_AXIS_LEFTX,
                                               -32768) == q15Min &&
              airfix::windows::sdlGamepadAxisValue(SDL_GAMEPAD_AXIS_LEFTY,
                                                   -32768) == q15One &&
              airfix::windows::sdlGamepadAxisValue(SDL_GAMEPAD_AXIS_RIGHTY,
                                                   1234) == -1234,
          "SDL signed axis normalization or Y inversion was incorrect");
  require(airfix::windows::sdlGamepadAxisValue(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                                               -100) == 0 &&
              airfix::windows::sdlGamepadAxisValue(
                  SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 20000) == 20000,
          "SDL trigger range was not clamped to positive Q15");
  require(!airfix::windows::sdlGamepadTriggerPressed(
              airfix::input::controllerTriggerActuationQ15 - 1) &&
              airfix::windows::sdlGamepadTriggerPressed(
                  airfix::input::controllerTriggerActuationQ15),
          "SDL trigger actuation threshold was incorrect");
}

void requireButton(const SDL_GamepadButton button,
                   const ControllerDigitalControl expected,
                   const std::string &context) {
  const auto mapping = airfix::windows::sdlGamepadButton(button);
  require(mapping.valid() && mapping.control == expected,
          context + " mapped incorrectly");
}

void testGamepadButtons() {
  requireButton(SDL_GAMEPAD_BUTTON_SOUTH, ControllerDigitalControl::uiConfirm,
                "south button");
  requireButton(SDL_GAMEPAD_BUTTON_EAST, ControllerDigitalControl::uiCancel,
                "east button");
  requireButton(SDL_GAMEPAD_BUTTON_WEST, ControllerDigitalControl::cameraCycle,
                "west button");
  requireButton(SDL_GAMEPAD_BUTTON_NORTH,
                ControllerDigitalControl::missionStatus, "north button");
  requireButton(SDL_GAMEPAD_BUTTON_START, ControllerDigitalControl::pause,
                "start button");
  requireButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                ControllerDigitalControl::cameraRecenter, "right-stick click");
  requireButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
                ControllerDigitalControl::rearView, "left shoulder");
  requireButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
                ControllerDigitalControl::weaponNext, "right shoulder");
  requireButton(SDL_GAMEPAD_BUTTON_DPAD_UP,
                ControllerDigitalControl::throttleUp, "D-pad up");
  requireButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                ControllerDigitalControl::throttleDown, "D-pad down");
  requireButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                ControllerDigitalControl::uiPrevious, "D-pad left");
  requireButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, ControllerDigitalControl::uiNext,
                "D-pad right");
}

void testPreparedProfileIsInstalledAtAdapterConstruction() {
  const auto initialRecord =
      airfix::input::makeDefaultControllerInputProfileRecord();
  const auto initial =
      airfix::input::resolveControllerInputProfile(initialRecord);
  const auto initialConfiguration =
      initial.complete()
          ? airfix::input::prepareControllerInputRuntimeConfiguration(
                *initial.profile)
          : airfix::input::ControllerInputRuntimeConfigurationResult{};
  require(initialConfiguration.complete(),
          "initial controller configuration did not prepare");

  airfix::windows::AirfixSdlInputAdapter adapter{
      *initialConfiguration.configuration};
  require(adapter.activeControllerProfile() != nullptr &&
              adapter.activeControllerProfile()->record() == initialRecord,
          "Windows adapter did not start with the prepared profile");
}

} // namespace

int main() {
  if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
    std::cerr << "AirfixSdlInputMappingTests failed: SDL initialization\n";
    return 1;
  }
  int exitCode = 0;
  try {
    testKeyboardUsesUsbHidIds();
    testMouseMappingAndScaling();
    testGamepadAxes();
    testGamepadButtons();
    testPreparedProfileIsInstalledAtAdapterConstruction();
  } catch (const std::exception &error) {
    std::cerr << "AirfixSdlInputMappingTests failed: " << error.what() << '\n';
    exitCode = 1;
  }
  SDL_Quit();
  if (exitCode != 0) {
    return exitCode;
  }

  std::cout << "AirfixSdlInputMappingTests passed\n";
  return 0;
}
