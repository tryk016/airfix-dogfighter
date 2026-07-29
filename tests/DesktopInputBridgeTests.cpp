#include "airfix/input/DesktopInputBridge.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using airfix::input::AnalogAxis;
using airfix::input::ControllerDigitalControl;
using airfix::input::ControllerSample;
using airfix::input::DesktopControllerAxis;
using airfix::input::DesktopInputBridge;
using airfix::input::DigitalAction;
using airfix::input::InputContext;
using airfix::input::InputFrame;
using airfix::input::q15Min;
using airfix::input::q15One;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] InputFrame tick(DesktopInputBridge &bridge,
                              const std::uint64_t simulationTick) {
  const auto result = bridge.tick(simulationTick);
  require(result.accepted, "desktop input tick failed");
  return result.frame;
}

void testKeyboardGameplayAndMenuBindings() {
  using namespace airfix::input::controls::keyboard;

  DesktopInputBridge bridge;
  require(bridge.key(arrowRight, true), "right key press failed");
  require(bridge.key(arrowUp, true), "up key press failed");
  require(bridge.key(w, true), "throttle key press failed");
  require(bridge.key(space, true), "primary key press failed");
  require(bridge.key(leftControl, true), "secondary key press failed");
  require(bridge.key(tab, true), "weapon key press failed");
  require(bridge.key(r, true), "rear-view key press failed");
  require(bridge.key(c, true), "camera key press failed");
  require(bridge.key(f, true), "recenter key press failed");
  require(bridge.key(m, true), "mission key press failed");

  const auto gameplay = tick(bridge, 1U);
  require(gameplay.analog(AnalogAxis::flightBank) == q15One,
          "right key did not produce positive bank");
  require(gameplay.analog(AnalogAxis::flightPitch) == q15One,
          "up key did not produce positive pitch");
  require(gameplay.analog(AnalogAxis::flightThrottleDelta) == q15One,
          "W did not produce positive throttle delta");
  for (const auto action : {
           DigitalAction::combatPrimaryFire,
           DigitalAction::combatSecondaryFire,
           DigitalAction::combatWeaponNext,
           DigitalAction::cameraRearView,
           DigitalAction::cameraCycle,
           DigitalAction::cameraRecenter,
           DigitalAction::missionStatus,
       }) {
    require(gameplay.pressed(action) && gameplay.held(action),
            "keyboard gameplay action was not routed");
  }

  bridge.focusLost();
  bridge.focusGained();
  (void)tick(bridge, 2U);
  (void)tick(bridge, 3U);
  bridge.setContext(InputContext::menu);
  (void)tick(bridge, 4U);
  (void)tick(bridge, 5U);
  require(bridge.key(arrowLeft, true), "menu left press failed");
  require(bridge.key(arrowDown, true), "menu down press failed");
  require(bridge.key(enter, true), "menu confirm press failed");
  require(bridge.key(escape, true), "menu cancel press failed");
  require(bridge.key(q, true), "previous tab press failed");
  require(bridge.key(e, true), "next tab press failed");

  const auto menu = tick(bridge, 6U);
  require(menu.analog(AnalogAxis::uiNavigateX) == q15Min &&
              menu.analog(AnalogAxis::uiNavigateY) == q15Min,
          "keyboard menu navigation was not routed");
  require(menu.pressed(DigitalAction::uiConfirm) &&
              menu.pressed(DigitalAction::uiCancel) &&
              menu.pressed(DigitalAction::uiTabPrevious) &&
              menu.pressed(DigitalAction::uiTabNext),
          "keyboard menu actions were incomplete");
}

