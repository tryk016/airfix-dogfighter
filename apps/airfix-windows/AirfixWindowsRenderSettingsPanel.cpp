#include "AirfixWindowsRenderSettingsPanel.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace airfix::windows {
namespace {

constexpr float renderScaleStep = 5.0F;
constexpr float minimumDpiScale = 0.75F;
constexpr float maximumDpiScale = 3.0F;

[[nodiscard]] constexpr std::uint8_t
itemValue(const AirfixWindowsRenderSettingsItem item) noexcept {
  return static_cast<std::uint8_t>(item);
}

[[nodiscard]] bool
validOutput(const AirfixWindowsUiPixelExtent &output) noexcept {
  return output.width != 0U && output.height != 0U &&
         std::isfinite(output.dpiScale) && output.dpiScale > 0.0F;
}

[[nodiscard]] AirfixWindowsUiPixelExtent
sanitizedOutput(const AirfixWindowsUiPixelExtent output) noexcept {
  return validOutput(output) ? output : AirfixWindowsUiPixelExtent{};
}

[[nodiscard]] constexpr AirfixWindowsRenderSettingsItem
selectedItem(const AirfixWindowsRenderSettingsScreen screen,
             const AirfixWindowsRenderSettingsItem pauseItem,
             const AirfixWindowsRenderSettingsItem settingsItem) noexcept {
  return screen == AirfixWindowsRenderSettingsScreen::pause ? pauseItem
                                                            : settingsItem;
}

[[nodiscard]] constexpr std::uint8_t
firstItemIndex(const AirfixWindowsRenderSettingsScreen screen) noexcept {
  return screen == AirfixWindowsRenderSettingsScreen::pause
             ? itemValue(AirfixWindowsRenderSettingsItem::displaySettings)
             : itemValue(AirfixWindowsRenderSettingsItem::renderScale);
}

[[nodiscard]] constexpr std::uint8_t
lastItemIndex(const AirfixWindowsRenderSettingsScreen screen) noexcept {
  return screen == AirfixWindowsRenderSettingsScreen::pause
             ? itemValue(AirfixWindowsRenderSettingsItem::resume)
             : itemValue(AirfixWindowsRenderSettingsItem::cancel);
}

[[nodiscard]] constexpr std::int32_t
magnitude(const input::Q15 value) noexcept {
  const auto wide = static_cast<std::int32_t>(value);
  return wide < 0 ? -wide : wide;
}

[[nodiscard]] constexpr bool
isValueItem(const AirfixWindowsRenderSettingsItem item) noexcept {
  return item >= AirfixWindowsRenderSettingsItem::renderScale &&
         item <= AirfixWindowsRenderSettingsItem::rendererStatistics;
}

[[nodiscard]] float nextScaleValue(const float current,
                                   const std::int32_t direction) noexcept {
  if (direction < 0) {
    return std::max(render::native_render_policy::minimumRenderScalePercent,
                    (std::ceil(current / renderScaleStep) - 1.0F) *
                        renderScaleStep);
  }
  return std::min(render::native_render_policy::maximumRenderScalePercent,
                  (std::floor(current / renderScaleStep) + 1.0F) *
                      renderScaleStep);
}

struct PanelLayout final {
  float scale{1.0F};
  AirfixWindowsUiPixelRect panel;
  AirfixWindowsUiPixelRect title;
  AirfixWindowsUiPixelRect status;
  float rowX{};
  float rowY{};
  float rowWidth{};
  float rowHeight{};
  float rowGap{};
  float controlWidth{};
};

[[nodiscard]] PanelLayout
makeLayout(const AirfixWindowsUiPixelExtent output,
           const AirfixWindowsRenderSettingsScreen screen) noexcept {
  const float width = static_cast<float>(output.width);
  const float height = static_cast<float>(output.height);
  const float dpi =
      std::clamp(output.dpiScale, minimumDpiScale, maximumDpiScale);
  const float basePanelWidth =
      screen == AirfixWindowsRenderSettingsScreen::pause ? 590.0F : 840.0F;
  const float itemCount =
      screen == AirfixWindowsRenderSettingsScreen::pause ? 2.0F : 6.0F;
  const float baseRowHeight =
      screen == AirfixWindowsRenderSettingsScreen::pause ? 78.0F : 67.0F;
  const float baseRowGap = 10.0F;
  const float baseTopSpace =
      screen == AirfixWindowsRenderSettingsScreen::pause ? 124.0F : 114.0F;
  const float baseBottomSpace =
      screen == AirfixWindowsRenderSettingsScreen::pause ? 70.0F : 100.0F;
  const float basePanelHeight = baseTopSpace + itemCount * baseRowHeight +
                                (itemCount - 1.0F) * baseRowGap +
                                baseBottomSpace;
  const float shortestSide = std::min(width, height);
  const float margin =
      std::min(20.0F * dpi, std::max(0.001F, shortestSide * 0.04F));
  const float availableWidth = std::max(0.001F, width - margin * 2.0F);
  const float availableHeight = std::max(0.001F, height - margin * 2.0F);
  const float scale =
      std::max(0.001F, std::min({dpi, availableWidth / basePanelWidth,
                                 availableHeight / basePanelHeight}));
  const float panelWidth = basePanelWidth * scale;
  const float panelHeight = basePanelHeight * scale;
  const float rowHeight = baseRowHeight * scale;
  const float rowGap = baseRowGap * scale;
  const float topSpace = baseTopSpace * scale;
  const float bottomSpace = baseBottomSpace * scale;
  const float panelX = (width - panelWidth) * 0.5F;
  const float panelY = (height - panelHeight) * 0.5F;
  const float innerMargin = std::min(42.0F * scale, panelWidth * 0.07F);
  const float rowWidth = panelWidth - innerMargin * 2.0F;
  const float rowsHeight = itemCount * rowHeight + (itemCount - 1.0F) * rowGap;
  const float idealRowY = panelY + topSpace;
  const float maximumRowY = panelY + panelHeight - bottomSpace - rowsHeight;
  const float rowY =
      std::max(panelY + 74.0F * scale, std::min(idealRowY, maximumRowY));
  const float statusTop = rowY + rowsHeight + 18.0F * scale;

  return {
      .scale = scale,
      .panel = {panelX, panelY, panelWidth, panelHeight},
      .title = {panelX + innerMargin, panelY + 28.0F * scale, rowWidth,
                52.0F * scale},
      .status = {panelX + innerMargin, statusTop, rowWidth,
                 std::max(30.0F * scale,
                          panelY + panelHeight - statusTop - 18.0F * scale)},
      .rowX = panelX + innerMargin,
      .rowY = rowY,
      .rowWidth = rowWidth,
      .rowHeight = rowHeight,
      .rowGap = rowGap,
      .controlWidth = std::min(290.0F * scale, rowWidth * 0.48F),
  };
}

[[nodiscard]] AirfixWindowsRenderSettingsViewItem
makeViewItem(const AirfixWindowsRenderSettingsItem item,
             const std::uint8_t index, const PanelLayout &layout,
             const AirfixWindowsRenderSettingsItem selected,
             const bool enabled) noexcept {
  AirfixWindowsRenderSettingsViewItem result{
      .item = item,
      .bounds =
          {
              layout.rowX,
              layout.rowY + static_cast<float>(index) *
                                (layout.rowHeight + layout.rowGap),
              layout.rowWidth,
              layout.rowHeight,
          },
      .previousBounds = {},
      .nextBounds = {},
      .selected = item == selected,
      .enabled = enabled,
  };
  if (isValueItem(item)) {
    const float buttonWidth =
        std::min(layout.rowHeight, layout.controlWidth * 0.28F);
    const float controlX =
        result.bounds.x + result.bounds.width - layout.controlWidth;
    result.previousBounds = {
        controlX,
        result.bounds.y,
        buttonWidth,
        result.bounds.height,
    };
    result.nextBounds = {
        result.bounds.x + result.bounds.width - buttonWidth,
        result.bounds.y,
        buttonWidth,
        result.bounds.height,
    };
  }
  return result;
}

} // namespace

