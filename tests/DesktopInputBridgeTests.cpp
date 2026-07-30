#include "airfix/input/DesktopInputBridge.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using airfix::input::AnalogAxis;
using airfix::input::ControllerDigitalControl;
using airfix::input::ControllerInputProfileRecord;
using airfix::input::ControllerSample;
using airfix::input::DesktopControllerAxis;
using airfix::input::DesktopInputBridge;
using airfix::input::DigitalAction;
using airfix::input::InputContext;
using airfix::input::InputFrame;
using airfix::input::q15Min;
using airfix::input::q15One;

static_assert(
    std::is_nothrow_move_assignable_v<airfix::input::InputRouter>);
static_assert(std::is_nothrow_move_assignable_v<
              airfix::input::ControllerInputBatchBridge>);
static_assert(std::is_nothrow_move_assignable_v<DesktopInputBridge>);

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

[[nodiscard]] ControllerInputProfileRecord remappedControllerRecord() {
  auto record = airfix::input::makeDefaultControllerInputProfileRecord();
  bool changedAxis = false;
  bool changedConfirm = false;
  bool preservedConfirmRecovery = false;
  for (std::size_t index = 0U; index < record.bindingCount; ++index) {
    auto &binding = record.bindings[index];
    if (binding.control == airfix::input::controls::controller::leftStickX &&
        binding.targetKind == airfix::input::BindingTargetKind::analog &&
        binding.target == static_cast<std::uint8_t>(AnalogAxis::flightBank)) {
      binding.target = static_cast<std::uint8_t>(AnalogAxis::flightPitch);
      changedAxis = true;
    } else if (binding.control ==
                   airfix::input::controls::controller::facePrimary &&
               binding.targetKind ==
                   airfix::input::BindingTargetKind::digital &&
               binding.target ==
                   static_cast<std::uint8_t>(DigitalAction::uiConfirm)) {
      binding.target = static_cast<std::uint8_t>(DigitalAction::uiTabNext);
      changedConfirm = true;
    } else if (binding.control ==
                   airfix::input::controls::controller::rightShoulder &&
               binding.targetKind ==
                   airfix::input::BindingTargetKind::digital &&
               binding.target ==
                   static_cast<std::uint8_t>(DigitalAction::uiTabNext)) {
      binding.target = static_cast<std::uint8_t>(DigitalAction::uiConfirm);
      preservedConfirmRecovery = true;
    }
  }
  require(changedAxis, "default controller bank binding was not found");
  require(changedConfirm, "default controller confirm binding was not found");
  require(preservedConfirmRecovery,
          "default controller tab-next binding was not found");
  record.axes[0U].inverted = 1U;
  return record;
}

[[nodiscard]] ControllerInputProfileRecord sparseControllerRecord() {
  auto record = airfix::input::makeDefaultControllerInputProfileRecord();
  std::size_t writeIndex = 0U;
  for (std::size_t readIndex = 0U; readIndex < record.bindingCount;
       ++readIndex) {
    const auto &binding = record.bindings[readIndex];
    if (binding.control == airfix::input::controls::controller::rightStickX ||
        binding.control == airfix::input::controls::controller::faceLeft) {
      continue;
    }
    record.bindings[writeIndex] = binding;
    ++writeIndex;
  }
  require(writeIndex < record.bindingCount,
          "sparse controller profile did not remove a binding");
  for (std::size_t index = writeIndex; index < record.bindings.size();
       ++index) {
    record.bindings[index] = {};
  }
  record.bindingCount = static_cast<std::uint8_t>(writeIndex);
  return record;
}

void testPreparedProfileOwnsFreshRouterAndCalibratedBridge() {
  const auto record = remappedControllerRecord();
  const auto resolved = airfix::input::resolveControllerInputProfile(record);
  require(resolved.complete(), "remapped profile did not resolve");
  const auto prepared =
      airfix::input::prepareControllerInputRuntimeConfiguration(
          *resolved.profile);
  require(prepared.complete(),
          "remapped runtime configuration did not prepare");

  DesktopInputBridge bridge{*prepared.configuration};
  require(bridge.controllerProfile() != nullptr &&
              bridge.controllerProfile()->record() == record,
          "fresh desktop bridge lost its prepared profile");
  openControllerNeutralGate(bridge);
  require(bridge.controllerAxis(DesktopControllerAxis::bank, 12000),
          "configured controller axis failed");
  const auto frame = tick(bridge, 3U);
  require(frame.analog(AnalogAxis::flightBank) == 0,
          "fresh router retained the replaced bank mapping");
  require(frame.analog(AnalogAxis::flightPitch) == -12000,
          "fresh bridge did not apply calibration before remapping");
}

