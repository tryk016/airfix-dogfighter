#pragma once

#include "airfix/input/TouchControlsLayout.hpp"

#include <cstdint>
#include <optional>

namespace airfix::input {

inline constexpr std::int32_t touchThrottleMaximumQ15 = 32767;
inline constexpr std::int32_t touchThrottleMidpointQ15 = 16384;
inline constexpr std::int32_t touchThrottleDetentTriggerRadiusQ15 = 512;
inline constexpr std::int32_t touchThrottleDetentReleaseRadiusQ15 = 1536;

enum class TouchControlsHapticEvent : std::uint8_t {
  controlSelection = 0,
  throttleIdleDetent,
  throttleMidpointDetent,
  throttleFullDetent,
};

// Fire feedback is deliberately absent here. Weapon haptics must originate
// from an accepted gameplay feedback event, not from a platform input attempt.
[[nodiscard]] std::optional<TouchControlsHapticEvent>
touchControlPressHapticEvent(TouchControlElement element) noexcept;

// Tracks only the tactile presentation state of one active throttle gesture.
// It cannot alter, quantize, or delay the Q15 value published to gameplay.
// Trigger and release radii provide hysteresis around the 0/50/100% detents.
class TouchThrottleDetentTracker final {
public:
  [[nodiscard]] std::optional<TouchControlsHapticEvent>
  observe(std::int32_t throttleQ15) noexcept;

  void reset() noexcept;

private:
  std::optional<std::int32_t> previous_;
  std::optional<TouchControlsHapticEvent> latched_;
};

} // namespace airfix::input
