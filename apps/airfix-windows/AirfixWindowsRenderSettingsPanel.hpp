#pragma once

#include "airfix/input/InputFrame.hpp"
#include "airfix/settings/RenderPresentationSettingsMenuModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::windows {

inline constexpr std::int32_t airfixWindowsUiNavigationActuation = 16384;
inline constexpr std::int32_t airfixWindowsUiNavigationRelease = 8192;

enum class AirfixWindowsRenderSettingsScreen : std::uint8_t {
  pause,
  displaySettings,
};

enum class AirfixWindowsRenderSettingsItem : std::uint8_t {
  displaySettings,
  resume,
  renderScale,
  presentation,
  visualProfile,
  rendererStatistics,
  apply,
  cancel,
  count,
};

enum class AirfixWindowsRenderSettingsStatus : std::uint8_t {
  ready,
  noChanges,
  applying,
  applied,
  applyFailed,
  persistenceUnavailable,
  invalidSettings,
};

enum class AirfixWindowsRenderSettingsSessionOverride : std::uint8_t {
  renderScale = 1U << 0U,
  presentation = 1U << 1U,
  visualProfile = 1U << 2U,
  rendererStatistics = 1U << 3U,
};

using AirfixWindowsRenderSettingsSessionOverrideMask = std::uint8_t;

inline constexpr AirfixWindowsRenderSettingsSessionOverrideMask
    airfixWindowsRenderSettingsAllSessionOverrides = static_cast<
        AirfixWindowsRenderSettingsSessionOverrideMask>(
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::renderScale) |
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::presentation) |
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::visualProfile) |
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::rendererStatistics));

[[nodiscard]] constexpr AirfixWindowsRenderSettingsSessionOverrideMask
airfixWindowsRenderSettingsSessionOverrideMask(
    const render::RenderPresentationSettingsOverride &overrides) noexcept {
  AirfixWindowsRenderSettingsSessionOverrideMask mask{};
  if (overrides.renderScalePercent.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::renderScale);
  }
  if (overrides.scenePresentation.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::presentation);
  }
  if (overrides.visualProfile.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::visualProfile);
  }
  if (overrides.diagnosticsOverlayEnabled.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::rendererStatistics);
  }
  return mask;
}

struct AirfixWindowsUiPixelExtent final {
  std::uint32_t width{1920U};
  std::uint32_t height{1080U};
  float dpiScale{1.0F};

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsUiPixelExtent &,
             const AirfixWindowsUiPixelExtent &) noexcept = default;
};

struct AirfixWindowsUiPixelRect final {
  float x{};
  float y{};
  float width{};
  float height{};

  [[nodiscard]] constexpr bool contains(const float pointX,
                                        const float pointY) const noexcept {
    return pointX >= x && pointY >= y && pointX < x + width &&
           pointY < y + height;
  }

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsUiPixelRect &,
             const AirfixWindowsUiPixelRect &) noexcept = default;
};

struct AirfixWindowsRenderSettingsViewItem final {
  AirfixWindowsRenderSettingsItem item{
      AirfixWindowsRenderSettingsItem::displaySettings};
  AirfixWindowsUiPixelRect bounds;
  AirfixWindowsUiPixelRect previousBounds;
  AirfixWindowsUiPixelRect nextBounds;
  bool selected{};
  bool enabled{true};

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsRenderSettingsViewItem &,
             const AirfixWindowsRenderSettingsViewItem &) noexcept = default;
};

inline constexpr std::size_t airfixWindowsRenderSettingsMaximumViewItems = 6U;

// A storage-neutral, bounded view description. It intentionally contains no
// paths, persistence records, checksums, texture identities, or arbitrary
// caller strings.
struct AirfixWindowsRenderSettingsViewSnapshot final {
  AirfixWindowsRenderSettingsScreen screen{
      AirfixWindowsRenderSettingsScreen::pause};
  AirfixWindowsRenderSettingsItem selectedItem{
      AirfixWindowsRenderSettingsItem::displaySettings};
  AirfixWindowsRenderSettingsStatus status{
      AirfixWindowsRenderSettingsStatus::ready};
  render::RenderPresentationSettings appliedSettings;
  render::RenderPresentationSettings draftSettings;
  AirfixWindowsUiPixelExtent output;
  float layoutScale{1.0F};
  AirfixWindowsUiPixelRect panelBounds;
  AirfixWindowsUiPixelRect titleBounds;
  AirfixWindowsUiPixelRect statusBounds;
  std::array<AirfixWindowsRenderSettingsViewItem,
             airfixWindowsRenderSettingsMaximumViewItems>
      items{};
  std::uint8_t itemCount{};
  AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask{};
  bool dirty{};
  bool applying{};
  bool persistenceAvailable{true};