std::optional<AirfixWindowsRenderSettingsPanel>
AirfixWindowsRenderSettingsPanel::create(
    const render::RenderPresentationSettings &applied,
    const bool persistenceAvailable, const AirfixWindowsUiPixelExtent output,
    const AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask,
    const bool resumeAvailable) noexcept {
  auto model = settings::RenderPresentationSettingsMenuModel::create(
      applied, {.persistenceAvailable = persistenceAvailable});
  if (!model.has_value()) {
    return std::nullopt;
  }
  return AirfixWindowsRenderSettingsPanel{
      std::move(*model),
      sanitizedOutput(output),
      static_cast<AirfixWindowsRenderSettingsSessionOverrideMask>(
          sessionOverrideMask & airfixWindowsRenderSettingsAllSessionOverrides),
      resumeAvailable,
  };
}

AirfixWindowsRenderSettingsPanel::AirfixWindowsRenderSettingsPanel(
    settings::RenderPresentationSettingsMenuModel model,
    const AirfixWindowsUiPixelExtent output,
    const AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask,
    const bool resumeAvailable) noexcept
    : model_(std::move(model)), output_(output),
      sessionOverrideMask_(sessionOverrideMask),
      status_(model_.persistenceAvailable()
                  ? AirfixWindowsRenderSettingsStatus::ready
                  : AirfixWindowsRenderSettingsStatus::persistenceUnavailable),
      resumeAvailable_(resumeAvailable) {}

