#import "AirfixRenderSettingsPanelViewController.h"

#import "AirfixIOSInputCoordinator.h"
#import "AirfixRenderSettingsCoordinator.h"

#include "airfix/input/InputFrame.hpp"
#include "airfix/settings/RenderPresentationSettingsMenuModel.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

constexpr int16_t kNavigationActuation =
    airfix::input::uiNavigationActuationQ15;
constexpr int16_t kNavigationRelease = airfix::input::uiNavigationReleaseQ15;
constexpr float kControllerScaleStep = 5.0F;
constexpr float kControllerFovStep = 1.0F;

enum class SettingsRow : NSUInteger {
  renderScale = 0U,
  presentation = 1U,
  verticalFovAdjustment = 2U,
  visualProfile = 3U,
  diagnostics = 4U,
  apply = 5U,
  cancel = 6U,
  count = 7U,
};

inline constexpr NSUInteger kSettingsRowCount =
    static_cast<NSUInteger>(SettingsRow::count);

[[nodiscard]] constexpr NSUInteger rowIndex(const SettingsRow row) noexcept {
  return static_cast<NSUInteger>(row);
}

[[nodiscard]] BOOL magnitudeAtMost(const int16_t value,
                                   const int16_t limit) noexcept {
  const auto wide = static_cast<int32_t>(value);
  const auto magnitude = wide < 0 ? -wide : wide;
  return magnitude <= static_cast<int32_t>(limit);
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
  row.distribution = UIStackViewDistributionFill;
  row.spacing = 16.0;
  row.layoutMargins = UIEdgeInsetsMake(10.0, 12.0, 10.0, 12.0);
  row.layoutMarginsRelativeArrangement = YES;
  row.layer.cornerRadius = 10.0;
  row.layer.borderWidth = 1.0;
  return row;
}

} // namespace

@interface AirfixRenderSettingsPanelViewController () {
  __weak AirfixRenderSettingsCoordinator *_coordinator;
  std::optional<airfix::settings::RenderPresentationSettingsMenuModel> _model;
  std::optional<airfix::settings::RenderPresentationSettingsMenuApplyTicket>
      _activeTicket;
  __strong UIView *_focusRows[kSettingsRowCount];
  SettingsRow _selectedRow;
  BOOL _verticalNavigationLatched;
  BOOL _horizontalNavigationLatched;
}

@property(nonatomic, strong) UISlider *renderScaleSlider;
@property(nonatomic, strong) UILabel *renderScaleValueLabel;
@property(nonatomic, strong) UISegmentedControl *presentationControl;
@property(nonatomic, strong) UISlider *verticalFovSlider;
@property(nonatomic, strong) UILabel *verticalFovValueLabel;
@property(nonatomic, strong) UISegmentedControl *visualProfileControl;
@property(nonatomic, strong) UISwitch *diagnosticsSwitch;
@property(nonatomic, strong) UIButton *applyButton;
@property(nonatomic, strong) UIButton *cancelButton;
@property(nonatomic, strong) UILabel *statusLabel;

- (void)renderScaleChanged:(UISlider *)sender;
- (void)presentationChanged:(UISegmentedControl *)sender;
- (void)verticalFovChanged:(UISlider *)sender;
- (void)visualProfileChanged:(UISegmentedControl *)sender;
- (void)diagnosticsChanged:(UISwitch *)sender;
- (void)applySettings;
- (void)cancelOrClose;
- (void)refreshControls;
- (void)setSelectedRow:(SettingsRow)row announce:(BOOL)announce;
- (void)moveSelection:(NSInteger)direction;
- (void)adjustSelectedValue:(NSInteger)direction;
- (void)activateSelectedRow;
- (void)finishWithMessage:(NSString *)message;

@end

@implementation AirfixRenderSettingsPanelViewController