void testMousePulsesAndAxisReset() {
  using namespace airfix::input::controls::mouse;

  DesktopInputBridge bridge;
  require(bridge.mouseMotion(7000, -8000), "mouse motion failed");
  require(bridge.mouseButton(leftButton, true), "mouse primary press failed");
  require(bridge.mouseButton(rightButton, true),
          "mouse secondary press failed");

  const auto moved = tick(bridge, 1U);
  require(moved.analog(AnalogAxis::cameraLookX) == 7000 &&
              moved.analog(AnalogAxis::cameraLookY) == -8000,
          "mouse motion did not reach camera axes");
  require(moved.held(DigitalAction::combatPrimaryFire) &&
              moved.held(DigitalAction::combatSecondaryFire),
          "mouse fire buttons were not held");

  const auto settled = tick(bridge, 2U);
  require(settled.analog(AnalogAxis::cameraLookX) == 0 &&
              settled.analog(AnalogAxis::cameraLookY) == 0,
          "relative mouse axes did not reset after one tick");
  require(settled.held(DigitalAction::combatPrimaryFire),
          "mouse hold was lost with motion reset");

  require(bridge.mousePulse(wheelUp), "mouse wheel pulse failed");
  const auto wheel = tick(bridge, 3U);
  require(wheel.pressed(DigitalAction::combatWeaponNext) &&
              wheel.released(DigitalAction::combatWeaponNext) &&
              !wheel.held(DigitalAction::combatWeaponNext),
          "mouse wheel tap lost an edge");
}

void openControllerNeutralGate(DesktopInputBridge &bridge) {
  require(bridge.connectController(1U, {}),
          "neutral controller connection failed");
  (void)tick(bridge, 1U);
  (void)tick(bridge, 2U);
}

void testControllerDeadzoneEdgesAndDisconnect() {
  DesktopInputBridge bridge;
  openControllerNeutralGate(bridge);

  require(bridge.controllerAxis(DesktopControllerAxis::bank, 4095),
          "controller deadzone axis failed");
  require(bridge.controllerAxis(DesktopControllerAxis::pitch, -6000),
          "controller pitch failed");
  require(
      bridge.controllerButton(ControllerDigitalControl::primaryTrigger, true),
      "controller trigger press failed");
  require(
      bridge.controllerButton(ControllerDigitalControl::primaryTrigger, false),
      "controller trigger release failed");

  const auto frame = tick(bridge, 3U);
  require(frame.analog(AnalogAxis::flightBank) == 0,
          "controller deadzone was not applied");
  require(frame.analog(AnalogAxis::flightPitch) == -6000,
          "controller pitch was changed");
  require(frame.pressed(DigitalAction::combatPrimaryFire) &&
              frame.released(DigitalAction::combatPrimaryFire) &&
              !frame.held(DigitalAction::combatPrimaryFire),
          "complete controller tap was not preserved");

  require(
      bridge.controllerButton(ControllerDigitalControl::secondaryTrigger, true),
      "controller secondary press failed");
  require(tick(bridge, 4U).held(DigitalAction::combatSecondaryFire),
          "controller secondary hold was not routed");
  require(bridge.disconnectController(), "controller disconnect failed");
  const auto disconnected = tick(bridge, 5U);
  require(disconnected.released(DigitalAction::combatSecondaryFire) &&
              !disconnected.held(DigitalAction::combatSecondaryFire),
          "controller disconnect did not synthesize release");
}

void testControllerReplacementRequiresNeutral() {
  DesktopInputBridge bridge;
  openControllerNeutralGate(bridge);
  require(bridge.controllerAxis(DesktopControllerAxis::bank, 9000),
          "first controller bank failed");
  require(tick(bridge, 3U).analog(AnalogAxis::flightBank) == 9000,
          "first controller never acquired bank");
  require(bridge.disconnectController(), "first controller loss failed");

  ControllerSample heldReplacement{};
  heldReplacement.bank = 10000;
  require(bridge.connectController(2U, heldReplacement),
          "replacement controller connection failed");
  require(tick(bridge, 4U).analog(AnalogAxis::flightBank) == 0,
          "held replacement controller bypassed neutral gate");
  require(bridge.controllerAxis(DesktopControllerAxis::bank, 0),
          "replacement controller neutral failed");
  (void)tick(bridge, 5U);
  (void)tick(bridge, 6U);
  require(bridge.controllerAxis(DesktopControllerAxis::bank, 10000),
          "replacement controller reacquisition failed");
  require(tick(bridge, 7U).analog(AnalogAxis::flightBank) == 10000,
          "replacement controller did not recover after neutral");
}

