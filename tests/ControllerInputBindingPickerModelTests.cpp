#include "airfix/settings/ControllerInputBindingPickerModel.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <type_traits>

namespace {

using namespace airfix;

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

[[nodiscard]] settings::ControllerInputProfileMenuModel makeMenu() {
  const auto record = input::makeDefaultControllerInputProfileRecord();
  const auto model =
      settings::ControllerInputProfileMenuModel::create(record, record);
  require(model.has_value(), "default profile did not create a menu model");
  return *model;
}

[[nodiscard]] std::size_t controlIndex(const input::ControlId control) {
  const auto catalog = input::controllerAssignableControlCatalog();
  for (std::size_t index = 0U; index < catalog.size(); ++index) {
    if (catalog[index].control == control) {
      return index;
    }
  }
  fail("control is absent from assignable catalog");
}

[[nodiscard]] input::ControlId
actionControl(const settings::ControllerInputProfileMenuModel &menu,
              const input::ControllerDigitalGameplayAction action) {
  const auto lookup = menu.draftDigitalGameplayBinding(action);
  require(lookup.editable(), "test action is not uniquely editable");
  const auto *binding = menu.draftBinding(lookup.bindingIndex);
  require(binding != nullptr, "editable action has no draft binding");
  return binding->control;
}

void beginPreselectsTheExactCurrentControl() {
  auto menu = makeMenu();
  settings::ControllerInputBindingPickerModel picker;
  const auto opened =
      picker.begin(menu, input::ControllerDigitalGameplayAction::primaryFire);
  require(
      opened.status == settings::ControllerInputBindingPickerStatus::opened &&
          opened.action ==
              input::ControllerDigitalGameplayAction::primaryFire &&
          opened.control == input::controls::controller::rightTrigger &&
          picker.phase() ==
              settings::ControllerInputBindingPickerPhase::choosingControl &&
          picker.selectedControl() ==
              input::controls::controller::rightTrigger &&
          picker.selectedControlIndex() ==
              controlIndex(input::controls::controller::rightTrigger),
      "picker did not preselect the exact current binding");
}

void selectionIsBoundedAndInvalidOperationsDoNotMutate() {
  auto menu = makeMenu();
  const auto before = menu.draftRecord();
  settings::ControllerInputBindingPickerModel picker;
  require(!picker.selectControlIndex(0U) &&
              picker.applySelection(menu).status ==
                  settings::ControllerInputBindingPickerStatus::invalidPhase &&
              menu.draftRecord() == before,
          "closed picker accepted a selection or mutated the profile");

  static_cast<void>(
      picker.begin(menu, input::ControllerDigitalGameplayAction::primaryFire));
  require(!picker.selectControlIndex(
              input::controllerAssignableControlCatalog().size()) &&
              picker.selectedControl() ==
                  input::controls::controller::rightTrigger,
          "out-of-range control selection changed picker state");
  for (std::size_t index = 0U;
       index < input::controllerAssignableControlCatalog().size() + 5U;
       ++index) {
    picker.moveControlSelection(-1);
  }
  require(picker.selectedControlIndex() == 0U,
          "picker moved before the first control");
  for (std::size_t index = 0U;
       index < input::controllerAssignableControlCatalog().size() + 5U;
       ++index) {
    picker.moveControlSelection(1);
  }
  require(picker.selectedControlIndex() ==
              input::controllerAssignableControlCatalog().size() - 1U,
          "picker moved beyond the last control");
}

void unoccupiedGameplayControlMovesWithoutConflict() {
  auto menu = makeMenu();
  const auto before = menu.draftRecord();
  settings::ControllerInputBindingPickerModel picker;
  static_cast<void>(
      picker.begin(menu, input::ControllerDigitalGameplayAction::primaryFire));
  require(picker.selectControlIndex(
              controlIndex(input::controls::controller::dpadLeft)),
          "picker rejected an assignable control");
  const auto result = picker.applySelection(menu);
  require(result.accepted() &&
              picker.phase() ==
                  settings::ControllerInputBindingPickerPhase::closed &&
              actionControl(
                  menu, input::ControllerDigitalGameplayAction::primaryFire) ==
                  input::controls::controller::dpadLeft &&
              menu.draftRecord().axes == before.axes && menu.dirty(),
          "unoccupied gameplay control did not produce one clean move");
}

void conflictIsCancelFirstAndSwapNeedsExplicitConfirmation() {
  auto menu = makeMenu();
  const auto before = menu.draftRecord();
  settings::ControllerInputBindingPickerModel picker;
  static_cast<void>(
      picker.begin(menu, input::ControllerDigitalGameplayAction::primaryFire));
  require(picker.selectControlIndex(
              controlIndex(input::controls::controller::leftTrigger)),
          "conflict control could not be selected");

  const auto conflict = picker.applySelection(menu);
  require(conflict.needsSwapConfirmation() &&
              conflict.conflictingAction ==
                  input::ControllerDigitalGameplayAction::secondaryFire &&
              picker.phase() ==
                  settings::ControllerInputBindingPickerPhase::confirmingSwap &&
              menu.draftRecord() == before,
          "cancel-first conflict mutated the profile or hid the swap");
  require(
      picker.cancelSwapConfirmation() &&
          picker.phase() ==
              settings::ControllerInputBindingPickerPhase::choosingControl &&
          menu.draftRecord() == before,
      "cancelling swap did not return to the unchanged picker");

  const auto conflictAgain = picker.applySelection(menu);
  require(conflictAgain.needsSwapConfirmation(),
          "reselected conflict was not revalidated");
  const auto swapped = picker.confirmSwap(menu);
  require(
      swapped.accepted() &&
          picker.phase() ==
              settings::ControllerInputBindingPickerPhase::closed &&
          actionControl(menu,
                        input::ControllerDigitalGameplayAction::primaryFire) ==
              input::controls::controller::leftTrigger &&
          actionControl(
              menu, input::ControllerDigitalGameplayAction::secondaryFire) ==
              input::controls::controller::rightTrigger &&
          menu.draftRecord().axes == before.axes,
      "explicit swap was not atomic or did not preserve calibration");
}

void protectedConflictsRemainInThePicker() {
  auto menu = makeMenu();
  const auto before = menu.draftRecord();
  settings::ControllerInputBindingPickerModel picker;
  static_cast<void>(
      picker.begin(menu, input::ControllerDigitalGameplayAction::primaryFire));
  require(picker.selectControlIndex(
              controlIndex(input::controls::controller::menu)),
          "protected menu control could not be selected");
  const auto result = picker.applySelection(menu);
  require(
      result.status ==
              settings::ControllerInputBindingPickerStatus::protectedConflict &&
          !result.conflictingAction.has_value() &&
          picker.phase() ==
              settings::ControllerInputBindingPickerPhase::choosingControl &&
          menu.draftRecord() == before,
      "protected pause binding was mutated or offered as a swap");
}

void swapConfirmationRevalidatesTheCurrentDraft() {
  auto menu = makeMenu();
  settings::ControllerInputBindingPickerModel picker;
  static_cast<void>(
      picker.begin(menu, input::ControllerDigitalGameplayAction::primaryFire));
  require(picker.selectControlIndex(
              controlIndex(input::controls::controller::leftTrigger)) &&
              picker.applySelection(menu).needsSwapConfirmation(),
          "revalidation fixture did not reach conflict confirmation");

  const auto changedConflict = menu.rebindDigitalGameplayAction(
      input::ControllerDigitalGameplayAction::secondaryFire,
      input::controls::controller::dpadLeft);
  require(changedConflict.accepted(),
          "revalidation fixture could not change the conflicting action");

  const auto confirmed = picker.confirmSwap(menu);
  require(
      confirmed.accepted() &&
          actionControl(menu,
                        input::ControllerDigitalGameplayAction::primaryFire) ==
              input::controls::controller::leftTrigger &&
          actionControl(
              menu, input::ControllerDigitalGameplayAction::secondaryFire) ==
              input::controls::controller::dpadLeft,
      "confirmation trusted stale conflict data instead of revalidating");
}

void beginRejectsSavingInvalidAndCustomActions() {
  auto menu = makeMenu();
  settings::ControllerInputBindingPickerModel picker;
  const auto invalid = picker.begin(
      menu, static_cast<input::ControllerDigitalGameplayAction>(255U));
  require(invalid.status ==
                  settings::ControllerInputBindingPickerStatus::invalidAction &&
              picker.phase() ==
                  settings::ControllerInputBindingPickerPhase::closed,
          "forged action opened the picker");

  auto custom = input::makeDefaultControllerInputProfileRecord();
  const auto lookup = input::controllerDigitalGameplayBinding(
      custom, input::ControllerDigitalGameplayAction::missionStatus);
  require(lookup.editable() &&
              custom.bindingCount < input::controllerProfileBindingCapacity,
          "custom-layout fixture cannot be constructed");
  auto duplicate = custom.bindings[lookup.bindingIndex];
  duplicate.control = input::controls::controller::facePrimary;
  duplicate.physicalKind = input::PhysicalEventKind::digital;
  custom.bindings[custom.bindingCount] = duplicate;
  ++custom.bindingCount;
  const auto customMenu =
      settings::ControllerInputProfileMenuModel::create(custom, custom);
  require(
      customMenu.has_value() &&
          picker.begin(*customMenu,
                       input::ControllerDigitalGameplayAction::missionStatus)
                  .status ==
              settings::ControllerInputBindingPickerStatus::actionUnavailable,
      "ambiguous custom action was guessed by the picker");

  auto saving = makeMenu();
  require(saving
              .setSensitivityPermille(input::ControllerAxisElement::leftStickX,
                                      1050U)
              .accepted(),
          "saving fixture edit failed");
  const auto ticket = saving.beginSave();
  require(ticket.has_value() &&
              picker.begin(saving,
                           input::ControllerDigitalGameplayAction::primaryFire)
                      .status ==
                  settings::ControllerInputBindingPickerStatus::saveInProgress,
          "in-flight save allowed a picker to open");
}

void saveStartingAfterOpenStillFreezesTheDraft() {
  auto menu = makeMenu();
  require(menu.setSensitivityPermille(input::ControllerAxisElement::leftStickX,
                                      1050U)
              .accepted(),
          "post-open save fixture edit failed");
  settings::ControllerInputBindingPickerModel picker;
  static_cast<void>(
      picker.begin(menu, input::ControllerDigitalGameplayAction::primaryFire));
  require(picker.selectControlIndex(
              controlIndex(input::controls::controller::dpadLeft)),
          "post-open save fixture selection failed");
  const auto beforeSave = menu.draftRecord();
  const auto ticket = menu.beginSave();
  require(ticket.has_value(), "post-open save fixture did not begin");
  const auto frozen = picker.applySelection(menu);
  require(
      frozen.status ==
              settings::ControllerInputBindingPickerStatus::saveInProgress &&
          picker.phase() ==
              settings::ControllerInputBindingPickerPhase::choosingControl &&
          menu.draftRecord() == beforeSave,
      "picker mutated a draft frozen by an in-flight save");
}

static_assert(
    std::is_trivially_copyable_v<settings::ControllerInputBindingPickerResult>);
static_assert(
    std::is_trivially_copyable_v<settings::ControllerInputBindingPickerModel>);

} // namespace

int main() {
  beginPreselectsTheExactCurrentControl();
  selectionIsBoundedAndInvalidOperationsDoNotMutate();
  unoccupiedGameplayControlMovesWithoutConflict();
  conflictIsCancelFirstAndSwapNeedsExplicitConfirmation();
  protectedConflictsRemainInThePicker();
  swapConfirmationRevalidatesTheCurrentDraft();
  beginRejectsSavingInvalidAndCustomActions();
  saveStartingAfterOpenStillFreezesTheDraft();
  std::cout << "Controller input binding picker model tests passed\n";
  return EXIT_SUCCESS;
}
