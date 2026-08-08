#include "airfix/input/TouchControlsHaptics.hpp"

#include <array>
#include <cstdint>

namespace airfix::input {
namespace {

struct Detent final {
  std::int32_t center{};
  TouchControlsHapticEvent event{};
};

constexpr std::array<Detent, 3U> detents{{
    {.center = 0, .event = TouchControlsHapticEvent::throttleIdleDetent},
    {.center = touchThrottleMidpointQ15,
     .event = TouchControlsHapticEvent::throttleMidpointDetent},
    {.center = touchThrottleMaximumQ15,
     .event = TouchControlsHapticEvent::throttleFullDetent},
}};

[[nodiscard]] constexpr std::int32_t
distance(const std::int32_t value, const std::int32_t center) noexcept {
  const auto delta = value - center;
  return delta < 0 ? -delta : delta;
}

[[nodiscard]] constexpr const Detent *
detentForEvent(const TouchControlsHapticEvent event) noexcept {
  for (const auto &detent : detents) {
    if (detent.event == event) {
      return &detent;
    }
  }
  return nullptr;
}

[[nodiscard]] constexpr bool crossedMidpoint(const std::int32_t previous,
                                             const std::int32_t current) {
  return (previous < touchThrottleMidpointQ15 &&
          current >= touchThrottleMidpointQ15) ||
         (previous > touchThrottleMidpointQ15 &&
          current <= touchThrottleMidpointQ15);
}

} // namespace

std::optional<TouchControlsHapticEvent>
touchControlPressHapticEvent(const TouchControlElement element) noexcept {
  switch (element) {
  case TouchControlElement::throttleIncrease:
  case TouchControlElement::throttleDecrease:
  case TouchControlElement::weaponNext:
  case TouchControlElement::rearView:
  case TouchControlElement::cameraCycle:
  case TouchControlElement::cameraRecenter:
  case TouchControlElement::missionStatus:
  case TouchControlElement::pause:
    return TouchControlsHapticEvent::controlSelection;
  case TouchControlElement::flightStick:
  case TouchControlElement::throttle:
  case TouchControlElement::primaryFire:
  case TouchControlElement::secondaryFire:
  case TouchControlElement::cameraLook:
  case TouchControlElement::count:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<TouchControlsHapticEvent>
TouchThrottleDetentTracker::observe(const std::int32_t throttleQ15) noexcept {
  if (throttleQ15 < 0 || throttleQ15 > touchThrottleMaximumQ15) {
    reset();
    return std::nullopt;
  }

  const auto previous = previous_;
  previous_ = throttleQ15;

  std::optional<TouchControlsHapticEvent> released;
  if (latched_.has_value()) {
    const Detent *const latchedDetent = detentForEvent(*latched_);
    if (latchedDetent != nullptr &&
        distance(throttleQ15, latchedDetent->center) <=
            touchThrottleDetentReleaseRadiusQ15) {
      return std::nullopt;
    }
    released = latched_;
    latched_.reset();
  }

  const Detent *nearest = nullptr;
  std::int32_t nearestDistance = touchThrottleDetentTriggerRadiusQ15 + 1;
  for (const auto &detent : detents) {
    const auto candidateDistance = distance(throttleQ15, detent.center);
    if (candidateDistance <= touchThrottleDetentTriggerRadiusQ15 &&
        candidateDistance < nearestDistance) {
      nearest = &detent;
      nearestDistance = candidateDistance;
    }
  }
  if (nearest != nullptr &&
      (!released.has_value() || nearest->event != *released)) {
    latched_ = nearest->event;
    return nearest->event;
  }

  if (previous.has_value() && crossedMidpoint(*previous, throttleQ15) &&
      released != TouchControlsHapticEvent::throttleMidpointDetent) {
    latched_ = TouchControlsHapticEvent::throttleMidpointDetent;
    return latched_;
  }
  return std::nullopt;
}

void TouchThrottleDetentTracker::reset() noexcept {
  previous_.reset();
  latched_.reset();
}

} // namespace airfix::input
