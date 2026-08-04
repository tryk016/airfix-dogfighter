#pragma once

#include "AirfixWindowsRenderSettingsPanel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace airfix::windows {

inline constexpr std::size_t airfixWindowsUiSemanticTextCapacity = 96U;
inline constexpr std::size_t airfixWindowsUiMaximumSemanticNodes = 27U;
inline constexpr std::uint8_t airfixWindowsUiSemanticNoParent = 0xFFU;

struct AirfixWindowsUiSemanticText final {
  std::array<wchar_t, airfixWindowsUiSemanticTextCapacity> codeUnits{};
  std::uint8_t length{};

  [[nodiscard]] constexpr const wchar_t *c_str() const noexcept {
    return codeUnits.data();
  }

  [[nodiscard]] constexpr std::wstring_view view() const noexcept {
    return {codeUnits.data(), length};
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return length == 0U; }

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsUiSemanticText &,
             const AirfixWindowsUiSemanticText &) noexcept = default;
};

enum class AirfixWindowsUiSemanticRole : std::uint8_t {
  window,
  heading,
  status,
  action,
  adjustableValue,
  decrementButton,
  incrementButton,
};

enum class AirfixWindowsUiSemanticAction : std::uint8_t {
  focus = 1U << 0U,
  invoke = 1U << 1U,
  decrement = 1U << 2U,
  increment = 1U << 3U,
};

using AirfixWindowsUiSemanticActionMask = std::uint8_t;

[[nodiscard]] constexpr AirfixWindowsUiSemanticActionMask
airfixWindowsUiSemanticActionMask(
    const AirfixWindowsUiSemanticAction action) noexcept {
  return static_cast<AirfixWindowsUiSemanticActionMask>(action);
}

[[nodiscard]] constexpr bool airfixWindowsUiSemanticHasAction(
    const AirfixWindowsUiSemanticActionMask mask,
    const AirfixWindowsUiSemanticAction action) noexcept {
  return (mask & airfixWindowsUiSemanticActionMask(action)) != 0U;
}

struct AirfixWindowsUiSemanticNode final {
  std::uint16_t runtimeId{};
  std::uint8_t parentIndex{airfixWindowsUiSemanticNoParent};
  AirfixWindowsUiSemanticRole role{AirfixWindowsUiSemanticRole::window};
  AirfixWindowsRenderSettingsItem item{AirfixWindowsRenderSettingsItem::count};
  AirfixWindowsUiPixelRect bounds;
  AirfixWindowsUiSemanticText name;
  AirfixWindowsUiSemanticText value;
  AirfixWindowsUiSemanticActionMask actions{};
  bool enabled{true};
  bool selected{};
  bool visible{true};
  bool offscreen{};
  bool focusable{};

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsUiSemanticNode &,
             const AirfixWindowsUiSemanticNode &) noexcept = default;
};

struct AirfixWindowsUiSemanticTree final {
  std::uint64_t accessibilityGeneration{1U};
  AirfixWindowsRenderSettingsScreen screen{
      AirfixWindowsRenderSettingsScreen::pause};
  std::array<AirfixWindowsUiSemanticNode, airfixWindowsUiMaximumSemanticNodes>
      nodes{};
  std::uint8_t nodeCount{};

  [[nodiscard]] constexpr bool complete() const noexcept {
    return nodeCount >= 3U &&
           static_cast<std::size_t>(nodeCount) <= nodes.size();
  }
};

enum class AirfixWindowsUiSemanticIssue : std::uint8_t {
  invalidSnapshot,
  capacityExceeded,
};

struct AirfixWindowsUiSemanticBuildResult final {
  std::optional<AirfixWindowsUiSemanticTree> tree;
  std::optional<AirfixWindowsUiSemanticIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return tree.has_value() && !issue.has_value() && tree->complete();
  }
};

// These functions are the single bounded source of user-facing strings for
// both the rasterized panel and the future UI Automation provider.
[[nodiscard]] AirfixWindowsUiSemanticText
airfixWindowsUiItemLabel(AirfixWindowsRenderSettingsItem item) noexcept;
[[nodiscard]] AirfixWindowsUiSemanticText airfixWindowsUiItemValue(
    AirfixWindowsRenderSettingsItem item,
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept;
[[nodiscard]] AirfixWindowsUiSemanticText airfixWindowsUiTitle(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept;
[[nodiscard]] AirfixWindowsUiSemanticText airfixWindowsUiStatus(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept;
[[nodiscard]] constexpr bool airfixWindowsUiStatusIsWarning(
    AirfixWindowsRenderSettingsStatus status) noexcept {
  switch (status) {
  case AirfixWindowsRenderSettingsStatus::applyFailed:
  case AirfixWindowsRenderSettingsStatus::persistenceUnavailable:
  case AirfixWindowsRenderSettingsStatus::invalidSettings:
  case AirfixWindowsRenderSettingsStatus::enhancedTexturesUnavailable:
  case AirfixWindowsRenderSettingsStatus::
      textureReloadFailedRestartRequired:
  case AirfixWindowsRenderSettingsStatus::controllerProfileSaveFailed:
  case AirfixWindowsRenderSettingsStatus::
      controllerProfilePersistenceUnavailable:
  case AirfixWindowsRenderSettingsStatus::invalidControllerProfile:
  case AirfixWindowsRenderSettingsStatus::controllerBindingConflict:
  case AirfixWindowsRenderSettingsStatus::controllerBindingProtectedConflict:
  case AirfixWindowsRenderSettingsStatus::controllerBindingActionUnavailable:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] constexpr bool airfixWindowsUiItemHasChevron(
    const AirfixWindowsRenderSettingsItem item) noexcept {
  switch (item) {
  case AirfixWindowsRenderSettingsItem::displaySettings:
  case AirfixWindowsRenderSettingsItem::controllerCalibration:
  case AirfixWindowsRenderSettingsItem::leftStickX:
  case AirfixWindowsRenderSettingsItem::leftStickY:
  case AirfixWindowsRenderSettingsItem::rightStickX:
  case AirfixWindowsRenderSettingsItem::rightStickY:
  case AirfixWindowsRenderSettingsItem::buttonBindings:
  case AirfixWindowsRenderSettingsItem::back:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] AirfixWindowsUiSemanticBuildResult
buildAirfixWindowsUiSemanticTree(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept;

} // namespace airfix::windows