- (instancetype)initWithCoordinator:
    (AirfixRenderSettingsCoordinator *)coordinator {
  NSParameterAssert(coordinator != nil);
  self = [super initWithNibName:nil bundle:nil];
  if (self != nil) {
    _coordinator = coordinator;
    _model = airfix::settings::RenderPresentationSettingsMenuModel::create(
        [coordinator activeSettings],
        { .persistenceAvailable = coordinator.persistenceAvailable == YES, });
    _selectedRow = SettingsRow::renderScale;
    self.modalPresentationStyle = UIModalPresentationOverFullScreen;
  }
  return self;
}

- (void)loadView {
  UIView *root = [[UIView alloc] initWithFrame:CGRectZero];
  root.backgroundColor = [UIColor colorWithWhite:0.025 alpha:0.96];
  root.accessibilityViewIsModal = YES;
  self.view = root;

  UILabel *title = makeTextLabel(NSLocalizedString(@"Display settings", nil),
                                 UIFontTextStyleTitle1, UIColor.labelColor);
  title.textAlignment = NSTextAlignmentCenter;
  title.accessibilityTraits |= UIAccessibilityTraitHeader;
  title.accessibilityIdentifier = @"airfix.settings.display.title";

  UILabel *explanation = makeTextLabel(
      NSLocalizedString(
          @"These settings change presentation only. Gameplay, physics, "
          @"and the loaded mission are not reloaded.",
          nil),
      UIFontTextStyleFootnote, UIColor.secondaryLabelColor);
  explanation.textAlignment = NSTextAlignmentCenter;

  UISlider *slider = [[UISlider alloc] initWithFrame:CGRectZero];
  slider.translatesAutoresizingMaskIntoConstraints = NO;
  slider.minimumValue = 50.0F;
  slider.maximumValue = 200.0F;
  slider.continuous = YES;
  slider.accessibilityLabel = NSLocalizedString(@"3D render scale", nil);
  slider.accessibilityHint = NSLocalizedString(
      @"Adjusts scene resolution independently from interface resolution.",
      nil);
  slider.accessibilityIdentifier = @"airfix.settings.render-scale";
  [slider addTarget:self
                action:@selector(renderScaleChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.renderScaleSlider = slider;

  UILabel *scaleValue =
      makeTextLabel(@"100%", UIFontTextStyleBody, UIColor.secondaryLabelColor);
  scaleValue.textAlignment = NSTextAlignmentRight;
  UIFont *bodyFont = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
  scaleValue.font =
      [UIFont monospacedDigitSystemFontOfSize:bodyFont.pointSize
                                       weight:UIFontWeightSemibold];
  scaleValue.adjustsFontForContentSizeCategory = YES;
  [scaleValue setContentHuggingPriority:UILayoutPriorityRequired
                                forAxis:UILayoutConstraintAxisHorizontal];
  self.renderScaleValueLabel = scaleValue;

  UIStackView *scaleControl = [[UIStackView alloc] initWithArrangedSubviews:@[
    slider,
    scaleValue,
  ]];
  scaleControl.translatesAutoresizingMaskIntoConstraints = NO;
  scaleControl.axis = UILayoutConstraintAxisHorizontal;
  scaleControl.alignment = UIStackViewAlignmentCenter;
  scaleControl.spacing = 10.0;
  UIStackView *scaleRow =
      makeSettingRow(NSLocalizedString(@"Render scale", nil), scaleControl);

  UISegmentedControl *presentation =
      [[UISegmentedControl alloc] initWithItems:@[
        NSLocalizedString(@"Widescreen Hor+", nil),
        NSLocalizedString(@"Original 4:3", nil),
      ]];
  presentation.translatesAutoresizingMaskIntoConstraints = NO;
  presentation.accessibilityLabel =
      NSLocalizedString(@"Scene presentation", nil);
  presentation.accessibilityHint = NSLocalizedString(
      @"Widescreen expands horizontal field of view. Original keeps a "
      @"four-by-three comparison viewport.",
      nil);
  presentation.accessibilityIdentifier = @"airfix.settings.presentation";
  [presentation addTarget:self
                   action:@selector(presentationChanged:)
         forControlEvents:UIControlEventValueChanged];
  self.presentationControl = presentation;
  UIStackView *presentationRow =
      makeSettingRow(NSLocalizedString(@"Presentation", nil), presentation);

  UISlider *verticalFovSlider =
      [[UISlider alloc] initWithFrame:CGRectZero];
  verticalFovSlider.translatesAutoresizingMaskIntoConstraints = NO;
  verticalFovSlider.minimumValue =
      airfix::render::native_render_policy::
          minimumVerticalFovAdjustmentDegrees;
  verticalFovSlider.maximumValue =
      airfix::render::native_render_policy::
          maximumVerticalFovAdjustmentDegrees;
  verticalFovSlider.continuous = YES;
  verticalFovSlider.accessibilityLabel =
      NSLocalizedString(@"Vertical field of view increase", nil);
  verticalFovSlider.accessibilityHint = NSLocalizedString(
      @"Widens the camera vertically without changing gameplay or "
      @"widescreen aspect correction.",
      nil);
  verticalFovSlider.accessibilityIdentifier =
      @"airfix.settings.vertical-fov-adjustment";
  [verticalFovSlider addTarget:self
                        action:@selector(verticalFovChanged:)
              forControlEvents:UIControlEventValueChanged];
  self.verticalFovSlider = verticalFovSlider;

  UILabel *verticalFovValue =
      makeTextLabel(@"+0 deg", UIFontTextStyleBody,
                    UIColor.secondaryLabelColor);
  verticalFovValue.textAlignment = NSTextAlignmentRight;
  verticalFovValue.font =
      [UIFont monospacedDigitSystemFontOfSize:bodyFont.pointSize
                                       weight:UIFontWeightSemibold];
  verticalFovValue.adjustsFontForContentSizeCategory = YES;
  [verticalFovValue
      setContentHuggingPriority:UILayoutPriorityRequired
                        forAxis:UILayoutConstraintAxisHorizontal];
  self.verticalFovValueLabel = verticalFovValue;

  UIStackView *verticalFovControl =
      [[UIStackView alloc] initWithArrangedSubviews:@[
        verticalFovSlider,
        verticalFovValue,
      ]];
  verticalFovControl.translatesAutoresizingMaskIntoConstraints = NO;
  verticalFovControl.axis = UILayoutConstraintAxisHorizontal;
  verticalFovControl.alignment = UIStackViewAlignmentCenter;
  verticalFovControl.spacing = 10.0;
  UIStackView *verticalFovRow = makeSettingRow(
      NSLocalizedString(@"Vertical FOV increase", nil),
      verticalFovControl);

  UISegmentedControl *profile = [[UISegmentedControl alloc] initWithItems:@[
    NSLocalizedString(@"Classic", nil),
    NSLocalizedString(@"Enhanced (preview)", nil),
  ]];
  profile.translatesAutoresizingMaskIntoConstraints = NO;
  profile.accessibilityLabel = NSLocalizedString(@"Visual profile", nil);
  profile.accessibilityHint = NSLocalizedString(
      @"Enhanced currently records visual intent and does not yet imply "
      @"that every planned effect is implemented.",
      nil);
  profile.accessibilityIdentifier = @"airfix.settings.visual-profile";
  [profile addTarget:self
                action:@selector(visualProfileChanged:)
      forControlEvents:UIControlEventValueChanged];
  self.visualProfileControl = profile;
  UIStackView *profileRow =
      makeSettingRow(NSLocalizedString(@"Visual profile", nil), profile);

  UISwitch *diagnosticsSwitch = [[UISwitch alloc] initWithFrame:CGRectZero];
  diagnosticsSwitch.translatesAutoresizingMaskIntoConstraints = NO;
  diagnosticsSwitch.accessibilityLabel =
      NSLocalizedString(@"Renderer statistics overlay", nil);
  diagnosticsSwitch.accessibilityHint = NSLocalizedString(
      @"Shows resolution, performance, draw, light, and GPU memory data.", nil);
  diagnosticsSwitch.accessibilityIdentifier =
      @"airfix.settings.render-diagnostics";
  [diagnosticsSwitch addTarget:self
                        action:@selector(diagnosticsChanged:)
              forControlEvents:UIControlEventValueChanged];
  self.diagnosticsSwitch = diagnosticsSwitch;
  UIStackView *diagnosticsRow = makeSettingRow(
      NSLocalizedString(@"Renderer statistics", nil), diagnosticsSwitch);

  UILabel *previewNote = makeTextLabel(
      NSLocalizedString(
          @"Classic and Enhanced are renderer profiles. They are separate "
          @"from any optional private HD texture package.",
          nil),
      UIFontTextStyleFootnote, UIColor.secondaryLabelColor);

  UIButton *apply = [UIButton buttonWithType:UIButtonTypeSystem];
  apply.translatesAutoresizingMaskIntoConstraints = NO;
  [apply setTitle:NSLocalizedString(@"Apply", nil)
         forState:UIControlStateNormal];
  apply.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  apply.titleLabel.adjustsFontForContentSizeCategory = YES;
  apply.accessibilityIdentifier = @"airfix.settings.apply";
  [apply addTarget:self
                action:@selector(applySettings)
      forControlEvents:UIControlEventTouchUpInside];
  self.applyButton = apply;

  UIButton *cancel = [UIButton buttonWithType:UIButtonTypeSystem];
  cancel.translatesAutoresizingMaskIntoConstraints = NO;
  [cancel setTitle:NSLocalizedString(@"Cancel", nil)
          forState:UIControlStateNormal];
  cancel.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  cancel.titleLabel.adjustsFontForContentSizeCategory = YES;
  cancel.accessibilityIdentifier = @"airfix.settings.cancel";
  [cancel addTarget:self
                action:@selector(cancelOrClose)
      forControlEvents:UIControlEventTouchUpInside];
  self.cancelButton = cancel;

  UILabel *status =
      makeTextLabel(@"", UIFontTextStyleFootnote, UIColor.secondaryLabelColor);
  status.textAlignment = NSTextAlignmentCenter;
  status.accessibilityIdentifier = @"airfix.settings.status";
  self.statusLabel = status;

  _focusRows[rowIndex(SettingsRow::renderScale)] = scaleRow;
  _focusRows[rowIndex(SettingsRow::presentation)] = presentationRow;
  _focusRows[rowIndex(SettingsRow::verticalFovAdjustment)] =
      verticalFovRow;
  _focusRows[rowIndex(SettingsRow::visualProfile)] = profileRow;
  _focusRows[rowIndex(SettingsRow::diagnostics)] = diagnosticsRow;
  _focusRows[rowIndex(SettingsRow::apply)] = apply;
  _focusRows[rowIndex(SettingsRow::cancel)] = cancel;

  UIStackView *actions = [[UIStackView alloc] initWithArrangedSubviews:@[
    cancel,
    apply,
  ]];
  actions.translatesAutoresizingMaskIntoConstraints = NO;
  actions.axis = UILayoutConstraintAxisHorizontal;
  actions.distribution = UIStackViewDistributionFillEqually;
  actions.spacing = 16.0;

  UIStackView *form = [[UIStackView alloc] initWithArrangedSubviews:@[
    title,
    explanation,
    scaleRow,
    presentationRow,
    verticalFovRow,
    profileRow,
    diagnosticsRow,
    previewNote,
    status,
    actions,
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
  NSLayoutConstraint *maximumWidth =
      [form.widthAnchor constraintLessThanOrEqualToConstant:760.0];
  maximumWidth.priority = UILayoutPriorityRequired;
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
    maximumWidth,
    [slider.widthAnchor constraintGreaterThanOrEqualToConstant:150.0],
    [verticalFovSlider.widthAnchor
        constraintGreaterThanOrEqualToConstant:150.0],
    [apply.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
    [cancel.heightAnchor constraintGreaterThanOrEqualToConstant:48.0],
  ]];

  [self refreshControls];
  [self setSelectedRow:SettingsRow::renderScale announce:NO];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification,
                                  _focusRows[rowIndex(_selectedRow)]);
}

- (BOOL)accessibilityPerformEscape {
  [self cancelOrClose];
  return YES;
}

- (void)renderScaleChanged:(UISlider *)sender {
  if (!_model.has_value()) {
    return;
  }
  const float value = std::round(sender.value);
  const auto result = _model->setRenderScalePercent(value);
  if (!result.accepted()) {
    sender.value = _model->draftSettings().renderScalePercent;
  }
  [self refreshControls];
}

- (void)presentationChanged:(UISegmentedControl *)sender {
  if (!_model.has_value()) {
    return;
  }
  const auto value =
      sender.selectedSegmentIndex == 1
          ? airfix::render::ScenePresentationMode::originalFourByThree
          : airfix::render::ScenePresentationMode::widescreenHorPlus;
  (void)_model->setScenePresentation(value);
  [self refreshControls];
}

- (void)verticalFovChanged:(UISlider *)sender {
  if (!_model.has_value()) {
    return;
  }
  const float value = std::round(sender.value);
  const auto result =
      _model->setVerticalFovAdjustmentDegrees(value);
  if (!result.accepted()) {
    sender.value =
        _model->draftSettings().verticalFovAdjustmentDegrees;
  }
  [self refreshControls];
}

- (void)visualProfileChanged:(UISegmentedControl *)sender {
  if (!_model.has_value()) {
    return;
  }
  const auto value = sender.selectedSegmentIndex == 1
                         ? airfix::render::VisualProfile::enhanced
                         : airfix::render::VisualProfile::classic;
  (void)_model->setVisualProfile(value);
  [self refreshControls];
}

- (void)diagnosticsChanged:(UISwitch *)sender {
  if (!_model.has_value()) {
    return;
  }
  (void)_model->setDiagnosticsOverlayEnabled(sender.isOn == YES);
  [self refreshControls];
}

- (void)applySettings {
  if (!_model.has_value() || _activeTicket.has_value()) {
    return;
  }
  AirfixRenderSettingsCoordinator *coordinator = _coordinator;
  if (coordinator == nil) {
    _model->setPersistenceAvailable(false);
    self.statusLabel.text =
        NSLocalizedString(@"Display settings are unavailable.", nil);
    [self refreshControls];
    return;
  }
  _model->setPersistenceAvailable(coordinator.persistenceAvailable == YES);
  _activeTicket = _model->beginApply();
  if (!_activeTicket.has_value()) {
    self.statusLabel.text =
        _model->persistenceAvailable()
            ? NSLocalizedString(@"No display changes to apply.", nil)
            : NSLocalizedString(
                  @"Display settings cannot be saved on this installation.",
                  nil);
    [self refreshControls];
    return;
  }

  self.statusLabel.text =
      NSLocalizedString(@"Applying display settings...", nil);
  UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                  self.statusLabel.text);
  [self refreshControls];

  const auto candidate = _activeTicket->candidate;
  __weak AirfixRenderSettingsPanelViewController *weakSelf = self;
  [coordinator
      requestPersistentSettings:candidate
                     completion:^(
                         const AirfixRenderSettingsApplyResult result) {
                       AirfixRenderSettingsPanelViewController *strongSelf =
                           weakSelf;
                       if (strongSelf == nil ||
                           !strongSelf->_activeTicket.has_value() ||
                           !strongSelf->_model.has_value()) {
                         return;
                       }
                       const auto ticket = *strongSelf->_activeTicket;

                       if (result == AirfixRenderSettingsApplyResultApplied) {
                         if (!strongSelf->_model->finishApplySuccess(ticket)) {
                           return;
                         }
                         strongSelf->_activeTicket.reset();
                         [strongSelf
                             finishWithMessage:NSLocalizedString(
                                                   @"Display settings applied.",
                                                   nil)];
                         return;
                       }

                       if (!strongSelf->_model->finishApplyFailure(ticket)) {
                         return;
                       }
                       strongSelf->_activeTicket.reset();
                       NSString *message = NSLocalizedString(
                           @"Display settings were not changed. Try again.",
                           nil);
                       if (result ==
                           AirfixRenderSettingsApplyResultInvalidCandidate) {
                         message = NSLocalizedString(
                             @"The selected display settings are invalid.",
                             nil);
                       } else if (
                           result ==
                           AirfixRenderSettingsApplyResultPersistenceUnavailable) {
                         strongSelf->_model->setPersistenceAvailable(false);
                         message =
                             NSLocalizedString(@"Display settings cannot be "
                                               @"saved on this installation.",
                                               nil);
                       } else if (result ==
                                  AirfixRenderSettingsApplyResultSuperseded) {
                         message =
                             NSLocalizedString(@"A newer display-settings "
                                               @"request replaced this one.",
                                               nil);
                       }
                       strongSelf.statusLabel.text = message;
                       UIAccessibilityPostNotification(
                           UIAccessibilityAnnouncementNotification, message);
                       [strongSelf refreshControls];
                     }];
}

- (void)cancelOrClose {
  if (_model.has_value() && !_activeTicket.has_value()) {
    (void)_model->cancelDraft();
  }
  id<AirfixRenderSettingsPanelViewControllerDelegate> delegate = self.delegate;
  [delegate renderSettingsPanelViewControllerDidFinish:self];
}

- (void)finishWithMessage:(NSString *)message {
  self.statusLabel.text = message;
  UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                  message);
  id<AirfixRenderSettingsPanelViewControllerDelegate> delegate = self.delegate;
  [delegate renderSettingsPanelViewControllerDidFinish:self];
}

