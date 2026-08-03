#include "AirfixWindowsUiSemantics.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::windows::AirfixWindowsAccessibilityAction;
using airfix::windows::AirfixWindowsAccessibilityActionStatus;
using airfix::windows::AirfixWindowsControllerProfilePanelState;
using airfix::windows::AirfixWindowsPointerInput;
using airfix::windows::AirfixWindowsRenderSettingsItem;
using airfix::windows::AirfixWindowsRenderSettingsPanel;
using airfix::windows::AirfixWindowsRenderSettingsScreen;
using airfix::windows::AirfixWindowsUiPixelExtent;
using airfix::windows::AirfixWindowsUiSemanticAction;
using airfix::windows::AirfixWindowsUiSemanticBuildResult;
using airfix::windows::AirfixWindowsUiSemanticRole;
using airfix::windows::AirfixWindowsUiSemanticTree;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
makePanel(const AirfixWindowsUiPixelExtent output = {},
          const float uiScale = 100.0F) {
  airfix::render::RenderPresentationSettings settings;
  settings.uiScalePercent = uiScale;
  auto panel = AirfixWindowsRenderSettingsPanel::create(settings, true, output);
  require(panel.has_value(), "display panel fixture was rejected");
  return *panel;
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
makeControllerPanel(const AirfixWindowsUiPixelExtent output = {},
                    const float uiScale = 100.0F) {
  airfix::render::RenderPresentationSettings settings;
  settings.uiScalePercent = uiScale;
  const auto profile = airfix::input::makeDefaultControllerInputProfileRecord();
  auto panel = AirfixWindowsRenderSettingsPanel::create(
      settings, true, output, 0U, true,
      AirfixWindowsControllerProfilePanelState{
          .active = profile,
          .persisted = profile,
          .capabilities = {.persistenceAvailable = true},
      });
  require(panel.has_value(), "controller panel fixture was rejected");
  return *panel;
}

[[nodiscard]] auto action(AirfixWindowsRenderSettingsPanel &panel,
                          const AirfixWindowsRenderSettingsItem item,
                          const AirfixWindowsAccessibilityAction command) {
  const auto view = panel.snapshot();
  return panel.consumeAccessibilityAction(
      view.screen, view.accessibilityGeneration, item, command);
}

[[nodiscard]] airfix::windows::AirfixWindowsRenderSettingsViewItem
visibleItem(const AirfixWindowsRenderSettingsPanel &panel,
            const AirfixWindowsRenderSettingsItem item) {
  const auto snapshot = panel.snapshot();
  const auto found = std::find_if(
      snapshot.items.begin(), snapshot.items.begin() + snapshot.itemCount,
      [item](const auto &candidate) { return candidate.item == item; });
  require(found != snapshot.items.begin() + snapshot.itemCount,
          "expected visible item was absent");
  return *found;
}

void invoke(AirfixWindowsRenderSettingsPanel &panel,
            const AirfixWindowsRenderSettingsItem item) {
  const auto result =
      action(panel, item, AirfixWindowsAccessibilityAction::invoke);
  require(result.accepted() && result.intent.empty(),
          "navigation accessibility action was rejected or emitted intent");
}

[[nodiscard]] AirfixWindowsUiSemanticTree
tree(const AirfixWindowsRenderSettingsPanel &panel) {
  const auto result =
      airfix::windows::buildAirfixWindowsUiSemanticTree(panel.snapshot());
  require(result.complete(), "valid panel snapshot did not build semantics");
  return *result.tree;
}

[[nodiscard]] std::size_t rowCount(const AirfixWindowsUiSemanticTree &tree) {
  return static_cast<std::size_t>(std::count_if(
      tree.nodes.begin(), tree.nodes.begin() + tree.nodeCount,
      [](const auto &node) {
        return node.role == AirfixWindowsUiSemanticRole::action ||
               node.role == AirfixWindowsUiSemanticRole::adjustableValue;
      }));
}

void requireBoundedPathFreeTree(const AirfixWindowsUiSemanticTree &tree) {
  require(tree.complete(), "semantic tree is incomplete");
  std::array<std::uint16_t,
             airfix::windows::airfixWindowsUiMaximumSemanticNodes>
      ids{};
  for (std::uint8_t index = 0U; index < tree.nodeCount; ++index) {
    const auto &node = tree.nodes[index];
    require(!node.name.empty(), "semantic node has no bounded name");
    require(node.name.length < node.name.codeUnits.size(),
            "semantic name is not terminated inside its bound");
    require(node.value.length < node.value.codeUnits.size(),
            "semantic value is not terminated inside its bound");
    const auto inspect = [&](const std::wstring_view value) {
      require(value.find(L'\\') == std::wstring_view::npos &&
                  value.find(L'/') == std::wstring_view::npos &&
                  value.find(L"sha256") == std::wstring_view::npos &&
                  value.find(L"SHA-256") == std::wstring_view::npos,
              "semantic text exposed a path or checksum marker");
    };
    inspect(node.name.view());
    inspect(node.value.view());
    if (index == 0U) {
      require(node.parentIndex ==
                  airfix::windows::airfixWindowsUiSemanticNoParent,
              "semantic root unexpectedly has a parent");
    } else {
      require(node.parentIndex < index,
              "semantic child parent is not an earlier stable node");
    }
    ids[index] = node.runtimeId;
  }
  std::sort(ids.begin(), ids.begin() + tree.nodeCount);
  require(std::adjacent_find(ids.begin(), ids.begin() + tree.nodeCount) ==
              ids.begin() + tree.nodeCount,
          "semantic runtime IDs are not unique");
}

void allScreensExposeBoundedStableSemantics() {
  auto panel = makeControllerPanel();
  auto current = tree(panel);
  require(current.screen == AirfixWindowsRenderSettingsScreen::pause &&
              current.nodeCount == 6U && rowCount(current) == 3U,
          "pause semantics omitted a logical action");
  requireBoundedPathFreeTree(current);

  invoke(panel, AirfixWindowsRenderSettingsItem::displaySettings);
  current = tree(panel);
  require(current.screen ==
                  AirfixWindowsRenderSettingsScreen::displaySettings &&
              current.nodeCount == 26U && rowCount(current) == 9U,
          "display semantics shape is not bounded 3+9+14");
  requireBoundedPathFreeTree(current);
  invoke(panel, AirfixWindowsRenderSettingsItem::cancel);

  invoke(panel, AirfixWindowsRenderSettingsItem::controllerCalibration);
  current = tree(panel);
  require(current.screen ==
                  AirfixWindowsRenderSettingsScreen::controllerCalibration &&
              current.nodeCount == 11U && rowCount(current) == 8U,
          "controller overview semantics omitted a logical row");
  requireBoundedPathFreeTree(current);

  invoke(panel, AirfixWindowsRenderSettingsItem::leftStickX);
  current = tree(panel);
  require(
      current.screen ==
              AirfixWindowsRenderSettingsScreen::controllerAxisCalibration &&
          current.nodeCount == 20U && rowCount(current) == 7U,
      "axis semantics shape is not bounded 3+7+10");
  requireBoundedPathFreeTree(current);
  invoke(panel, AirfixWindowsRenderSettingsItem::back);

  invoke(panel, AirfixWindowsRenderSettingsItem::buttonBindings);
  current = tree(panel);
  require(current.screen ==
                  AirfixWindowsRenderSettingsScreen::controllerButtonBindings &&
              current.nodeCount == 12U && rowCount(current) == 5U,
          "binding semantics shape is not bounded 3+5+4");
  requireBoundedPathFreeTree(current);

  const auto assignment =
      action(panel, AirfixWindowsRenderSettingsItem::bindingAssignment,
             AirfixWindowsAccessibilityAction::increment);
  require(assignment.accepted(), "binding assignment did not increment");
  invoke(panel, AirfixWindowsRenderSettingsItem::moveBinding);
  const auto conflictSnapshot = panel.snapshot();
  current = tree(panel);
  require(
      current.screen ==
              AirfixWindowsRenderSettingsScreen::controllerBindingConflict &&
          current.nodeCount == 5U && rowCount(current) == 2U &&
          conflictSnapshot.logicalItems[0].item ==
              AirfixWindowsRenderSettingsItem::cancel &&
          conflictSnapshot.logicalItems[0].selected,
      "conflict semantics did not preserve cancel-first policy");
  requireBoundedPathFreeTree(current);
}

void offscreenRowsRemainDiscoverableButNotHitTestable() {
  auto panel = makePanel({640U, 360U, 1.0F}, 150.0F);
  invoke(panel, AirfixWindowsRenderSettingsItem::displaySettings);
  const auto snapshot = panel.snapshot();
  require(snapshot.itemCount < snapshot.logicalItemCount &&
              snapshot.logicalItemCount == 9U,
          "small enlarged panel did not exercise clipped rows");
  const auto offscreen = std::count_if(
      snapshot.logicalItems.begin(),
      snapshot.logicalItems.begin() + snapshot.logicalItemCount,
      [](const auto &item) { return item.offscreen && !item.visible; });
  require(offscreen != 0,
          "logical snapshot did not label clipped rows as offscreen");

  const auto semantics = tree(panel);
  require(rowCount(semantics) == 9U,
          "semantic tree silently dropped clipped logical rows");
  const auto offscreenRows = std::count_if(
      semantics.nodes.begin(), semantics.nodes.begin() + semantics.nodeCount,
      [](const auto &node) {
        return (node.role == AirfixWindowsUiSemanticRole::action ||
                node.role == AirfixWindowsUiSemanticRole::adjustableValue) &&
               node.offscreen && !node.visible;
      });
  require(static_cast<std::size_t>(offscreenRows) ==
              static_cast<std::size_t>(offscreen),
          "semantic offscreen state diverged from the panel snapshot");

  const auto focus = action(panel, AirfixWindowsRenderSettingsItem::cancel,
                            AirfixWindowsAccessibilityAction::focus);
  require(focus.accepted() && panel.snapshot().selectedItem ==
                                  AirfixWindowsRenderSettingsItem::cancel,
          "offscreen logical row could not receive bounded focus");
}

void typedActionsMatchPanelPolicyAndFailClosed() {
  auto pointerPanel = makePanel();
  invoke(pointerPanel, AirfixWindowsRenderSettingsItem::displaySettings);
  require(action(pointerPanel, AirfixWindowsRenderSettingsItem::apply,
                 AirfixWindowsAccessibilityAction::focus)
              .accepted(),
          "pointer equivalence fixture could not focus disabled Apply");
  const auto renderScale =
      visibleItem(pointerPanel, AirfixWindowsRenderSettingsItem::renderScale);
  static_cast<void>(pointerPanel.consumePointer(AirfixWindowsPointerInput{
      .xPixels = renderScale.nextBounds.x + renderScale.nextBounds.width * 0.5F,
      .yPixels =
          renderScale.nextBounds.y + renderScale.nextBounds.height * 0.5F,
      .primaryPressed = true,
  }));
  const auto pointerResult = pointerPanel.snapshot();

  auto panel = makePanel();
  invoke(panel, AirfixWindowsRenderSettingsItem::displaySettings);
  const auto before = panel.snapshot();

  const auto stale = panel.consumeAccessibilityAction(
      AirfixWindowsRenderSettingsScreen::pause, before.accessibilityGeneration,
      AirfixWindowsRenderSettingsItem::renderScale,
      AirfixWindowsAccessibilityAction::increment);
  require(stale.status == AirfixWindowsAccessibilityActionStatus::staleScreen &&
              panel.snapshot() == before,
          "stale accessibility action changed the panel");

  const auto unavailable =
      action(panel, AirfixWindowsRenderSettingsItem::leftStickX,
             AirfixWindowsAccessibilityAction::focus);
  require(unavailable.status ==
                  AirfixWindowsAccessibilityActionStatus::itemUnavailable &&
              panel.snapshot() == before,
          "foreign-screen item changed the panel");

  const auto forged =
      action(panel, AirfixWindowsRenderSettingsItem::cancel,
             static_cast<AirfixWindowsAccessibilityAction>(0xFFU));
  require(forged.status ==
                  AirfixWindowsAccessibilityActionStatus::actionUnsupported &&
              panel.snapshot() == before,
          "forged accessibility action changed the panel");

  const auto disabled = action(panel, AirfixWindowsRenderSettingsItem::apply,
                               AirfixWindowsAccessibilityAction::invoke);
  require(disabled.status ==
                  AirfixWindowsAccessibilityActionStatus::itemDisabled &&
              panel.snapshot() == before,
          "disabled Apply changed the panel");

  const auto wrongInvoke =
      action(panel, AirfixWindowsRenderSettingsItem::renderScale,
             AirfixWindowsAccessibilityAction::invoke);
  require(wrongInvoke.status ==
                  AirfixWindowsAccessibilityActionStatus::actionUnsupported &&
              panel.snapshot() == before,
          "adjustable row accepted Invoke");
  const auto wrongIncrement =
      action(panel, AirfixWindowsRenderSettingsItem::cancel,
             AirfixWindowsAccessibilityAction::increment);
  require(wrongIncrement.status ==
                  AirfixWindowsAccessibilityActionStatus::actionUnsupported &&
              panel.snapshot() == before,
          "action row accepted Increment");

  const auto disabledFocus =
      action(panel, AirfixWindowsRenderSettingsItem::apply,
             AirfixWindowsAccessibilityAction::focus);
  require(disabledFocus.accepted() &&
              panel.snapshot().selectedItem ==
                  AirfixWindowsRenderSettingsItem::apply,
          "disabled row could not receive accessibility focus");

  const auto increment =
      action(panel, AirfixWindowsRenderSettingsItem::renderScale,
             AirfixWindowsAccessibilityAction::increment);
  require(increment.accepted() && panel.snapshot() == pointerResult,
          "typed increment diverged from pointer adjustment semantics");
  const auto decrement =
      action(panel, AirfixWindowsRenderSettingsItem::renderScale,
             AirfixWindowsAccessibilityAction::decrement);
  require(decrement.accepted() &&
              panel.snapshot().draftSettings.renderScalePercent == 100.0F,
          "typed decrement did not reverse the settings model step");
  static_cast<void>(action(panel, AirfixWindowsRenderSettingsItem::renderScale,
                           AirfixWindowsAccessibilityAction::increment));
  const auto apply = action(panel, AirfixWindowsRenderSettingsItem::apply,
                            AirfixWindowsAccessibilityAction::invoke);
  require(apply.accepted() && apply.intent.applyTicket.has_value() &&
              !apply.intent.resumeRequested &&
              !apply.intent.controllerProfileSaveTicket.has_value(),
          "typed Apply did not emit exactly one immutable ticket");
}

void controllerActionsCoverSevenActionsFourteenControlsAndSaving() {
  auto panel = makeControllerPanel();
  invoke(panel, AirfixWindowsRenderSettingsItem::controllerCalibration);
  invoke(panel, AirfixWindowsRenderSettingsItem::buttonBindings);

  const auto initial = panel.snapshot();
  for (std::size_t index = 1U;
       index < airfix::input::controllerDigitalGameplayActionCount; ++index) {
    require(action(panel, AirfixWindowsRenderSettingsItem::bindingAction,
                   AirfixWindowsAccessibilityAction::increment)
                .accepted(),
            "typed action cycle was rejected");
  }
  require(panel.snapshot().selectedControllerBindingAction ==
              airfix::input::ControllerDigitalGameplayAction::missionStatus,
          "seven-action range did not reach its final action");
  for (std::size_t index = 1U;
       index < airfix::input::controllerDigitalGameplayActionCount; ++index) {
    require(action(panel, AirfixWindowsRenderSettingsItem::bindingAction,
                   AirfixWindowsAccessibilityAction::decrement)
                .accepted(),
            "typed reverse action range was rejected");
  }
  require(panel.snapshot().selectedControllerBindingAction ==
              initial.selectedControllerBindingAction,
          "seven-action range did not return to its initial action");

  for (std::size_t index = 1U;
       index < airfix::input::controllerAssignableControlCount; ++index) {
    require(action(panel, AirfixWindowsRenderSettingsItem::bindingAssignment,
                   AirfixWindowsAccessibilityAction::increment)
                .accepted(),
            "typed assignment cycle was rejected");
  }
  require(panel.snapshot().selectedControllerBindingControlIndex ==
              airfix::input::controllerAssignableControlCount - 1U,
          "fourteen-control range did not reach its final control");
  for (std::size_t index = 1U;
       index < airfix::input::controllerAssignableControlCount; ++index) {
    require(action(panel, AirfixWindowsRenderSettingsItem::bindingAssignment,
                   AirfixWindowsAccessibilityAction::decrement)
                .accepted(),
            "typed reverse assignment range was rejected");
  }
  require(panel.snapshot().selectedControllerBindingControlIndex ==
              initial.selectedControllerBindingControlIndex,
          "fourteen-control range did not return to its initial control");

  invoke(panel, AirfixWindowsRenderSettingsItem::back);
  invoke(panel, AirfixWindowsRenderSettingsItem::leftStickX);
  require(action(panel, AirfixWindowsRenderSettingsItem::sensitivity,
                 AirfixWindowsAccessibilityAction::increment)
              .accepted(),
          "axis sensitivity edit was rejected");
  invoke(panel, AirfixWindowsRenderSettingsItem::back);
  const auto save =
      action(panel, AirfixWindowsRenderSettingsItem::saveControllerProfile,
             AirfixWindowsAccessibilityAction::invoke);
  require(save.accepted() &&
              save.intent.controllerProfileSaveTicket.has_value(),
          "typed controller save did not emit a ticket");
  const auto saving = panel.snapshot();
  require(saving.controllerProfileSaving,
          "controller panel did not enter saving phase");
  const auto blocked =
      action(panel, AirfixWindowsRenderSettingsItem::leftStickX,
             AirfixWindowsAccessibilityAction::invoke);
  require(blocked.status ==
                  AirfixWindowsAccessibilityActionStatus::itemDisabled &&
              panel.snapshot() == saving,
          "saving phase accepted a disabled controller action");
  const auto savingFocus =
      action(panel, AirfixWindowsRenderSettingsItem::leftStickX,
             AirfixWindowsAccessibilityAction::focus);
  require(savingFocus.accepted() &&
              panel.snapshot().selectedItem ==
                  AirfixWindowsRenderSettingsItem::leftStickX,
          "saving row could not receive accessibility focus");
}

void staleGenerationRejectsSameScreenContextChanges() {
  auto panel = makeControllerPanel();
  invoke(panel, AirfixWindowsRenderSettingsItem::controllerCalibration);
  invoke(panel, AirfixWindowsRenderSettingsItem::leftStickX);
  const auto leftAxisView = panel.snapshot();
  invoke(panel, AirfixWindowsRenderSettingsItem::back);
  invoke(panel, AirfixWindowsRenderSettingsItem::rightStickX);
  const auto rightBefore = panel.snapshot();

  const auto delayed = panel.consumeAccessibilityAction(
      leftAxisView.screen, leftAxisView.accessibilityGeneration,
      AirfixWindowsRenderSettingsItem::sensitivity,
      AirfixWindowsAccessibilityAction::increment);
  require(delayed.status ==
                  AirfixWindowsAccessibilityActionStatus::staleContext &&
              panel.snapshot() == rightBefore,
          "same-screen delayed action mutated a different controller axis");

  auto previewPanel = makeControllerPanel();
  invoke(previewPanel, AirfixWindowsRenderSettingsItem::controllerCalibration);
  invoke(previewPanel, AirfixWindowsRenderSettingsItem::leftStickX);
  const auto previewGeneration =
      previewPanel.snapshot().accessibilityGeneration;
  airfix::windows::AirfixWindowsControllerAxisInputSnapshot preview{};
  preview.rawAxes[0] = 16384;
  previewPanel.setControllerAxisInput(preview);
  require(previewPanel.snapshot().accessibilityGeneration == previewGeneration,
          "preview-only sample invalidated accessibility actions");
  preview.connected = true;
  previewPanel.setControllerAxisInput(preview);
  require(previewPanel.snapshot().accessibilityGeneration != previewGeneration,
          "controller connection/status change kept a stale generation");
}

void semanticActionsAdvertiseOnlySupportedOperations() {
  auto panel = makePanel();
  invoke(panel, AirfixWindowsRenderSettingsItem::displaySettings);
  const auto semantics = tree(panel);
  require(airfix::windows::airfixWindowsUiMaximumSemanticNodes == 27U &&
              semantics.nodeCount <=
                  airfix::windows::airfixWindowsUiMaximumSemanticNodes,
          "semantic node-capacity contract changed unexpectedly");
  for (std::uint8_t index = 3U; index < semantics.nodeCount; ++index) {
    const auto &node = semantics.nodes[index];
    if (node.role == AirfixWindowsUiSemanticRole::action) {
      require(airfix::windows::airfixWindowsUiSemanticHasAction(
                  node.actions, AirfixWindowsUiSemanticAction::focus) &&
                  airfix::windows::airfixWindowsUiSemanticHasAction(
                      node.actions, AirfixWindowsUiSemanticAction::invoke) &&
                  !airfix::windows::airfixWindowsUiSemanticHasAction(
                      node.actions, AirfixWindowsUiSemanticAction::increment),
              "action row advertised adjustable operations");
    } else if (node.role == AirfixWindowsUiSemanticRole::adjustableValue) {
      require(airfix::windows::airfixWindowsUiSemanticHasAction(
                  node.actions, AirfixWindowsUiSemanticAction::focus) &&
                  airfix::windows::airfixWindowsUiSemanticHasAction(
                      node.actions, AirfixWindowsUiSemanticAction::decrement) &&
                  airfix::windows::airfixWindowsUiSemanticHasAction(
                      node.actions, AirfixWindowsUiSemanticAction::increment) &&
                  !airfix::windows::airfixWindowsUiSemanticHasAction(
                      node.actions, AirfixWindowsUiSemanticAction::invoke),
              "adjustable row advertised an invalid action set");
    }
  }

  auto invalid = panel.snapshot();
  invalid.logicalItemCount = 10U;
  AirfixWindowsUiSemanticBuildResult rejected =
      airfix::windows::buildAirfixWindowsUiSemanticTree(invalid);
  require(!rejected.complete() && rejected.issue.has_value(),
          "forged logical count was accepted");

  invalid = panel.snapshot();
  invalid.items[0] = invalid.items[1];
  rejected = airfix::windows::buildAirfixWindowsUiSemanticTree(invalid);
  require(!rejected.complete() && rejected.issue.has_value(),
          "duplicate visible row was accepted");

  invalid = panel.snapshot();
  invalid.logicalItems[0].enabled = !invalid.logicalItems[0].enabled;
  rejected = airfix::windows::buildAirfixWindowsUiSemanticTree(invalid);
  require(!rejected.complete() && rejected.issue.has_value(),
          "visible/logical enabled-state divergence was accepted");

  const auto requireRejected = [&](const auto &snapshot,
                                   const std::string_view message) {
    const auto result =
        airfix::windows::buildAirfixWindowsUiSemanticTree(snapshot);
    require(!result.complete() && result.issue.has_value(), message);
  };

  invalid = panel.snapshot();
  invalid.accessibilityGeneration = 0U;
  requireRejected(invalid, "zero accessibility generation was accepted");
  invalid = panel.snapshot();
  invalid.screen = static_cast<AirfixWindowsRenderSettingsScreen>(0xFFU);
  requireRejected(invalid, "forged screen was accepted");
  invalid = panel.snapshot();
  invalid.selectedItem = AirfixWindowsRenderSettingsItem::count;
  requireRejected(invalid, "forged selected item was accepted");
  invalid = panel.snapshot();
  invalid.status =
      static_cast<airfix::windows::AirfixWindowsRenderSettingsStatus>(0xFFU);
  requireRejected(invalid, "forged status was accepted");
  invalid = panel.snapshot();
  invalid.draftSettings.renderScalePercent =
      std::numeric_limits<float>::quiet_NaN();
  requireRejected(invalid, "non-finite render setting was accepted");
  invalid = panel.snapshot();
  invalid.output.dpiScale = std::numeric_limits<float>::quiet_NaN();
  requireRejected(invalid, "non-finite DPI was accepted");
  invalid = panel.snapshot();
  invalid.layoutScale = 0.0F;
  requireRejected(invalid, "zero layout scale was accepted");
  invalid = panel.snapshot();
  invalid.panelBounds.x = std::numeric_limits<float>::quiet_NaN();
  requireRejected(invalid, "non-finite panel bounds were accepted");
  invalid = panel.snapshot();
  invalid.logicalItems[0].bounds.x =
      static_cast<float>(invalid.output.width) + 1.0F;
  invalid.items[0] = invalid.logicalItems[0];
  requireRejected(invalid, "out-of-output row bounds were accepted");
  invalid = panel.snapshot();
  invalid.logicalItems[0].bounds = {};
  invalid.items[0] = invalid.logicalItems[0];
  requireRejected(invalid, "empty visible row bounds were accepted");
  invalid = panel.snapshot();
  std::swap(invalid.logicalItems[0], invalid.logicalItems[1]);
  std::swap(invalid.items[0], invalid.items[1]);
  requireRejected(invalid, "wrong per-screen logical order was accepted");
  invalid = panel.snapshot();
  invalid.selectedControllerAxis = airfix::input::ControllerAxisElement::count;
  requireRejected(invalid, "forged controller axis was accepted");
  invalid = panel.snapshot();
  invalid.selectedControllerBindingAction =
      airfix::input::ControllerDigitalGameplayAction::count;
  requireRejected(invalid, "forged controller action was accepted");
  invalid = panel.snapshot();
  invalid.controllerBindingPickerPhase =
      static_cast<airfix::settings::ControllerInputBindingPickerPhase>(0xFFU);
  requireRejected(invalid, "forged binding-picker phase was accepted");
  invalid = panel.snapshot();
  invalid.controllerDraftAxes[0].responseCurve =
      airfix::input::ControllerResponseCurve::count;
  requireRejected(invalid, "forged response curve was accepted");
}

} // namespace

int main() {
  try {
    allScreensExposeBoundedStableSemantics();
    offscreenRowsRemainDiscoverableButNotHitTestable();
    typedActionsMatchPanelPolicyAndFailClosed();
    controllerActionsCoverSevenActionsFourteenControlsAndSaving();
    staleGenerationRejectsSameScreenContextChanges();
    semanticActionsAdvertiseOnlySupportedOperations();
  } catch (const std::exception &error) {
    std::cerr << "AirfixWindowsUiSemanticsTests: " << error.what() << '\n';
    return 1;
  }
  std::cout << "AirfixWindowsUiSemanticsTests: PASS\n";
  return 0;
}
