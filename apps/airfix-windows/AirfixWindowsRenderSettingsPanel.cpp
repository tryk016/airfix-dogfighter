#include "AirfixWindowsRenderSettingsPanel.hpp"

#include "airfix/input/ControllerInputBatchBridge.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>

namespace airfix::windows {
namespace {

constexpr float renderScaleStep = 5.0F;
constexpr std::uint16_t controllerDeadzoneStep = 512U;
constexpr std::uint16_t controllerSensitivityStep = 50U;
constexpr float minimumDpiScale = 0.75F;
constexpr float maximumDpiScale = 3.0F;

using Item = AirfixWindowsRenderSettingsItem;
using Screen = AirfixWindowsRenderSettingsScreen;

constexpr std::array<Item, 2U> pauseItems{
    Item::displaySettings,
    Item::resume,
};
constexpr std::array<Item, 3U> pauseItemsWithControllerProfile{
    Item::displaySettings,
    Item::controllerCalibration,
    Item::resume,
};
constexpr std::array<Item, 6U> displaySettingsItems{
    Item::renderScale,        Item::presentation, Item::visualProfile,
    Item::rendererStatistics, Item::apply,        Item::cancel,
};
constexpr std::array<Item, 7U> controllerProfileItems{
    Item::leftStickX,  Item::leftStickY,          Item::rightStickX,
    Item::rightStickY, Item::resetAllCalibration, Item::saveControllerProfile,
    Item::cancel,
};
constexpr std::array<Item, 7U> controllerAxisItems{
    Item::innerDeadzone, Item::outerSaturation, Item::sensitivity,
    Item::responseCurve, Item::inversion,       Item::resetAxis,
    Item::back,
};

[[nodiscard]] bool
validOutput(const AirfixWindowsUiPixelExtent &output) noexcept {
  return output.width != 0U && output.height != 0U &&
         std::isfinite(output.dpiScale) && output.dpiScale > 0.0F;
}

[[nodiscard]] AirfixWindowsUiPixelExtent
sanitizedOutput(const AirfixWindowsUiPixelExtent output) noexcept {
  return validOutput(output) ? output : AirfixWindowsUiPixelExtent{};
}

[[nodiscard]] constexpr std::span<const Item>
itemsForScreen(const Screen screen,
               const bool controllerProfileAvailable) noexcept {
  switch (screen) {
  case Screen::pause:
    return controllerProfileAvailable
               ? std::span<const Item>{pauseItemsWithControllerProfile}
               : std::span<const Item>{pauseItems};
  case Screen::displaySettings:
    return displaySettingsItems;
  case Screen::controllerCalibration:
    return controllerProfileItems;
  case Screen::controllerAxisCalibration:
    return controllerAxisItems;
  }
  return {};
}

[[nodiscard]] constexpr std::int32_t
magnitude(const input::Q15 value) noexcept {
  const auto wide = static_cast<std::int32_t>(value);
  return wide < 0 ? -wide : wide;
}

[[nodiscard]] constexpr bool
isValueItem(const AirfixWindowsRenderSettingsItem item) noexcept {
  switch (item) {
  case Item::renderScale:
  case Item::presentation:
  case Item::visualProfile:
  case Item::rendererStatistics:
  case Item::innerDeadzone:
  case Item::outerSaturation:
  case Item::sensitivity:
  case Item::responseCurve:
  case Item::inversion:
    return true;
  case Item::displaySettings:
  case Item::controllerCalibration:
  case Item::resume:
  case Item::apply:
  case Item::cancel:
  case Item::leftStickX:
  case Item::leftStickY:
  case Item::rightStickX:
  case Item::rightStickY:
  case Item::resetAxis:
  case Item::resetAllCalibration:
  case Item::saveControllerProfile:
  case Item::back:
  case Item::count:
    return false;
  }
  return false;
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
           const AirfixWindowsRenderSettingsScreen screen,
           const std::size_t visibleItemCount) noexcept {
  const float width = static_cast<float>(output.width);
  const float height = static_cast<float>(output.height);
  const float dpi =
      std::clamp(output.dpiScale, minimumDpiScale, maximumDpiScale);
  const bool pause = screen == Screen::pause;
  const bool controller = screen == Screen::controllerCalibration ||
                          screen == Screen::controllerAxisCalibration;
  const float basePanelWidth = pause ? 590.0F : (controller ? 920.0F : 840.0F);
  const float itemCount = static_cast<float>(visibleItemCount);
  const float baseRowHeight = pause ? 78.0F : (controller ? 61.0F : 67.0F);
  const float baseRowGap = 10.0F;
  const float baseTopSpace = pause ? 124.0F : 114.0F;
  const float baseBottomSpace =
      pause ? 70.0F
            : (screen == Screen::controllerAxisCalibration ? 150.0F : 100.0F);
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
    const bool resumeAvailable,
    std::optional<AirfixWindowsControllerProfilePanelState>
        controllerProfile) noexcept {
  auto model = settings::RenderPresentationSettingsMenuModel::create(
      applied, {.persistenceAvailable = persistenceAvailable});
  if (!model.has_value()) {
    return std::nullopt;
  }
  std::optional<settings::ControllerInputProfileMenuModel>
      controllerProfileModel;
  if (controllerProfile.has_value()) {
    controllerProfileModel = settings::ControllerInputProfileMenuModel::create(
        controllerProfile->active, controllerProfile->persisted,
        controllerProfile->capabilities);
    if (!controllerProfileModel.has_value()) {
      return std::nullopt;
    }
  }
  return AirfixWindowsRenderSettingsPanel{
      std::move(*model),
      std::move(controllerProfileModel),
      sanitizedOutput(output),
      static_cast<AirfixWindowsRenderSettingsSessionOverrideMask>(
          sessionOverrideMask & airfixWindowsRenderSettingsAllSessionOverrides),
      resumeAvailable,
  };
}

AirfixWindowsRenderSettingsPanel::AirfixWindowsRenderSettingsPanel(
    settings::RenderPresentationSettingsMenuModel model,
    std::optional<settings::ControllerInputProfileMenuModel>
        controllerProfileModel,
    const AirfixWindowsUiPixelExtent output,
    const AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask,
    const bool resumeAvailable) noexcept
    : model_(std::move(model)),
      controllerProfileModel_(std::move(controllerProfileModel)),
      output_(output), sessionOverrideMask_(sessionOverrideMask),
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
    if (screen_ == Screen::displaySettings) {
      closeDisplaySettings();
    } else if (screen_ == Screen::controllerAxisCalibration) {
      screen_ = Screen::controllerCalibration;
      selectedControllerProfileItem_ = Item::leftStickX;
    } else if (screen_ == Screen::controllerCalibration) {
      closeControllerCalibration();
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
    selectionForCurrentScreen() = hit->item;
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
      .selectedItem = selectionForCurrentScreen(),
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
      .controllerDraftAxes = {},
      .selectedControllerAxis = selectedControllerAxis_,
      .controllerPreviewRaw = input::q15Zero,
      .controllerPreviewEffective = input::q15Zero,
      .dirty = model_.dirty(),
      .applying = model_.phase() ==
                  settings::RenderPresentationSettingsMenuPhase::applying,
      .persistenceAvailable = model_.persistenceAvailable(),
      .controllerProfileAvailable = controllerProfileModel_.has_value(),
      .controllerProfileDirty = false,
      .controllerProfileSaving = false,
      .controllerProfilePersistenceAvailable = false,
      .controllerProfileRepairRequired = false,
      .controllerProfileRestartRequired = false,
      .controllerConnected = controllerAxisInput_.connected,
  };