  [[nodiscard]] friend constexpr bool operator==(
      const AirfixWindowsRenderSettingsViewSnapshot &,
      const AirfixWindowsRenderSettingsViewSnapshot &) noexcept = default;
};

struct AirfixWindowsPointerInput final {
  float xPixels{};
  float yPixels{};
  std::int32_t wheelY{};
  bool primaryPressed{};
};

struct AirfixWindowsRenderSettingsIntent final {
  std::optional<settings::RenderPresentationSettingsMenuApplyTicket>
      applyTicket;
  bool resumeRequested{};

  [[nodiscard]] constexpr bool empty() const noexcept {
    return !applyTicket.has_value() && !resumeRequested;
  }
};

// Owner-thread Windows presentation-panel state. The class performs no
// renderer, filesystem, registry, or persistence work. Apply returns the
// shared model's immutable ticket to the product coordinator.
class AirfixWindowsRenderSettingsPanel final {
public:
  [[nodiscard]] static std::optional<AirfixWindowsRenderSettingsPanel> create(
      const render::RenderPresentationSettings &applied,
      bool persistenceAvailable = true, AirfixWindowsUiPixelExtent output = {},
      AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask = 0U,
      bool resumeAvailable = true) noexcept;

  AirfixWindowsRenderSettingsPanel(const AirfixWindowsRenderSettingsPanel &) =
      default;
  AirfixWindowsRenderSettingsPanel(
      AirfixWindowsRenderSettingsPanel &&) noexcept = default;
  AirfixWindowsRenderSettingsPanel &
  operator=(const AirfixWindowsRenderSettingsPanel &) = default;
  AirfixWindowsRenderSettingsPanel &
  operator=(AirfixWindowsRenderSettingsPanel &&) noexcept = default;
  ~AirfixWindowsRenderSettingsPanel() = default;

  [[nodiscard]] AirfixWindowsRenderSettingsIntent
  consumeInputFrame(const input::InputFrame &frame) noexcept;

  [[nodiscard]] AirfixWindowsRenderSettingsIntent
  consumePointer(const AirfixWindowsPointerInput &pointer) noexcept;

  [[nodiscard]] AirfixWindowsRenderSettingsViewSnapshot
  snapshot() const noexcept;

  void setOutput(AirfixWindowsUiPixelExtent output) noexcept;
  void setPersistenceAvailable(bool available) noexcept;
  void setSessionOverrideMask(
      AirfixWindowsRenderSettingsSessionOverrideMask mask) noexcept;
  void setResumeAvailable(bool available) noexcept;

  [[nodiscard]] bool
  finishApplySuccess(const settings::RenderPresentationSettingsMenuApplyTicket
                         &ticket) noexcept;
  [[nodiscard]] bool
  finishApplyFailure(const settings::RenderPresentationSettingsMenuApplyTicket
                         &ticket) noexcept;

  [[nodiscard]] AirfixWindowsRenderSettingsScreen screen() const noexcept {
    return screen_;
  }

private:
  AirfixWindowsRenderSettingsPanel(
      settings::RenderPresentationSettingsMenuModel model,
      AirfixWindowsUiPixelExtent output,
      AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask,
      bool resumeAvailable) noexcept;

  void moveSelection(std::int32_t direction) noexcept;
  void adjustSelectedValue(std::int32_t direction) noexcept;
  [[nodiscard]] AirfixWindowsRenderSettingsIntent
  activateSelectedItem() noexcept;
  void closeDisplaySettings() noexcept;

  settings::RenderPresentationSettingsMenuModel model_;
  AirfixWindowsUiPixelExtent output_;
  AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask_{};
  std::optional<settings::RenderPresentationSettingsMenuApplyTicket>
      activeTicket_;
  AirfixWindowsRenderSettingsScreen screen_{
      AirfixWindowsRenderSettingsScreen::pause};
  AirfixWindowsRenderSettingsItem selectedPauseItem_{
      AirfixWindowsRenderSettingsItem::displaySettings};
  AirfixWindowsRenderSettingsItem selectedSettingsItem_{
      AirfixWindowsRenderSettingsItem::renderScale};
  AirfixWindowsRenderSettingsStatus status_{
      AirfixWindowsRenderSettingsStatus::ready};
  bool verticalNavigationLatched_{};
  bool horizontalNavigationLatched_{};
  bool resumeAvailable_{true};
};

} // namespace airfix::windows
