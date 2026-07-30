#import "AirfixControllerCalibrationPanelViewController.h"

#import "AirfixControllerInputProfileCoordinator.h"
#import "AirfixIOSInputCoordinator.h"

#include "airfix/input/ControllerInputBatchBridge.hpp"
#include "airfix/input/InputFrame.hpp"
#include "airfix/settings/ControllerInputBindingPickerModel.hpp"
#include "airfix/settings/ControllerInputProfileMenuModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace {

constexpr int16_t kNavigationActuation =
    airfix::input::uiNavigationActuationQ15;
constexpr int16_t kNavigationRelease = airfix::input::uiNavigationReleaseQ15;
constexpr std::uint16_t kDeadzoneStep = 512U;
constexpr std::uint16_t kSensitivityStep = 50U;
constexpr NSUInteger kOverviewRowCount = 8U;
constexpr NSUInteger kAxisRowCount = 7U;
constexpr NSUInteger kBindingRowCount =
    airfix::input::controllerDigitalGameplayActionCount + 2U;
constexpr NSUInteger kPickerRowCount =
    airfix::input::controllerAssignableControlCount + 1U;
constexpr NSUInteger kConfirmationRowCount = 2U;
constexpr std::uint64_t kPreviewTickDivisor = 4U;

enum class ControllerSettingsScreen : std::uint8_t {
  overview,
  axis,
  bindings,
  picker,
  conflict,
  resetBindingsConfirmation,
};

enum class OverviewRow : NSUInteger {
  leftStickX = 0U,
  leftStickY = 1U,
  rightStickX = 2U,
  rightStickY = 3U,
  buttonBindings = 4U,
  resetAllAxes = 5U,
  save = 6U,
  cancel = 7U,
};

enum class AxisRow : NSUInteger {
  innerDeadzone = 0U,
  outerSaturation = 1U,
  sensitivity = 2U,
  responseCurve = 3U,
  invert = 4U,
  resetAxis = 5U,
  back = 6U,
};

enum class BindingRow : NSUInteger {
  primaryFire = 0U,
  secondaryFire = 1U,
  weaponNext = 2U,
  rearView = 3U,
  cameraCycle = 4U,
  cameraRecenter = 5U,
  missionStatus = 6U,
  resetAll = 7U,
  back = 8U,
};

static_assert(kOverviewRowCount ==
              static_cast<NSUInteger>(OverviewRow::cancel) + 1U);
static_assert(kAxisRowCount == static_cast<NSUInteger>(AxisRow::back) + 1U);
static_assert(kBindingRowCount ==
              static_cast<NSUInteger>(BindingRow::back) + 1U);

[[nodiscard]] BOOL magnitudeAtMost(const int16_t value,
                                   const int16_t limit) noexcept {
  const auto wide = static_cast<std::int32_t>(value);
  const auto magnitude = wide < 0 ? -wide : wide;
  return magnitude <= static_cast<std::int32_t>(limit);
}

[[nodiscard]] NSString *
axisTitle(const airfix::input::ControllerAxisElement axis) {
  using airfix::input::ControllerAxisElement;
  switch (axis) {
  case ControllerAxisElement::leftStickX:
    return NSLocalizedString(@"Left stick X", nil);
  case ControllerAxisElement::leftStickY:
    return NSLocalizedString(@"Left stick Y", nil);
  case ControllerAxisElement::rightStickX:
    return NSLocalizedString(@"Right stick X", nil);
  case ControllerAxisElement::rightStickY:
    return NSLocalizedString(@"Right stick Y", nil);
  case ControllerAxisElement::count:
    return NSLocalizedString(@"Unknown axis", nil);
  }
  return NSLocalizedString(@"Unknown axis", nil);
}

[[nodiscard]] NSString *
curveTitle(const airfix::input::ControllerResponseCurve curve) {
  using airfix::input::ControllerResponseCurve;
  switch (curve) {
  case ControllerResponseCurve::linear:
    return NSLocalizedString(@"Linear", nil);
  case ControllerResponseCurve::squared:
    return NSLocalizedString(@"Squared", nil);
  case ControllerResponseCurve::cubic:
    return NSLocalizedString(@"Cubic", nil);
  case ControllerResponseCurve::count:
    return NSLocalizedString(@"Invalid", nil);
  }
  return NSLocalizedString(@"Invalid", nil);
}

[[nodiscard]] NSString *
actionTitle(const airfix::input::ControllerDigitalGameplayAction action) {
  using airfix::input::ControllerDigitalGameplayAction;
  switch (action) {
  case ControllerDigitalGameplayAction::primaryFire:
    return NSLocalizedString(@"Primary fire", nil);
  case ControllerDigitalGameplayAction::secondaryFire:
    return NSLocalizedString(@"Secondary fire", nil);
  case ControllerDigitalGameplayAction::weaponNext:
    return NSLocalizedString(@"Next weapon", nil);
  case ControllerDigitalGameplayAction::rearView:
    return NSLocalizedString(@"Rear view", nil);
  case ControllerDigitalGameplayAction::cameraCycle:
    return NSLocalizedString(@"Cycle camera", nil);
  case ControllerDigitalGameplayAction::cameraRecenter:
    return NSLocalizedString(@"Recenter camera", nil);
  case ControllerDigitalGameplayAction::missionStatus:
    return NSLocalizedString(@"Mission status", nil);
  case ControllerDigitalGameplayAction::count:
    return NSLocalizedString(@"Unknown action", nil);
  }
  return NSLocalizedString(@"Unknown action", nil);
}

[[nodiscard]] NSString *controlTitle(const airfix::input::ControlId control) {
  using namespace airfix::input::controls::controller;
  if (control == rightTrigger) {
    return NSLocalizedString(@"Right trigger", nil);
  }
  if (control == leftTrigger) {
    return NSLocalizedString(@"Left trigger", nil);
  }
  if (control == rightShoulder) {
    return NSLocalizedString(@"Right shoulder", nil);
  }
  if (control == leftShoulder) {
    return NSLocalizedString(@"Left shoulder", nil);
  }
  if (control == facePrimary) {
    return NSLocalizedString(@"Primary face button", nil);
  }
  if (control == faceSecondary) {
    return NSLocalizedString(@"Secondary face button", nil);
  }
  if (control == faceLeft) {
    return NSLocalizedString(@"Left face button", nil);
  }
  if (control == faceTop) {
    return NSLocalizedString(@"Top face button", nil);
  }
  if (control == rightStickClick) {
    return NSLocalizedString(@"Right stick click", nil);
  }
  if (control == dpadUp) {
    return NSLocalizedString(@"D-pad up", nil);
  }
  if (control == dpadDown) {
    return NSLocalizedString(@"D-pad down", nil);
  }
  if (control == dpadLeft) {
    return NSLocalizedString(@"D-pad left", nil);
  }
  if (control == dpadRight) {
    return NSLocalizedString(@"D-pad right", nil);
  }
  if (control == menu) {
    return NSLocalizedString(@"Menu button", nil);
  }
  return NSLocalizedString(@"Unknown control", nil);
}

[[nodiscard]] NSString *unavailableBindingTitle(
    const airfix::input::ControllerDigitalGameplayBindingStatus status) {
  using airfix::input::ControllerDigitalGameplayBindingStatus;
  switch (status) {
  case ControllerDigitalGameplayBindingStatus::missing:
    return NSLocalizedString(@"Unavailable — missing", nil);
  case ControllerDigitalGameplayBindingStatus::ambiguous:
    return NSLocalizedString(@"Unavailable — multiple assignments", nil);
  case ControllerDigitalGameplayBindingStatus::unsupportedLayout:
    return NSLocalizedString(@"Unavailable — custom layout", nil);
  case ControllerDigitalGameplayBindingStatus::editable:
    break;
  }
  return NSLocalizedString(@"Unavailable", nil);
}

[[nodiscard]] UILabel *makeTextLabel(NSString *text, UIFontTextStyle style,
                                     UIColor *color) {
  UILabel *label = [[UILabel alloc] initWithFrame:CGRectZero];
  label.translatesAutoresizingMaskIntoConstraints = NO;
  label.text = text;
  label.numberOfLines = 0;
  label.textColor = color;
  label.font = [UIFont preferredFontForTextStyle:style];
  label.adjustsFontForContentSizeCategory = YES;
  return label;
}

[[nodiscard]] UIStackView *makeSettingRow(NSString *title, UIView *control) {
  UILabel *label =
      makeTextLabel(title, UIFontTextStyleBody, UIColor.labelColor);
  [label setContentHuggingPriority:UILayoutPriorityRequired
                           forAxis:UILayoutConstraintAxisHorizontal];
  UIStackView *row =
      [[UIStackView alloc] initWithArrangedSubviews:@[ label, control ]];
  row.translatesAutoresizingMaskIntoConstraints = NO;
  row.axis = UILayoutConstraintAxisHorizontal;
  row.alignment = UIStackViewAlignmentCenter;
  row.spacing = 16.0;
  row.layoutMargins = UIEdgeInsetsMake(10.0, 12.0, 10.0, 12.0);
  row.layoutMarginsRelativeArrangement = YES;
  row.layer.cornerRadius = 10.0;
  row.layer.borderWidth = 1.0;
  return row;
}

[[nodiscard]] UIButton *makeActionButton(NSString *title,
                                         NSString *identifier) {
  UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
  button.translatesAutoresizingMaskIntoConstraints = NO;
  [button setTitle:title forState:UIControlStateNormal];
  button.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  button.titleLabel.adjustsFontForContentSizeCategory = YES;
  button.titleLabel.numberOfLines = 0;
  button.contentHorizontalAlignment =
      UIControlContentHorizontalAlignmentLeading;
  button.accessibilityIdentifier = identifier;
  return button;
}