void testFreshProfilePairDiscardsOldInputAndRequiresNeutralSnapshot() {
  DesktopInputBridge active;
  active.setContext(InputContext::menu);
  openControllerNeutralGate(active);
  require(active.controllerButton(ControllerDigitalControl::uiConfirm, true),
          "old pending controller press failed");

  const auto record = remappedControllerRecord();
  const auto resolved = airfix::input::resolveControllerInputProfile(record);
  require(resolved.complete(), "replacement profile did not resolve");
  const auto prepared =
      airfix::input::prepareControllerInputRuntimeConfiguration(
          *resolved.profile);
  require(prepared.complete(), "replacement configuration did not prepare");

  DesktopInputBridge candidate{*prepared.configuration};
  candidate.setContext(InputContext::menu);
  candidate.resetForGameplayBoundary();
  ControllerSample heldSnapshot{};
  heldSnapshot.bank = 12000;
  heldSnapshot.uiConfirmPressed = true;
  require(candidate.connectController(41U, heldSnapshot),
          "replacement full controller snapshot failed");

  active = std::move(candidate);
  const auto blocked = tick(active, 3U);
  require(blocked.analog(AnalogAxis::uiNavigateX) == 0 &&
              !blocked.pressed(DigitalAction::uiConfirm) &&
              !blocked.held(DigitalAction::uiConfirm) &&
              !blocked.pressed(DigitalAction::uiTabNext) &&
              !blocked.held(DigitalAction::uiTabNext),
          "held snapshot or old pending edge bypassed replacement gate");

  require(active.controllerAxis(DesktopControllerAxis::bank, 0),
          "replacement neutral axis failed");
  require(active.controllerButton(ControllerDigitalControl::uiConfirm, false),
          "replacement neutral button failed");
  const auto firstNeutral = tick(active, 4U);
  require(!firstNeutral.pressed(DigitalAction::uiTabNext),
          "first neutral tick admitted replacement input");
  const auto secondNeutral = tick(active, 5U);
  require(!secondNeutral.pressed(DigitalAction::uiTabNext),
          "second neutral tick synthesized replacement input");

  require(active.controllerButton(ControllerDigitalControl::uiConfirm, true),
          "fresh post-gate controller press failed");
  const auto remapped = tick(active, 6U);
  require(remapped.pressed(DigitalAction::uiTabNext) &&
              remapped.held(DigitalAction::uiTabNext) &&
              !remapped.pressed(DigitalAction::uiConfirm) &&
              !remapped.held(DigitalAction::uiConfirm),
          "fresh press did not use the replacement binding table");
}

void testSparseProfileIgnoresUnmappedPhysicalState() {
  const auto record = sparseControllerRecord();
  const auto resolved = airfix::input::resolveControllerInputProfile(record);
  require(resolved.complete(), "sparse controller profile did not resolve");
  const auto prepared =
      airfix::input::prepareControllerInputRuntimeConfiguration(
          *resolved.profile);
  require(prepared.complete(),
          "sparse controller configuration did not prepare");

  DesktopInputBridge bridge{*prepared.configuration};
  bridge.setContext(InputContext::menu);
  ControllerSample heldUnmapped{};
  heldUnmapped.lookX = 12000;
  heldUnmapped.cameraCyclePressed = true;
  require(bridge.connectController(71U, heldUnmapped),
          "sparse controller snapshot failed");
  const auto first = tick(bridge, 1U);
  require(bridge.healthy() &&
              first.analog(AnalogAxis::cameraLookX) == 0 &&
              !first.held(DigitalAction::cameraCycle),
          "unmapped full-state input reached the router");

  require(bridge.controllerAxis(DesktopControllerAxis::lookX, 0),
          "unmapped axis release failed");
  require(bridge.controllerButton(ControllerDigitalControl::cameraCycle,
                                  false),
          "unmapped button release failed");
  (void)tick(bridge, 2U);

  require(bridge.controllerButton(ControllerDigitalControl::uiConfirm, true),
          "mapped sparse-profile button press failed");
  const auto mapped = tick(bridge, 3U);
  require(bridge.healthy() && mapped.pressed(DigitalAction::uiConfirm) &&
              mapped.held(DigitalAction::uiConfirm),
          "remaining sparse-profile binding stopped working");
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

void testGameplayBoundaryRequiresFreshNeutralInput() {
  using namespace airfix::input::controls;

  DesktopInputBridge bridge;
  require(bridge.key(keyboard::space, true), "pre-boundary fire press failed");
  require(tick(bridge, 1U).held(DigitalAction::combatPrimaryFire),
          "pre-boundary fire was not held");

  bridge.resetForGameplayBoundary();
  require(!tick(bridge, 2U).held(DigitalAction::combatPrimaryFire),
          "gameplay boundary retained a held action");
  require(bridge.key(keyboard::space, true),
          "held-on-boundary fire event failed");
  require(!tick(bridge, 3U).held(DigitalAction::combatPrimaryFire),
          "held-on-boundary input bypassed the neutral gate");
  require(bridge.key(keyboard::space, false),
          "post-boundary fire release failed");
  (void)tick(bridge, 4U);
  (void)tick(bridge, 5U);
  require(bridge.key(keyboard::space, true),
          "fresh post-boundary fire press failed");
  require(tick(bridge, 6U).held(DigitalAction::combatPrimaryFire),
          "input did not recover after boundary neutralization");
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
    testPreparedProfileOwnsFreshRouterAndCalibratedBridge();
    testFreshProfilePairDiscardsOldInputAndRequiresNeutralSnapshot();
    testSparseProfileIgnoresUnmappedPhysicalState();
    testLifecycleNeutralization();
    testUnfocusedControllerEventsAreIgnored();
    testGameplayBoundaryRequiresFreshNeutralInput();
    testKeyboardAndMouseDeviceRemoval();
    testInvalidControllerInputFailsClosed();
  } catch (const std::exception &error) {
    std::cerr << "DesktopInputBridgeTests failed: " << error.what() << '\n';
    return 1;
  }

  std::cout << "DesktopInputBridgeTests passed\n";
  return 0;
}
