#include "airfix/input/TouchControlsLayout.hpp"

#include <algorithm>
#include <cmath>

namespace airfix::input {
namespace {

constexpr float minimumTargetSize = 44.0F;

[[nodiscard]] constexpr std::size_t
indexOf(const TouchControlElement element) noexcept {
  return static_cast<std::size_t>(element);
}

[[nodiscard]] bool finiteRect(const TouchControlRect &rect) noexcept {
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height);
}

[[nodiscard]] bool emptyRect(const TouchControlRect &rect) noexcept {
  return rect.width <= 0.0F || rect.height <= 0.0F;
}

[[nodiscard]] TouchControlRect clampToBounds(TouchControlRect frame,
                                             const TouchControlRect &bounds) {
  if (emptyRect(bounds)) {
    return {};
  }

  frame.width = std::min(std::max(0.0F, frame.width), bounds.width);
  frame.height = std::min(std::max(0.0F, frame.height), bounds.height);
  frame.x =
      std::clamp(frame.x, bounds.x, bounds.x + bounds.width - frame.width);
  frame.y =
      std::clamp(frame.y, bounds.y, bounds.y + bounds.height - frame.height);
  return frame;
}

[[nodiscard]] TouchControlRect intersect(const TouchControlRect &left,
                                         const TouchControlRect &right) {
  const float minimumX = std::max(left.x, right.x);
  const float minimumY = std::max(left.y, right.y);
  const float maximumX = std::min(left.x + left.width, right.x + right.width);
  const float maximumY = std::min(left.y + left.height, right.y + right.height);
  if (maximumX <= minimumX || maximumY <= minimumY) {
    return {};
  }
  return {
      .x = minimumX,
      .y = minimumY,
      .width = maximumX - minimumX,
      .height = maximumY - minimumY,
  };
}

[[nodiscard]] constexpr TouchControlRect expanded(const TouchControlRect &rect,
                                                  const float amount) {
  return {
      .x = rect.x - amount,
      .y = rect.y - amount,
      .width = rect.width + amount * 2.0F,
      .height = rect.height + amount * 2.0F,
  };
}

[[nodiscard]] TouchControlRect
enforceMinimumCaptureSize(const TouchControlRect &capture,
                          const TouchControlRect &safeBounds) {
  if (emptyRect(capture) || emptyRect(safeBounds)) {
    return {};
  }

  const float width =
      std::min(safeBounds.width, std::max(minimumTargetSize, capture.width));
  const float height =
      std::min(safeBounds.height, std::max(minimumTargetSize, capture.height));
  return clampToBounds(
      {
          .x = capture.x + capture.width * 0.5F - width * 0.5F,
          .y = capture.y + capture.height * 0.5F - height * 0.5F,
          .width = width,
          .height = height,
      },
      safeBounds);
}

[[nodiscard]] TouchControlRect
mirrorHorizontally(const TouchControlRect &frame,
                   const TouchControlRect &safeBounds) {
  if (emptyRect(frame)) {
    return {};
  }
  return {
      .x = safeBounds.x + safeBounds.width - (frame.x - safeBounds.x) -
           frame.width,
      .y = frame.y,
      .width = frame.width,
      .height = frame.height,
  };
}

} // namespace