[[nodiscard]] std::uint16_t steppedValue(const std::uint16_t value,
                                         const std::uint16_t minimum,
                                         const std::uint16_t maximum,
                                         const std::uint16_t step,
                                         const NSInteger direction) noexcept {
  const auto wide = static_cast<std::int32_t>(value) +
                    (direction < 0 ? -static_cast<std::int32_t>(step)
                                   : static_cast<std::int32_t>(step));
  return static_cast<std::uint16_t>(
      std::clamp(wide, static_cast<std::int32_t>(minimum),
                 static_cast<std::int32_t>(maximum)));
}

} // namespace

@interface AirfixControllerCalibrationPanelViewController () {
  __weak AirfixControllerInputProfileCoordinator *_coordinator;
  std::optional<airfix::settings::ControllerInputProfileMenuModel> _model;
  airfix::settings::ControllerInputBindingPickerModel _bindingPicker;
  std::optional<airfix::settings::ControllerInputProfileMenuSaveTicket>
      _activeTicket;
  __strong UIView *_overviewRows[kOverviewRowCount];
  __strong UIView *_axisRows[kAxisRowCount];
  __strong UIView *_bindingRows[kBindingRowCount];
  __strong UIView *_pickerRows[kPickerRowCount];
  __strong UIView *_confirmationRows[kConfirmationRowCount];
  __strong UIButton *_bindingActionButtons
      [airfix::input::controllerDigitalGameplayActionCount];
  __strong UIButton
      *_pickerControlButtons[airfix::input::controllerAssignableControlCount];
  ControllerSettingsScreen _screen;
  airfix::input::ControllerAxisElement _selectedAxis;
  airfix::input::ControllerDigitalGameplayAction _selectedAction;
  NSUInteger _selectedRow;
  BOOL _verticalNavigationLatched;
  BOOL _horizontalNavigationLatched;
  BOOL _lastControllerConnected;
  std::uint64_t _lastPreviewTick;
  std::array<int16_t, airfix::input::controllerProfileAxisCount>
      _rawPreviewAxes;
}

@property(nonatomic, strong) UILabel *titleLabel;
@property(nonatomic, strong) UILabel *explanationLabel;
@property(nonatomic, strong) UILabel *previewLabel;
@property(nonatomic, strong) UIProgressView *rawPreviewBar;
@property(nonatomic, strong) UIProgressView *adjustedPreviewBar;
@property(nonatomic, strong) UIStackView *previewStack;
@property(nonatomic, strong) UIScrollView *scrollView;
@property(nonatomic, strong) UIStackView *overviewStack;
@property(nonatomic, strong) UIStackView *axisStack;
@property(nonatomic, strong) UIStackView *bindingsStack;
@property(nonatomic, strong) UIStackView *pickerStack;
@property(nonatomic, strong) UIStackView *confirmationStack;
@property(nonatomic, strong) UILabel *confirmationLabel;
@property(nonatomic, strong) UISlider *innerDeadzoneSlider;
@property(nonatomic, strong) UILabel *innerDeadzoneValueLabel;
@property(nonatomic, strong) UISlider *outerSaturationSlider;
@property(nonatomic, strong) UILabel *outerSaturationValueLabel;
@property(nonatomic, strong) UISlider *sensitivitySlider;
@property(nonatomic, strong) UILabel *sensitivityValueLabel;
@property(nonatomic, strong) UISegmentedControl *responseCurveControl;
@property(nonatomic, strong) UISwitch *invertSwitch;
@property(nonatomic, strong) UIButton *resetAllButton;
@property(nonatomic, strong) UIButton *buttonBindingsButton;
@property(nonatomic, strong) UIButton *resetAllBindingsButton;
@property(nonatomic, strong) UIButton *saveButton;
@property(nonatomic, strong) UIButton *closeButton;
@property(nonatomic, strong) UIButton *resetAxisButton;
@property(nonatomic, strong) UIButton *backButton;
@property(nonatomic, strong) UIButton *confirmationCancelButton;
@property(nonatomic, strong) UIButton *confirmationActionButton;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, readwrite) BOOL persistedDuringPresentation;

- (void)selectAxisButton:(UIButton *)sender;
- (void)updateStatusAfterDraftEdit;
- (void)innerDeadzoneChanged:(UISlider *)sender;
- (void)outerSaturationChanged:(UISlider *)sender;
- (void)sensitivityChanged:(UISlider *)sender;
- (void)responseCurveChanged:(UISegmentedControl *)sender;
- (void)invertChanged:(UISwitch *)sender;
- (void)resetSelectedAxis;
- (void)resetAllAxes;
- (void)showBindings;
- (void)selectBindingActionButton:(UIButton *)sender;
- (void)selectPickerControlButton:(UIButton *)sender;
- (void)showResetBindingsConfirmation;
- (void)cancelConfirmation;
- (void)confirmSwapOrReset;
- (void)saveProfile;
- (void)backToOverview;
- (void)goBack;
- (void)cancelOrClose;
- (void)refreshControls;
- (void)refreshBindingRows;
- (void)refreshPickerRows;
- (void)refreshPreview;
- (NSUInteger)currentRowCount;
- (UIView *_Nullable)rowViewAtIndex:(NSUInteger)index;
- (void)setSelectedRow:(NSUInteger)row announce:(BOOL)announce;
- (void)moveSelection:(NSInteger)direction;
- (void)adjustSelectedValue:(NSInteger)direction;
- (void)activateSelectedRow;
- (void)showAxis:(airfix::input::ControllerAxisElement)axis;
- (void)showBindingPickerForAction:
    (airfix::input::ControllerDigitalGameplayAction)action;
- (void)applyPickerSelection;

@end

@implementation AirfixControllerCalibrationPanelViewController

- (instancetype)initWithCoordinator:
    (AirfixControllerInputProfileCoordinator *)coordinator {
  NSParameterAssert(coordinator != nil);
  self = [super initWithNibName:nil bundle:nil];
  if (self != nil) {
    _coordinator = coordinator;
    _model = airfix::settings::ControllerInputProfileMenuModel::create(
        [coordinator activeProfile], [coordinator persistentProfile], {
          .persistenceAvailable = coordinator.persistenceAvailable == YES,
          .repairRequired = coordinator.repairRequired == YES,
        });
    _screen = ControllerSettingsScreen::overview;
    _selectedAxis = airfix::input::ControllerAxisElement::leftStickX;
    _selectedAction =
        airfix::input::ControllerDigitalGameplayAction::primaryFire;
    _selectedRow = 0U;
    _lastPreviewTick = std::numeric_limits<std::uint64_t>::max();
    self.modalPresentationStyle = UIModalPresentationOverFullScreen;
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
  }
  return self;
}

