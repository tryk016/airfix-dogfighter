#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::input {

enum class TouchControlsHandedness : std::uint8_t {
  rightHanded = 0,
  leftHanded,
};

enum class TouchControlsDensity : std::uint8_t {
  automatic = 0,
  compact,
};

inline constexpr std::uint32_t touchControlsLayoutProfileSchemaVersion = 1U;

struct TouchControlsLayoutProfile final {
  std::uint32_t schemaVersion{touchControlsLayoutProfileSchemaVersion};
  TouchControlsHandedness handedness{TouchControlsHandedness::rightHanded};
  TouchControlsDensity density{TouchControlsDensity::automatic};

  [[nodiscard]] friend constexpr bool
  operator==(const TouchControlsLayoutProfile &,
             const TouchControlsLayoutProfile &) noexcept = default;
};

[[nodiscard]] constexpr bool validTouchControlsHandedness(
    const TouchControlsHandedness handedness) noexcept {
  return handedness == TouchControlsHandedness::rightHanded ||
         handedness == TouchControlsHandedness::leftHanded;
}

[[nodiscard]] constexpr bool
validTouchControlsDensity(const TouchControlsDensity density) noexcept {
  return density == TouchControlsDensity::automatic ||
         density == TouchControlsDensity::compact;
}

[[nodiscard]] constexpr bool validTouchControlsLayoutProfile(
    const TouchControlsLayoutProfile &profile) noexcept {
  return profile.schemaVersion == touchControlsLayoutProfileSchemaVersion &&
         validTouchControlsHandedness(profile.handedness) &&
         validTouchControlsDensity(profile.density);
}

enum class TouchControlElement : std::uint8_t {
  flightStick = 0,
  throttle,
  throttleIncrease,
  throttleDecrease,
  primaryFire,
  secondaryFire,
  weaponNext,
  rearView,
  cameraCycle,
  cameraRecenter,
  missionStatus,
  pause,
  cameraLook,
  count,
};

inline constexpr std::size_t touchControlElementCount =
    static_cast<std::size_t>(TouchControlElement::count);

struct TouchControlRect final {
  float x{};
  float y{};
  float width{};
  float height{};

  [[nodiscard]] friend constexpr bool
  operator==(const TouchControlRect &,
             const TouchControlRect &) noexcept = default;
};

enum class TouchControlsLayoutStatus : std::uint8_t {
  ready = 0,
  invalidProfile,
  invalidSafeBounds,
};

struct TouchControlsLayoutResult final {
  TouchControlsLayoutStatus status{
      TouchControlsLayoutStatus::invalidSafeBounds};
  bool compact{};
  std::array<TouchControlRect, touchControlElementCount> visualFrames{};
  std::array<TouchControlRect, touchControlElementCount> captureFrames{};
  float stickTravelRadius{};
  float lookTravelRadius{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == TouchControlsLayoutStatus::ready;
  }

  [[nodiscard]] constexpr const TouchControlRect &
  visualFrame(const TouchControlElement element) const noexcept {
    return visualFrames[static_cast<std::size_t>(element)];
  }

  [[nodiscard]] constexpr const TouchControlRect &
  captureFrame(const TouchControlElement element) const noexcept {
    return captureFrames[static_cast<std::size_t>(element)];
  }
};

// Computes UIKit-compatible, top-left-origin geometry using an already inset
// safe-area rectangle. The default profile reproduces the original native
// layout. Left-handed mode mirrors every visual and capture rectangle within
// the same safe bounds; it never remaps semantic actions. Callers must cancel
// active touches before publishing a different profile or safe-area layout.
[[nodiscard]] TouchControlsLayoutResult buildTouchControlsLayout(
    const TouchControlRect &safeBounds,
    const TouchControlsLayoutProfile &profile = {}) noexcept;

} // namespace airfix::input
