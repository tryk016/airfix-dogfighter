#import "AirfixControllerCalibrationPanelViewController.h"

#import "AirfixControllerInputProfileCoordinator.h"
#import "AirfixIOSInputCoordinator.h"

#include "airfix/input/ControllerInputBatchBridge.hpp"
#include "airfix/input/InputFrame.hpp"
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
constexpr NSUInteger kRowCount = 7U;
constexpr std::uint64_t kPreviewTickDivisor = 4U;

enum class CalibrationScreen : std::uint8_t {
  overview,
  axis,
};

enum class OverviewRow : NSUInteger {
  leftStickX = 0U,
  leftStickY = 1U,
  rightStickX = 2U,
  rightStickY = 3U,
  resetAll = 4U,
  save = 5U,
  close = 6U,
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
  std::optional<airfix::settings::ControllerInputProfileMenuSaveTicket>
      _activeTicket;
  __strong UIView *_overviewRows[kRowCount];
  __strong UIView *_axisRows[kRowCount];
  CalibrationScreen _screen;
  airfix::input::ControllerAxisElement _selectedAxis;
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
@property(nonatomic, strong) UIStackView *overviewStack;
@property(nonatomic, strong) UIStackView *axisStack;
@property(nonatomic, strong) UISlider *innerDeadzoneSlider;
@property(nonatomic, strong) UILabel *innerDeadzoneValueLabel;
@property(nonatomic, strong) UISlider *outerSaturationSlider;
@property(nonatomic, strong) UILabel *outerSaturationValueLabel;
@property(nonatomic, strong) UISlider *sensitivitySlider;
@property(nonatomic, strong) UILabel *sensitivityValueLabel;
@property(nonatomic, strong) UISegmentedControl *responseCurveControl;
@property(nonatomic, strong) UISwitch *invertSwitch;
@property(nonatomic, strong) UIButton *resetAllButton;
@property(nonatomic, strong) UIButton *saveButton;
@property(nonatomic, strong) UIButton *closeButton;
@property(nonatomic, strong) UIButton *resetAxisButton;
@property(nonatomic, strong) UIButton *backButton;
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
- (void)saveProfile;
- (void)backToOverview;
- (void)cancelOrClose;
- (void)refreshControls;
- (void)refreshPreview;
- (void)setSelectedRow:(NSUInteger)row announce:(BOOL)announce;
- (void)moveSelection:(NSInteger)direction;
- (void)adjustSelectedValue:(NSInteger)direction;
- (void)activateSelectedRow;
- (void)showAxis:(airfix::input::ControllerAxisElement)axis;

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
    _screen = CalibrationScreen::overview;
    _selectedAxis = airfix::input::ControllerAxisElement::leftStickX;
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