AirfixWindowsRenderSettingsIntent
AirfixWindowsRenderSettingsPanel::consumeInputFrame(
    const input::InputFrame &frame) noexcept {
  using input::AnalogAxis;
  using input::DigitalAction;

  if (frame.pressed(DigitalAction::uiCancel)) {
    if (screen_ == AirfixWindowsRenderSettingsScreen::displaySettings) {
      closeDisplaySettings();
    }
    return {};
  }

  const auto vertical = frame.analog(AnalogAxis::uiNavigateY);
  if (magnitude(vertical) <= airfixWindowsUiNavigationRelease) {
    verticalNavigationLatched_ = false;
  }
  bool movedVertically = false;
  if (!verticalNavigationLatched_ &&
      vertical >= airfixWindowsUiNavigationActuation) {
    verticalNavigationLatched_ = true;
    movedVertically = true;
    moveSelection(-1);
  } else if (!verticalNavigationLatched_ &&
             vertical <= -airfixWindowsUiNavigationActuation) {
    verticalNavigationLatched_ = true;
    movedVertically = true;
    moveSelection(1);
  }

  const auto horizontal = frame.analog(AnalogAxis::uiNavigateX);
  if (magnitude(horizontal) <= airfixWindowsUiNavigationRelease) {
    horizontalNavigationLatched_ = false;
  }
  if (!movedVertically && !horizontalNavigationLatched_ &&
      horizontal >= airfixWindowsUiNavigationActuation) {
    horizontalNavigationLatched_ = true;
    adjustSelectedValue(1);
  } else if (!movedVertically && !horizontalNavigationLatched_ &&
             horizontal <= -airfixWindowsUiNavigationActuation) {
    horizontalNavigationLatched_ = true;
    adjustSelectedValue(-1);
  }

  if (frame.pressed(DigitalAction::uiTabPrevious)) {
    adjustSelectedValue(-1);
  }
  if (frame.pressed(DigitalAction::uiTabNext)) {
    adjustSelectedValue(1);
  }
  if (frame.pressed(DigitalAction::uiConfirm)) {
    return activateSelectedItem();
  }
  return {};
}