void testLifecycleNeutralization() {
  using namespace airfix::input::controls::keyboard;

  DesktopInputBridge bridge;
  require(bridge.key(space, true), "initial fire press failed");
  require(tick(bridge, 1U).held(DigitalAction::combatPrimaryFire),
          "initial fire was not held");

  bridge.focusLost();
  const auto lost = tick(bridge, 2U);
  require(lost.released(DigitalAction::combatPrimaryFire) &&
              !lost.held(DigitalAction::combatPrimaryFire),
          "focus loss did not release keyboard input");
  require(bridge.key(space, true), "unfocused input should be safely ignored");
  require(!tick(bridge, 3U).held(DigitalAction::combatPrimaryFire),
          "unfocused key leaked into a frame");

  bridge.focusGained();
  require(bridge.key(space, true), "held-on-regain key press failed");
  require(!tick(bridge, 4U).held(DigitalAction::combatPrimaryFire),
          "held input bypassed lifecycle neutral gate");
  require(bridge.key(space, false), "regain key release failed");
  (void)tick(bridge, 5U);
  (void)tick(bridge, 6U);
  require(bridge.key(space, true), "post-neutral fire press failed");
  require(tick(bridge, 7U).held(DigitalAction::combatPrimaryFire),
          "input did not recover after two neutral ticks");
}

void testUnfocusedControllerEventsAreIgnored() {
  DesktopInputBridge bridge;
  openControllerNeutralGate(bridge);
  bridge.focusLost();
  require(bridge.controllerAxis(DesktopControllerAxis::bank, 12000),
          "unfocused controller axis was not safely ignored");
  require(
      bridge.controllerButton(ControllerDigitalControl::primaryTrigger, true),
      "unfocused controller button was not safely ignored");
  require(bridge.healthy(), "unfocused controller event failed the bridge");
  require(!tick(bridge, 3U).held(DigitalAction::combatPrimaryFire),
          "unfocused controller event leaked into the frame");
}

void testKeyboardAndMouseDeviceRemoval() {
  using namespace airfix::input::controls;

  DesktopInputBridge bridge;
  require(bridge.key(keyboard::space, true), "keyboard hold failed");
  require(bridge.mouseButton(mouse::rightButton, true), "mouse hold failed");
  const auto held = tick(bridge, 1U);
  require(held.held(DigitalAction::combatPrimaryFire) &&
              held.held(DigitalAction::combatSecondaryFire),
          "device holds did not reach the frame");

  bridge.keyboardDisconnected();
  bridge.mouseDisconnected();
  const auto removed = tick(bridge, 2U);
  require(removed.released(DigitalAction::combatPrimaryFire) &&
              removed.released(DigitalAction::combatSecondaryFire) &&
              !removed.held(DigitalAction::combatPrimaryFire) &&
              !removed.held(DigitalAction::combatSecondaryFire),
          "device removal did not release keyboard and mouse actions");
}

void testInvalidControllerInputFailsClosed() {
  DesktopInputBridge bridge;
  ControllerSample invalid{};
  invalid.bank = static_cast<std::int16_t>(-32768);
  require(!bridge.connectController(1U, invalid),
          "invalid Q15 controller state was accepted");
  require(!bridge.healthy(), "invalid state did not fail closed");
  require(!bridge.tick(1U).accepted, "failed bridge emitted a frame");
}

} // namespace

int main() {
  try {
    testKeyboardGameplayAndMenuBindings();
    testMousePulsesAndAxisReset();
    testControllerDeadzoneEdgesAndDisconnect();
    testControllerReplacementRequiresNeutral();
    testLifecycleNeutralization();
    testUnfocusedControllerEventsAreIgnored();
    testKeyboardAndMouseDeviceRemoval();
    testInvalidControllerInputFailsClosed();
  } catch (const std::exception &error) {
    std::cerr << "DesktopInputBridgeTests failed: " << error.what() << '\n';
    return 1;
  }

  std::cout << "DesktopInputBridgeTests passed\n";
  return 0;
}