- (void)refreshControls {
  if (!_model.has_value()) {
    self.renderScaleSlider.enabled = NO;
    self.presentationControl.enabled = NO;
    self.verticalFovSlider.enabled = NO;
    self.visualProfileControl.enabled = NO;
    self.diagnosticsSwitch.enabled = NO;
    self.applyButton.enabled = NO;
    self.statusLabel.text =
        NSLocalizedString(@"Display settings are unavailable.", nil);
    return;
  }

  const auto &draft = _model->draftSettings();
  self.renderScaleSlider.value = draft.renderScalePercent;
  self.renderScaleValueLabel.text =
      [NSString stringWithFormat:@"%.0f%%", draft.renderScalePercent];
  self.renderScaleSlider.accessibilityValue = self.renderScaleValueLabel.text;
  self.presentationControl.selectedSegmentIndex =
      draft.scenePresentation ==
              airfix::render::ScenePresentationMode::originalFourByThree
          ? 1
          : 0;
  self.verticalFovSlider.value =
      draft.verticalFovAdjustmentDegrees;
  self.verticalFovValueLabel.text = [NSString
      stringWithFormat:@"+%.0f deg",
                       draft.verticalFovAdjustmentDegrees];
  self.verticalFovSlider.accessibilityValue =
      self.verticalFovValueLabel.text;
  self.visualProfileControl.selectedSegmentIndex =
      draft.visualProfile == airfix::render::VisualProfile::enhanced ? 1 : 0;
  self.diagnosticsSwitch.on = draft.diagnosticsOverlayEnabled;

  const BOOL applying =
      _model->phase() ==
      airfix::settings::RenderPresentationSettingsMenuPhase::applying;
  self.renderScaleSlider.enabled = !applying;
  self.presentationControl.enabled = !applying;
  self.verticalFovSlider.enabled = !applying;
  self.visualProfileControl.enabled = !applying;
  self.diagnosticsSwitch.enabled = !applying;
  self.applyButton.enabled = _model->canApply();
  [self.cancelButton setTitle:(applying ? NSLocalizedString(@"Close", nil)
                                        : NSLocalizedString(@"Cancel", nil))
                     forState:UIControlStateNormal];
  if (!_model->persistenceAvailable() && self.statusLabel.text.length == 0U) {
    self.statusLabel.text = NSLocalizedString(
        @"Display settings cannot be saved on this installation.", nil);
  }
}