- (void)loadView {
  UIView *root = [[UIView alloc] initWithFrame:CGRectZero];
  root.backgroundColor = [UIColor colorWithWhite:0.025 alpha:0.96];
  root.accessibilityViewIsModal = YES;
  self.view = root;

  UILabel *title = makeTextLabel(NSLocalizedString(@"Controller settings", nil),
                                 UIFontTextStyleTitle1, UIColor.labelColor);
  title.textAlignment = NSTextAlignmentCenter;
  title.accessibilityTraits |= UIAccessibilityTraitHeader;
  title.accessibilityIdentifier = @"airfix.controller-settings.title";
  self.titleLabel = title;

  UILabel *explanation = makeTextLabel(
      NSLocalizedString(
          @"Changes are saved for the next launch. The controller currently "
          @"driving the mission is never replaced in place.",
          nil),
      UIFontTextStyleFootnote, UIColor.secondaryLabelColor);
  explanation.textAlignment = NSTextAlignmentCenter;
  self.explanationLabel = explanation;

  UILabel *preview =
      makeTextLabel(NSLocalizedString(@"Controller not connected", nil),
                    UIFontTextStyleFootnote, UIColor.secondaryLabelColor);
  preview.textAlignment = NSTextAlignmentCenter;
  UIFont *bodyFont = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
  preview.font = [UIFont monospacedDigitSystemFontOfSize:bodyFont.pointSize
                                                  weight:UIFontWeightMedium];
  preview.accessibilityIdentifier = @"airfix.controller-settings.preview";
  self.previewLabel = preview;

  UIProgressView *rawBar =
      [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleBar];
  rawBar.translatesAutoresizingMaskIntoConstraints = NO;
  rawBar.progressTintColor = UIColor.systemBlueColor;
  rawBar.accessibilityLabel = NSLocalizedString(@"Raw axis magnitude", nil);
  self.rawPreviewBar = rawBar;

  UIProgressView *adjustedBar =
      [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleBar];
  adjustedBar.translatesAutoresizingMaskIntoConstraints = NO;
  adjustedBar.progressTintColor = UIColor.systemYellowColor;
  adjustedBar.accessibilityLabel =
      NSLocalizedString(@"Calibrated axis magnitude", nil);
  self.adjustedPreviewBar = adjustedBar;

  UIStackView *previewStack = [[UIStackView alloc] initWithArrangedSubviews:@[
    preview,
    rawBar,
    adjustedBar,
  ]];
  previewStack.translatesAutoresizingMaskIntoConstraints = NO;
  previewStack.axis = UILayoutConstraintAxisVertical;
  previewStack.spacing = 6.0;
  self.previewStack = previewStack;

  NSMutableArray<UIView *> *axisButtons = [NSMutableArray arrayWithCapacity:4U];
  const std::array<airfix::input::ControllerAxisElement, 4U> axes{{
      airfix::input::ControllerAxisElement::leftStickX,
      airfix::input::ControllerAxisElement::leftStickY,
      airfix::input::ControllerAxisElement::rightStickX,
      airfix::input::ControllerAxisElement::rightStickY,
  }};
  for (NSUInteger index = 0U; index < axes.size(); ++index) {
    UIButton *button = makeActionButton(
        axisTitle(axes[index]),
        [NSString stringWithFormat:@"airfix.controller-settings.axis.%lu",
                                   static_cast<unsigned long>(index)]);
    button.tag = static_cast<NSInteger>(index);
    button.contentHorizontalAlignment =
        UIControlContentHorizontalAlignmentLeading;
    [button addTarget:self
                  action:@selector(selectAxisButton:)
        forControlEvents:UIControlEventTouchUpInside];
    _overviewRows[index] = button;
    [axisButtons addObject:button];
  }

  UIButton *buttonBindings =
      makeActionButton(NSLocalizedString(@"Button bindings", nil),
                       @"airfix.controller-settings.bindings.open");
  buttonBindings.accessibilityHint =
      NSLocalizedString(@"Shows editable gameplay button assignments.", nil);
  [buttonBindings addTarget:self
                     action:@selector(showBindings)
           forControlEvents:UIControlEventTouchUpInside];
  self.buttonBindingsButton = buttonBindings;
  _overviewRows[static_cast<NSUInteger>(OverviewRow::buttonBindings)] =
      buttonBindings;

  UIButton *resetAll =
      makeActionButton(NSLocalizedString(@"Reset all axes", nil),
                       @"airfix.controller-settings.axes.reset-all");
  [resetAll addTarget:self
                action:@selector(resetAllAxes)
      forControlEvents:UIControlEventTouchUpInside];
  self.resetAllButton = resetAll;
  _overviewRows[static_cast<NSUInteger>(OverviewRow::resetAllAxes)] = resetAll;

  UIButton *save =
      makeActionButton(NSLocalizedString(@"Save for next launch", nil),
                       @"airfix.controller-settings.save");
  [save addTarget:self
                action:@selector(saveProfile)
      forControlEvents:UIControlEventTouchUpInside];
  self.saveButton = save;
  _overviewRows[static_cast<NSUInteger>(OverviewRow::save)] = save;

  UIButton *close = makeActionButton(NSLocalizedString(@"Cancel", nil),
                                     @"airfix.controller-settings.cancel");
  [close addTarget:self
                action:@selector(cancelOrClose)
      forControlEvents:UIControlEventTouchUpInside];
  self.closeButton = close;
  close.accessibilityHint = NSLocalizedString(
      @"Discards the complete unsaved controller draft.", nil);
  _overviewRows[static_cast<NSUInteger>(OverviewRow::cancel)] = close;

  UIStackView *overview = [[UIStackView alloc] initWithArrangedSubviews:@[
    axisButtons[0],
    axisButtons[1],
    axisButtons[2],
    axisButtons[3],
    buttonBindings,
    resetAll,
    save,
    close,
  ]];
  overview.translatesAutoresizingMaskIntoConstraints = NO;
  overview.axis = UILayoutConstraintAxisVertical;
  overview.spacing = 10.0;
  self.overviewStack = overview;

  UISlider *inner = [[UISlider alloc] initWithFrame:CGRectZero];
  inner.translatesAutoresizingMaskIntoConstraints = NO;
  inner.minimumValue = 0.0F;
  inner.maximumValue = static_cast<float>(airfix::input::q15One - 1);
  inner.continuous = YES;
  inner.accessibilityLabel = NSLocalizedString(@"Inner deadzone", nil);
  inner.accessibilityIdentifier = @"airfix.controller-settings.inner-deadzone";
  [inner addTarget:self
                action:@selector(innerDeadzoneChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.innerDeadzoneSlider = inner;
  UILabel *innerValue =
      makeTextLabel(@"0", UIFontTextStyleBody, UIColor.secondaryLabelColor);
  innerValue.font =
      [UIFont monospacedDigitSystemFontOfSize:bodyFont.pointSize
                                       weight:UIFontWeightSemibold];
  self.innerDeadzoneValueLabel = innerValue;
  UIStackView *innerControl =
      [[UIStackView alloc] initWithArrangedSubviews:@[ inner, innerValue ]];
  innerControl.axis = UILayoutConstraintAxisHorizontal;
  innerControl.spacing = 10.0;
  UIStackView *innerRow =
      makeSettingRow(NSLocalizedString(@"Inner deadzone", nil), innerControl);
  _axisRows[static_cast<NSUInteger>(AxisRow::innerDeadzone)] = innerRow;

  UISlider *outer = [[UISlider alloc] initWithFrame:CGRectZero];
  outer.translatesAutoresizingMaskIntoConstraints = NO;
  outer.minimumValue = 1.0F;
  outer.maximumValue = static_cast<float>(airfix::input::q15One);
  outer.continuous = YES;
  outer.accessibilityLabel = NSLocalizedString(@"Outer saturation", nil);
  outer.accessibilityIdentifier =
      @"airfix.controller-settings.outer-saturation";
  [outer addTarget:self
                action:@selector(outerSaturationChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.outerSaturationSlider = outer;
  UILabel *outerValue =
      makeTextLabel(@"32767", UIFontTextStyleBody, UIColor.secondaryLabelColor);
  outerValue.font =
      [UIFont monospacedDigitSystemFontOfSize:bodyFont.pointSize
                                       weight:UIFontWeightSemibold];
  self.outerSaturationValueLabel = outerValue;
  UIStackView *outerControl =
      [[UIStackView alloc] initWithArrangedSubviews:@[ outer, outerValue ]];
  outerControl.axis = UILayoutConstraintAxisHorizontal;
  outerControl.spacing = 10.0;
  UIStackView *outerRow =
      makeSettingRow(NSLocalizedString(@"Outer saturation", nil), outerControl);
  _axisRows[static_cast<NSUInteger>(AxisRow::outerSaturation)] = outerRow;

  UISlider *sensitivity = [[UISlider alloc] initWithFrame:CGRectZero];
  sensitivity.translatesAutoresizingMaskIntoConstraints = NO;
  sensitivity.minimumValue =
      airfix::input::controllerAxisMinimumSensitivityPermille;
  sensitivity.maximumValue =
      airfix::input::controllerAxisMaximumSensitivityPermille;
  sensitivity.continuous = YES;
  sensitivity.accessibilityLabel = NSLocalizedString(@"Sensitivity", nil);
  sensitivity.accessibilityIdentifier =
      @"airfix.controller-settings.sensitivity";
  [sensitivity addTarget:self
                  action:@selector(sensitivityChanged:)
        forControlEvents:UIControlEventValueChanged];
  self.sensitivitySlider = sensitivity;
  UILabel *sensitivityValue =
      makeTextLabel(@"100%", UIFontTextStyleBody, UIColor.secondaryLabelColor);
  sensitivityValue.font =
      [UIFont monospacedDigitSystemFontOfSize:bodyFont.pointSize
                                       weight:UIFontWeightSemibold];
  self.sensitivityValueLabel = sensitivityValue;
  UIStackView *sensitivityControl =
      [[UIStackView alloc] initWithArrangedSubviews:@[
        sensitivity,
        sensitivityValue,
      ]];
  sensitivityControl.axis = UILayoutConstraintAxisHorizontal;
  sensitivityControl.spacing = 10.0;
  UIStackView *sensitivityRow = makeSettingRow(
      NSLocalizedString(@"Sensitivity", nil), sensitivityControl);
  _axisRows[static_cast<NSUInteger>(AxisRow::sensitivity)] = sensitivityRow;

  UISegmentedControl *response = [[UISegmentedControl alloc] initWithItems:@[
    NSLocalizedString(@"Linear", nil),
    NSLocalizedString(@"Squared", nil),
    NSLocalizedString(@"Cubic", nil),
  ]];
  response.translatesAutoresizingMaskIntoConstraints = NO;
  response.accessibilityLabel = NSLocalizedString(@"Response curve", nil);
  response.accessibilityIdentifier =
      @"airfix.controller-settings.response-curve";
  [response addTarget:self
                action:@selector(responseCurveChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.responseCurveControl = response;
  UIStackView *responseRow =
      makeSettingRow(NSLocalizedString(@"Response curve", nil), response);
  _axisRows[static_cast<NSUInteger>(AxisRow::responseCurve)] = responseRow;

  UISwitch *invert = [[UISwitch alloc] initWithFrame:CGRectZero];
  invert.translatesAutoresizingMaskIntoConstraints = NO;
  invert.accessibilityLabel = NSLocalizedString(@"Invert axis", nil);
  invert.accessibilityIdentifier = @"airfix.controller-settings.invert";
  [invert addTarget:self
                action:@selector(invertChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.invertSwitch = invert;
  UIStackView *invertRow =
      makeSettingRow(NSLocalizedString(@"Invert", nil), invert);
  _axisRows[static_cast<NSUInteger>(AxisRow::invert)] = invertRow;

  UIButton *resetAxis =
      makeActionButton(NSLocalizedString(@"Reset this axis", nil),
                       @"airfix.controller-settings.axis.reset");
  [resetAxis addTarget:self
                action:@selector(resetSelectedAxis)
      forControlEvents:UIControlEventTouchUpInside];
  self.resetAxisButton = resetAxis;
  _axisRows[static_cast<NSUInteger>(AxisRow::resetAxis)] = resetAxis;

  UIButton *back = makeActionButton(NSLocalizedString(@"Back to settings", nil),
                                    @"airfix.controller-settings.axis.back");
  [back addTarget:self
                action:@selector(backToOverview)
      forControlEvents:UIControlEventTouchUpInside];
  self.backButton = back;
  _axisRows[static_cast<NSUInteger>(AxisRow::back)] = back;

  UIStackView *axisStack = [[UIStackView alloc] initWithArrangedSubviews:@[
    innerRow,
    outerRow,
    sensitivityRow,
    responseRow,
    invertRow,
    resetAxis,
    back,
  ]];
  axisStack.translatesAutoresizingMaskIntoConstraints = NO;
  axisStack.axis = UILayoutConstraintAxisVertical;
  axisStack.spacing = 10.0;
  axisStack.hidden = YES;
  self.axisStack = axisStack;

  NSMutableArray<UIView *> *bindingViews = [NSMutableArray
      arrayWithCapacity:airfix::input::controllerDigitalGameplayActionCount +
                        2U];
  for (const auto &descriptor :
       airfix::input::controllerDigitalGameplayActionCatalog()) {
    const auto index = static_cast<NSUInteger>(descriptor.action);
    UIButton *button = makeActionButton(
        actionTitle(descriptor.action),
        [NSString stringWithFormat:@"airfix.controller-settings.binding.%lu",
                                   static_cast<unsigned long>(index)]);
    button.tag = static_cast<NSInteger>(index);
    button.accessibilityHint = NSLocalizedString(
        @"Chooses a controller control for this action.", nil);
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:48.0].active =
        YES;
    [button addTarget:self
                  action:@selector(selectBindingActionButton:)
        forControlEvents:UIControlEventTouchUpInside];
    _bindingActionButtons[index] = button;
    _bindingRows[index] = button;
    [bindingViews addObject:button];
  }

  UIButton *resetBindings =
      makeActionButton(NSLocalizedString(@"Reset all assignments", nil),
                       @"airfix.controller-settings.bindings.reset-all");
  resetBindings.accessibilityHint = NSLocalizedString(
      @"Requires confirmation and preserves stick calibration.", nil);
  [resetBindings addTarget:self
                    action:@selector(showResetBindingsConfirmation)
          forControlEvents:UIControlEventTouchUpInside];
  self.resetAllBindingsButton = resetBindings;
  _bindingRows[static_cast<NSUInteger>(BindingRow::resetAll)] = resetBindings;
  [bindingViews addObject:resetBindings];

  UIButton *bindingsBack =
      makeActionButton(NSLocalizedString(@"Back to settings", nil),
                       @"airfix.controller-settings.bindings.back");
  [bindingsBack addTarget:self
                   action:@selector(goBack)
         forControlEvents:UIControlEventTouchUpInside];
  _bindingRows[static_cast<NSUInteger>(BindingRow::back)] = bindingsBack;
  [bindingViews addObject:bindingsBack];

  UIStackView *bindingsStack =
      [[UIStackView alloc] initWithArrangedSubviews:bindingViews];
  bindingsStack.translatesAutoresizingMaskIntoConstraints = NO;
  bindingsStack.axis = UILayoutConstraintAxisVertical;
  bindingsStack.spacing = 10.0;
  bindingsStack.hidden = YES;
  self.bindingsStack = bindingsStack;

  NSMutableArray<UIView *> *pickerViews = [NSMutableArray
      arrayWithCapacity:airfix::input::controllerAssignableControlCount + 1U];
  const auto controls = airfix::input::controllerAssignableControlCatalog();
  for (NSUInteger index = 0U; index < controls.size(); ++index) {
    UIButton *button = makeActionButton(
        controlTitle(controls[index].control),
        [NSString
            stringWithFormat:@"airfix.controller-settings.picker.control.%lu",
                             static_cast<unsigned long>(index)]);
    button.tag = static_cast<NSInteger>(index);
    button.accessibilityHint =
        NSLocalizedString(@"Assigns this control. A conflict requires a "
                          @"separate swap confirmation.",
                          nil);
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:48.0].active =
        YES;
    [button addTarget:self
                  action:@selector(selectPickerControlButton:)
        forControlEvents:UIControlEventTouchUpInside];
    _pickerControlButtons[index] = button;
    _pickerRows[index] = button;
    [pickerViews addObject:button];
  }

  UIButton *pickerBack =
      makeActionButton(NSLocalizedString(@"Back to button bindings", nil),
                       @"airfix.controller-settings.picker.back");
  [pickerBack addTarget:self
                 action:@selector(goBack)
       forControlEvents:UIControlEventTouchUpInside];
  _pickerRows[airfix::input::controllerAssignableControlCount] = pickerBack;
  [pickerViews addObject:pickerBack];

  UIStackView *pickerStack =
      [[UIStackView alloc] initWithArrangedSubviews:pickerViews];
  pickerStack.translatesAutoresizingMaskIntoConstraints = NO;
  pickerStack.axis = UILayoutConstraintAxisVertical;
  pickerStack.spacing = 10.0;
  pickerStack.hidden = YES;
  self.pickerStack = pickerStack;

  UILabel *confirmationLabel =
      makeTextLabel(@"", UIFontTextStyleBody, UIColor.labelColor);
  confirmationLabel.textAlignment = NSTextAlignmentCenter;
  confirmationLabel.accessibilityIdentifier =
      @"airfix.controller-settings.confirmation.message";
  self.confirmationLabel = confirmationLabel;

  UIButton *confirmationCancel =
      makeActionButton(NSLocalizedString(@"Cancel", nil),
                       @"airfix.controller-settings.confirmation.cancel");
  [confirmationCancel addTarget:self
                         action:@selector(cancelConfirmation)
               forControlEvents:UIControlEventTouchUpInside];
  self.confirmationCancelButton = confirmationCancel;
  _confirmationRows[0U] = confirmationCancel;

  UIButton *confirmationAction =
      makeActionButton(NSLocalizedString(@"Swap", nil),
                       @"airfix.controller-settings.confirmation.action");
  [confirmationAction addTarget:self
                         action:@selector(confirmSwapOrReset)
               forControlEvents:UIControlEventTouchUpInside];
  self.confirmationActionButton = confirmationAction;
  _confirmationRows[1U] = confirmationAction;

  UIStackView *confirmationStack =
      [[UIStackView alloc] initWithArrangedSubviews:@[
        confirmationLabel,
        confirmationCancel,
        confirmationAction,
      ]];
  confirmationStack.translatesAutoresizingMaskIntoConstraints = NO;
  confirmationStack.axis = UILayoutConstraintAxisVertical;
  confirmationStack.spacing = 12.0;
  confirmationStack.hidden = YES;
  self.confirmationStack = confirmationStack;

  UILabel *status =
      makeTextLabel(@"", UIFontTextStyleFootnote, UIColor.secondaryLabelColor);
  status.textAlignment = NSTextAlignmentCenter;
  status.accessibilityIdentifier = @"airfix.controller-settings.status";
  self.statusLabel = status;

  UIStackView *form = [[UIStackView alloc] initWithArrangedSubviews:@[
    title,
    explanation,
    previewStack,
    overview,
    axisStack,
    bindingsStack,
    pickerStack,
    confirmationStack,
    status,
  ]];
  form.translatesAutoresizingMaskIntoConstraints = NO;
  form.axis = UILayoutConstraintAxisVertical;
  form.spacing = 14.0;
  form.alignment = UIStackViewAlignmentFill;

  UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:CGRectZero];
  scroll.translatesAutoresizingMaskIntoConstraints = NO;
  scroll.alwaysBounceVertical = NO;
  scroll.accessibilityIdentifier = @"airfix.controller-settings.scroll";
  self.scrollView = scroll;
  [root addSubview:scroll];
  [scroll addSubview:form];

  UILayoutGuide *safeArea = root.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [scroll.topAnchor constraintEqualToAnchor:safeArea.topAnchor],
    [scroll.bottomAnchor constraintEqualToAnchor:safeArea.bottomAnchor],
    [scroll.leadingAnchor constraintEqualToAnchor:safeArea.leadingAnchor],
    [scroll.trailingAnchor constraintEqualToAnchor:safeArea.trailingAnchor],
    [form.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor
                                   constant:20.0],
    [form.bottomAnchor
        constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor
                       constant:-20.0],
    [form.centerXAnchor
        constraintEqualToAnchor:scroll.frameLayoutGuide.centerXAnchor],
    [form.widthAnchor
        constraintLessThanOrEqualToAnchor:scroll.frameLayoutGuide.widthAnchor
                                 constant:-32.0],
    [form.widthAnchor constraintLessThanOrEqualToConstant:760.0],
    [inner.widthAnchor constraintGreaterThanOrEqualToConstant:150.0],
    [outer.widthAnchor constraintGreaterThanOrEqualToConstant:150.0],
    [sensitivity.widthAnchor constraintGreaterThanOrEqualToConstant:150.0],
    [resetAll.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [save.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [close.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [resetAxis.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [back.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [buttonBindings.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [resetBindings.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [bindingsBack.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [pickerBack.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [confirmationCancel.heightAnchor
        constraintGreaterThanOrEqualToConstant:48.0],
    [confirmationAction.heightAnchor
        constraintGreaterThanOrEqualToConstant:48.0],
  ]];

  [self refreshControls];
  [self setSelectedRow:0U announce:NO];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  UIView *selected = [self rowViewAtIndex:_selectedRow];
  UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification,
                                  selected);
}

- (void)viewSafeAreaInsetsDidChange {
  [super viewSafeAreaInsetsDidChange];
  UIView *selected = [self rowViewAtIndex:_selectedRow];
  if (selected != nil) {
    [self.scrollView scrollRectToVisible:[selected convertRect:selected.bounds
                                                        toView:self.scrollView]
                                animated:NO];
  }
}

- (BOOL)accessibilityPerformEscape {
  [self goBack];
  return YES;
}

- (void)selectAxisButton:(UIButton *)sender {
  const auto index = static_cast<std::size_t>(sender.tag);
  if (index >= airfix::input::controllerProfileAxisCount) {
    return;
  }
  [self showAxis:static_cast<airfix::input::ControllerAxisElement>(index)];
}

- (void)innerDeadzoneChanged:(UISlider *)sender {
  if (!_model.has_value()) {
    return;
  }
  const auto *axis = _model->draftAxisCalibration(_selectedAxis);
  if (axis == nullptr) {
    return;
  }
  const auto maximum =
      static_cast<std::uint16_t>(axis->outerSaturationQ15 - 1U);
  const auto value = static_cast<std::uint16_t>(
      std::clamp(std::lround(sender.value), 0L, static_cast<long>(maximum)));
  const auto result = _model->setInnerDeadzoneQ15(_selectedAxis, value);
  if (result.accepted()) {
    [self updateStatusAfterDraftEdit];
  }
  [self refreshControls];
}

- (void)outerSaturationChanged:(UISlider *)sender {
  if (!_model.has_value()) {
    return;
  }
  const auto *axis = _model->draftAxisCalibration(_selectedAxis);
  if (axis == nullptr) {
    return;
  }
  const auto minimum = static_cast<std::uint16_t>(axis->innerDeadzoneQ15 + 1U);
  const auto value = static_cast<std::uint16_t>(
      std::clamp(std::lround(sender.value), static_cast<long>(minimum),
                 static_cast<long>(airfix::input::q15One)));
  const auto result = _model->setOuterSaturationQ15(_selectedAxis, value);
  if (result.accepted()) {
    [self updateStatusAfterDraftEdit];
  }
  [self refreshControls];
}

- (void)sensitivityChanged:(UISlider *)sender {
  if (!_model.has_value()) {
    return;
  }
  const auto value = static_cast<std::uint16_t>(
      std::clamp(std::lround(sender.value),
                 static_cast<long>(
                     airfix::input::controllerAxisMinimumSensitivityPermille),
                 static_cast<long>(
                     airfix::input::controllerAxisMaximumSensitivityPermille)));
  const auto result = _model->setSensitivityPermille(_selectedAxis, value);
  if (result.accepted()) {
    [self updateStatusAfterDraftEdit];
  }
  [self refreshControls];
}

- (void)responseCurveChanged:(UISegmentedControl *)sender {
  if (!_model.has_value() || sender.selectedSegmentIndex < 0 ||
      sender.selectedSegmentIndex >=
          static_cast<NSInteger>(
              airfix::input::ControllerResponseCurve::count)) {
    return;
  }
  const auto result = _model->setResponseCurve(
      _selectedAxis, static_cast<airfix::input::ControllerResponseCurve>(
                         sender.selectedSegmentIndex));
  if (result.accepted()) {
    [self updateStatusAfterDraftEdit];
  }
  [self refreshControls];
}

- (void)invertChanged:(UISwitch *)sender {
  if (_model.has_value()) {
    const auto result = _model->setInverted(_selectedAxis, sender.isOn == YES);
    if (result.accepted()) {
      [self updateStatusAfterDraftEdit];
    }
    [self refreshControls];
  }
}

- (void)resetSelectedAxis {
  if (_model.has_value()) {
    const auto result = _model->resetAxis(_selectedAxis);
    if (result.accepted()) {
      [self updateStatusAfterDraftEdit];
    }
    [self refreshControls];
  }
}

- (void)resetAllAxes {
  if (_model.has_value()) {
    const auto result = _model->resetAllAxes();
    if (result.accepted()) {
      [self updateStatusAfterDraftEdit];
    }
    [self refreshControls];
  }
}

- (void)showBindings {
  if (!_model.has_value() || _activeTicket.has_value()) {
    return;
  }
  _bindingPicker.close();
  _screen = ControllerSettingsScreen::bindings;
  self.overviewStack.hidden = YES;
  self.axisStack.hidden = YES;
  self.pickerStack.hidden = YES;
  self.confirmationStack.hidden = YES;
  self.bindingsStack.hidden = NO;
  self.previewStack.hidden = YES;
  self.titleLabel.text = NSLocalizedString(@"Button bindings", nil);
  self.explanationLabel.text = NSLocalizedString(
      @"Choose one of the seven gameplay actions. Actions marked unavailable "
      @"cannot be safely edited from this bounded list.",
      nil);
  _verticalNavigationLatched = NO;
  _horizontalNavigationLatched = NO;
  [self refreshControls];
  [self setSelectedRow:static_cast<NSUInteger>(_selectedAction) announce:YES];
}

- (void)selectBindingActionButton:(UIButton *)sender {
  const auto index = static_cast<std::size_t>(sender.tag);
  if (index >= airfix::input::controllerDigitalGameplayActionCount) {
    return;
  }
  [self showBindingPickerForAction:
            static_cast<airfix::input::ControllerDigitalGameplayAction>(index)];
}

- (void)selectPickerControlButton:(UIButton *)sender {
  const auto index = static_cast<std::size_t>(sender.tag);
  if (!_model.has_value() || !_bindingPicker.selectControlIndex(index)) {
    return;
  }
  [self setSelectedRow:static_cast<NSUInteger>(index) announce:NO];
  [self applyPickerSelection];
}

- (void)showBindingPickerForAction:
    (airfix::input::ControllerDigitalGameplayAction)action {
  if (!_model.has_value() || _activeTicket.has_value()) {
    return;
  }
  const auto result = _bindingPicker.begin(*_model, action);
  if (result.status !=
      airfix::settings::ControllerInputBindingPickerStatus::opened) {
    self.statusLabel.text =
        NSLocalizedString(@"This action is unavailable for remapping.", nil);
    UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                    self.statusLabel.text);
    [self refreshControls];
    return;
  }

  _selectedAction = action;
  _screen = ControllerSettingsScreen::picker;
  self.overviewStack.hidden = YES;
  self.axisStack.hidden = YES;
  self.bindingsStack.hidden = YES;
  self.confirmationStack.hidden = YES;
  self.pickerStack.hidden = NO;
  self.previewStack.hidden = YES;
  self.titleLabel.text = actionTitle(action);
  self.explanationLabel.text = NSLocalizedString(
      @"Choose a controller control. If another editable action uses it, "
      @"you can cancel or explicitly swap the two assignments.",
      nil);
  _verticalNavigationLatched = NO;
  _horizontalNavigationLatched = NO;
  [self refreshControls];
  [self setSelectedRow:_bindingPicker.selectedControlIndex() announce:YES];
}

- (void)applyPickerSelection {
  if (!_model.has_value() ||
      _bindingPicker.phase() !=
          airfix::settings::ControllerInputBindingPickerPhase::
              choosingControl) {
    return;
  }
  const auto result = _bindingPicker.applySelection(*_model);
  if (result.accepted()) {
    NSString *message = [NSString
        stringWithFormat:NSLocalizedString(@"%@ is now assigned to %@.", nil),
                         actionTitle(_selectedAction),
                         result.control.has_value()
                             ? controlTitle(*result.control)
                             : NSLocalizedString(@"the selected control", nil)];
    const BOOL dirty = _model->dirty();
    [self showBindings];
    self.statusLabel.text = [message
        stringByAppendingString:(dirty ? NSLocalizedString(@" Unsaved changes.",
                                                           nil)
                                       : NSLocalizedString(
                                             @" No unsaved assignment change.",
                                             nil))];
    UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                    self.statusLabel.text);
    return;
  }

  if (result.needsSwapConfirmation()) {
    _screen = ControllerSettingsScreen::conflict;
    self.pickerStack.hidden = YES;
    self.confirmationStack.hidden = NO;
    self.titleLabel.text = NSLocalizedString(@"Assignment conflict", nil);
    self.explanationLabel.text = NSLocalizedString(
        @"Cancel is selected by default. Swap is a separate action.", nil);
    self.confirmationLabel.text = [NSString
        stringWithFormat:NSLocalizedString(
                             @"%@ is already assigned to %@. Swap %@ and %@?",
                             nil),
                         result.control.has_value()
                             ? controlTitle(*result.control)
                             : NSLocalizedString(@"This control", nil),
                         result.conflictingAction.has_value()
                             ? actionTitle(*result.conflictingAction)
                             : NSLocalizedString(@"another action", nil),
                         actionTitle(_selectedAction),
                         result.conflictingAction.has_value()
                             ? actionTitle(*result.conflictingAction)
                             : NSLocalizedString(@"the other action", nil)];
    [self.confirmationActionButton
        setTitle:NSLocalizedString(@"Swap assignments", nil)
        forState:UIControlStateNormal];
    self.confirmationActionButton.accessibilityIdentifier =
        @"airfix.controller-settings.conflict.swap";
    [self refreshControls];
    [self setSelectedRow:0U announce:YES];
    return;
  }

  using airfix::settings::ControllerInputBindingPickerStatus;
  switch (result.status) {
  case ControllerInputBindingPickerStatus::protectedConflict:
    self.statusLabel.text = NSLocalizedString(
        @"That control is reserved by a protected controller assignment.", nil);
    break;
  case ControllerInputBindingPickerStatus::saveInProgress:
    self.statusLabel.text =
        NSLocalizedString(@"Wait for the save to finish.", nil);
    break;
  case ControllerInputBindingPickerStatus::actionUnavailable:
    self.statusLabel.text =
        NSLocalizedString(@"This action is unavailable for remapping.", nil);
    break;
  case ControllerInputBindingPickerStatus::invalidControl:
  case ControllerInputBindingPickerStatus::invalidProfile:
  case ControllerInputBindingPickerStatus::invalidAction:
  case ControllerInputBindingPickerStatus::invalidPhase:
    self.statusLabel.text =
        NSLocalizedString(@"That assignment could not be applied.", nil);
    break;
  case ControllerInputBindingPickerStatus::opened:
  case ControllerInputBindingPickerStatus::accepted:
  case ControllerInputBindingPickerStatus::conflict:
    self.statusLabel.text =
        NSLocalizedString(@"That assignment could not be applied.", nil);
    break;
  }
  UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                  self.statusLabel.text);
  [self refreshControls];
}

- (void)showResetBindingsConfirmation {
  if (!_model.has_value() || _activeTicket.has_value()) {
    return;
  }
  _bindingPicker.close();
  _screen = ControllerSettingsScreen::resetBindingsConfirmation;
  self.bindingsStack.hidden = YES;
  self.confirmationStack.hidden = NO;
  self.titleLabel.text = NSLocalizedString(@"Reset all assignments?", nil);
  self.explanationLabel.text = NSLocalizedString(
      @"Cancel is selected by default. Stick calibration will be preserved.",
      nil);
  self.confirmationLabel.text = NSLocalizedString(
      @"This restores all controller button assignments to their defaults.",
      nil);
  [self.confirmationActionButton
      setTitle:NSLocalizedString(@"Reset assignments", nil)
      forState:UIControlStateNormal];
  self.confirmationActionButton.accessibilityIdentifier =
      @"airfix.controller-settings.bindings.confirm-reset";
  [self refreshControls];
  [self setSelectedRow:0U announce:YES];
}

- (void)cancelConfirmation {
  if (_activeTicket.has_value()) {
    return;
  }
  if (_screen == ControllerSettingsScreen::conflict) {
    (void)_bindingPicker.cancelSwapConfirmation();
    _screen = ControllerSettingsScreen::picker;
    self.confirmationStack.hidden = YES;
    self.pickerStack.hidden = NO;
    self.titleLabel.text = actionTitle(_selectedAction);
    self.explanationLabel.text = NSLocalizedString(
        @"Choose a controller control. No assignment was changed.", nil);
    [self refreshControls];
    [self setSelectedRow:_bindingPicker.selectedControlIndex() announce:YES];
    return;
  }
  if (_screen == ControllerSettingsScreen::resetBindingsConfirmation) {
    [self showBindings];
    [self setSelectedRow:static_cast<NSUInteger>(BindingRow::resetAll)
                announce:YES];
  }
}

- (void)confirmSwapOrReset {
  if (!_model.has_value() || _activeTicket.has_value()) {
    return;
  }
  if (_screen == ControllerSettingsScreen::conflict) {
    const auto result = _bindingPicker.confirmSwap(*_model);
    if (result.accepted()) {
      NSString *message =
          _model->dirty()
              ? NSLocalizedString(@"Assignments swapped. Unsaved changes.", nil)
              : NSLocalizedString(@"Assignments swapped back to the saved "
                                  @"profile.",
                                  nil);
      [self showBindings];
      self.statusLabel.text = message;
      UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                      message);
      return;
    }
    self.statusLabel.text = NSLocalizedString(
        @"The assignments changed before the swap could be completed.", nil);
    UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                    self.statusLabel.text);
    if (_bindingPicker.phase() ==
        airfix::settings::ControllerInputBindingPickerPhase::choosingControl) {
      _screen = ControllerSettingsScreen::picker;
      self.confirmationStack.hidden = YES;
      self.pickerStack.hidden = NO;
      [self refreshControls];
      [self setSelectedRow:_bindingPicker.selectedControlIndex() announce:YES];
    }
    return;
  }
  if (_screen == ControllerSettingsScreen::resetBindingsConfirmation) {
    const auto result = _model->resetAllControllerBindings();
    if (result.accepted()) {
      const BOOL dirty = _model->dirty();
      [self showBindings];
      self.statusLabel.text =
          dirty ? NSLocalizedString(
                      @"Default assignments restored. Stick calibration was "
                      @"preserved. Unsaved changes.",
                      nil)
                : NSLocalizedString(
                      @"Default assignments restored. Stick calibration was "
                      @"preserved.",
                      nil);
      UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                      self.statusLabel.text);
    } else {
      self.statusLabel.text =
          NSLocalizedString(@"Assignments could not be reset.", nil);
      [self refreshControls];
    }
  }
}

- (void)updateStatusAfterDraftEdit {
  if (!_model.has_value()) {
    return;
  }
  if (_model->dirty()) {
    self.statusLabel.text = NSLocalizedString(@"Unsaved changes.", nil);
  } else if (_model->canSave()) {
    self.statusLabel.text =
        NSLocalizedString(@"Controller profile repair is ready to save.", nil);
  } else {
    self.statusLabel.text = @"";
  }
}

- (void)saveProfile {
  if (!_model.has_value() || _activeTicket.has_value()) {
    return;
  }
  AirfixControllerInputProfileCoordinator *coordinator = _coordinator;
  if (coordinator == nil) {
    _model->setPersistenceAvailable(false);
    self.statusLabel.text = NSLocalizedString(
        @"Controller settings cannot be saved on this installation.", nil);
    [self refreshControls];
    return;
  }
  _model->setPersistenceAvailable(coordinator.persistenceAvailable == YES);
  _activeTicket = _model->beginSave();
  if (!_activeTicket.has_value()) {
    self.statusLabel.text =
        _model->persistenceAvailable()
            ? NSLocalizedString(@"No controller setting changes to save.", nil)
            : NSLocalizedString(@"Controller settings cannot be saved on this "
                                @"installation.",
                                nil);
    [self refreshControls];
    return;
  }

  self.statusLabel.text =
      NSLocalizedString(@"Saving controller settings...", nil);
  UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                  self.statusLabel.text);
  [self refreshControls];

  const auto candidate = _activeTicket->candidate;
  __weak AirfixControllerCalibrationPanelViewController *weakSelf = self;
  [coordinator
      requestPersistentProfile:candidate
                    completion:^(
                        const AirfixControllerInputProfileSaveResult result) {
                      AirfixControllerCalibrationPanelViewController
                          *strongSelf = weakSelf;
                      if (strongSelf == nil ||
                          !strongSelf->_activeTicket.has_value() ||
                          !strongSelf->_model.has_value()) {
                        return;
                      }
                      const auto ticket = *strongSelf->_activeTicket;
                      if (result ==
                          AirfixControllerInputProfileSaveResultSaved) {
                        if (!strongSelf->_model->finishSaveSuccess(ticket)) {
                          return;
                        }
                        strongSelf->_activeTicket.reset();
                        strongSelf.persistedDuringPresentation = YES;
                        NSString *message =
                            ticket.repairsPersistence &&
                                    ticket.candidate ==
                                        strongSelf->_model->activeRecord()
                                ? NSLocalizedString(
                                      @"Controller profile repaired.", nil)
                                : NSLocalizedString(
                                      @"Saved. Changes take effect after "
                                      @"restart.",
                                      nil);
                        strongSelf.statusLabel.text = message;
                        UIAccessibilityPostNotification(
                            UIAccessibilityAnnouncementNotification, message);
                        [strongSelf refreshControls];
                        return;
                      }

                      if (!strongSelf->_model->finishSaveFailure(ticket)) {
                        return;
                      }
                      strongSelf->_activeTicket.reset();
                      strongSelf->_bindingPicker.close();
                      NSString *message = NSLocalizedString(
                          @"Controller settings were not saved. Try again.",
                          nil);
                      if (result ==
                          AirfixControllerInputProfileSaveResultInvalidCandidate) {
                        message = NSLocalizedString(
                            @"The selected controller settings are invalid.",
                            nil);
                      } else if (
                          result ==
                          AirfixControllerInputProfileSaveResultPersistenceUnavailable) {
                        strongSelf->_model->setPersistenceAvailable(false);
                        message = NSLocalizedString(
                            @"Controller settings cannot be saved on this "
                            @"installation.",
                            nil);
                      } else if (result ==
                                 AirfixControllerInputProfileSaveResultBusy) {
                        message = NSLocalizedString(
                            @"Another controller-profile save is still "
                            @"finishing.",
                            nil);
                      }
                      strongSelf.statusLabel.text = message;
                      UIAccessibilityPostNotification(
                          UIAccessibilityAnnouncementNotification, message);
                      [strongSelf refreshControls];
                    }];
}

- (void)backToOverview {
  if (_activeTicket.has_value()) {
    return;
  }
  const NSUInteger destination =
      _screen == ControllerSettingsScreen::axis
          ? static_cast<NSUInteger>(_selectedAxis)
          : static_cast<NSUInteger>(OverviewRow::buttonBindings);
  _bindingPicker.close();
  _screen = ControllerSettingsScreen::overview;
  self.axisStack.hidden = YES;
  self.bindingsStack.hidden = YES;
  self.pickerStack.hidden = YES;
  self.confirmationStack.hidden = YES;
  self.overviewStack.hidden = NO;
  self.previewStack.hidden = NO;
  self.titleLabel.text = NSLocalizedString(@"Controller settings", nil);
  self.explanationLabel.text = NSLocalizedString(
      @"Edit stick calibration or button bindings, then save the complete "
      @"profile for the next launch.",
      nil);
  _verticalNavigationLatched = NO;
  _horizontalNavigationLatched = NO;
  [self refreshControls];
  [self setSelectedRow:destination announce:YES];
}

- (void)goBack {
  if (_activeTicket.has_value()) {
    self.statusLabel.text =
        NSLocalizedString(@"Wait for the save to finish.", nil);
    UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                    self.statusLabel.text);
    return;
  }
  switch (_screen) {
  case ControllerSettingsScreen::overview:
    [self cancelOrClose];
    break;
  case ControllerSettingsScreen::axis:
  case ControllerSettingsScreen::bindings:
    [self backToOverview];
    break;
  case ControllerSettingsScreen::picker:
    _bindingPicker.close();
    [self showBindings];
    break;
  case ControllerSettingsScreen::conflict:
  case ControllerSettingsScreen::resetBindingsConfirmation:
    [self cancelConfirmation];
    break;
  }
}

- (void)cancelOrClose {
  if (_activeTicket.has_value()) {
    self.statusLabel.text =
        NSLocalizedString(@"Wait for the save to finish.", nil);
    UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                    self.statusLabel.text);
    return;
  }
  if (_model.has_value() && !_activeTicket.has_value()) {
    (void)_model->cancelDraft();
  }
  _bindingPicker.close();
  id<AirfixControllerCalibrationPanelViewControllerDelegate> delegate =
      self.delegate;
  [delegate controllerCalibrationPanelViewControllerDidFinish:self];
}

- (void)refreshControls {
  if (!_model.has_value()) {
    self.statusLabel.text =
        NSLocalizedString(@"Controller settings are unavailable.", nil);
    self.saveButton.enabled = NO;
    self.closeButton.enabled = YES;
    return;
  }
  const auto *axis = _model->draftAxisCalibration(_selectedAxis);
  if (axis == nullptr) {
    self.statusLabel.text =
        NSLocalizedString(@"Controller settings are unavailable.", nil);
    self.saveButton.enabled = NO;
    self.closeButton.enabled = YES;
    return;
  }

  self.innerDeadzoneSlider.maximumValue =
      static_cast<float>(axis->outerSaturationQ15 - 1U);
  self.innerDeadzoneSlider.value = static_cast<float>(axis->innerDeadzoneQ15);
  self.innerDeadzoneValueLabel.text = [NSString
      stringWithFormat:@"%u",
                       static_cast<unsigned int>(axis->innerDeadzoneQ15)];
  self.innerDeadzoneSlider.accessibilityValue =
      self.innerDeadzoneValueLabel.text;

  self.outerSaturationSlider.minimumValue =
      static_cast<float>(axis->innerDeadzoneQ15 + 1U);
  self.outerSaturationSlider.value =
      static_cast<float>(axis->outerSaturationQ15);
  self.outerSaturationValueLabel.text = [NSString
      stringWithFormat:@"%u",
                       static_cast<unsigned int>(axis->outerSaturationQ15)];
  self.outerSaturationSlider.accessibilityValue =
      self.outerSaturationValueLabel.text;

  self.sensitivitySlider.value = static_cast<float>(axis->sensitivityPermille);
  self.sensitivityValueLabel.text =
      [NSString stringWithFormat:@"%.0f%%", axis->sensitivityPermille / 10.0];
  self.sensitivitySlider.accessibilityValue = self.sensitivityValueLabel.text;
  self.responseCurveControl.selectedSegmentIndex =
      static_cast<NSInteger>(axis->responseCurve);
  self.responseCurveControl.accessibilityValue =
      curveTitle(axis->responseCurve);
  self.invertSwitch.on = axis->inverted != 0U;

  const BOOL saving = _model->phase() ==
                      airfix::settings::ControllerInputProfileMenuPhase::saving;
  self.innerDeadzoneSlider.enabled = !saving;
  self.outerSaturationSlider.enabled = !saving;
  self.sensitivitySlider.enabled = !saving;
  self.responseCurveControl.enabled = !saving;
  self.invertSwitch.enabled = !saving;
  self.resetAxisButton.enabled = !saving;
  self.resetAllButton.enabled = !saving;
  self.buttonBindingsButton.enabled = !saving;
  self.resetAllBindingsButton.enabled = !saving;
  self.backButton.enabled = !saving;
  self.saveButton.enabled = _model->canSave();
  self.closeButton.enabled = !saving;
  [self.closeButton setTitle:NSLocalizedString(@"Cancel", nil)
                    forState:UIControlStateNormal];

  for (NSUInteger index = 0U; index < kOverviewRowCount; ++index) {
    if ([_overviewRows[index] isKindOfClass:UIButton.class]) {
      ((UIButton *)_overviewRows[index]).enabled =
          index == static_cast<NSUInteger>(OverviewRow::save)
              ? _model->canSave()
              : !saving;
    }
  }
  for (NSUInteger index = 0U; index < kBindingRowCount; ++index) {
    ((UIButton *)_bindingRows[index]).enabled = !saving;
  }
  for (NSUInteger index = 0U; index < kPickerRowCount; ++index) {
    ((UIButton *)_pickerRows[index]).enabled = !saving;
  }
  self.confirmationCancelButton.enabled = !saving;
  self.confirmationActionButton.enabled = !saving;

  [self refreshBindingRows];
  [self refreshPickerRows];
  if (!_model->persistenceAvailable() && self.statusLabel.text.length == 0U) {
    self.statusLabel.text = NSLocalizedString(
        @"Controller settings cannot be saved on this installation.", nil);
  }
  [self refreshPreview];
}

- (void)refreshBindingRows {
  if (!_model.has_value()) {
    return;
  }
  for (const auto &descriptor :
       airfix::input::controllerDigitalGameplayActionCatalog()) {
    const auto index = static_cast<NSUInteger>(descriptor.action);
    const auto lookup = _model->draftDigitalGameplayBinding(descriptor.action);
    NSString *value = unavailableBindingTitle(lookup.status);
    if (lookup.editable()) {
      const auto *binding = _model->draftBinding(lookup.bindingIndex);
      if (binding != nullptr) {
        value = controlTitle(binding->control);
      }
    }
    UIButton *button = _bindingActionButtons[index];
    [button setTitle:[NSString stringWithFormat:@"%@ - %@",
                                                actionTitle(descriptor.action),
                                                value]
            forState:UIControlStateNormal];
    button.accessibilityLabel = actionTitle(descriptor.action);
    button.accessibilityValue = value;
  }
}

- (void)refreshPickerRows {
  const auto controls = airfix::input::controllerAssignableControlCatalog();
  const auto chosen = _bindingPicker.selectedControlIndex();
  for (NSUInteger index = 0U; index < controls.size(); ++index) {
    UIButton *button = _pickerControlButtons[index];
    const BOOL selected =
        _bindingPicker.phase() !=
            airfix::settings::ControllerInputBindingPickerPhase::closed &&
        index == chosen;
    NSString *title = controlTitle(controls[index].control);
    [button setTitle:(selected ? [NSString
                                     stringWithFormat:NSLocalizedString(
                                                          @"Selected: %@", nil),
                                                      title]
                               : title)
            forState:UIControlStateNormal];
    button.accessibilityLabel = title;
    button.accessibilityValue =
        selected ? NSLocalizedString(@"Selected control", nil) : nil;
    if (selected) {
      button.accessibilityTraits |= UIAccessibilityTraitSelected;
    } else {
      button.accessibilityTraits &= ~UIAccessibilityTraitSelected;
    }
  }
}

- (void)refreshPreview {
  if (!_model.has_value()) {
    return;
  }
  const auto index = static_cast<std::size_t>(_selectedAxis);
  if (index >= _rawPreviewAxes.size()) {
    return;
  }
  if (!_lastControllerConnected) {
    self.previewLabel.text =
        NSLocalizedString(@"Controller not connected", nil);
    self.rawPreviewBar.progress = 0.0F;
    self.adjustedPreviewBar.progress = 0.0F;
    return;
  }

  const int16_t raw = _rawPreviewAxes[index];
  const auto adjusted = airfix::input::transformControllerAxisForTransport(
      raw, _selectedAxis, _model->resolvedDraftProfile());
  const int16_t calibrated = adjusted.value_or(0);
  self.previewLabel.text =
      [NSString stringWithFormat:@"%@  raw %+d  calibrated %+d",
                                 axisTitle(_selectedAxis), raw, calibrated];
  const auto magnitude = [](const int16_t value) noexcept {
    const auto wide = static_cast<std::int32_t>(value);
    return static_cast<float>(wide < 0 ? -wide : wide) /
           static_cast<float>(airfix::input::q15One);
  };
  self.rawPreviewBar.progress = magnitude(raw);
  self.adjustedPreviewBar.progress = magnitude(calibrated);
  self.rawPreviewBar.accessibilityValue =
      [NSString stringWithFormat:@"%+d", raw];
  self.adjustedPreviewBar.accessibilityValue =
      [NSString stringWithFormat:@"%+d", calibrated];
}

- (NSUInteger)currentRowCount {
  switch (_screen) {
  case ControllerSettingsScreen::overview:
    return kOverviewRowCount;
  case ControllerSettingsScreen::axis:
    return kAxisRowCount;
  case ControllerSettingsScreen::bindings:
    return kBindingRowCount;
  case ControllerSettingsScreen::picker:
    return kPickerRowCount;
  case ControllerSettingsScreen::conflict:
  case ControllerSettingsScreen::resetBindingsConfirmation:
    return kConfirmationRowCount;
  }
  return 0U;
}

- (UIView *)rowViewAtIndex:(NSUInteger)index {
  if (index >= [self currentRowCount]) {
    return nil;
  }
  switch (_screen) {
  case ControllerSettingsScreen::overview:
    return _overviewRows[index];
  case ControllerSettingsScreen::axis:
    return _axisRows[index];
  case ControllerSettingsScreen::bindings:
    return _bindingRows[index];
  case ControllerSettingsScreen::picker:
    return _pickerRows[index];
  case ControllerSettingsScreen::conflict:
  case ControllerSettingsScreen::resetBindingsConfirmation:
    return _confirmationRows[index];
  }
  return nil;
}

- (void)setSelectedRow:(NSUInteger)row announce:(BOOL)announce {
  const NSUInteger count = [self currentRowCount];
  if (row >= count) {
    return;
  }
  _selectedRow = row;
  if (_screen == ControllerSettingsScreen::picker &&
      row < airfix::input::controllerAssignableControlCount) {
    (void)_bindingPicker.selectControlIndex(row);
    [self refreshPickerRows];
  }
  for (NSUInteger index = 0U; index < count; ++index) {
    UIView *view = [self rowViewAtIndex:index];
    const BOOL active = index == row;
    view.layer.borderColor = (active ? UIColor.systemYellowColor
                                     : [UIColor colorWithWhite:1.0 alpha:0.18])
                                 .CGColor;
    view.layer.borderWidth = active ? 2.5 : 1.0;
    view.layer.cornerRadius = 10.0;
  }
  UIView *selected = [self rowViewAtIndex:row];
  if (selected != nil) {
    [self.view layoutIfNeeded];
    [self.scrollView scrollRectToVisible:[selected convertRect:selected.bounds
                                                        toView:self.scrollView]
                                animated:YES];
  }
  if (announce && selected != nil) {
    UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification,
                                    selected);
  }
}

- (void)moveSelection:(NSInteger)direction {
  const NSUInteger count = [self currentRowCount];
  if (count == 0U) {
    return;
  }
  const NSInteger next =
      std::clamp(static_cast<NSInteger>(_selectedRow) + direction,
                 static_cast<NSInteger>(0), static_cast<NSInteger>(count - 1U));
  [self setSelectedRow:static_cast<NSUInteger>(next) announce:YES];
}

- (void)adjustSelectedValue:(NSInteger)direction {
  if (!_model.has_value() || direction == 0 ||
      _model->phase() ==
          airfix::settings::ControllerInputProfileMenuPhase::saving ||
      _screen != ControllerSettingsScreen::axis) {
    return;
  }
  const auto *axis = _model->draftAxisCalibration(_selectedAxis);
  if (axis == nullptr) {
    return;
  }
  switch (static_cast<AxisRow>(_selectedRow)) {
  case AxisRow::innerDeadzone:
    (void)_model->setInnerDeadzoneQ15(
        _selectedAxis,
        steppedValue(axis->innerDeadzoneQ15, 0U,
                     static_cast<std::uint16_t>(axis->outerSaturationQ15 - 1U),
                     kDeadzoneStep, direction));
    break;
  case AxisRow::outerSaturation:
    (void)_model->setOuterSaturationQ15(
        _selectedAxis,
        steppedValue(axis->outerSaturationQ15,
                     static_cast<std::uint16_t>(axis->innerDeadzoneQ15 + 1U),
                     static_cast<std::uint16_t>(airfix::input::q15One),
                     kDeadzoneStep, direction));
    break;
  case AxisRow::sensitivity:
    (void)_model->setSensitivityPermille(
        _selectedAxis,
        steppedValue(axis->sensitivityPermille,
                     airfix::input::controllerAxisMinimumSensitivityPermille,
                     airfix::input::controllerAxisMaximumSensitivityPermille,
                     kSensitivityStep, direction));
    break;
  case AxisRow::responseCurve: {
    const auto current = static_cast<std::int32_t>(axis->responseCurve);
    const auto maximum = static_cast<std::int32_t>(
                             airfix::input::ControllerResponseCurve::count) -
                         1;
    (void)_model->setResponseCurve(
        _selectedAxis,
        static_cast<airfix::input::ControllerResponseCurve>(
            std::clamp(current + (direction < 0 ? -1 : 1), 0, maximum)));
    break;
  }
  case AxisRow::invert:
    (void)_model->setInverted(_selectedAxis, direction > 0);
    break;
  case AxisRow::resetAxis:
  case AxisRow::back:
    break;
  }
  [self updateStatusAfterDraftEdit];
  [self refreshControls];
}

- (void)activateSelectedRow {
  if (!_model.has_value() ||
      _model->phase() ==
          airfix::settings::ControllerInputProfileMenuPhase::saving) {
    return;
  }
  if (_screen == ControllerSettingsScreen::overview) {
    switch (static_cast<OverviewRow>(_selectedRow)) {
    case OverviewRow::leftStickX:
    case OverviewRow::leftStickY:
    case OverviewRow::rightStickX:
    case OverviewRow::rightStickY:
      [self showAxis:static_cast<airfix::input::ControllerAxisElement>(
                         _selectedRow)];
      return;
    case OverviewRow::buttonBindings:
      [self showBindings];
      return;
    case OverviewRow::resetAllAxes:
      [self resetAllAxes];
      return;
    case OverviewRow::save:
      [self saveProfile];
      return;
    case OverviewRow::cancel:
      [self cancelOrClose];
      return;
    }
  }

  if (_screen == ControllerSettingsScreen::axis) {
    switch (static_cast<AxisRow>(_selectedRow)) {
    case AxisRow::innerDeadzone:
    case AxisRow::outerSaturation:
    case AxisRow::sensitivity:
    case AxisRow::responseCurve:
      [self adjustSelectedValue:1];
      break;
    case AxisRow::invert: {
      const auto *axis = _model->draftAxisCalibration(_selectedAxis);
      if (axis != nullptr) {
        const auto result =
            _model->setInverted(_selectedAxis, axis->inverted == 0U);
        if (result.accepted()) {
          [self updateStatusAfterDraftEdit];
        }
        [self refreshControls];
      }
      break;
    }
    case AxisRow::resetAxis:
      [self resetSelectedAxis];
      break;
    case AxisRow::back:
      [self backToOverview];
      break;
    }
    return;
  }

  if (_screen == ControllerSettingsScreen::bindings) {
    if (_selectedRow < airfix::input::controllerDigitalGameplayActionCount) {
      [self showBindingPickerForAction:
                static_cast<airfix::input::ControllerDigitalGameplayAction>(
                    _selectedRow)];
    } else if (_selectedRow == static_cast<NSUInteger>(BindingRow::resetAll)) {
      [self showResetBindingsConfirmation];
    } else {
      [self backToOverview];
    }
    return;
  }

  if (_screen == ControllerSettingsScreen::picker) {
    if (_selectedRow < airfix::input::controllerAssignableControlCount) {
      (void)_bindingPicker.selectControlIndex(_selectedRow);
      [self applyPickerSelection];
    } else {
      [self goBack];
    }
    return;
  }

  if (_screen == ControllerSettingsScreen::conflict ||
      _screen == ControllerSettingsScreen::resetBindingsConfirmation) {
    if (_selectedRow == 0U) {
      [self cancelConfirmation];
    } else {
      [self confirmSwapOrReset];
    }
  }
}

- (void)showAxis:(airfix::input::ControllerAxisElement)axis {
  if (axis >= airfix::input::ControllerAxisElement::count ||
      _activeTicket.has_value()) {
    return;
  }
  _selectedAxis = axis;
  _screen = ControllerSettingsScreen::axis;
  self.overviewStack.hidden = YES;
  self.bindingsStack.hidden = YES;
  self.pickerStack.hidden = YES;
  self.confirmationStack.hidden = YES;
  self.axisStack.hidden = NO;
  self.previewStack.hidden = NO;
  self.titleLabel.text = axisTitle(axis);
  self.explanationLabel.text = NSLocalizedString(
      @"Raw and calibrated values update live. The draft affects this "
      @"preview only until the next launch.",
      nil);
  _verticalNavigationLatched = NO;
  _horizontalNavigationLatched = NO;
  [self refreshControls];
  [self setSelectedRow:0U announce:YES];
}

- (void)consumeUIInputSnapshot:(AirfixUIInputSnapshot *)input {
  NSAssert(NSThread.isMainThread, @"Controller-settings input belongs to main");
  if (input == nil) {
    return;
  }
  const BOOL connectionChanged =
      _lastControllerConnected != input.isControllerConnected;
  _lastControllerConnected = input.isControllerConnected;
  _rawPreviewAxes = {
      input.controllerLeftStickX,
      input.controllerLeftStickY,
      input.controllerRightStickX,
      input.controllerRightStickY,
  };
  if (_lastPreviewTick == std::numeric_limits<std::uint64_t>::max() ||
      input.tick >= _lastPreviewTick + kPreviewTickDivisor ||
      connectionChanged || !_lastControllerConnected) {
    _lastPreviewTick = input.tick;
    [self refreshPreview];
  }

  if (input.cancelPressed) {
    [self goBack];
    return;
  }

  if (_activeTicket.has_value() ||
      (_model.has_value() &&
       _model->phase() ==
           airfix::settings::ControllerInputProfileMenuPhase::saving)) {
    return;
  }

  if (magnitudeAtMost(input.navigationY, kNavigationRelease)) {
    _verticalNavigationLatched = NO;
  }
  BOOL movedVertically = NO;
  if (!_verticalNavigationLatched &&
      input.navigationY >= kNavigationActuation) {
    _verticalNavigationLatched = YES;
    movedVertically = YES;
    [self moveSelection:-1];
  } else if (!_verticalNavigationLatched &&
             input.navigationY <= -kNavigationActuation) {
    _verticalNavigationLatched = YES;
    movedVertically = YES;
    [self moveSelection:1];
  }

  if (magnitudeAtMost(input.navigationX, kNavigationRelease)) {
    _horizontalNavigationLatched = NO;
  }
  if (_screen == ControllerSettingsScreen::axis && !movedVertically &&
      !_horizontalNavigationLatched &&
      input.navigationX >= kNavigationActuation) {
    _horizontalNavigationLatched = YES;
    [self adjustSelectedValue:1];
  } else if (_screen == ControllerSettingsScreen::axis && !movedVertically &&
             !_horizontalNavigationLatched &&
             input.navigationX <= -kNavigationActuation) {
    _horizontalNavigationLatched = YES;
    [self adjustSelectedValue:-1];
  }

  if (input.tabPreviousPressed) {
    if (_screen == ControllerSettingsScreen::axis) {
      [self adjustSelectedValue:-1];
    } else {
      [self moveSelection:-1];
    }
  }
  if (input.tabNextPressed) {
    if (_screen == ControllerSettingsScreen::axis) {
      [self adjustSelectedValue:1];
    } else {
      [self moveSelection:1];
    }
  }
  if (input.confirmPressed) {
    [self activateSelectedRow];
  }
}

@end
