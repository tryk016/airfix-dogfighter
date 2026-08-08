#include "airfix/input/TouchControlsHaptics.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testControlSelectionPolicyExcludesGameplayFireAttempts() {
  using airfix::input::TouchControlElement;
  using airfix::input::TouchControlsHapticEvent;
  for (const auto element : {
           TouchControlElement::throttleIncrease,
           TouchControlElement::throttleDecrease,
           TouchControlElement::weaponNext,
           TouchControlElement::rearView,
           TouchControlElement::cameraCycle,
           TouchControlElement::cameraRecenter,
           TouchControlElement::missionStatus,
           TouchControlElement::pause,
       }) {
    require(airfix::input::touchControlPressHapticEvent(element) ==
                TouchControlsHapticEvent::controlSelection,
            "touch UI selection lost its haptic event");
  }
  for (const auto element : {
           TouchControlElement::flightStick,
           TouchControlElement::throttle,
           TouchControlElement::primaryFire,
           TouchControlElement::secondaryFire,
           TouchControlElement::cameraLook,
           TouchControlElement::count,
       }) {
    require(!airfix::input::touchControlPressHapticEvent(element).has_value(),
            "continuous input or unaccepted fire attempt gained haptics");
  }
}

void testThrottleDetentsUseHysteresisAndDoNotChangeValues() {
  using airfix::input::TouchControlsHapticEvent;
  airfix::input::TouchThrottleDetentTracker tracker;

  require(tracker.observe(0) == TouchControlsHapticEvent::throttleIdleDetent,
          "idle detent did not trigger");
  require(!tracker.observe(200).has_value() &&
              !tracker.observe(900).has_value(),
          "idle detent repeated inside its release radius");
  require(!tracker.observe(2000).has_value(),
          "leaving idle unexpectedly emitted feedback");

  require(tracker.observe(17000) ==
              TouchControlsHapticEvent::throttleMidpointDetent,
          "fast midpoint crossing did not trigger");
  require(!tracker.observe(16300).has_value() &&
              !tracker.observe(17000).has_value(),
          "midpoint jitter repeated feedback");
  require(!tracker.observe(19000).has_value(),
          "leaving midpoint unexpectedly emitted feedback");
  require(tracker.observe(16000) ==
              TouchControlsHapticEvent::throttleMidpointDetent,
          "intentional reverse midpoint crossing did not rearm");

  require(!tracker.observe(30000).has_value(),
          "ordinary high throttle emitted feedback");
  require(tracker.observe(32767) ==
              TouchControlsHapticEvent::throttleFullDetent,
          "full detent did not trigger");
  require(!tracker.observe(32600).has_value(),
          "full detent repeated inside hysteresis");
}

void testResetAndInvalidValuesFailSilent() {
  using airfix::input::TouchControlsHapticEvent;
  airfix::input::TouchThrottleDetentTracker tracker;
  require(!tracker.observe(-1).has_value() &&
              !tracker.observe(32767 + 1).has_value(),
          "invalid throttle value emitted haptics");
  require(tracker.observe(16384) ==
              TouchControlsHapticEvent::throttleMidpointDetent,
          "valid detent did not recover after invalid input");
  tracker.reset();
  require(tracker.observe(16384) ==
              TouchControlsHapticEvent::throttleMidpointDetent,
          "explicit gesture reset did not rearm detents");
}

void testDirectDetentTransitionsUseTheDestinationSample() {
  using airfix::input::TouchControlsHapticEvent;
  airfix::input::TouchThrottleDetentTracker tracker;

  require(tracker.observe(0) == TouchControlsHapticEvent::throttleIdleDetent,
          "idle setup detent did not trigger");
  require(tracker.observe(16384) ==
              TouchControlsHapticEvent::throttleMidpointDetent,
          "direct idle-to-midpoint transition lost destination feedback");

  tracker.reset();
  require(tracker.observe(0) == TouchControlsHapticEvent::throttleIdleDetent,
          "idle setup detent did not rearm");
  require(tracker.observe(32767) ==
              TouchControlsHapticEvent::throttleFullDetent,
          "direct idle-to-full transition lost destination feedback");

  tracker.reset();
  require(tracker.observe(16384) ==
              TouchControlsHapticEvent::throttleMidpointDetent,
          "midpoint setup detent did not rearm");
  require(tracker.observe(32767) ==
              TouchControlsHapticEvent::throttleFullDetent,
          "direct midpoint-to-full transition lost destination feedback");
}

} // namespace

int main() {
  try {
    testControlSelectionPolicyExcludesGameplayFireAttempts();
    testThrottleDetentsUseHysteresisAndDoNotChangeValues();
    testResetAndInvalidValuesFailSilent();
    testDirectDetentTransitionsUseTheDestinationSample();
    std::cout << "Touch controls haptics tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Touch controls haptics tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
