#include "airfix/input/TouchControlsLayout.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using airfix::input::TouchControlElement;
using airfix::input::TouchControlRect;
using airfix::input::TouchControlsDensity;
using airfix::input::TouchControlsHandedness;
using airfix::input::TouchControlsLayoutProfile;
using airfix::input::TouchControlsLayoutStatus;

void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] bool close(const float left, const float right,
                         const float tolerance = 0.001F) {
  return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool close(const TouchControlRect &left,
                         const TouchControlRect &right,
                         const float tolerance = 0.001F) {
  return close(left.x, right.x, tolerance) &&
         close(left.y, right.y, tolerance) &&
         close(left.width, right.width, tolerance) &&
         close(left.height, right.height, tolerance);
}

[[nodiscard]] bool containedBy(const TouchControlRect &inner,
                               const TouchControlRect &outer) {
  return inner.width >= 0.0F && inner.height >= 0.0F &&
         inner.x >= outer.x - 0.001F && inner.y >= outer.y - 0.001F &&
         inner.x + inner.width <= outer.x + outer.width + 0.001F &&
         inner.y + inner.height <= outer.y + outer.height + 0.001F;
}

void testDefaultRegularLayoutMatchesNativeReference() {
  const TouchControlRect bounds{.width = 844.0F, .height = 390.0F};
  const auto layout = airfix::input::buildTouchControlsLayout(bounds);
  require(layout.ready() && !layout.compact,
          "regular reference layout did not build");
  require(close(layout.visualFrame(TouchControlElement::flightStick),
                {.x = 20.0F, .y = 246.0F, .width = 124.0F, .height = 124.0F}),
          "regular stick frame drifted from the native layout");
  require(close(layout.visualFrame(TouchControlElement::throttle),
                {.x = 20.0F, .y = 84.0F, .width = 52.0F, .height = 150.0F}),
          "regular throttle frame drifted from the native layout");
  require(close(layout.visualFrame(TouchControlElement::primaryFire),
                {.x = 736.0F, .y = 282.0F, .width = 88.0F, .height = 88.0F}),
          "regular primary-fire frame drifted from the native layout");
  require(close(layout.visualFrame(TouchControlElement::missionStatus),
                {.x = 20.0F, .y = 20.0F, .width = 96.0F, .height = 52.0F}),
          "regular mission frame drifted from the native layout");
  require(close(layout.visualFrame(TouchControlElement::pause),
                {.x = 772.0F, .y = 20.0F, .width = 52.0F, .height = 52.0F}),
          "regular pause frame drifted from the native layout");
  require(close(layout.visualFrame(TouchControlElement::cameraLook),
                {.x = 286.96F, .y = 84.0F, .width = 537.04F, .height = 150.0F}),
          "regular camera-look frame drifted from the native layout");
  require(close(layout.stickTravelRadius, 33.0F) &&
              close(layout.lookTravelRadius, 67.5F),
          "regular travel radii drifted from the native layout");
}

void testAutomaticAndForcedCompactLayouts() {
  const auto compact = airfix::input::buildTouchControlsLayout(
      {.width = 667.0F, .height = 375.0F});
  require(compact.ready() && compact.compact,
          "SE-sized layout did not select compact geometry");
  require(close(compact.visualFrame(TouchControlElement::flightStick),
                {.x = 12.0F, .y = 267.0F, .width = 96.0F, .height = 96.0F}),
          "compact stick frame is incorrect");
  require(close(compact.visualFrame(TouchControlElement::primaryFire),
                {.x = 583.0F, .y = 291.0F, .width = 72.0F, .height = 72.0F}),
          "compact primary-fire frame is incorrect");

  const TouchControlsLayoutProfile forced{
      .density = TouchControlsDensity::compact,
  };
  const auto forcedCompact = airfix::input::buildTouchControlsLayout(
      {.width = 844.0F, .height = 390.0F}, forced);
  require(
      forcedCompact.ready() && forcedCompact.compact &&
          close(
              forcedCompact.visualFrame(TouchControlElement::flightStick).width,
              96.0F),
      "forced compact profile did not override regular geometry");
}

void testLeftHandedLayoutMirrorsEveryFrameInsideOffsetSafeBounds() {
  const TouchControlRect bounds{
      .x = 59.0F,
      .y = 0.0F,
      .width = 726.0F,
      .height = 390.0F,
  };
  const auto standard = airfix::input::buildTouchControlsLayout(bounds);
  const auto mirrored = airfix::input::buildTouchControlsLayout(
      bounds, {
                  .handedness = TouchControlsHandedness::leftHanded,
              });
  require(standard.ready() && mirrored.ready() &&
              standard.compact == mirrored.compact,
          "left-handed layout did not build");

  for (std::size_t index = 0U; index < airfix::input::touchControlElementCount;
       ++index) {
    const auto element = static_cast<TouchControlElement>(index);
    const auto expectedMirror = [&](const TouchControlRect &frame) {
      if (frame.width <= 0.0F || frame.height <= 0.0F) {
        return TouchControlRect{};
      }
      return TouchControlRect{
          .x = bounds.x + bounds.width - (frame.x - bounds.x) - frame.width,
          .y = frame.y,
          .width = frame.width,
          .height = frame.height,
      };
    };
    require(close(mirrored.visualFrame(element),
                  expectedMirror(standard.visualFrame(element))) &&
                close(mirrored.captureFrame(element),
                      expectedMirror(standard.captureFrame(element))),
            "left-handed mirroring changed more than horizontal placement");
  }
  require(mirrored.visualFrame(TouchControlElement::flightStick).x >
              mirrored.visualFrame(TouchControlElement::primaryFire).x,
          "left-handed layout did not exchange the flight/combat sides");
}

void testSupportedSafeAreasStayBoundedWithUsableCaptureTargets() {
  constexpr std::array<TouchControlRect, 6U> safeAreas{{
      {.width = 640.0F, .height = 480.0F},
      {.width = 667.0F, .height = 375.0F},
      {.x = 47.0F, .width = 750.0F, .height = 375.0F},
      {.x = 59.0F, .width = 734.0F, .height = 393.0F},
      {.x = 59.0F, .width = 814.0F, .height = 430.0F},
      {.x = 88.0F, .width = 1680.0F, .height = 540.0F},
  }};

  for (const auto &bounds : safeAreas) {
    for (const auto handedness : {TouchControlsHandedness::rightHanded,
                                  TouchControlsHandedness::leftHanded}) {
      const auto layout = airfix::input::buildTouchControlsLayout(
          bounds, {.handedness = handedness});
      require(layout.ready(), "supported safe-area layout failed");
      for (std::size_t index = 0U;
           index < airfix::input::touchControlElementCount; ++index) {
        const auto element = static_cast<TouchControlElement>(index);
        const auto &visual = layout.visualFrame(element);
        const auto &capture = layout.captureFrame(element);
        require(containedBy(visual, bounds) && containedBy(capture, bounds),
                "layout escaped its safe-area bounds");
        if (capture.width > 0.0F && capture.height > 0.0F) {
          require(capture.width >= 44.0F && capture.height >= 44.0F,
                  "capture frame violated the minimum target size");
        }
      }
    }
  }
}

void testInvalidProfileAndBoundsFailClosed() {
  auto profile = TouchControlsLayoutProfile{};
  profile.schemaVersion = 2U;
  require(airfix::input::buildTouchControlsLayout(
              {.width = 844.0F, .height = 390.0F}, profile)
                  .status == TouchControlsLayoutStatus::invalidProfile,
          "future profile schema was accepted");

  profile = {};
  profile.handedness = static_cast<TouchControlsHandedness>(0xFFU);
  require(airfix::input::buildTouchControlsLayout(
              {.width = 844.0F, .height = 390.0F}, profile)
                  .status == TouchControlsLayoutStatus::invalidProfile,
          "forged handedness was accepted");

  profile = {};
  profile.density = static_cast<TouchControlsDensity>(0xFFU);
  require(airfix::input::buildTouchControlsLayout(
              {.width = 844.0F, .height = 390.0F}, profile)
                  .status == TouchControlsLayoutStatus::invalidProfile,
          "forged density was accepted");

  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (const auto bounds : std::array<TouchControlRect, 4U>{
           TouchControlRect{},
           TouchControlRect{.width = -1.0F, .height = 390.0F},
           TouchControlRect{.width = 844.0F, .height = 0.0F},
           TouchControlRect{.x = nan, .width = 844.0F, .height = 390.0F},
       }) {
    const auto layout = airfix::input::buildTouchControlsLayout(bounds);
    require(layout.status == TouchControlsLayoutStatus::invalidSafeBounds &&
                !layout.ready(),
            "invalid safe bounds did not fail closed");
  }
}

} // namespace

int main() {
  try {
    testDefaultRegularLayoutMatchesNativeReference();
    testAutomaticAndForcedCompactLayouts();
    testLeftHandedLayoutMirrorsEveryFrameInsideOffsetSafeBounds();
    testSupportedSafeAreasStayBoundedWithUsableCaptureTargets();
    testInvalidProfileAndBoundsFailClosed();
  } catch (const std::exception &error) {
    std::cerr << "TouchControlsLayoutTests failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