  if (controllerProfileModel_.has_value()) {
    result.controllerDraftAxes = controllerProfileModel_->draftRecord().axes;
    result.controllerProfileDirty = controllerProfileModel_->dirty();
    result.controllerProfileSaving =
        controllerProfileModel_->phase() ==
        settings::ControllerInputProfileMenuPhase::saving;
    result.controllerProfilePersistenceAvailable =
        controllerProfileModel_->persistenceAvailable();
    result.controllerProfileRepairRequired =
        controllerProfileModel_->repairRequired();
    result.controllerProfileRestartRequired =
        controllerProfileModel_->restartRequired();
    const auto axisIndex = static_cast<std::size_t>(selectedControllerAxis_);
    if (axisIndex < controllerAxisInput_.rawAxes.size()) {
      result.controllerPreviewRaw = controllerAxisInput_.rawAxes[axisIndex];
      const auto transformed = input::transformControllerAxisForTransport(
          result.controllerPreviewRaw, selectedControllerAxis_,
          controllerProfileModel_->resolvedDraftProfile());
      if (transformed.has_value()) {
        result.controllerPreviewEffective = *transformed;
      }
    }
  }

  const auto visibleItems =
      itemsForScreen(screen_, controllerProfileModel_.has_value());
  const auto layout = makeLayout(output_, screen_, visibleItems.size());
  result.layoutScale = layout.scale;
  result.panelBounds = layout.panel;
  result.titleBounds = layout.title;
  result.statusBounds = layout.status;
  result.itemCount = static_cast<std::uint8_t>(visibleItems.size());
  for (std::uint8_t index = 0U; index < result.itemCount; ++index) {
    const auto item = visibleItems[index];
    bool enabled = true;
    switch (item) {
    case Item::resume:
      enabled = resumeAvailable_;
      break;
    case Item::controllerCalibration:
      enabled = controllerProfileModel_.has_value();
      break;
    case Item::renderScale:
    case Item::presentation:
    case Item::visualProfile:
    case Item::rendererStatistics:
      enabled = !result.applying;
      break;
    case Item::apply:
      enabled = model_.canApply();
      break;
    case Item::leftStickX:
    case Item::leftStickY:
    case Item::rightStickX:
    case Item::rightStickY:
    case Item::innerDeadzone:
    case Item::outerSaturation:
    case Item::sensitivity:
    case Item::responseCurve:
    case Item::inversion:
    case Item::resetAxis:
    case Item::resetAllCalibration:
      enabled = controllerProfileModel_.has_value() &&
                !result.controllerProfileSaving;
      break;
    case Item::saveControllerProfile:
      enabled = controllerProfileModel_.has_value() &&
                controllerProfileModel_->canSave();
      break;
    case Item::cancel:
      enabled = screen_ == Screen::displaySettings
                    ? model_.canCancel()
                    : (!controllerProfileModel_.has_value() ||
                       controllerProfileModel_->canCancel());
      break;
    case Item::back:
      enabled = controllerProfileModel_.has_value() &&
                controllerProfileModel_->canCancel();
      break;
    case Item::displaySettings:
    case Item::count:
      break;
    }
    result.items[index] =
        makeViewItem(item, index, layout, result.selectedItem, enabled);
  }
  return result;
}

[[nodiscard]] constexpr std::uint16_t
nextUnsignedValue(const std::uint16_t current, const std::uint16_t minimum,
                  const std::uint16_t maximum, const std::uint16_t step,
                  const std::int32_t direction) noexcept {
  if (direction < 0) {
    const auto previous = current > step
                              ? static_cast<std::uint16_t>(current - step)
                              : std::uint16_t{0};
    return std::max(minimum, previous);
  }
  const auto widened = static_cast<std::uint32_t>(current) + step;
  return static_cast<std::uint16_t>(std::min<std::uint32_t>(maximum, widened));
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

void AirfixWindowsRenderSettingsPanel::setControllerProfilePersistenceAvailable(
    const bool available) noexcept {
  if (!controllerProfileModel_.has_value()) {
    return;
  }
  controllerProfileModel_->setPersistenceAvailable(available);
  if (!available &&
      status_ != AirfixWindowsRenderSettingsStatus::controllerProfileSaving) {
    status_ = AirfixWindowsRenderSettingsStatus::
        controllerProfilePersistenceUnavailable;
  } else if (available && status_ ==
                              AirfixWindowsRenderSettingsStatus::
                                  controllerProfilePersistenceUnavailable) {
    status_ = controllerProfileModel_->restartRequired()
                  ? AirfixWindowsRenderSettingsStatus::
                        controllerProfileSavedRestartRequired
                  : AirfixWindowsRenderSettingsStatus::controllerProfileReady;
  }
}

void AirfixWindowsRenderSettingsPanel::setControllerAxisInput(
    const AirfixWindowsControllerAxisInputSnapshot &input) noexcept {
  controllerAxisInput_ = input;
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

bool AirfixWindowsRenderSettingsPanel::finishControllerProfileSaveSuccess(
    const settings::ControllerInputProfileMenuSaveTicket &ticket) noexcept {
  if (!controllerProfileModel_.has_value() ||
      !activeControllerProfileTicket_.has_value() ||
      *activeControllerProfileTicket_ != ticket ||
      !controllerProfileModel_->finishSaveSuccess(ticket)) {
    return false;
  }
  activeControllerProfileTicket_.reset();
  status_ = controllerProfileModel_->restartRequired()
                ? AirfixWindowsRenderSettingsStatus::
                      controllerProfileSavedRestartRequired
                : AirfixWindowsRenderSettingsStatus::controllerProfileSaved;
  screen_ = Screen::pause;
  selectedPauseItem_ = Item::controllerCalibration;
  return true;
}

bool AirfixWindowsRenderSettingsPanel::finishControllerProfileSaveFailure(
    const settings::ControllerInputProfileMenuSaveTicket &ticket) noexcept {
  if (!controllerProfileModel_.has_value() ||
      !activeControllerProfileTicket_.has_value() ||
      *activeControllerProfileTicket_ != ticket ||
      !controllerProfileModel_->finishSaveFailure(ticket)) {
    return false;
  }
  activeControllerProfileTicket_.reset();
  status_ = AirfixWindowsRenderSettingsStatus::controllerProfileSaveFailed;
  return true;
}

void AirfixWindowsRenderSettingsPanel::moveSelection(
    const std::int32_t direction) noexcept {
  if (direction == 0) {
    return;
  }
  const auto visibleItems =
      itemsForScreen(screen_, controllerProfileModel_.has_value());
  if (visibleItems.empty()) {
    return;
  }
  auto &selection = selectionForCurrentScreen();
  const auto current =
      std::find(visibleItems.begin(), visibleItems.end(), selection);
  const auto currentIndex = current == visibleItems.end()
                                ? 0
                                : static_cast<std::int32_t>(std::distance(
                                      visibleItems.begin(), current));
  const auto nextIndex =
      std::clamp(currentIndex + (direction < 0 ? -1 : 1), 0,
                 static_cast<std::int32_t>(visibleItems.size() - 1U));
  selection = visibleItems[static_cast<std::size_t>(nextIndex)];
}

AirfixWindowsRenderSettingsItem &
AirfixWindowsRenderSettingsPanel::selectionForCurrentScreen() noexcept {
  switch (screen_) {
  case Screen::pause:
    return selectedPauseItem_;
  case Screen::displaySettings:
    return selectedSettingsItem_;
  case Screen::controllerCalibration:
    return selectedControllerProfileItem_;
  case Screen::controllerAxisCalibration:
    return selectedControllerAxisItem_;
  }
  return selectedPauseItem_;
}

const AirfixWindowsRenderSettingsItem &
AirfixWindowsRenderSettingsPanel::selectionForCurrentScreen() const noexcept {
  switch (screen_) {
  case Screen::pause:
    return selectedPauseItem_;
  case Screen::displaySettings:
    return selectedSettingsItem_;
  case Screen::controllerCalibration:
    return selectedControllerProfileItem_;
  case Screen::controllerAxisCalibration:
    return selectedControllerAxisItem_;
  }
  return selectedPauseItem_;
}

void AirfixWindowsRenderSettingsPanel::adjustSelectedValue(
    const std::int32_t direction) noexcept {
  if (direction == 0) {
    return;
  }
  if (screen_ == Screen::pause || screen_ == Screen::controllerCalibration) {
    moveSelection(direction);
    return;
  }
  if (screen_ == Screen::displaySettings) {
    if (model_.phase() ==
        settings::RenderPresentationSettingsMenuPhase::applying) {
      return;
    }

    const auto &draft = model_.draftSettings();
    settings::RenderPresentationSettingsMenuEditResult result;
    switch (selectedSettingsItem_) {
    case Item::renderScale:
      result = model_.setRenderScalePercent(
          nextScaleValue(draft.renderScalePercent, direction));
      break;
    case Item::presentation:
      result = model_.setScenePresentation(
          direction < 0 ? render::ScenePresentationMode::widescreenHorPlus
                        : render::ScenePresentationMode::originalFourByThree);
      break;
    case Item::visualProfile:
      result = model_.setVisualProfile(direction < 0
                                           ? render::VisualProfile::classic
                                           : render::VisualProfile::enhanced);
      break;
    case Item::rendererStatistics:
      result = model_.setDiagnosticsOverlayEnabled(direction > 0);
      break;
    default:
      return;
    }
    status_ = result.accepted()
                  ? AirfixWindowsRenderSettingsStatus::ready
                  : AirfixWindowsRenderSettingsStatus::invalidSettings;
    return;
  }

  if (screen_ != Screen::controllerAxisCalibration ||
      !controllerProfileModel_.has_value() ||
      controllerProfileModel_->phase() ==
          settings::ControllerInputProfileMenuPhase::saving) {
    return;
  }
  const auto *const calibration =
      controllerProfileModel_->draftAxisCalibration(selectedControllerAxis_);
  if (calibration == nullptr) {
    status_ = AirfixWindowsRenderSettingsStatus::invalidControllerProfile;
    return;
  }

  settings::ControllerInputProfileMenuEditResult result;
  switch (selectedControllerAxisItem_) {
  case Item::innerDeadzone: {
    const auto maximum =
        static_cast<std::uint16_t>(calibration->outerSaturationQ15 - 1U);
    result = controllerProfileModel_->setInnerDeadzoneQ15(
        selectedControllerAxis_,
        nextUnsignedValue(calibration->innerDeadzoneQ15, 0U, maximum,
                          controllerDeadzoneStep, direction));
    break;
  }
  case Item::outerSaturation: {
    const auto minimum =
        static_cast<std::uint16_t>(calibration->innerDeadzoneQ15 + 1U);
    result = controllerProfileModel_->setOuterSaturationQ15(
        selectedControllerAxis_,
        nextUnsignedValue(calibration->outerSaturationQ15, minimum,
                          static_cast<std::uint16_t>(input::q15One),
                          controllerDeadzoneStep, direction));
    break;
  }
  case Item::sensitivity:
    result = controllerProfileModel_->setSensitivityPermille(
        selectedControllerAxis_,
        nextUnsignedValue(calibration->sensitivityPermille,
                          input::controllerAxisMinimumSensitivityPermille,
                          input::controllerAxisMaximumSensitivityPermille,
                          controllerSensitivityStep, direction));
    break;
  case Item::responseCurve: {
    const auto current = static_cast<std::int32_t>(calibration->responseCurve);
    const auto maximum =
        static_cast<std::int32_t>(input::ControllerResponseCurve::count) - 1;
    result = controllerProfileModel_->setResponseCurve(
        selectedControllerAxis_,
        static_cast<input::ControllerResponseCurve>(
            std::clamp(current + (direction < 0 ? -1 : 1), 0, maximum)));
    break;
  }
  case Item::inversion:
    result = controllerProfileModel_->setInverted(selectedControllerAxis_,
                                                  direction > 0);
    break;
  default:
    return;
  }
  status_ =
      result.accepted()
          ? (controllerProfileModel_->persistenceAvailable()
                 ? AirfixWindowsRenderSettingsStatus::controllerProfileReady
                 : AirfixWindowsRenderSettingsStatus::
                       controllerProfilePersistenceUnavailable)
          : AirfixWindowsRenderSettingsStatus::invalidControllerProfile;
}

AirfixWindowsRenderSettingsIntent
AirfixWindowsRenderSettingsPanel::activateSelectedItem() noexcept {
  if (screen_ == Screen::pause) {
    if (selectedPauseItem_ == Item::displaySettings) {
      screen_ = Screen::displaySettings;
      selectedSettingsItem_ = Item::renderScale;
      if (!model_.persistenceAvailable()) {
        status_ = AirfixWindowsRenderSettingsStatus::persistenceUnavailable;
      }
      return {};
    }
    if (selectedPauseItem_ == Item::controllerCalibration &&
        controllerProfileModel_.has_value()) {
      screen_ = Screen::controllerCalibration;
      selectedControllerProfileItem_ = Item::leftStickX;
      status_ = !controllerProfileModel_->persistenceAvailable()
                    ? AirfixWindowsRenderSettingsStatus::
                          controllerProfilePersistenceUnavailable
                    : (controllerProfileModel_->restartRequired()
                           ? AirfixWindowsRenderSettingsStatus::
                                 controllerProfileSavedRestartRequired
                           : AirfixWindowsRenderSettingsStatus::
                                 controllerProfileReady);
      return {};
    }
    if (selectedPauseItem_ == Item::resume) {
      return {
          .applyTicket = std::nullopt,
          .controllerProfileSaveTicket = std::nullopt,
          .resumeRequested = resumeAvailable_,
      };
    }
    return {};
  }

  if (screen_ == Screen::displaySettings) {
    switch (selectedSettingsItem_) {
    case Item::renderScale:
    case Item::presentation:
    case Item::visualProfile:
      adjustSelectedValue(1);
      return {};
    case Item::rendererStatistics:
      if (model_.phase() ==
          settings::RenderPresentationSettingsMenuPhase::idle) {
        const auto result = model_.setDiagnosticsOverlayEnabled(
            !model_.draftSettings().diagnosticsOverlayEnabled);
        status_ = result.accepted()
                      ? AirfixWindowsRenderSettingsStatus::ready
                      : AirfixWindowsRenderSettingsStatus::invalidSettings;
      }
      return {};
    case Item::apply: {
      auto ticket = model_.beginApply();
      if (!ticket.has_value()) {
        status_ =
            !model_.persistenceAvailable()
                ? AirfixWindowsRenderSettingsStatus::persistenceUnavailable
                : (model_.dirty()
                       ? AirfixWindowsRenderSettingsStatus::applying
                       : AirfixWindowsRenderSettingsStatus::noChanges);
        return {};
      }
      activeTicket_ = ticket;
      status_ = AirfixWindowsRenderSettingsStatus::applying;
      return {.applyTicket = std::move(ticket)};
    }
    case Item::cancel:
      closeDisplaySettings();
      return {};
    default:
      return {};
    }
  }

  if (!controllerProfileModel_.has_value()) {
    return {};
  }
  if (screen_ == Screen::controllerCalibration) {
    switch (selectedControllerProfileItem_) {
    case Item::leftStickX:
    case Item::leftStickY:
    case Item::rightStickX:
    case Item::rightStickY:
      openSelectedControllerAxis();
      return {};
    case Item::resetAllCalibration: {
      const auto result = controllerProfileModel_->resetAllAxes();
      status_ =
          result.accepted()
              ? (controllerProfileModel_->persistenceAvailable()
                     ? AirfixWindowsRenderSettingsStatus::controllerProfileReady
                     : AirfixWindowsRenderSettingsStatus::
                           controllerProfilePersistenceUnavailable)
              : AirfixWindowsRenderSettingsStatus::invalidControllerProfile;
      return {};
    }
    case Item::saveControllerProfile: {
      auto ticket = controllerProfileModel_->beginSave();
      if (!ticket.has_value()) {
        status_ = !controllerProfileModel_->persistenceAvailable()
                      ? AirfixWindowsRenderSettingsStatus::
                            controllerProfilePersistenceUnavailable
                      : ((controllerProfileModel_->dirty() ||
                          controllerProfileModel_->repairRequired())
                             ? AirfixWindowsRenderSettingsStatus::
                                   controllerProfileSaving
                             : AirfixWindowsRenderSettingsStatus::
                                   controllerProfileNoChanges);
        return {};
      }
      activeControllerProfileTicket_ = ticket;
      status_ = AirfixWindowsRenderSettingsStatus::controllerProfileSaving;
      return {
          .applyTicket = std::nullopt,
          .controllerProfileSaveTicket = std::move(ticket),
          .resumeRequested = false,
      };
    }
    case Item::cancel:
      closeControllerCalibration();
      return {};
    default:
      return {};
    }
  }

  if (screen_ != Screen::controllerAxisCalibration) {
    return {};
  }
  switch (selectedControllerAxisItem_) {
  case Item::innerDeadzone:
  case Item::outerSaturation:
  case Item::sensitivity:
  case Item::responseCurve:
    adjustSelectedValue(1);
    return {};
  case Item::inversion: {
    const auto *const calibration =
        controllerProfileModel_->draftAxisCalibration(selectedControllerAxis_);
    if (calibration == nullptr) {
      status_ = AirfixWindowsRenderSettingsStatus::invalidControllerProfile;
      return {};
    }
    const auto result = controllerProfileModel_->setInverted(
        selectedControllerAxis_, calibration->inverted == 0U);
    status_ =
        result.accepted()
            ? (controllerProfileModel_->persistenceAvailable()
                   ? AirfixWindowsRenderSettingsStatus::controllerProfileReady
                   : AirfixWindowsRenderSettingsStatus::
                         controllerProfilePersistenceUnavailable)
            : AirfixWindowsRenderSettingsStatus::invalidControllerProfile;
    return {};
  }
  case Item::resetAxis: {
    const auto result =
        controllerProfileModel_->resetAxis(selectedControllerAxis_);
    status_ =
        result.accepted()
            ? (controllerProfileModel_->persistenceAvailable()
                   ? AirfixWindowsRenderSettingsStatus::controllerProfileReady
                   : AirfixWindowsRenderSettingsStatus::
                         controllerProfilePersistenceUnavailable)
            : AirfixWindowsRenderSettingsStatus::invalidControllerProfile;
    return {};
  }
  case Item::back:
    screen_ = Screen::controllerCalibration;
    selectedControllerProfileItem_ = Item::leftStickX;
    return {};
  default:
    return {};
  }
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

void AirfixWindowsRenderSettingsPanel::closeControllerCalibration() noexcept {
  if (!controllerProfileModel_.has_value()) {
    screen_ = Screen::pause;
    selectedPauseItem_ = Item::displaySettings;
    return;
  }
  if (controllerProfileModel_->phase() ==
      settings::ControllerInputProfileMenuPhase::idle) {
    (void)controllerProfileModel_->cancelDraft();
  }
  screen_ = Screen::pause;
  selectedPauseItem_ = Item::controllerCalibration;
  status_ =
      !controllerProfileModel_->persistenceAvailable()
          ? AirfixWindowsRenderSettingsStatus::
                controllerProfilePersistenceUnavailable
          : (controllerProfileModel_->restartRequired()
                 ? AirfixWindowsRenderSettingsStatus::
                       controllerProfileSavedRestartRequired
                 : AirfixWindowsRenderSettingsStatus::controllerProfileReady);
}

void AirfixWindowsRenderSettingsPanel::openSelectedControllerAxis() noexcept {
  switch (selectedControllerProfileItem_) {
  case Item::leftStickX:
    selectedControllerAxis_ = input::ControllerAxisElement::leftStickX;
    break;
  case Item::leftStickY:
    selectedControllerAxis_ = input::ControllerAxisElement::leftStickY;
    break;
  case Item::rightStickX:
    selectedControllerAxis_ = input::ControllerAxisElement::rightStickX;
    break;
  case Item::rightStickY:
    selectedControllerAxis_ = input::ControllerAxisElement::rightStickY;
    break;
  default:
    return;
  }
  screen_ = Screen::controllerAxisCalibration;
  selectedControllerAxisItem_ = Item::innerDeadzone;
  status_ = controllerProfileModel_->persistenceAvailable()
                ? AirfixWindowsRenderSettingsStatus::controllerProfileReady
                : AirfixWindowsRenderSettingsStatus::
                      controllerProfilePersistenceUnavailable;
}

} // namespace airfix::windows
