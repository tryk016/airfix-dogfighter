#import "AirfixTouchControlsSettingsPanelViewController.h"

#import "AirfixIOSInputCoordinator.h"
#import "AirfixTouchControlsPreferencesCoordinator.h"

#include "airfix/input/InputFrame.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr int16_t kNavigationActuation =
    airfix::input::uiNavigationActuationQ15;
constexpr int16_t kNavigationRelease = airfix::input::uiNavigationReleaseQ15;
constexpr float kOpacityStep = 5.0F;

typedef NS_ENUM(NSUInteger, AirfixTouchSettingsRow) {
  AirfixTouchSettingsRowHandedness = 0U,
  AirfixTouchSettingsRowDensity,
  AirfixTouchSettingsRowOpacity,
  AirfixTouchSettingsRowVisibility,
  AirfixTouchSettingsRowReset,
  AirfixTouchSettingsRowSave,
  AirfixTouchSettingsRowClose,
  AirfixTouchSettingsRowCount,
};

[[nodiscard]] BOOL magnitudeAtMost(const int16_t value,
                                   const int16_t limit) noexcept {
  const auto wide = static_cast<int32_t>(value);
  const auto magnitude = wide < 0 ? -wide : wide;
  return magnitude <= static_cast<int32_t>(limit);
}