- (void)setSelectedRow:(SettingsRow)row announce:(BOOL)announce {
  const NSUInteger count = kSettingsRowCount;
  const NSUInteger selected = rowIndex(row);
  if (selected >= count) {
    return;
  }
  _selectedRow = row;
  for (NSUInteger index = 0U; index < count; ++index) {
    UIView *view = _focusRows[index];
    const BOOL active = index == selected;
    view.layer.borderColor = (active ? UIColor.systemYellowColor
                                     : [UIColor colorWithWhite:1.0 alpha:0.18])
                                 .CGColor;
    view.layer.borderWidth = active ? 2.5 : 1.0;
  }
  if (announce) {
    UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification,
                                    _focusRows[selected]);
  }
}

- (void)moveSelection:(NSInteger)direction {
  const NSInteger count = static_cast<NSInteger>(kSettingsRowCount);
  const NSInteger current = static_cast<NSInteger>(rowIndex(_selectedRow));
  const NSInteger next =
      std::clamp(current + direction, static_cast<NSInteger>(0),
                 count - static_cast<NSInteger>(1));
  [self setSelectedRow:static_cast<SettingsRow>(next) announce:YES];
}

- (void)adjustSelectedValue:(NSInteger)direction {
  if (!_model.has_value() || direction == 0 ||
      _model->phase() ==
          airfix::settings::RenderPresentationSettingsMenuPhase::applying) {
    return;
  }
  const auto &draft = _model->draftSettings();
  switch (_selectedRow) {
  case SettingsRow::renderScale: {
    const float next =
        std::clamp(draft.renderScalePercent +
                       static_cast<float>(direction) * kControllerScaleStep,
                   50.0F, 200.0F);
    (void)_model->setRenderScalePercent(next);
    break;
  }
  case SettingsRow::presentation:
    (void)_model->setScenePresentation(
        direction < 0
            ? airfix::render::ScenePresentationMode::widescreenHorPlus
            : airfix::render::ScenePresentationMode::originalFourByThree);
    break;
  case SettingsRow::verticalFovAdjustment: {
    const float next = std::clamp(
        draft.verticalFovAdjustmentDegrees +
            static_cast<float>(direction) * kControllerFovStep,
        airfix::render::native_render_policy::
            minimumVerticalFovAdjustmentDegrees,
        airfix::render::native_render_policy::
            maximumVerticalFovAdjustmentDegrees);
    (void)_model->setVerticalFovAdjustmentDegrees(next);
    break;
  }
  case SettingsRow::visualProfile:
    (void)_model->setVisualProfile(
        direction < 0 ? airfix::render::VisualProfile::classic
                      : airfix::render::VisualProfile::enhanced);
    break;
  case SettingsRow::diagnostics:
    (void)_model->setDiagnosticsOverlayEnabled(direction > 0);
    break;
  case SettingsRow::apply:
  case SettingsRow::cancel:
  case SettingsRow::count:
    break;
  }
  [self refreshControls];
}