AirfixWindowsRenderSettingsIntent
AirfixWindowsRenderSettingsPanel::consumePointer(
    const AirfixWindowsPointerInput &pointer) noexcept {
  const auto view = snapshot();
  std::optional<AirfixWindowsRenderSettingsViewItem> hit;
  for (std::uint8_t index = 0U; index < view.itemCount; ++index) {
    if (view.items[index].bounds.contains(pointer.xPixels, pointer.yPixels)) {
      hit = view.items[index];
      break;
    }
  }

  if (hit.has_value()) {
    if (screen_ == AirfixWindowsRenderSettingsScreen::pause) {
      selectedPauseItem_ = hit->item;
    } else {
      selectedSettingsItem_ = hit->item;
    }
  }

  if (pointer.wheelY != 0) {
    const auto direction = pointer.wheelY > 0 ? -1 : 1;
    const auto wideWheel = static_cast<std::int64_t>(pointer.wheelY);
    const auto wheelMagnitude = wideWheel < 0 ? -wideWheel : wideWheel;
    const auto steps =
        static_cast<std::int32_t>(std::min<std::int64_t>(wheelMagnitude, 10));
    for (std::int32_t step = 0; step < steps; ++step) {
      moveSelection(direction);
    }
  }

  if (!pointer.primaryPressed || !hit.has_value() || !hit->enabled) {
    return {};
  }
  if (hit->previousBounds.contains(pointer.xPixels, pointer.yPixels)) {
    adjustSelectedValue(-1);
    return {};
  }
  if (hit->nextBounds.contains(pointer.xPixels, pointer.yPixels)) {
    adjustSelectedValue(1);
    return {};
  }
  return activateSelectedItem();
}

AirfixWindowsRenderSettingsViewSnapshot
AirfixWindowsRenderSettingsPanel::snapshot() const noexcept {
  AirfixWindowsRenderSettingsViewSnapshot result{
      .screen = screen_,
      .selectedItem =
          selectedItem(screen_, selectedPauseItem_, selectedSettingsItem_),
      .status = status_,
      .appliedSettings = model_.appliedSettings(),
      .draftSettings = model_.draftSettings(),
      .output = output_,
      .layoutScale = 1.0F,
      .panelBounds = {},
      .titleBounds = {},
      .statusBounds = {},
      .items = {},
      .itemCount = 0U,
      .sessionOverrideMask = sessionOverrideMask_,
      .dirty = model_.dirty(),
      .applying = model_.phase() ==
                  settings::RenderPresentationSettingsMenuPhase::applying,
      .persistenceAvailable = model_.persistenceAvailable(),
  };

  const auto layout = makeLayout(output_, screen_);
  result.layoutScale = layout.scale;
  result.panelBounds = layout.panel;
  result.titleBounds = layout.title;
  result.statusBounds = layout.status;
  const bool editable = !result.applying;
  if (screen_ == AirfixWindowsRenderSettingsScreen::pause) {
    result.itemCount = 2U;
    result.items[0] =
        makeViewItem(AirfixWindowsRenderSettingsItem::displaySettings, 0U,
                     layout, result.selectedItem, true);
    result.items[1] =
        makeViewItem(AirfixWindowsRenderSettingsItem::resume, 1U, layout,
                     result.selectedItem, resumeAvailable_);
    return result;
  }

  result.itemCount = 6U;
  result.items[0] = makeViewItem(AirfixWindowsRenderSettingsItem::renderScale,
                                 0U, layout, result.selectedItem, editable);
  result.items[1] = makeViewItem(AirfixWindowsRenderSettingsItem::presentation,
                                 1U, layout, result.selectedItem, editable);
  result.items[2] = makeViewItem(AirfixWindowsRenderSettingsItem::visualProfile,
                                 2U, layout, result.selectedItem, editable);
  result.items[3] =
      makeViewItem(AirfixWindowsRenderSettingsItem::rendererStatistics, 3U,
                   layout, result.selectedItem, editable);
  result.items[4] =
      makeViewItem(AirfixWindowsRenderSettingsItem::apply, 4U, layout,
                   result.selectedItem, model_.canApply());
  result.items[5] = makeViewItem(AirfixWindowsRenderSettingsItem::cancel, 5U,
                                 layout, result.selectedItem, true);
  return result;
}

void AirfixWindowsRenderSettingsPanel::setOutput(
    const AirfixWindowsUiPixelExtent output) noexcept {
  output_ = sanitizedOutput(output);
}