[[nodiscard]] UILabel *makeLabel(NSString *text, UIFontTextStyle style,
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

[[nodiscard]] UIStackView *makeRow(NSString *title, UIView *control) {
  UILabel *label = makeLabel(title, UIFontTextStyleBody, UIColor.labelColor);
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

} // namespace

@interface AirfixTouchControlsSettingsPanelViewController () {
  __weak AirfixTouchControlsPreferencesCoordinator *_coordinator;
  airfix::input::TouchControlsPreferences _draft;
  __strong UIView *_focusRows[AirfixTouchSettingsRowCount];
  AirfixTouchSettingsRow _selectedRow;
  BOOL _verticalNavigationLatched;
  BOOL _horizontalNavigationLatched;
}

@property(nonatomic, strong) UISegmentedControl *handednessControl;
@property(nonatomic, strong) UISegmentedControl *densityControl;
@property(nonatomic, strong) UISlider *opacitySlider;
@property(nonatomic, strong) UILabel *opacityValueLabel;
@property(nonatomic, strong) UISegmentedControl *visibilityControl;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UIButton *resetButton;
@property(nonatomic, strong) UIButton *saveButton;
@property(nonatomic, strong) UIButton *closeButton;

- (void)refreshControls;
- (void)setSelectedRow:(AirfixTouchSettingsRow)row announce:(BOOL)announce;
- (void)moveSelection:(NSInteger)direction;
- (void)adjustSelectedValue:(NSInteger)direction;
- (void)activateSelectedRow;
- (void)saveAndApply;
- (void)resetDefaults;
- (void)closePanel;

@end

@implementation AirfixTouchControlsSettingsPanelViewController

- (instancetype)initWithCoordinator:
    (AirfixTouchControlsPreferencesCoordinator *)coordinator {
  NSParameterAssert(coordinator != nil);
  self = [super initWithNibName:nil bundle:nil];
  if (self != nil) {
    _coordinator = coordinator;
    _draft = [coordinator activePreferences];
    _selectedRow = AirfixTouchSettingsRowHandedness;
    self.modalPresentationStyle = UIModalPresentationOverFullScreen;
  }
  return self;
}

- (void)loadView {
  UIView *root = [[UIView alloc] initWithFrame:CGRectZero];
  root.backgroundColor = [UIColor colorWithWhite:0.025 alpha:0.96];
  root.accessibilityViewIsModal = YES;
  self.view = root;

  UILabel *title = makeLabel(NSLocalizedString(@"Touch controls", nil),
                             UIFontTextStyleTitle1, UIColor.labelColor);
  title.textAlignment = NSTextAlignmentCenter;
  title.accessibilityTraits |= UIAccessibilityTraitHeader;
  title.accessibilityIdentifier = @"airfix.settings.touch.title";

  UILabel *explanation = makeLabel(
      NSLocalizedString(
          @"Choose the side and density of the flight overlay. Overlay "
           "strength changes resting backgrounds only; labels, borders, "
           "active feedback and touch targets stay clear. Auto-hide removes "
           "the overlay while a controller is connected.",
          nil),
      UIFontTextStyleFootnote, UIColor.secondaryLabelColor);
  explanation.textAlignment = NSTextAlignmentCenter;

  UISegmentedControl *handedness = [[UISegmentedControl alloc] initWithItems:@[
    NSLocalizedString(@"Right-handed", nil),
    NSLocalizedString(@"Left-handed", nil),
  ]];
  handedness.translatesAutoresizingMaskIntoConstraints = NO;
  handedness.accessibilityIdentifier = @"airfix.settings.touch.handedness";
  [handedness addTarget:self
                 action:@selector(handednessChanged:)
       forControlEvents:UIControlEventValueChanged];
  self.handednessControl = handedness;
  UIStackView *handednessRow =
      makeRow(NSLocalizedString(@"Layout", nil), handedness);

  UISegmentedControl *density = [[UISegmentedControl alloc] initWithItems:@[
    NSLocalizedString(@"Automatic", nil),
    NSLocalizedString(@"Compact", nil),
  ]];
  density.translatesAutoresizingMaskIntoConstraints = NO;
  density.accessibilityIdentifier = @"airfix.settings.touch.density";
  [density addTarget:self
                action:@selector(densityChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.densityControl = density;
  UIStackView *densityRow =
      makeRow(NSLocalizedString(@"Control size", nil), density);

  UISlider *opacity = [[UISlider alloc] initWithFrame:CGRectZero];
  opacity.translatesAutoresizingMaskIntoConstraints = NO;
  opacity.minimumValue =
      airfix::input::minimumTouchControlsRestingOpacityPercent;
  opacity.maximumValue =
      airfix::input::maximumTouchControlsRestingOpacityPercent;
  opacity.continuous = YES;
  opacity.accessibilityLabel = NSLocalizedString(@"Overlay strength", nil);
  opacity.accessibilityHint = NSLocalizedString(
      @"Adjusts the resting background opacity of touch controls.", nil);
  opacity.accessibilityIdentifier = @"airfix.settings.touch.opacity";
  [opacity addTarget:self
                action:@selector(opacityChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.opacitySlider = opacity;

  UILabel *opacityValue =
      makeLabel(@"100%", UIFontTextStyleBody, UIColor.secondaryLabelColor);
  opacityValue.font = [UIFont
      monospacedDigitSystemFontOfSize:
          [UIFont preferredFontForTextStyle:UIFontTextStyleBody].pointSize
                               weight:UIFontWeightSemibold];
  [opacityValue setContentHuggingPriority:UILayoutPriorityRequired
                                  forAxis:UILayoutConstraintAxisHorizontal];
  self.opacityValueLabel = opacityValue;
  UIStackView *opacityControl =
      [[UIStackView alloc] initWithArrangedSubviews:@[ opacity, opacityValue ]];
  opacityControl.translatesAutoresizingMaskIntoConstraints = NO;
  opacityControl.axis = UILayoutConstraintAxisHorizontal;
  opacityControl.alignment = UIStackViewAlignmentCenter;
  opacityControl.spacing = 10.0;
  UIStackView *opacityRow =
      makeRow(NSLocalizedString(@"Overlay strength", nil), opacityControl);

  UISegmentedControl *visibility = [[UISegmentedControl alloc] initWithItems:@[
    NSLocalizedString(@"Auto-hide", nil),
    NSLocalizedString(@"Always visible", nil),
  ]];
  visibility.translatesAutoresizingMaskIntoConstraints = NO;
  visibility.accessibilityLabel =
      NSLocalizedString(@"Touch overlay with controller", nil);
  visibility.accessibilityHint = NSLocalizedString(
      @"Choose whether touch controls hide while a controller is connected.",
      nil);
  visibility.accessibilityIdentifier = @"airfix.settings.touch.visibility";
  [visibility addTarget:self
                 action:@selector(visibilityChanged:)
       forControlEvents:UIControlEventValueChanged];
  self.visibilityControl = visibility;
  UIStackView *visibilityRow =
      makeRow(NSLocalizedString(@"With controller", nil), visibility);

  UIButton *reset = [UIButton buttonWithType:UIButtonTypeSystem];
  [reset setTitle:NSLocalizedString(@"Reset defaults", nil)
         forState:UIControlStateNormal];
  reset.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  reset.accessibilityIdentifier = @"airfix.settings.touch.reset";
  [reset addTarget:self
                action:@selector(resetDefaults)
      forControlEvents:UIControlEventTouchUpInside];
  self.resetButton = reset;

  UIButton *save = [UIButton buttonWithType:UIButtonTypeSystem];
  [save setTitle:NSLocalizedString(@"Save and apply", nil)
        forState:UIControlStateNormal];
  save.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  save.accessibilityIdentifier = @"airfix.settings.touch.save";
  [save addTarget:self
                action:@selector(saveAndApply)
      forControlEvents:UIControlEventTouchUpInside];
  self.saveButton = save;

  UIButton *close = [UIButton buttonWithType:UIButtonTypeSystem];
  [close setTitle:NSLocalizedString(@"Close", nil)
         forState:UIControlStateNormal];
  close.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  close.accessibilityIdentifier = @"airfix.settings.touch.close";
  [close addTarget:self
                action:@selector(closePanel)
      forControlEvents:UIControlEventTouchUpInside];
  self.closeButton = close;

  UIStackView *actions =
      [[UIStackView alloc] initWithArrangedSubviews:@[ reset, save, close ]];
  actions.translatesAutoresizingMaskIntoConstraints = NO;
  actions.axis = UILayoutConstraintAxisHorizontal;
  actions.distribution = UIStackViewDistributionFillEqually;
  actions.spacing = 12.0;

  UILabel *status =
      makeLabel(@"", UIFontTextStyleFootnote, UIColor.secondaryLabelColor);
  status.textAlignment = NSTextAlignmentCenter;
  status.accessibilityIdentifier = @"airfix.settings.touch.status";
  self.statusLabel = status;

  UIStackView *form = [[UIStackView alloc] initWithArrangedSubviews:@[
    title,
    explanation,
    handednessRow,
    densityRow,
    opacityRow,
    visibilityRow,
    actions,
    status,
  ]];
  form.translatesAutoresizingMaskIntoConstraints = NO;
  form.axis = UILayoutConstraintAxisVertical;
  form.spacing = 14.0;

  UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:CGRectZero];
  scroll.translatesAutoresizingMaskIntoConstraints = NO;
  [root addSubview:scroll];
  [scroll addSubview:form];
  UILayoutGuide *safe = root.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [scroll.topAnchor constraintEqualToAnchor:safe.topAnchor],
    [scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],
    [scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
    [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
    [form.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor
                                   constant:20.0],
    [form.bottomAnchor
        constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor
                       constant:-20.0],
    [form.leadingAnchor
        constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor
                       constant:24.0],
    [form.trailingAnchor
        constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor
                       constant:-24.0],
    [form.widthAnchor
        constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor
                       constant:-48.0],
    [opacity.widthAnchor constraintGreaterThanOrEqualToConstant:180.0],
  ]];

  _focusRows[AirfixTouchSettingsRowHandedness] = handednessRow;
  _focusRows[AirfixTouchSettingsRowDensity] = densityRow;
  _focusRows[AirfixTouchSettingsRowOpacity] = opacityRow;
  _focusRows[AirfixTouchSettingsRowVisibility] = visibilityRow;
  _focusRows[AirfixTouchSettingsRowReset] = reset;
  _focusRows[AirfixTouchSettingsRowSave] = save;
  _focusRows[AirfixTouchSettingsRowClose] = close;
  [self refreshControls];
  [self setSelectedRow:_selectedRow announce:NO];
}

- (void)handednessChanged:(UISegmentedControl *)sender {
  _draft.layout.handedness =
      sender.selectedSegmentIndex == 1
          ? airfix::input::TouchControlsHandedness::leftHanded
          : airfix::input::TouchControlsHandedness::rightHanded;
  [self refreshControls];
}

- (void)densityChanged:(UISegmentedControl *)sender {
  _draft.layout.density = sender.selectedSegmentIndex == 1
                              ? airfix::input::TouchControlsDensity::compact
                              : airfix::input::TouchControlsDensity::automatic;
  [self refreshControls];
}

- (void)opacityChanged:(UISlider *)sender {
  const float rounded = std::round(sender.value / kOpacityStep) * kOpacityStep;
  _draft.restingOpacityPercent = static_cast<std::uint8_t>(std::clamp(
      rounded,
      static_cast<float>(
          airfix::input::minimumTouchControlsRestingOpacityPercent),
      static_cast<float>(
          airfix::input::maximumTouchControlsRestingOpacityPercent)));
  [self refreshControls];
}

- (void)visibilityChanged:(UISegmentedControl *)sender {
  _draft.visibilityMode =
      sender.selectedSegmentIndex == 1
          ? airfix::input::TouchControlsVisibilityMode::alwaysVisible
          : airfix::input::TouchControlsVisibilityMode::automaticControllerHide;
  [self refreshControls];
}

- (void)refreshControls {
  self.handednessControl.selectedSegmentIndex =
      _draft.layout.handedness ==
              airfix::input::TouchControlsHandedness::leftHanded
          ? 1
          : 0;
  self.densityControl.selectedSegmentIndex =
      _draft.layout.density == airfix::input::TouchControlsDensity::compact ? 1
                                                                            : 0;
  self.opacitySlider.value = _draft.restingOpacityPercent;
  self.opacityValueLabel.text =
      [NSString stringWithFormat:@"%u%%", _draft.restingOpacityPercent];
  self.visibilityControl.selectedSegmentIndex =
      _draft.visibilityMode ==
              airfix::input::TouchControlsVisibilityMode::alwaysVisible
          ? 1
          : 0;
  AirfixTouchControlsPreferencesCoordinator *coordinator = _coordinator;
  const BOOL available = coordinator != nil &&
                         coordinator.persistenceAvailable &&
                         !coordinator.isSaving;
  self.saveButton.enabled = available;
  self.resetButton.enabled = coordinator != nil && !coordinator.isSaving;
  if (coordinator != nil && !coordinator.persistenceAvailable) {
    self.statusLabel.text = NSLocalizedString(
        @"Touch settings are read-only; current safe defaults remain active.",
        nil);
  } else if (coordinator != nil && coordinator.repairRequired) {
    self.statusLabel.text = NSLocalizedString(
        @"Recovered settings can be repaired with Save and apply.", nil);
  }
}

- (void)resetDefaults {
  _draft = {};
  [self refreshControls];
}

- (void)saveAndApply {
  AirfixTouchControlsPreferencesCoordinator *coordinator = _coordinator;
  if (coordinator == nil || coordinator.isSaving) {
    return;
  }
  self.statusLabel.text = NSLocalizedString(@"Saving...", nil);
  [self refreshControls];
  __weak AirfixTouchControlsSettingsPanelViewController *weakSelf = self;
  [coordinator
      requestPreferences:_draft
              completion:^(AirfixTouchControlsPreferencesSaveResult result) {
                AirfixTouchControlsSettingsPanelViewController *strongSelf =
                    weakSelf;
                if (strongSelf == nil) {
                  return;
                }
                switch (result) {
                case AirfixTouchControlsPreferencesSaveResultSaved:
                  strongSelf.statusLabel.text =
                      NSLocalizedString(@"Saved and applied.", nil);
                  break;
                case AirfixTouchControlsPreferencesSaveResultInvalidCandidate:
                  strongSelf.statusLabel.text =
                      NSLocalizedString(@"These settings are invalid.", nil);
                  break;
                case AirfixTouchControlsPreferencesSaveResultPersistenceUnavailable:
                  strongSelf.statusLabel.text = NSLocalizedString(
                      @"Touch settings storage is unavailable.", nil);
                  break;
                case AirfixTouchControlsPreferencesSaveResultSaveFailed:
                  strongSelf.statusLabel.text = NSLocalizedString(
                      @"Touch settings could not be saved.", nil);
                  break;
                case AirfixTouchControlsPreferencesSaveResultBusy:
                  strongSelf.statusLabel.text = NSLocalizedString(
                      @"Touch settings are still saving.", nil);
                  break;
                }
                [strongSelf refreshControls];
              }];
  [self refreshControls];
}

- (void)closePanel {
  if (_coordinator.isSaving) {
    return;
  }
  [self.delegate touchControlsSettingsPanelViewControllerDidFinish:self];
}

- (void)setSelectedRow:(AirfixTouchSettingsRow)row announce:(BOOL)announce {
  _selectedRow = static_cast<AirfixTouchSettingsRow>(
      std::min<NSUInteger>(row, AirfixTouchSettingsRowCount - 1U));
  for (NSUInteger index = 0U; index < AirfixTouchSettingsRowCount; ++index) {
    UIView *view = _focusRows[index];
    const BOOL active = index == _selectedRow;
    view.layer.borderColor =
        (active ? UIColor.systemYellowColor : UIColor.clearColor).CGColor;
    view.layer.borderWidth = active ? 2.5 : 0.0;
    view.layer.cornerRadius = 10.0;
  }
  if (announce) {
    UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification,
                                    _focusRows[_selectedRow]);
  }
}

- (void)moveSelection:(NSInteger)direction {
  const NSInteger count = AirfixTouchSettingsRowCount;
  NSInteger next = static_cast<NSInteger>(_selectedRow) + direction;
  next = (next % count + count) % count;
  [self setSelectedRow:static_cast<AirfixTouchSettingsRow>(next) announce:YES];
}

- (void)adjustSelectedValue:(NSInteger)direction {
  switch (_selectedRow) {
  case AirfixTouchSettingsRowHandedness:
    self.handednessControl.selectedSegmentIndex = direction < 0 ? 0 : 1;
    [self handednessChanged:self.handednessControl];
    break;
  case AirfixTouchSettingsRowDensity:
    self.densityControl.selectedSegmentIndex = direction < 0 ? 0 : 1;
    [self densityChanged:self.densityControl];
    break;
  case AirfixTouchSettingsRowOpacity:
    self.opacitySlider.value += direction < 0 ? -kOpacityStep : kOpacityStep;
    [self opacityChanged:self.opacitySlider];
    break;
  case AirfixTouchSettingsRowVisibility:
    self.visibilityControl.selectedSegmentIndex = direction < 0 ? 0 : 1;
    [self visibilityChanged:self.visibilityControl];
    break;
  case AirfixTouchSettingsRowReset:
  case AirfixTouchSettingsRowSave:
  case AirfixTouchSettingsRowClose:
  case AirfixTouchSettingsRowCount:
    break;
  }
}

- (void)activateSelectedRow {
  switch (_selectedRow) {
  case AirfixTouchSettingsRowHandedness:
  case AirfixTouchSettingsRowDensity:
  case AirfixTouchSettingsRowVisibility:
    [self adjustSelectedValue:1];
    break;
  case AirfixTouchSettingsRowOpacity:
    break;
  case AirfixTouchSettingsRowReset:
    [self resetDefaults];
    break;
  case AirfixTouchSettingsRowSave:
    [self saveAndApply];
    break;
  case AirfixTouchSettingsRowClose:
    [self closePanel];
    break;
  case AirfixTouchSettingsRowCount:
    break;
  }
}

- (void)consumeUIInputSnapshot:(AirfixUIInputSnapshot *)input {
  NSAssert(NSThread.isMainThread, @"Touch-settings input belongs to main");
  if (input == nil) {
    return;
  }
  if (input.cancelPressed) {
    [self closePanel];
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