- (void)activateSelectedRow {
  switch (_selectedRow) {
  case SettingsRow::renderScale:
    [self adjustSelectedValue:1];
    break;
  case SettingsRow::presentation:
    [self adjustSelectedValue:self.presentationControl.selectedSegmentIndex == 0
                                  ? 1
                                  : -1];
    break;
  case SettingsRow::verticalFovAdjustment:
    [self adjustSelectedValue:1];
    break;
  case SettingsRow::visualProfile:
    [self
        adjustSelectedValue:self.visualProfileControl.selectedSegmentIndex == 0
                                ? 1
                                : -1];
    break;
  case SettingsRow::diagnostics:
    if (_model.has_value()) {
      (void)_model->setDiagnosticsOverlayEnabled(
          !_model->draftSettings().diagnosticsOverlayEnabled);
      [self refreshControls];
    }
    break;
  case SettingsRow::apply:
    [self applySettings];
    break;
  case SettingsRow::cancel:
    [self cancelOrClose];
    break;
  case SettingsRow::count:
    break;
  }
}

- (void)consumeUIInputSnapshot:(AirfixUIInputSnapshot *)input {
  NSAssert(NSThread.isMainThread, @"Display-settings input belongs to main");
  if (input == nil) {
    return;
  }
  if (input.cancelPressed) {
    [self cancelOrClose];
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