void AirfixWindowsRenderSettingsPanel::setPersistenceAvailable(
    const bool available) noexcept {
  model_.setPersistenceAvailable(available);
  if (!available && status_ != AirfixWindowsRenderSettingsStatus::applying) {
    status_ = AirfixWindowsRenderSettingsStatus::persistenceUnavailable;
  } else if (available &&
             status_ ==
                 AirfixWindowsRenderSettingsStatus::persistenceUnavailable) {
    status_ = AirfixWindowsRenderSettingsStatus::ready;
  }
}

void AirfixWindowsRenderSettingsPanel::setSessionOverrideMask(
    const AirfixWindowsRenderSettingsSessionOverrideMask mask) noexcept {
  sessionOverrideMask_ =
      static_cast<AirfixWindowsRenderSettingsSessionOverrideMask>(
          mask & airfixWindowsRenderSettingsAllSessionOverrides);
}

void AirfixWindowsRenderSettingsPanel::setResumeAvailable(
    const bool available) noexcept {
  resumeAvailable_ = available;
}

bool AirfixWindowsRenderSettingsPanel::finishApplySuccess(
    const settings::RenderPresentationSettingsMenuApplyTicket
        &ticket) noexcept {
  if (!activeTicket_.has_value() || *activeTicket_ != ticket ||
      !model_.finishApplySuccess(ticket)) {
    return false;
  }
  activeTicket_.reset();
  status_ = AirfixWindowsRenderSettingsStatus::applied;
  screen_ = AirfixWindowsRenderSettingsScreen::pause;
  selectedPauseItem_ = AirfixWindowsRenderSettingsItem::displaySettings;
  return true;
}

bool AirfixWindowsRenderSettingsPanel::finishApplyFailure(
    const settings::RenderPresentationSettingsMenuApplyTicket
        &ticket) noexcept {
  if (!activeTicket_.has_value() || *activeTicket_ != ticket ||
      !model_.finishApplyFailure(ticket)) {
    return false;
  }
  activeTicket_.reset();
  status_ = AirfixWindowsRenderSettingsStatus::applyFailed;
  return true;
}

void AirfixWindowsRenderSettingsPanel::moveSelection(
    const std::int32_t direction) noexcept {
  if (direction == 0) {
    return;
  }
  auto &selection = screen_ == AirfixWindowsRenderSettingsScreen::pause
                        ? selectedPauseItem_
                        : selectedSettingsItem_;
  const auto first = static_cast<std::int32_t>(firstItemIndex(screen_));
  const auto last = static_cast<std::int32_t>(lastItemIndex(screen_));
  const auto current = static_cast<std::int32_t>(itemValue(selection));
  selection = static_cast<AirfixWindowsRenderSettingsItem>(
      std::clamp(current + direction, first, last));
}

void AirfixWindowsRenderSettingsPanel::adjustSelectedValue(
    const std::int32_t direction) noexcept {
  if (direction == 0) {
    return;
  }
  if (screen_ == AirfixWindowsRenderSettingsScreen::pause) {
    moveSelection(direction);
    return;
  }
  if (model_.phase() ==
      settings::RenderPresentationSettingsMenuPhase::applying) {
    return;
  }

  const auto &draft = model_.draftSettings();
  settings::RenderPresentationSettingsMenuEditResult result;
  switch (selectedSettingsItem_) {
  case AirfixWindowsRenderSettingsItem::renderScale:
    result = model_.setRenderScalePercent(
        nextScaleValue(draft.renderScalePercent, direction));
    break;
  case AirfixWindowsRenderSettingsItem::presentation:
    result = model_.setScenePresentation(
        direction < 0 ? render::ScenePresentationMode::widescreenHorPlus
                      : render::ScenePresentationMode::originalFourByThree);
    break;
  case AirfixWindowsRenderSettingsItem::visualProfile:
    result = model_.setVisualProfile(direction < 0
                                         ? render::VisualProfile::classic
                                         : render::VisualProfile::enhanced);
    break;
  case AirfixWindowsRenderSettingsItem::rendererStatistics:
    result = model_.setDiagnosticsOverlayEnabled(direction > 0);
    break;
  case AirfixWindowsRenderSettingsItem::displaySettings:
  case AirfixWindowsRenderSettingsItem::resume:
  case AirfixWindowsRenderSettingsItem::apply:
  case AirfixWindowsRenderSettingsItem::cancel:
  case AirfixWindowsRenderSettingsItem::count:
    return;
  }
  status_ = result.accepted()
                ? AirfixWindowsRenderSettingsStatus::ready
                : AirfixWindowsRenderSettingsStatus::invalidSettings;
}