  UILabel *title =
      makeTextLabel(NSLocalizedString(@"Controller calibration", nil),
                    UIFontTextStyleTitle1, UIColor.labelColor);
  title.textAlignment = NSTextAlignmentCenter;
  title.accessibilityTraits |= UIAccessibilityTraitHeader;
  title.accessibilityIdentifier = @"airfix.controller-calibration.title";
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
  preview.accessibilityIdentifier = @"airfix.controller-calibration.preview";
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
        [NSString stringWithFormat:@"airfix.controller-calibration.axis.%lu",
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

  UIButton *resetAll =
      makeActionButton(NSLocalizedString(@"Reset all axes", nil),
                       @"airfix.controller-calibration.reset-all");
  [resetAll addTarget:self
                action:@selector(resetAllAxes)
      forControlEvents:UIControlEventTouchUpInside];
  self.resetAllButton = resetAll;
  _overviewRows[static_cast<NSUInteger>(OverviewRow::resetAll)] = resetAll;

  UIButton *save =
      makeActionButton(NSLocalizedString(@"Save for next launch", nil),
                       @"airfix.controller-calibration.save");
  [save addTarget:self
                action:@selector(saveProfile)
      forControlEvents:UIControlEventTouchUpInside];
  self.saveButton = save;
  _overviewRows[static_cast<NSUInteger>(OverviewRow::save)] = save;

  UIButton *close = makeActionButton(NSLocalizedString(@"Cancel", nil),
                                     @"airfix.controller-calibration.close");
  [close addTarget:self
                action:@selector(cancelOrClose)
      forControlEvents:UIControlEventTouchUpInside];
  self.closeButton = close;
  _overviewRows[static_cast<NSUInteger>(OverviewRow::close)] = close;

  UIStackView *overview = [[UIStackView alloc] initWithArrangedSubviews:@[
    axisButtons[0],
    axisButtons[1],
    axisButtons[2],
    axisButtons[3],
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
  inner.accessibilityIdentifier =
      @"airfix.controller-calibration.inner-deadzone";
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
      @"airfix.controller-calibration.outer-saturation";
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
      @"airfix.controller-calibration.sensitivity";
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
      @"airfix.controller-calibration.response-curve";
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
  invert.accessibilityIdentifier = @"airfix.controller-calibration.invert";
  [invert addTarget:self
                action:@selector(invertChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.invertSwitch = invert;
  UIStackView *invertRow =
      makeSettingRow(NSLocalizedString(@"Invert", nil), invert);
  _axisRows[static_cast<NSUInteger>(AxisRow::invert)] = invertRow;

  UIButton *resetAxis =
      makeActionButton(NSLocalizedString(@"Reset this axis", nil),
                       @"airfix.controller-calibration.reset-axis");
  [resetAxis addTarget:self
                action:@selector(resetSelectedAxis)
      forControlEvents:UIControlEventTouchUpInside];
  self.resetAxisButton = resetAxis;
  _axisRows[static_cast<NSUInteger>(AxisRow::resetAxis)] = resetAxis;

  UIButton *back = makeActionButton(NSLocalizedString(@"Back to axes", nil),
                                    @"airfix.controller-calibration.back");
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

  UILabel *status =
      makeTextLabel(@"", UIFontTextStyleFootnote, UIColor.secondaryLabelColor);
  status.textAlignment = NSTextAlignmentCenter;
  status.accessibilityIdentifier = @"airfix.controller-calibration.status";
  self.statusLabel = status;

  UIStackView *form = [[UIStackView alloc] initWithArrangedSubviews:@[
    title,
    explanation,
    previewStack,
    overview,
    axisStack,
    status,
  ]];
  form.translatesAutoresizingMaskIntoConstraints = NO;
  form.axis = UILayoutConstraintAxisVertical;
  form.spacing = 14.0;
  form.alignment = UIStackViewAlignmentFill;

  UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:CGRectZero];
  scroll.translatesAutoresizingMaskIntoConstraints = NO;
  scroll.alwaysBounceVertical = NO;
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
  ]];

  [self refreshControls];
  [self setSelectedRow:0U announce:NO];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  UIView *selected = _screen == CalibrationScreen::overview
                         ? _overviewRows[_selectedRow]
                         : _axisRows[_selectedRow];
  UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification,
                                  selected);
}

- (BOOL)accessibilityPerformEscape {
  if (_screen == CalibrationScreen::axis) {
    [self backToOverview];
  } else {
    [self cancelOrClose];
  }
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
        @"Controller calibration cannot be saved on this installation.", nil);
    [self refreshControls];
    return;
  }
  _model->setPersistenceAvailable(coordinator.persistenceAvailable == YES);
  _activeTicket = _model->beginSave();
  if (!_activeTicket.has_value()) {
    self.statusLabel.text =
        _model->persistenceAvailable()
            ? NSLocalizedString(@"No controller calibration changes to save.",
                                nil)
            : NSLocalizedString(
                  @"Controller calibration cannot be saved on this "
                  @"installation.",
                  nil);
    [self refreshControls];
    return;
  }

  self.statusLabel.text =
      NSLocalizedString(@"Saving controller calibration...", nil);
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
                      NSString *message = NSLocalizedString(
                          @"Controller calibration was not saved. Try again.",
                          nil);
                      if (result ==
                          AirfixControllerInputProfileSaveResultInvalidCandidate) {
                        message = NSLocalizedString(
                            @"The selected controller calibration is invalid.",
                            nil);
                      } else if (
                          result ==
                          AirfixControllerInputProfileSaveResultPersistenceUnavailable) {
                        strongSelf->_model->setPersistenceAvailable(false);
                        message = NSLocalizedString(
                            @"Controller calibration cannot be saved on this "
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
  _screen = CalibrationScreen::overview;
  self.axisStack.hidden = YES;
  self.overviewStack.hidden = NO;
  self.titleLabel.text = NSLocalizedString(@"Controller calibration", nil);
  self.explanationLabel.text = NSLocalizedString(
      @"Choose an axis to edit. Changes are saved for the next launch.", nil);
  _selectedRow = static_cast<NSUInteger>(_selectedAxis);
  _verticalNavigationLatched = NO;
  _horizontalNavigationLatched = NO;
  [self refreshControls];
  [self setSelectedRow:_selectedRow announce:YES];
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
  id<AirfixControllerCalibrationPanelViewControllerDelegate> delegate =
      self.delegate;
  [delegate controllerCalibrationPanelViewControllerDidFinish:self];
}

- (void)refreshControls {
  if (!_model.has_value()) {
    self.statusLabel.text =
        NSLocalizedString(@"Controller calibration is unavailable.", nil);
    self.saveButton.enabled = NO;
    return;
  }
  const auto *axis = _model->draftAxisCalibration(_selectedAxis);
  if (axis == nullptr) {
    self.statusLabel.text =
        NSLocalizedString(@"Controller calibration is unavailable.", nil);
    self.saveButton.enabled = NO;
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
  self.backButton.enabled = !saving;
  self.saveButton.enabled = _model->canSave();
  [self.closeButton
      setTitle:(_model->dirty() ? NSLocalizedString(@"Cancel", nil)
                                : NSLocalizedString(@"Close", nil))
      forState:UIControlStateNormal];
  if (!_model->persistenceAvailable() && self.statusLabel.text.length == 0U) {
    self.statusLabel.text = NSLocalizedString(
        @"Controller calibration cannot be saved on this installation.", nil);
  }
  [self refreshPreview];
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

- (void)setSelectedRow:(NSUInteger)row announce:(BOOL)announce {
  if (row >= kRowCount) {
    return;
  }
  _selectedRow = row;
  for (NSUInteger index = 0U; index < kRowCount; ++index) {
    UIView *view = _screen == CalibrationScreen::overview ? _overviewRows[index]
                                                          : _axisRows[index];
    const BOOL active = index == row;
    view.layer.borderColor = (active ? UIColor.systemYellowColor
                                     : [UIColor colorWithWhite:1.0 alpha:0.18])
                                 .CGColor;
    view.layer.borderWidth = active ? 2.5 : 1.0;
    view.layer.cornerRadius = 10.0;
  }
  if (announce) {
    UIView *selected = _screen == CalibrationScreen::overview
                           ? _overviewRows[row]
                           : _axisRows[row];
    UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification,
                                    selected);
  }
}

- (void)moveSelection:(NSInteger)direction {
  const NSInteger next = std::clamp(
      static_cast<NSInteger>(_selectedRow) + direction,
      static_cast<NSInteger>(0), static_cast<NSInteger>(kRowCount - 1U));
  [self setSelectedRow:static_cast<NSUInteger>(next) announce:YES];
}

- (void)adjustSelectedValue:(NSInteger)direction {
  if (!_model.has_value() || direction == 0 ||
      _model->phase() ==
          airfix::settings::ControllerInputProfileMenuPhase::saving ||
      _screen != CalibrationScreen::axis) {
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
  if (_screen == CalibrationScreen::overview) {
    switch (static_cast<OverviewRow>(_selectedRow)) {
    case OverviewRow::leftStickX:
    case OverviewRow::leftStickY:
    case OverviewRow::rightStickX:
    case OverviewRow::rightStickY:
      [self showAxis:static_cast<airfix::input::ControllerAxisElement>(
                         _selectedRow)];
      return;
    case OverviewRow::resetAll:
      [self resetAllAxes];
      return;
    case OverviewRow::save:
      [self saveProfile];
      return;
    case OverviewRow::close:
      [self cancelOrClose];
      return;
    }
  }

  switch (static_cast<AxisRow>(_selectedRow)) {
  case AxisRow::innerDeadzone:
  case AxisRow::outerSaturation:
  case AxisRow::sensitivity:
  case AxisRow::responseCurve:
    [self adjustSelectedValue:1];
    break;
  case AxisRow::invert:
    if (_model.has_value()) {
      const auto *axis = _model->draftAxisCalibration(_selectedAxis);
      if (axis != nullptr) {
        const auto result =
            _model->setInverted(_selectedAxis, axis->inverted == 0U);
        if (result.accepted()) {
          [self updateStatusAfterDraftEdit];
        }
        [self refreshControls];
      }
    }
    break;
  case AxisRow::resetAxis:
    [self resetSelectedAxis];
    break;
  case AxisRow::back:
    [self backToOverview];
    break;
  }
}

- (void)showAxis:(airfix::input::ControllerAxisElement)axis {
  if (axis >= airfix::input::ControllerAxisElement::count ||
      _activeTicket.has_value()) {
    return;
  }
  _selectedAxis = axis;
  _screen = CalibrationScreen::axis;
  self.overviewStack.hidden = YES;
  self.axisStack.hidden = NO;
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
  NSAssert(NSThread.isMainThread,
           @"Controller-calibration input belongs to main");
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
    if (_screen == CalibrationScreen::axis) {
      [self backToOverview];
    } else {
      [self cancelOrClose];
    }
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
  if (!movedVertically && !_horizontalNavigationLatched &&
      input.navigationX >= kNavigationActuation) {
    _horizontalNavigationLatched = YES;
    [self adjustSelectedValue:1];
  } else if (!movedVertically && !_horizontalNavigationLatched &&
             input.navigationX <= -kNavigationActuation) {
    _horizontalNavigationLatched = YES;
    [self adjustSelectedValue:-1];
  }

  if (input.tabPreviousPressed) {
    [self adjustSelectedValue:-1];
  }
  if (input.tabNextPressed) {
    [self adjustSelectedValue:1];
  }
  if (input.confirmPressed) {
    [self activateSelectedRow];
  }
}

@end