TouchControlsLayoutResult
buildTouchControlsLayout(const TouchControlRect &safeBounds,
                         const TouchControlsLayoutProfile &profile) noexcept {
  TouchControlsLayoutResult result;
  if (!validTouchControlsLayoutProfile(profile)) {
    result.status = TouchControlsLayoutStatus::invalidProfile;
    return result;
  }
  if (!finiteRect(safeBounds) || emptyRect(safeBounds)) {
    result.status = TouchControlsLayoutStatus::invalidSafeBounds;
    return result;
  }

  const float safeWidth = safeBounds.width;
  const float safeHeight = safeBounds.height;
  const bool compact = profile.density == TouchControlsDensity::compact ||
                       safeHeight <= 375.0F || safeWidth < 760.0F;
  const float margin = compact ? 12.0F : 20.0F;
  const float gap = compact ? 8.0F : 12.0F;
  const float standardButton = compact ? 44.0F : 52.0F;

  float stickDiameter = compact ? 96.0F : 124.0F;
  stickDiameter = std::min(stickDiameter, std::max(88.0F, safeHeight * 0.36F));
  const TouchControlRect stickFrame = clampToBounds(
      {
          .x = safeBounds.x + margin,
          .y = safeBounds.y + safeHeight - margin - stickDiameter,
          .width = stickDiameter,
          .height = stickDiameter,
      },
      safeBounds);

  const float missionWidth = compact ? 76.0F : 96.0F;
  const TouchControlRect missionFrame = clampToBounds(
      {
          .x = safeBounds.x + margin,
          .y = safeBounds.y + margin,
          .width = missionWidth,
          .height = standardButton,
      },
      safeBounds);

  const float throttleTop = missionFrame.y + missionFrame.height + gap;
  const float availableThrottleHeight = stickFrame.y - gap - throttleTop;
  const float throttleHeight =
      std::clamp(compact ? 118.0F : 160.0F, minimumTargetSize,
                 std::max(minimumTargetSize, availableThrottleHeight));
  const TouchControlRect throttleFrame = clampToBounds(
      {
          .x = safeBounds.x + margin,
          .y = throttleTop,
          .width = standardButton,
          .height = throttleHeight,
      },
      safeBounds);

  const TouchControlRect throttleIncreaseFrame = clampToBounds(
      {
          .x = throttleFrame.x + throttleFrame.width + gap,
          .y = throttleFrame.y,
          .width = standardButton,
          .height = standardButton,
      },
      safeBounds);
  const TouchControlRect throttleDecreaseFrame = clampToBounds(
      {
          .x = throttleFrame.x + throttleFrame.width + gap,
          .y = throttleFrame.y + throttleFrame.height - standardButton,
          .width = standardButton,
          .height = standardButton,
      },
      safeBounds);

  const float primarySize = compact ? 72.0F : 88.0F;
  const float secondarySize = compact ? 56.0F : 68.0F;
  const float utilitySize = compact ? 50.0F : 58.0F;
  const TouchControlRect primaryFrame = clampToBounds(
      {
          .x = safeBounds.x + safeWidth - margin - primarySize,
          .y = safeBounds.y + safeHeight - margin - primarySize,
          .width = primarySize,
          .height = primarySize,
      },
      safeBounds);
  const TouchControlRect secondaryFrame = clampToBounds(
      {
          .x = primaryFrame.x - gap - secondarySize,
          .y = primaryFrame.y + primaryFrame.height - secondarySize,
          .width = secondarySize,
          .height = secondarySize,
      },
      safeBounds);
  const TouchControlRect weaponFrame = clampToBounds(
      {
          .x = secondaryFrame.x - gap - utilitySize,
          .y = primaryFrame.y + primaryFrame.height - utilitySize,
          .width = utilitySize,
          .height = utilitySize,
      },
      safeBounds);
  const TouchControlRect rearFrame = clampToBounds(
      {
          .x = weaponFrame.x - gap - utilitySize,
          .y = primaryFrame.y + primaryFrame.height - utilitySize,
          .width = utilitySize,
          .height = utilitySize,
      },
      safeBounds);

  const float pauseSize = compact ? 48.0F : 52.0F;
  const float recenterWidth = compact ? 50.0F : 58.0F;
  const float cameraWidth = compact ? 58.0F : 68.0F;
  const TouchControlRect pauseFrame = clampToBounds(
      {
          .x = safeBounds.x + safeWidth - margin - pauseSize,
          .y = safeBounds.y + margin,
          .width = pauseSize,
          .height = pauseSize,
      },
      safeBounds);
  const TouchControlRect recenterFrame = clampToBounds(
      {
          .x = pauseFrame.x - gap - recenterWidth,
          .y = pauseFrame.y,
          .width = recenterWidth,
          .height = pauseSize,
      },
      safeBounds);
  const TouchControlRect cameraFrame = clampToBounds(
      {
          .x = recenterFrame.x - gap - cameraWidth,
          .y = pauseFrame.y,
          .width = cameraWidth,
          .height = pauseSize,
      },
      safeBounds);

  result.visualFrames[indexOf(TouchControlElement::flightStick)] = stickFrame;
  result.visualFrames[indexOf(TouchControlElement::throttle)] = throttleFrame;
  result.visualFrames[indexOf(TouchControlElement::throttleIncrease)] =
      throttleIncreaseFrame;
  result.visualFrames[indexOf(TouchControlElement::throttleDecrease)] =
      throttleDecreaseFrame;
  result.visualFrames[indexOf(TouchControlElement::primaryFire)] = primaryFrame;
  result.visualFrames[indexOf(TouchControlElement::secondaryFire)] =
      secondaryFrame;
  result.visualFrames[indexOf(TouchControlElement::weaponNext)] = weaponFrame;
  result.visualFrames[indexOf(TouchControlElement::rearView)] = rearFrame;
  result.visualFrames[indexOf(TouchControlElement::cameraCycle)] = cameraFrame;
  result.visualFrames[indexOf(TouchControlElement::cameraRecenter)] =
      recenterFrame;
  result.visualFrames[indexOf(TouchControlElement::missionStatus)] =
      missionFrame;
  result.visualFrames[indexOf(TouchControlElement::pause)] = pauseFrame;

  for (std::size_t element = indexOf(TouchControlElement::throttleIncrease);
       element <= indexOf(TouchControlElement::pause); ++element) {
    result.captureFrames[element] = enforceMinimumCaptureSize(
        intersect(safeBounds, expanded(result.visualFrames[element], 2.0F)),
        safeBounds);
  }
  result.captureFrames[indexOf(TouchControlElement::flightStick)] =
      enforceMinimumCaptureSize(
          intersect(safeBounds, expanded(stickFrame, 8.0F)), safeBounds);
  result.captureFrames[indexOf(TouchControlElement::throttle)] =
      enforceMinimumCaptureSize(
          intersect(safeBounds, expanded(throttleFrame, 8.0F)), safeBounds);

  const float topOfLook = std::max(cameraFrame.y + cameraFrame.height,
                                   missionFrame.y + missionFrame.height) +
                          gap;
  const float bottomOfLook = std::min(primaryFrame.y, stickFrame.y) - gap;
  const float leftOfLook =
      std::max(throttleIncreaseFrame.x + throttleIncreaseFrame.width + gap,
               safeBounds.x + safeWidth * (compact ? 0.39F : 0.34F));
  const float rightOfLook = safeBounds.x + safeWidth - margin;
  TouchControlRect lookFrame;
  if (rightOfLook - leftOfLook >= minimumTargetSize &&
      bottomOfLook - topOfLook >= minimumTargetSize) {
    lookFrame = {
        .x = leftOfLook,
        .y = topOfLook,
        .width = rightOfLook - leftOfLook,
        .height = bottomOfLook - topOfLook,
    };
  }
  result.visualFrames[indexOf(TouchControlElement::cameraLook)] = lookFrame;
  result.captureFrames[indexOf(TouchControlElement::cameraLook)] = lookFrame;

  const float knobDiameter = compact ? 38.0F : 48.0F;
  result.stickTravelRadius =
      std::max(1.0F, (stickFrame.width - knobDiameter) * 0.5F - 5.0F);
  result.lookTravelRadius =
      std::max(minimumTargetSize,
               std::min(compact ? 76.0F : 96.0F,
                        std::min(lookFrame.width, lookFrame.height) * 0.45F));
  result.compact = compact;

  if (profile.handedness == TouchControlsHandedness::leftHanded) {
    for (auto &frame : result.visualFrames) {
      frame = mirrorHorizontally(frame, safeBounds);
    }
    for (auto &frame : result.captureFrames) {
      frame = mirrorHorizontally(frame, safeBounds);
    }
  }

  result.status = TouchControlsLayoutStatus::ready;
  return result;
}

} // namespace airfix::input