AirfixWindowsRenderSettingsIntent
AirfixWindowsRenderSettingsPanel::activateSelectedItem() noexcept {
  if (screen_ == AirfixWindowsRenderSettingsScreen::pause) {
    if (selectedPauseItem_ ==
        AirfixWindowsRenderSettingsItem::displaySettings) {
      screen_ = AirfixWindowsRenderSettingsScreen::displaySettings;
      selectedSettingsItem_ = AirfixWindowsRenderSettingsItem::renderScale;
      if (!model_.persistenceAvailable()) {
        status_ = AirfixWindowsRenderSettingsStatus::persistenceUnavailable;
      }
      return {};
    }
    if (selectedPauseItem_ == AirfixWindowsRenderSettingsItem::resume) {
      return {
          .applyTicket = std::nullopt,
          .resumeRequested = resumeAvailable_,
      };
    }
    return {};
  }

  switch (selectedSettingsItem_) {
  case AirfixWindowsRenderSettingsItem::renderScale:
  case AirfixWindowsRenderSettingsItem::presentation:
  case AirfixWindowsRenderSettingsItem::visualProfile:
    adjustSelectedValue(1);
    return {};
  case AirfixWindowsRenderSettingsItem::rendererStatistics:
    if (model_.phase() == settings::RenderPresentationSettingsMenuPhase::idle) {
      const auto result = model_.setDiagnosticsOverlayEnabled(
          !model_.draftSettings().diagnosticsOverlayEnabled);
      status_ = result.accepted()
                    ? AirfixWindowsRenderSettingsStatus::ready
                    : AirfixWindowsRenderSettingsStatus::invalidSettings;
    }
    return {};
  case AirfixWindowsRenderSettingsItem::apply: {
    auto ticket = model_.beginApply();
    if (!ticket.has_value()) {
      status_ =
          !model_.persistenceAvailable()
              ? AirfixWindowsRenderSettingsStatus::persistenceUnavailable
              : (model_.dirty() ? AirfixWindowsRenderSettingsStatus::applying
                                : AirfixWindowsRenderSettingsStatus::noChanges);
      return {};
    }
    activeTicket_ = ticket;
    status_ = AirfixWindowsRenderSettingsStatus::applying;
    return {.applyTicket = std::move(ticket)};
  }
  case AirfixWindowsRenderSettingsItem::cancel:
    closeDisplaySettings();
    return {};
  case AirfixWindowsRenderSettingsItem::displaySettings:
  case AirfixWindowsRenderSettingsItem::resume:
  case AirfixWindowsRenderSettingsItem::count:
    return {};
  }
  return {};
}

void AirfixWindowsRenderSettingsPanel::closeDisplaySettings() noexcept {
  if (model_.phase() == settings::RenderPresentationSettingsMenuPhase::idle) {
    (void)model_.cancelDraft();
  }
  screen_ = AirfixWindowsRenderSettingsScreen::pause;
  selectedPauseItem_ = AirfixWindowsRenderSettingsItem::displaySettings;
  if (status_ == AirfixWindowsRenderSettingsStatus::ready ||
      status_ == AirfixWindowsRenderSettingsStatus::noChanges ||
      status_ == AirfixWindowsRenderSettingsStatus::invalidSettings) {
    status_ = model_.persistenceAvailable()
                  ? AirfixWindowsRenderSettingsStatus::ready
                  : AirfixWindowsRenderSettingsStatus::persistenceUnavailable;
  }
}

} // namespace airfix::windows
