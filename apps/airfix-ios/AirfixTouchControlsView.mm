#import "AirfixTouchControlsView.h"

#import "AirfixIOSHapticFeedbackRouter.h"

#import <QuartzCore/QuartzCore.h>

#include "airfix/input/TouchControlsHaptics.hpp"
#include "airfix/input/TouchControlsPreferences.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr int16_t kQ15Maximum = 32767;
constexpr int16_t kAccessibilityAxisStep = 8192;
constexpr int16_t kAccessibilityThrottleStep = 4096;
constexpr CGFloat kMinimumTargetSize = 44.0;
constexpr CGFloat kWeaponSelectionDistance = 18.0;
constexpr NSTimeInterval kWeaponSelectionHoldDuration = 0.40;
constexpr NSUInteger kButtonCount =
    static_cast<NSUInteger>(AirfixTouchButtonCount);
constexpr uint8_t kWeaponSlotCount = 8U;
static_assert(kButtonCount == 10U);

typedef NS_ENUM(NSInteger, AirfixAccessibilityControl) {
  AirfixAccessibilityControlMission = 0,
  AirfixAccessibilityControlBank,
  AirfixAccessibilityControlPitch,
  AirfixAccessibilityControlThrottle,
  AirfixAccessibilityControlThrottleIncrease,
  AirfixAccessibilityControlThrottleDecrease,
  AirfixAccessibilityControlCameraLookX,
  AirfixAccessibilityControlCameraLookY,
  AirfixAccessibilityControlCameraCycle,
  AirfixAccessibilityControlCameraRecenter,
  AirfixAccessibilityControlPause,
  AirfixAccessibilityControlSecondaryFire,
  AirfixAccessibilityControlPrimaryFire,
  AirfixAccessibilityControlWeapon,
  AirfixAccessibilityControlRearView,
  AirfixAccessibilityControlCount,
};

[[nodiscard]] BOOL rectanglesDiffer(const CGRect first,
                                    const CGRect second) noexcept {
  return !CGRectEqualToRect(first, second);
}

[[nodiscard]] BOOL insetsDiffer(const UIEdgeInsets first,
                                const UIEdgeInsets second) noexcept {
  return !UIEdgeInsetsEqualToEdgeInsets(first, second);
}

[[nodiscard]] BOOL validButton(const AirfixTouchButton button) noexcept {
  return button >= AirfixTouchButtonThrottleIncrease &&
         button < AirfixTouchButtonCount;
}

[[nodiscard]] NSUInteger buttonIndex(const AirfixTouchButton button) noexcept {
  return static_cast<NSUInteger>(button);
}

[[nodiscard]] BOOL heldButton(const AirfixTouchButton button) noexcept {
  switch (button) {
  case AirfixTouchButtonThrottleIncrease:
  case AirfixTouchButtonThrottleDecrease:
  case AirfixTouchButtonPrimaryFire:
  case AirfixTouchButtonSecondaryFire:
  case AirfixTouchButtonRearView:
    return YES;
  case AirfixTouchButtonWeaponNext:
  case AirfixTouchButtonCameraCycle:
  case AirfixTouchButtonCameraRecenter:
  case AirfixTouchButtonMissionStatus:
  case AirfixTouchButtonPause:
  case AirfixTouchButtonCount:
    return NO;
  }
  return NO;
}

[[nodiscard]] int16_t q15FromUnitValue(const CGFloat value) noexcept {
  if (!std::isfinite(value)) {
    return 0;
  }
  const CGFloat clamped = std::clamp(value, -1.0, 1.0);
  return static_cast<int16_t>(
      std::lround(clamped * static_cast<CGFloat>(kQ15Maximum)));
}

[[nodiscard]] int16_t q15FromPositiveUnitValue(const CGFloat value) noexcept {
  if (!std::isfinite(value)) {
    return 0;
  }
  const CGFloat clamped = std::clamp(value, 0.0, 1.0);
  return static_cast<int16_t>(
      std::lround(clamped * static_cast<CGFloat>(kQ15Maximum)));
}

[[nodiscard]] UIColor *panelColor(const CGFloat alpha) {
  return [UIColor colorWithRed:0.035 green:0.078 blue:0.125 alpha:alpha];
}

[[nodiscard]] UIColor *flightAccentColor(const CGFloat alpha) {
  return [UIColor colorWithRed:0.32 green:0.80 blue:0.95 alpha:alpha];
}

[[nodiscard]] UIColor *fireAccentColor(const CGFloat alpha) {
  return [UIColor colorWithRed:1.0 green:0.37 blue:0.11 alpha:alpha];
}

[[nodiscard]] UIColor *secondaryAccentColor(const CGFloat alpha) {
  return [UIColor colorWithRed:1.0 green:0.76 blue:0.20 alpha:alpha];
}

[[nodiscard]] CGFloat
scaledRestingAlpha(const CGFloat base,
                   const std::uint8_t opacityPercent) noexcept {
  return base * static_cast<CGFloat>(opacityPercent) / 100.0;
}

[[nodiscard]] NSString *buttonTitle(const AirfixTouchButton button) {
  switch (button) {
  case AirfixTouchButtonThrottleIncrease:
    return @"+";
  case AirfixTouchButtonThrottleDecrease:
    return @"\u2212";
  case AirfixTouchButtonPrimaryFire:
    return @"FIRE";
  case AirfixTouchButtonSecondaryFire:
    return @"SEC";
  case AirfixTouchButtonWeaponNext:
    return @"WEAP";
  case AirfixTouchButtonRearView:
    return @"REAR";
  case AirfixTouchButtonCameraCycle:
    return @"CAM";
  case AirfixTouchButtonCameraRecenter:
    return @"CTR";
  case AirfixTouchButtonMissionStatus:
    return @"MISSION";
  case AirfixTouchButtonPause:
    return @"\u2016";
  case AirfixTouchButtonCount:
    return @"";
  }
  return @"";
}

[[nodiscard]] CGRect
cgRect(const airfix::input::TouchControlRect &rect) noexcept {
  return CGRectMake(static_cast<CGFloat>(rect.x), static_cast<CGFloat>(rect.y),
                    static_cast<CGFloat>(rect.width),
                    static_cast<CGFloat>(rect.height));
}

[[nodiscard]] airfix::input::TouchControlRect
touchRect(const CGRect rect) noexcept {
  return {
      .x = static_cast<float>(CGRectGetMinX(rect)),
      .y = static_cast<float>(CGRectGetMinY(rect)),
      .width = static_cast<float>(CGRectGetWidth(rect)),
      .height = static_cast<float>(CGRectGetHeight(rect)),
  };
}

[[nodiscard]] airfix::input::TouchControlElement
elementForButton(const AirfixTouchButton button) noexcept {
  using airfix::input::TouchControlElement;
  switch (button) {
  case AirfixTouchButtonThrottleIncrease:
    return TouchControlElement::throttleIncrease;
  case AirfixTouchButtonThrottleDecrease:
    return TouchControlElement::throttleDecrease;
  case AirfixTouchButtonPrimaryFire:
    return TouchControlElement::primaryFire;
  case AirfixTouchButtonSecondaryFire:
    return TouchControlElement::secondaryFire;
  case AirfixTouchButtonWeaponNext:
    return TouchControlElement::weaponNext;
  case AirfixTouchButtonRearView:
    return TouchControlElement::rearView;
  case AirfixTouchButtonCameraCycle:
    return TouchControlElement::cameraCycle;
  case AirfixTouchButtonCameraRecenter:
    return TouchControlElement::cameraRecenter;
  case AirfixTouchButtonMissionStatus:
    return TouchControlElement::missionStatus;
  case AirfixTouchButtonPause:
  case AirfixTouchButtonCount:
    return TouchControlElement::pause;
  }
  return TouchControlElement::pause;
}

} // namespace

@class AirfixTouchAccessibilityElement;

@interface AirfixHeldAccessibilityAction : UIAccessibilityCustomAction
@property(nonatomic) AirfixTouchButton button;
@property(nonatomic) BOOL pressed;
@end

@implementation AirfixHeldAccessibilityAction
@end

@interface AirfixWeaponAccessibilityAction : UIAccessibilityCustomAction
@property(nonatomic) uint8_t slot;
@end

@implementation AirfixWeaponAccessibilityAction
@end

@interface AirfixTouchControlsView () {
  UIView *_stickBaseView;
  UIView *_stickHorizontalGuideView;
  UIView *_stickVerticalGuideView;
  UIView *_stickKnobView;
  UIView *_throttleTrackView;
  UIView *_throttleFillView;
  UIView *_throttleThumbView;
  UILabel *_throttleLabel;
  UIView *_lookRegionView;
  UILabel *_lookRegionLabel;
  UIView *__strong _buttonViews[kButtonCount];
  UILabel *__strong _buttonLabels[kButtonCount];

  UITouch *_stickTouch;
  UITouch *_throttleTouch;
  UITouch *_lookTouch;
  UITouch *__strong _buttonTouches[kButtonCount];

  BOOL _physicalButtonPressed[kButtonCount];
  BOOL _accessibilityButtonLatched[kButtonCount];
  BOOL _accessibilityButtonPulse[kButtonCount];
  BOOL _buttonPressed[kButtonCount];

  int16_t _bankValue;
  int16_t _pitchValue;
  int16_t _throttleValue;
  int16_t _lookXValue;
  int16_t _lookYValue;

  CGRect _stickCaptureFrame;
  CGRect _throttleCaptureFrame;
  CGRect _lookCaptureFrame;
  CGRect _buttonCaptureFrames[kButtonCount];
  CGFloat _stickTravelRadius;
  CGFloat _lookTravelRadius;
  CGPoint _lookOrigin;

  CGPoint _weaponStartPoint;
  NSTimeInterval _weaponStartTimestamp;
  BOOL _weaponGestureActive;
  BOOL _weaponSelecting;
  BOOL _weaponHasCandidate;
  uint8_t _weaponCandidateSlot;
  uint8_t _lastWeaponSlot;
  BOOL _hasSelectedWeaponSlot;

  CGRect _laidOutBounds;
  UIEdgeInsets _laidOutSafeAreaInsets;
  BOOL _hasCompletedLayout;
  BOOL _isCancellingAll;
  NSUInteger _interactionGeneration;
  NSUInteger _axisGeneration;

  airfix::input::TouchControlsLayoutProfile _layoutProfile;
  std::uint8_t _restingOpacityPercent;
  airfix::input::TouchControlsHapticsMode _hapticsMode;
  airfix::input::TouchThrottleDetentTracker _throttleDetentTracker;
  AirfixIOSHapticFeedbackRouter *_hapticFeedbackRouter;

  NSArray<AirfixTouchAccessibilityElement *> *_controlAccessibilityElements;
}

- (void)configureTouchControls;
- (void)configureAccessibilityElements;
- (AirfixTouchAccessibilityElement *)
    accessibilityElementForControl:(AirfixAccessibilityControl)control
                             label:(NSString *)label
                              hint:(NSString *)hint
                        identifier:(NSString *)identifier
                            traits:(UIAccessibilityTraits)traits;
- (NSArray<UIAccessibilityCustomAction *> *)
    axisActionsForElement:(AirfixTouchAccessibilityElement *)element
             increaseName:(NSString *)increase
             decreaseName:(NSString *)decrease
               centerName:(NSString *)center;
- (NSArray<UIAccessibilityCustomAction *> *)
    heldActionsForElement:(AirfixTouchAccessibilityElement *)element
                   button:(AirfixTouchButton)button
                startName:(NSString *)startName
                 stopName:(NSString *)stopName;
- (void)updateAccessibilityFrames;
- (BOOL)pointEligibleForCameraLook:(CGPoint)point;

- (void)updateStickForTouch:(UITouch *)touch force:(BOOL)force;
- (void)updateThrottleForTouch:(UITouch *)touch force:(BOOL)force;
- (void)updateLookForTouch:(UITouch *)touch force:(BOOL)force;
- (void)updateWeaponForTouch:(UITouch *)touch;
- (void)releaseControlsForTouches:(NSSet<UITouch *> *)touches
                        cancelled:(BOOL)cancelled;

- (void)publishStickBank:(int16_t)bank
                   pitch:(int16_t)pitch
               forceBank:(BOOL)forceBank
              forcePitch:(BOOL)forcePitch
            ownedByTouch:(nullable UITouch *)touch;
- (void)publishLookX:(int16_t)lookX
               lookY:(int16_t)lookY
              forceX:(BOOL)forceX
              forceY:(BOOL)forceY
        ownedByTouch:(nullable UITouch *)touch;
- (void)publishAxis:(AirfixTouchAxis)axis
              value:(int16_t)value
              force:(BOOL)force;
- (void)setPhysicalButton:(AirfixTouchButton)button
                  pressed:(BOOL)pressed
                    force:(BOOL)force;
- (void)updateEffectiveButton:(AirfixTouchButton)button force:(BOOL)force;
- (void)pulseButton:(AirfixTouchButton)button
     asynchronously:(BOOL)asynchronously;
- (void)commitWeaponSlot:(uint8_t)slot;
- (void)playHapticEvent:(airfix::input::TouchControlsHapticEvent)event;

- (void)updateVisualState;
- (void)updateAccessibilityValues;
- (void)adjustAccessibilityAxis:(AirfixTouchAxis)axis
                      direction:(NSInteger)direction;
- (void)centerAccessibilityAxis:(AirfixTouchAxis)axis;
- (void)adjustAccessibilityThrottle:(NSInteger)direction;
- (void)setAccessibilityHeldButton:(AirfixTouchButton)button
                           pressed:(BOOL)pressed;
- (void)toggleAccessibilityHeldButton:(AirfixTouchButton)button;
- (void)activateAccessibilityEdgeButton:(AirfixTouchButton)button;
@end

@interface AirfixTouchAccessibilityElement : UIAccessibilityElement
@property(nonatomic, weak) AirfixTouchControlsView *controlsView;
@property(nonatomic) AirfixAccessibilityControl control;
- (AirfixTouchAxis)axis;
- (BOOL)isAxisControl;
- (BOOL)isThrottleControl;
- (BOOL)increaseAxis:(UIAccessibilityCustomAction *)action;
- (BOOL)decreaseAxis:(UIAccessibilityCustomAction *)action;
- (BOOL)centerAxis:(UIAccessibilityCustomAction *)action;
- (BOOL)setHeldAction:(UIAccessibilityCustomAction *)action;
- (BOOL)selectWeaponAction:(UIAccessibilityCustomAction *)action;
@end

@implementation AirfixTouchAccessibilityElement

- (AirfixTouchAxis)axis {
  switch (self.control) {
  case AirfixAccessibilityControlBank:
    return AirfixTouchAxisBank;
  case AirfixAccessibilityControlPitch:
    return AirfixTouchAxisPitch;
  case AirfixAccessibilityControlCameraLookX:
    return AirfixTouchAxisCameraLookX;
  case AirfixAccessibilityControlCameraLookY:
    return AirfixTouchAxisCameraLookY;
  case AirfixAccessibilityControlMission:
  case AirfixAccessibilityControlThrottle:
  case AirfixAccessibilityControlThrottleIncrease:
  case AirfixAccessibilityControlThrottleDecrease:
  case AirfixAccessibilityControlCameraCycle:
  case AirfixAccessibilityControlCameraRecenter:
  case AirfixAccessibilityControlPause:
  case AirfixAccessibilityControlSecondaryFire:
  case AirfixAccessibilityControlPrimaryFire:
  case AirfixAccessibilityControlWeapon:
  case AirfixAccessibilityControlRearView:
  case AirfixAccessibilityControlCount:
    return AirfixTouchAxisBank;
  }
  return AirfixTouchAxisBank;
}

- (BOOL)isAxisControl {
  return self.control == AirfixAccessibilityControlBank ||
         self.control == AirfixAccessibilityControlPitch ||
         self.control == AirfixAccessibilityControlCameraLookX ||
         self.control == AirfixAccessibilityControlCameraLookY;
}

- (BOOL)isThrottleControl {
  return self.control == AirfixAccessibilityControlThrottle;
}

- (BOOL)accessibilityActivate {
  switch (self.control) {
  case AirfixAccessibilityControlThrottleIncrease:
    [self.controlsView
        toggleAccessibilityHeldButton:AirfixTouchButtonThrottleIncrease];
    return YES;
  case AirfixAccessibilityControlThrottleDecrease:
    [self.controlsView
        toggleAccessibilityHeldButton:AirfixTouchButtonThrottleDecrease];
    return YES;
  case AirfixAccessibilityControlPrimaryFire:
    [self.controlsView
        toggleAccessibilityHeldButton:AirfixTouchButtonPrimaryFire];
    return YES;
  case AirfixAccessibilityControlSecondaryFire:
    [self.controlsView
        toggleAccessibilityHeldButton:AirfixTouchButtonSecondaryFire];
    return YES;
  case AirfixAccessibilityControlRearView:
    [self.controlsView toggleAccessibilityHeldButton:AirfixTouchButtonRearView];
    return YES;
  case AirfixAccessibilityControlWeapon:
    [self.controlsView
        activateAccessibilityEdgeButton:AirfixTouchButtonWeaponNext];
    return YES;
  case AirfixAccessibilityControlCameraCycle:
    [self.controlsView
        activateAccessibilityEdgeButton:AirfixTouchButtonCameraCycle];
    return YES;
  case AirfixAccessibilityControlCameraRecenter:
    [self.controlsView
        activateAccessibilityEdgeButton:AirfixTouchButtonCameraRecenter];
    return YES;
  case AirfixAccessibilityControlMission:
    [self.controlsView
        activateAccessibilityEdgeButton:AirfixTouchButtonMissionStatus];
    return YES;
  case AirfixAccessibilityControlPause:
    [self.controlsView activateAccessibilityEdgeButton:AirfixTouchButtonPause];
    return YES;
  case AirfixAccessibilityControlBank:
  case AirfixAccessibilityControlPitch:
  case AirfixAccessibilityControlThrottle:
  case AirfixAccessibilityControlCameraLookX:
  case AirfixAccessibilityControlCameraLookY:
  case AirfixAccessibilityControlCount:
    return NO;
  }
  return NO;
}

- (void)accessibilityIncrement {
  if ([self isAxisControl]) {
    [self.controlsView adjustAccessibilityAxis:self.axis direction:1];
  } else if ([self isThrottleControl]) {
    [self.controlsView adjustAccessibilityThrottle:1];
  }
}

- (void)accessibilityDecrement {
  if ([self isAxisControl]) {
    [self.controlsView adjustAccessibilityAxis:self.axis direction:-1];
  } else if ([self isThrottleControl]) {
    [self.controlsView adjustAccessibilityThrottle:-1];
  }
}

- (BOOL)increaseAxis:(UIAccessibilityCustomAction *)action {
  (void)action;
  [self accessibilityIncrement];
  return [self isAxisControl] || [self isThrottleControl];
}

- (BOOL)decreaseAxis:(UIAccessibilityCustomAction *)action {
  (void)action;
  [self accessibilityDecrement];
  return [self isAxisControl] || [self isThrottleControl];
}

- (BOOL)centerAxis:(UIAccessibilityCustomAction *)action {
  (void)action;
  if (![self isAxisControl]) {
    return NO;
  }
  [self.controlsView centerAccessibilityAxis:self.axis];
  return YES;
}

- (BOOL)setHeldAction:(UIAccessibilityCustomAction *)action {
  if (![action isKindOfClass:AirfixHeldAccessibilityAction.class]) {
    return NO;
  }
  AirfixHeldAccessibilityAction *heldAction =
      (AirfixHeldAccessibilityAction *)action;
  [self.controlsView setAccessibilityHeldButton:heldAction.button
                                        pressed:heldAction.pressed];
  return YES;
}

- (BOOL)selectWeaponAction:(UIAccessibilityCustomAction *)action {
  if (![action isKindOfClass:AirfixWeaponAccessibilityAction.class]) {
    return NO;
  }
  AirfixWeaponAccessibilityAction *weaponAction =
      (AirfixWeaponAccessibilityAction *)action;
  [self.controlsView commitWeaponSlot:weaponAction.slot];
  return YES;
}

@end

@implementation AirfixTouchControlsView

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self != nil) {
    [self configureTouchControls];
  }
  return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
  self = [super initWithCoder:coder];
  if (self != nil) {
    [self configureTouchControls];
  }
  return self;
}

- (AirfixTouchControlsHandedness)layoutHandedness {
  return _layoutProfile.handedness ==
                 airfix::input::TouchControlsHandedness::leftHanded
             ? AirfixTouchControlsHandednessLeft
             : AirfixTouchControlsHandednessRight;
}

- (void)setLayoutHandedness:(AirfixTouchControlsHandedness)layoutHandedness {
  NSAssert(NSThread.isMainThread,
           @"Touch-control layout must change on the main thread");
  if (!NSThread.isMainThread) {
    return;
  }

  airfix::input::TouchControlsHandedness candidate;
  switch (layoutHandedness) {
  case AirfixTouchControlsHandednessRight:
    candidate = airfix::input::TouchControlsHandedness::rightHanded;
    break;
  case AirfixTouchControlsHandednessLeft:
    candidate = airfix::input::TouchControlsHandedness::leftHanded;
    break;
  default:
    return;
  }
  if (_layoutProfile.handedness == candidate) {
    return;
  }
  [self cancelAllTouches];
  _layoutProfile.handedness = candidate;
  [self setNeedsLayout];
}

- (AirfixTouchControlsDensity)layoutDensity {
  return _layoutProfile.density == airfix::input::TouchControlsDensity::compact
             ? AirfixTouchControlsDensityCompact
             : AirfixTouchControlsDensityAutomatic;
}

- (void)setLayoutDensity:(AirfixTouchControlsDensity)layoutDensity {
  NSAssert(NSThread.isMainThread,
           @"Touch-control layout must change on the main thread");
  if (!NSThread.isMainThread) {
    return;
  }

  airfix::input::TouchControlsDensity candidate;
  switch (layoutDensity) {
  case AirfixTouchControlsDensityAutomatic:
    candidate = airfix::input::TouchControlsDensity::automatic;
    break;
  case AirfixTouchControlsDensityCompact:
    candidate = airfix::input::TouchControlsDensity::compact;
    break;
  default:
    return;
  }
  if (_layoutProfile.density == candidate) {
    return;
  }
  [self cancelAllTouches];
  _layoutProfile.density = candidate;
  [self setNeedsLayout];
}

- (BOOL)applyPreferences:
    (const airfix::input::TouchControlsPreferences &)preferences {
  NSAssert(NSThread.isMainThread,
           @"Touch-control preferences must change on the main thread");
  if (!NSThread.isMainThread ||
      airfix::input::validateTouchControlsPreferences(preferences)
          .has_value()) {
    return NO;
  }

  const BOOL layoutChanged = _layoutProfile != preferences.layout;
  if (layoutChanged) {
    [self cancelAllTouches];
  }
  _layoutProfile = preferences.layout;
  _restingOpacityPercent = preferences.restingOpacityPercent;
  if (_hapticsMode != preferences.hapticsMode) {
    _hapticsMode = preferences.hapticsMode;
    _throttleDetentTracker.reset();
    _hapticFeedbackRouter.enabled =
        _hapticsMode == airfix::input::TouchControlsHapticsMode::system;
  }
  if (layoutChanged) {
    [self setNeedsLayout];
  }
  [self updateVisualState];
  return YES;
}

- (void)configureTouchControls {
  self.backgroundColor = UIColor.clearColor;
  self.opaque = NO;
  self.multipleTouchEnabled = YES;
  self.isAccessibilityElement = NO;

  _layoutProfile = {};
  _restingOpacityPercent =
      airfix::input::defaultTouchControlsRestingOpacityPercent;
  _hapticsMode = airfix::input::TouchControlsHapticsMode::system;
  _hapticFeedbackRouter = [[AirfixIOSHapticFeedbackRouter alloc] init];
  _hapticFeedbackRouter.enabled = YES;

  _stickBaseView = [[UIView alloc] initWithFrame:CGRectZero];
  _stickHorizontalGuideView = [[UIView alloc] initWithFrame:CGRectZero];
  _stickVerticalGuideView = [[UIView alloc] initWithFrame:CGRectZero];
  _stickKnobView = [[UIView alloc] initWithFrame:CGRectZero];
  _throttleTrackView = [[UIView alloc] initWithFrame:CGRectZero];
  _throttleFillView = [[UIView alloc] initWithFrame:CGRectZero];
  _throttleThumbView = [[UIView alloc] initWithFrame:CGRectZero];
  _throttleLabel = [[UILabel alloc] initWithFrame:CGRectZero];
  _lookRegionView = [[UIView alloc] initWithFrame:CGRectZero];
  _lookRegionLabel = [[UILabel alloc] initWithFrame:CGRectZero];

  _stickBaseView.backgroundColor = panelColor(0.48);
  _stickBaseView.layer.borderColor = flightAccentColor(0.78).CGColor;
  _stickBaseView.layer.borderWidth = 2.0;
  _stickHorizontalGuideView.backgroundColor = flightAccentColor(0.30);
  _stickVerticalGuideView.backgroundColor = flightAccentColor(0.30);
  _stickKnobView.backgroundColor = panelColor(0.76);
  _stickKnobView.layer.borderColor = flightAccentColor(0.96).CGColor;
  _stickKnobView.layer.borderWidth = 2.0;

  _throttleTrackView.backgroundColor = panelColor(0.52);
  _throttleTrackView.layer.borderColor = flightAccentColor(0.78).CGColor;
  _throttleTrackView.layer.borderWidth = 1.5;
  _throttleFillView.backgroundColor = flightAccentColor(0.56);
  _throttleThumbView.backgroundColor = panelColor(0.84);
  _throttleThumbView.layer.borderColor = UIColor.whiteColor.CGColor;
  _throttleThumbView.layer.borderWidth = 1.5;
  _throttleLabel.text = @"THR";
  _throttleLabel.textAlignment = NSTextAlignmentCenter;
  _throttleLabel.textColor = UIColor.whiteColor;

  _lookRegionView.backgroundColor = panelColor(0.12);
  _lookRegionView.layer.borderColor = flightAccentColor(0.20).CGColor;
  _lookRegionView.layer.borderWidth = 1.0;
  _lookRegionView.layer.cornerRadius = 16.0;
  _lookRegionLabel.text = @"LOOK";
  _lookRegionLabel.textAlignment = NSTextAlignmentCenter;
  _lookRegionLabel.textColor = flightAccentColor(0.42);

  [_stickBaseView addSubview:_stickHorizontalGuideView];
  [_stickBaseView addSubview:_stickVerticalGuideView];
  [_stickBaseView addSubview:_stickKnobView];
  [_throttleTrackView addSubview:_throttleFillView];
  [_throttleTrackView addSubview:_throttleThumbView];
  [_throttleTrackView addSubview:_throttleLabel];
  [_lookRegionView addSubview:_lookRegionLabel];

  [self addSubview:_lookRegionView];
  [self addSubview:_stickBaseView];
  [self addSubview:_throttleTrackView];

  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    const AirfixTouchButton button = static_cast<AirfixTouchButton>(index);
    UIView *view = [[UIView alloc] initWithFrame:CGRectZero];
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectZero];
    label.text = buttonTitle(button);
    label.textAlignment = NSTextAlignmentCenter;
    label.textColor = UIColor.whiteColor;
    label.adjustsFontSizeToFitWidth = YES;
    label.minimumScaleFactor = 0.55;
    label.numberOfLines = 1;
    label.layer.shadowColor = UIColor.blackColor.CGColor;
    label.layer.shadowOpacity = 0.85F;
    label.layer.shadowRadius = 1.5;
    label.layer.shadowOffset = CGSizeMake(0.0, 1.0);
    [view addSubview:label];
    [self addSubview:view];
    _buttonViews[index] = view;
    _buttonLabels[index] = label;
  }

  NSArray<UIView *> *inertViews = @[
    _stickBaseView,
    _stickHorizontalGuideView,
    _stickVerticalGuideView,
    _stickKnobView,
    _throttleTrackView,
    _throttleFillView,
    _throttleThumbView,
    _throttleLabel,
    _lookRegionView,
    _lookRegionLabel,
  ];
  for (UIView *view in inertViews) {
    view.userInteractionEnabled = NO;
    view.isAccessibilityElement = NO;
  }
  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    _buttonViews[index].userInteractionEnabled = NO;
    _buttonViews[index].isAccessibilityElement = NO;
    _buttonLabels[index].userInteractionEnabled = NO;
    _buttonLabels[index].isAccessibilityElement = NO;
  }

  [self configureAccessibilityElements];
  [self updateVisualState];
  [self updateAccessibilityValues];
}

- (AirfixTouchAccessibilityElement *)
    accessibilityElementForControl:(AirfixAccessibilityControl)control
                             label:(NSString *)label
                              hint:(NSString *)hint
                        identifier:(NSString *)identifier
                            traits:(UIAccessibilityTraits)traits {
  AirfixTouchAccessibilityElement *element =
      [[AirfixTouchAccessibilityElement alloc]
          initWithAccessibilityContainer:self];
  element.controlsView = self;
  element.control = control;
  element.accessibilityLabel = NSLocalizedString(label, nil);
  element.accessibilityHint = NSLocalizedString(hint, nil);
  element.accessibilityIdentifier = identifier;
  element.accessibilityTraits = traits;
  return element;
}

- (NSArray<UIAccessibilityCustomAction *> *)
    axisActionsForElement:(AirfixTouchAccessibilityElement *)element
             increaseName:(NSString *)increase
             decreaseName:(NSString *)decrease
               centerName:(NSString *)center {
  return @[
    [[UIAccessibilityCustomAction alloc]
        initWithName:NSLocalizedString(increase, nil)
              target:element
            selector:@selector(increaseAxis:)],
    [[UIAccessibilityCustomAction alloc]
        initWithName:NSLocalizedString(decrease, nil)
              target:element
            selector:@selector(decreaseAxis:)],
    [[UIAccessibilityCustomAction alloc]
        initWithName:NSLocalizedString(center, nil)
              target:element
            selector:@selector(centerAxis:)],
  ];
}

- (NSArray<UIAccessibilityCustomAction *> *)
    heldActionsForElement:(AirfixTouchAccessibilityElement *)element
                   button:(AirfixTouchButton)button
                startName:(NSString *)startName
                 stopName:(NSString *)stopName {
  AirfixHeldAccessibilityAction *start = [[AirfixHeldAccessibilityAction alloc]
      initWithName:NSLocalizedString(startName, nil)
            target:element
          selector:@selector(setHeldAction:)];
  start.button = button;
  start.pressed = YES;
  AirfixHeldAccessibilityAction *stop = [[AirfixHeldAccessibilityAction alloc]
      initWithName:NSLocalizedString(stopName, nil)
            target:element
          selector:@selector(setHeldAction:)];
  stop.button = button;
  stop.pressed = NO;
  return @[ start, stop ];
}

- (void)configureAccessibilityElements {
  const UIAccessibilityTraits adjustable = UIAccessibilityTraitAdjustable;
  const UIAccessibilityTraits button = UIAccessibilityTraitButton;

  AirfixTouchAccessibilityElement *mission =
      [self accessibilityElementForControl:AirfixAccessibilityControlMission
                                     label:@"Mission status"
                                      hint:@"Double-tap to open mission status."
                                identifier:@"airfix.touch.mission"
                                    traits:button];
  AirfixTouchAccessibilityElement *bank =
      [self accessibilityElementForControl:AirfixAccessibilityControlBank
                                     label:@"Bank"
                                      hint:@"Adjust left or right, or use the "
                                           @"center action."
                                identifier:@"airfix.touch.bank"
                                    traits:adjustable];
  bank.accessibilityCustomActions = [self axisActionsForElement:bank
                                                   increaseName:@"Bank right"
                                                   decreaseName:@"Bank left"
                                                     centerName:@"Center bank"];

  AirfixTouchAccessibilityElement *pitch =
      [self accessibilityElementForControl:AirfixAccessibilityControlPitch
                                     label:@"Pitch"
                                      hint:@"Adjust up or down, or use the "
                                           @"center action."
                                identifier:@"airfix.touch.pitch"
                                    traits:adjustable];
  pitch.accessibilityCustomActions =
      [self axisActionsForElement:pitch
                     increaseName:@"Pitch up"
                     decreaseName:@"Pitch down"
                       centerName:@"Center pitch"];

  AirfixTouchAccessibilityElement *throttle =
      [self accessibilityElementForControl:AirfixAccessibilityControlThrottle
                                     label:@"Throttle target"
                                      hint:@"Swipe up or down to change the "
                                           @"latched throttle target."
                                identifier:@"airfix.touch.throttle"
                                    traits:adjustable];
  throttle.accessibilityCustomActions = @[
    [[UIAccessibilityCustomAction alloc]
        initWithName:NSLocalizedString(@"Increase throttle target", nil)
              target:throttle
            selector:@selector(increaseAxis:)],
    [[UIAccessibilityCustomAction alloc]
        initWithName:NSLocalizedString(@"Decrease throttle target", nil)
              target:throttle
            selector:@selector(decreaseAxis:)],
  ];

  AirfixTouchAccessibilityElement *throttleIncrease = [self
      accessibilityElementForControl:AirfixAccessibilityControlThrottleIncrease
                               label:@"Increase thrust"
                                hint:@"Double-tap to start or stop increasing "
                                     @"thrust."
                          identifier:@"airfix.touch.throttle.increase"
                              traits:button];
  throttleIncrease.accessibilityCustomActions =
      [self heldActionsForElement:throttleIncrease
                           button:AirfixTouchButtonThrottleIncrease
                        startName:@"Start increasing thrust"
                         stopName:@"Stop increasing thrust"];

  AirfixTouchAccessibilityElement *throttleDecrease = [self
      accessibilityElementForControl:AirfixAccessibilityControlThrottleDecrease
                               label:@"Decrease thrust"
                                hint:@"Double-tap to start or stop decreasing "
                                     @"thrust."
                          identifier:@"airfix.touch.throttle.decrease"
                              traits:button];
  throttleDecrease.accessibilityCustomActions =
      [self heldActionsForElement:throttleDecrease
                           button:AirfixTouchButtonThrottleDecrease
                        startName:@"Start decreasing thrust"
                         stopName:@"Stop decreasing thrust"];

  AirfixTouchAccessibilityElement *lookX =
      [self accessibilityElementForControl:AirfixAccessibilityControlCameraLookX
                                     label:@"Camera horizontal look"
                                      hint:@"Adjust left or right, or recenter "
                                           @"this axis."
                                identifier:@"airfix.touch.camera.look.x"
                                    traits:adjustable];
  lookX.accessibilityCustomActions =
      [self axisActionsForElement:lookX
                     increaseName:@"Look right"
                     decreaseName:@"Look left"
                       centerName:@"Center horizontal look"];

  AirfixTouchAccessibilityElement *lookY = [self
      accessibilityElementForControl:AirfixAccessibilityControlCameraLookY
                               label:@"Camera vertical look"
                                hint:
                                    @"Adjust up or down, or recenter this axis."
                          identifier:@"airfix.touch.camera.look.y"
                              traits:adjustable];
  lookY.accessibilityCustomActions =
      [self axisActionsForElement:lookY
                     increaseName:@"Look up"
                     decreaseName:@"Look down"
                       centerName:@"Center vertical look"];

  AirfixTouchAccessibilityElement *cameraCycle =
      [self accessibilityElementForControl:AirfixAccessibilityControlCameraCycle
                                     label:@"Cycle camera"
                                      hint:@"Double-tap to use the next camera."
                                identifier:@"airfix.touch.camera.cycle"
                                    traits:button];
  AirfixTouchAccessibilityElement *cameraRecenter = [self
      accessibilityElementForControl:AirfixAccessibilityControlCameraRecenter
                               label:@"Recenter camera"
                                hint:@"Double-tap to recenter the camera."
                          identifier:@"airfix.touch.camera.recenter"
                              traits:button];
  AirfixTouchAccessibilityElement *pause = [self
      accessibilityElementForControl:AirfixAccessibilityControlPause
                               label:@"Pause or resume"
                                hint:@"Double-tap to pause or resume gameplay."
                          identifier:@"airfix.touch.pause"
                              traits:button];

  AirfixTouchAccessibilityElement *secondary = [self
      accessibilityElementForControl:AirfixAccessibilityControlSecondaryFire
                               label:@"Secondary fire"
                                hint:@"Double-tap to start or stop secondary "
                                     @"fire."
                          identifier:@"airfix.touch.fire.secondary"
                              traits:button];
  secondary.accessibilityCustomActions =
      [self heldActionsForElement:secondary
                           button:AirfixTouchButtonSecondaryFire
                        startName:@"Start secondary fire"
                         stopName:@"Stop secondary fire"];

  AirfixTouchAccessibilityElement *primary = [self
      accessibilityElementForControl:AirfixAccessibilityControlPrimaryFire
                               label:@"Primary fire"
                                hint:
                                    @"Double-tap to start or stop primary fire."
                          identifier:@"airfix.touch.fire.primary"
                              traits:button];
  primary.accessibilityCustomActions =
      [self heldActionsForElement:primary
                           button:AirfixTouchButtonPrimaryFire
                        startName:@"Start primary fire"
                         stopName:@"Stop primary fire"];

  AirfixTouchAccessibilityElement *weapon =
      [self accessibilityElementForControl:AirfixAccessibilityControlWeapon
                                     label:@"Weapon"
                                      hint:@"Double-tap to cycle, or choose a "
                                           @"numbered weapon action."
                                identifier:@"airfix.touch.weapon"
                                    traits:button];
  NSMutableArray<UIAccessibilityCustomAction *> *weaponActions =
      [[NSMutableArray alloc] initWithCapacity:kWeaponSlotCount];
  for (uint8_t slot = 0U; slot < kWeaponSlotCount; ++slot) {
    NSString *name =
        [NSString stringWithFormat:NSLocalizedString(@"Select weapon %u", nil),
                                   static_cast<unsigned int>(slot + 1U)];
    AirfixWeaponAccessibilityAction *action =
        [[AirfixWeaponAccessibilityAction alloc]
            initWithName:name
                  target:weapon
                selector:@selector(selectWeaponAction:)];
    action.slot = slot;
    [weaponActions addObject:action];
  }
  weapon.accessibilityCustomActions = weaponActions;

  AirfixTouchAccessibilityElement *rear = [self
      accessibilityElementForControl:AirfixAccessibilityControlRearView
                               label:@"Rear view"
                                hint:@"Double-tap to start or stop rear view."
                          identifier:@"airfix.touch.camera.rear"
                              traits:button];
  rear.accessibilityCustomActions =
      [self heldActionsForElement:rear
                           button:AirfixTouchButtonRearView
                        startName:@"Start rear view"
                         stopName:@"Stop rear view"];

  _controlAccessibilityElements = @[
    mission,
    bank,
    pitch,
    throttle,
    throttleIncrease,
    throttleDecrease,
    lookX,
    lookY,
    cameraCycle,
    cameraRecenter,
    pause,
    secondary,
    primary,
    weapon,
    rear,
  ];
  self.accessibilityElements = _controlAccessibilityElements;
}

- (NSInteger)accessibilityElementCount {
  return static_cast<NSInteger>(_controlAccessibilityElements.count);
}

- (nullable id)accessibilityElementAtIndex:(NSInteger)index {
  if (index < 0 ||
      index >= static_cast<NSInteger>(_controlAccessibilityElements.count)) {
    return nil;
  }
  return _controlAccessibilityElements[static_cast<NSUInteger>(index)];
}

- (NSInteger)indexOfAccessibilityElement:(id)element {
  const NSUInteger index =
      [_controlAccessibilityElements indexOfObjectIdenticalTo:element];
  return index == NSNotFound ? NSNotFound : static_cast<NSInteger>(index);
}

- (void)safeAreaInsetsDidChange {
  [super safeAreaInsetsDidChange];
  [self setNeedsLayout];
}

- (void)layoutSubviews {
  [super layoutSubviews];

  const UIEdgeInsets safeInsets = self.safeAreaInsets;
  if (_hasCompletedLayout &&
      (rectanglesDiffer(_laidOutBounds, self.bounds) ||
       insetsDiffer(_laidOutSafeAreaInsets, safeInsets))) {
    [self cancelAllTouches];
  }
  _laidOutBounds = self.bounds;
  _laidOutSafeAreaInsets = safeInsets;
  _hasCompletedLayout = YES;

  CGRect safeBounds = UIEdgeInsetsInsetRect(self.bounds, safeInsets);
  if (CGRectIsEmpty(safeBounds) || CGRectIsNull(safeBounds)) {
    safeBounds = self.bounds;
  }

  const auto layout = airfix::input::buildTouchControlsLayout(
      touchRect(safeBounds), _layoutProfile);
  if (!layout.ready()) {
    return;
  }

  using airfix::input::TouchControlElement;
  const BOOL compact = layout.compact ? YES : NO;
  const CGRect stickFrame =
      cgRect(layout.visualFrame(TouchControlElement::flightStick));
  const CGRect throttleFrame =
      cgRect(layout.visualFrame(TouchControlElement::throttle));
  const CGRect primaryFrame =
      cgRect(layout.visualFrame(TouchControlElement::primaryFire));
  const CGRect secondaryFrame =
      cgRect(layout.visualFrame(TouchControlElement::secondaryFire));

  const CGFloat knobDiameter = compact ? 38.0 : 48.0;
  _stickBaseView.frame = stickFrame;
  _stickBaseView.layer.cornerRadius = CGRectGetWidth(stickFrame) * 0.5;
  const CGFloat guideInset = compact ? 14.0 : 18.0;
  _stickHorizontalGuideView.frame = CGRectMake(
      guideInset, std::floor((CGRectGetHeight(stickFrame) - 1.0) * 0.5),
      std::max<CGFloat>(0.0, CGRectGetWidth(stickFrame) - 2.0 * guideInset),
      1.0);
  _stickVerticalGuideView.frame = CGRectMake(
      std::floor((CGRectGetWidth(stickFrame) - 1.0) * 0.5), guideInset, 1.0,
      std::max<CGFloat>(0.0, CGRectGetHeight(stickFrame) - 2.0 * guideInset));
  _stickKnobView.bounds = CGRectMake(0.0, 0.0, knobDiameter, knobDiameter);
  _stickKnobView.layer.cornerRadius = knobDiameter * 0.5;
  _stickTravelRadius = static_cast<CGFloat>(layout.stickTravelRadius);

  _throttleTrackView.frame = throttleFrame;
  _throttleTrackView.layer.cornerRadius = CGRectGetWidth(throttleFrame) * 0.5;

  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    const auto element =
        elementForButton(static_cast<AirfixTouchButton>(index));
    const CGRect visualFrame = cgRect(layout.visualFrame(element));
    _buttonViews[index].frame = visualFrame;
    _buttonLabels[index].frame =
        CGRectInset(_buttonViews[index].bounds, 5.0, 4.0);
    _buttonLabels[index].font = [UIFont systemFontOfSize:(compact ? 12.0 : 14.0)
                                                  weight:UIFontWeightBold];
    _buttonViews[index].layer.cornerRadius =
        std::min<CGFloat>(14.0, CGRectGetHeight(visualFrame) * 0.28);
    _buttonViews[index].layer.borderWidth = 1.5;
    _buttonCaptureFrames[index] = cgRect(layout.captureFrame(element));
  }
  _buttonViews[buttonIndex(AirfixTouchButtonPrimaryFire)].layer.cornerRadius =
      CGRectGetHeight(primaryFrame) * 0.5;
  _buttonViews[buttonIndex(AirfixTouchButtonSecondaryFire)].layer.cornerRadius =
      CGRectGetHeight(secondaryFrame) * 0.5;
  _buttonLabels[buttonIndex(AirfixTouchButtonPrimaryFire)].font =
      [UIFont systemFontOfSize:(compact ? 14.0 : 17.0)
                        weight:UIFontWeightBlack];

  _stickCaptureFrame =
      cgRect(layout.captureFrame(TouchControlElement::flightStick));
  _throttleCaptureFrame =
      cgRect(layout.captureFrame(TouchControlElement::throttle));
  _lookCaptureFrame =
      cgRect(layout.captureFrame(TouchControlElement::cameraLook));
  _lookRegionView.frame = _lookCaptureFrame;
  _lookRegionLabel.frame = CGRectMake(
      8.0, 8.0,
      std::max<CGFloat>(0.0, CGRectGetWidth(_lookRegionView.bounds) - 16.0),
      20.0);
  _lookRegionLabel.font = [UIFont systemFontOfSize:(compact ? 10.0 : 12.0)
                                            weight:UIFontWeightSemibold];
  _lookTravelRadius = static_cast<CGFloat>(layout.lookTravelRadius);

  const CGFloat throttleInset = compact ? 8.0 : 10.0;
  const CGFloat thumbWidth = compact ? 56.0 : 66.0;
  const CGFloat thumbHeight = compact ? 28.0 : 32.0;
  const CGFloat usableThrottleHeight = std::max<CGFloat>(
      1.0, CGRectGetHeight(_throttleTrackView.bounds) - 2.0 * throttleInset);
  const CGFloat throttleUnit =
      static_cast<CGFloat>(_throttleValue) / static_cast<CGFloat>(kQ15Maximum);
  const CGFloat thumbCenterY = CGRectGetMaxY(_throttleTrackView.bounds) -
                               throttleInset -
                               throttleUnit * usableThrottleHeight;
  _throttleThumbView.bounds = CGRectMake(0.0, 0.0, thumbWidth, thumbHeight);
  _throttleThumbView.center =
      CGPointMake(CGRectGetMidX(_throttleTrackView.bounds), thumbCenterY);
  _throttleThumbView.layer.cornerRadius = compact ? 7.0 : 8.0;
  _throttleFillView.frame = CGRectMake(
      CGRectGetMidX(_throttleTrackView.bounds) - 3.0, thumbCenterY, 6.0,
      std::max<CGFloat>(0.0, CGRectGetMaxY(_throttleTrackView.bounds) -
                                 throttleInset - thumbCenterY));
  _throttleFillView.layer.cornerRadius = 3.0;
  _throttleLabel.frame = CGRectMake(
      3.0, 3.0,
      std::max<CGFloat>(0.0, CGRectGetWidth(_throttleTrackView.bounds) - 6.0),
      18.0);
  _throttleLabel.font =
      [UIFont systemFontOfSize:(compact ? 9.0 : 11.0) weight:UIFontWeightBold];

  [self updateAccessibilityFrames];
  [self updateVisualState];
}

- (void)updateAccessibilityFrames {
  if (_controlAccessibilityElements.count !=
      static_cast<NSUInteger>(AirfixAccessibilityControlCount)) {
    return;
  }

  const CGFloat stickHalfHeight =
      std::max(kMinimumTargetSize, CGRectGetHeight(_stickCaptureFrame) * 0.5);
  CGRect pitchFrame = _stickCaptureFrame;
  pitchFrame.size.height =
      std::min(stickHalfHeight, CGRectGetHeight(_stickCaptureFrame));
  CGRect bankFrame = _stickCaptureFrame;
  bankFrame.origin.y =
      CGRectGetMaxY(_stickCaptureFrame) - pitchFrame.size.height;
  bankFrame.size.height = pitchFrame.size.height;

  const CGFloat lookHalfHeight =
      std::max(kMinimumTargetSize, CGRectGetHeight(_lookCaptureFrame) * 0.5);
  CGRect lookYFrame = _lookCaptureFrame;
  lookYFrame.size.height =
      std::min(lookHalfHeight, CGRectGetHeight(_lookCaptureFrame));
  CGRect lookXFrame = _lookCaptureFrame;
  lookXFrame.origin.y =
      CGRectGetMaxY(_lookCaptureFrame) - lookYFrame.size.height;
  lookXFrame.size.height = lookYFrame.size.height;

  for (AirfixTouchAccessibilityElement
           *element in _controlAccessibilityElements) {
    CGRect frame = CGRectZero;
    switch (element.control) {
    case AirfixAccessibilityControlMission:
      frame = _buttonCaptureFrames[buttonIndex(AirfixTouchButtonMissionStatus)];
      break;
    case AirfixAccessibilityControlBank:
      frame = bankFrame;
      break;
    case AirfixAccessibilityControlPitch:
      frame = pitchFrame;
      break;
    case AirfixAccessibilityControlThrottle:
      frame = _throttleCaptureFrame;
      break;
    case AirfixAccessibilityControlThrottleIncrease:
      frame =
          _buttonCaptureFrames[buttonIndex(AirfixTouchButtonThrottleIncrease)];
      break;
    case AirfixAccessibilityControlThrottleDecrease:
      frame =
          _buttonCaptureFrames[buttonIndex(AirfixTouchButtonThrottleDecrease)];
      break;
    case AirfixAccessibilityControlCameraLookX:
      frame = lookXFrame;
      break;
    case AirfixAccessibilityControlCameraLookY:
      frame = lookYFrame;
      break;
    case AirfixAccessibilityControlCameraCycle:
      frame = _buttonCaptureFrames[buttonIndex(AirfixTouchButtonCameraCycle)];
      break;
    case AirfixAccessibilityControlCameraRecenter:
      frame =
          _buttonCaptureFrames[buttonIndex(AirfixTouchButtonCameraRecenter)];
      break;
    case AirfixAccessibilityControlPause:
      frame = _buttonCaptureFrames[buttonIndex(AirfixTouchButtonPause)];
      break;
    case AirfixAccessibilityControlSecondaryFire:
      frame = _buttonCaptureFrames[buttonIndex(AirfixTouchButtonSecondaryFire)];
      break;
    case AirfixAccessibilityControlPrimaryFire:
      frame = _buttonCaptureFrames[buttonIndex(AirfixTouchButtonPrimaryFire)];
      break;
    case AirfixAccessibilityControlWeapon:
      frame = _buttonCaptureFrames[buttonIndex(AirfixTouchButtonWeaponNext)];
      break;
    case AirfixAccessibilityControlRearView:
      frame = _buttonCaptureFrames[buttonIndex(AirfixTouchButtonRearView)];
      break;
    case AirfixAccessibilityControlCount:
      break;
    }
    element.accessibilityFrameInContainerSpace = frame;
  }
}

- (BOOL)pointEligibleForCameraLook:(CGPoint)point {
  if (!CGRectContainsPoint(_lookCaptureFrame, point) ||
      CGRectContainsPoint(_stickCaptureFrame, point) ||
      CGRectContainsPoint(_throttleCaptureFrame, point)) {
    return NO;
  }
  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    if (CGRectContainsPoint(_buttonCaptureFrames[index], point)) {
      return NO;
    }
  }
  return YES;
}

- (BOOL)pointInside:(CGPoint)point withEvent:(nullable UIEvent *)event {
  if (![super pointInside:point withEvent:event]) {
    return NO;
  }
  if (CGRectContainsPoint(_stickCaptureFrame, point) ||
      CGRectContainsPoint(_throttleCaptureFrame, point)) {
    return YES;
  }
  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    if (CGRectContainsPoint(_buttonCaptureFrames[index], point)) {
      return YES;
    }
  }
  return [self pointEligibleForCameraLook:point];
}

- (nullable UIView *)hitTest:(CGPoint)point
                   withEvent:(nullable UIEvent *)event {
  if (self.hidden || self.alpha < 0.01 || !self.userInteractionEnabled) {
    return nil;
  }
  return [self pointInside:point withEvent:event] ? self : nil;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches
           withEvent:(nullable UIEvent *)event {
  (void)event;
  const NSUInteger interactionGeneration = _interactionGeneration;
  static constexpr AirfixTouchButton capturePriority[] = {
      AirfixTouchButtonPause,
      AirfixTouchButtonMissionStatus,
      AirfixTouchButtonCameraCycle,
      AirfixTouchButtonCameraRecenter,
      AirfixTouchButtonPrimaryFire,
      AirfixTouchButtonSecondaryFire,
      AirfixTouchButtonWeaponNext,
      AirfixTouchButtonRearView,
      AirfixTouchButtonThrottleIncrease,
      AirfixTouchButtonThrottleDecrease,
  };

  for (UITouch *touch in touches) {
    if (_interactionGeneration != interactionGeneration) {
      return;
    }
    const CGPoint point = [touch locationInView:self];
    BOOL claimed = NO;
    for (const AirfixTouchButton button : capturePriority) {
      const NSUInteger index = buttonIndex(button);
      if (_buttonTouches[index] != nil ||
          !CGRectContainsPoint(_buttonCaptureFrames[index], point)) {
        continue;
      }
      if (button == AirfixTouchButtonThrottleIncrease &&
          _buttonTouches[buttonIndex(AirfixTouchButtonThrottleDecrease)] !=
              nil) {
        continue;
      }
      if (button == AirfixTouchButtonThrottleDecrease &&
          _buttonTouches[buttonIndex(AirfixTouchButtonThrottleIncrease)] !=
              nil) {
        continue;
      }

      _buttonTouches[index] = touch;
      if (button == AirfixTouchButtonWeaponNext) {
        _weaponStartPoint = point;
        _weaponStartTimestamp = touch.timestamp;
        _weaponGestureActive = YES;
        _weaponSelecting = NO;
        _weaponHasCandidate = NO;
        _weaponCandidateSlot = 0U;
        [self updateVisualState];
        __weak AirfixTouchControlsView *weakSelf = self;
        __weak UITouch *weakTouch = touch;
        dispatch_after(
            dispatch_time(DISPATCH_TIME_NOW,
                          static_cast<int64_t>(kWeaponSelectionHoldDuration *
                                               NSEC_PER_SEC)),
            dispatch_get_main_queue(), ^{
              AirfixTouchControlsView *strongSelf = weakSelf;
              UITouch *strongTouch = weakTouch;
              if (strongSelf == nil || strongTouch == nil ||
                  strongSelf->_interactionGeneration != interactionGeneration ||
                  !strongSelf->_weaponGestureActive ||
                  strongSelf->_buttonTouches[index] != strongTouch) {
                return;
              }
              if (strongSelf->_weaponSelecting) {
                return;
              }
              strongSelf->_weaponSelecting = YES;
              strongSelf->_weaponHasCandidate = NO;
              [strongSelf updateVisualState];
              [strongSelf updateAccessibilityValues];
            });
      } else {
        [self setPhysicalButton:button pressed:YES force:YES];
      }
      if (_interactionGeneration != interactionGeneration) {
        return;
      }
      claimed = YES;
      break;
    }
    if (claimed) {
      continue;
    }

    if (_stickTouch == nil && CGRectContainsPoint(_stickCaptureFrame, point)) {
      _stickTouch = touch;
      [self updateStickForTouch:touch force:YES];
      if (_interactionGeneration != interactionGeneration) {
        return;
      }
      continue;
    }
    if (_throttleTouch == nil &&
        CGRectContainsPoint(_throttleCaptureFrame, point)) {
      _throttleTouch = touch;
      _throttleDetentTracker.reset();
      [_hapticFeedbackRouter prepareSelectionFeedback];
      [self updateThrottleForTouch:touch force:YES];
      if (_interactionGeneration != interactionGeneration) {
        return;
      }
      continue;
    }
    if (_lookTouch == nil && [self pointEligibleForCameraLook:point]) {
      _lookTouch = touch;
      _lookOrigin = point;
      [self publishLookX:0 lookY:0 forceX:YES forceY:YES ownedByTouch:touch];
      if (_interactionGeneration != interactionGeneration) {
        return;
      }
    }
  }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches
           withEvent:(nullable UIEvent *)event {
  (void)event;
  const NSUInteger interactionGeneration = _interactionGeneration;
  if (_stickTouch != nil && [touches containsObject:_stickTouch]) {
    [self updateStickForTouch:_stickTouch force:NO];
    if (_interactionGeneration != interactionGeneration) {
      return;
    }
  }
  if (_throttleTouch != nil && [touches containsObject:_throttleTouch]) {
    [self updateThrottleForTouch:_throttleTouch force:NO];
    if (_interactionGeneration != interactionGeneration) {
      return;
    }
  }
  if (_lookTouch != nil && [touches containsObject:_lookTouch]) {
    [self updateLookForTouch:_lookTouch force:NO];
    if (_interactionGeneration != interactionGeneration) {
      return;
    }
  }
  UITouch *weaponTouch =
      _buttonTouches[buttonIndex(AirfixTouchButtonWeaponNext)];
  if (weaponTouch != nil && [touches containsObject:weaponTouch]) {
    [self updateWeaponForTouch:weaponTouch];
  }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches
           withEvent:(nullable UIEvent *)event {
  (void)event;
  [self releaseControlsForTouches:touches cancelled:NO];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches
               withEvent:(nullable UIEvent *)event {
  (void)event;
  [self releaseControlsForTouches:touches cancelled:YES];
}

- (void)releaseControlsForTouches:(NSSet<UITouch *> *)touches
                        cancelled:(BOOL)cancelled {
  if (touches.count == 0U) {
    return;
  }

  const NSUInteger interactionGeneration = _interactionGeneration;
  const BOOL releaseStick =
      _stickTouch != nil && [touches containsObject:_stickTouch];
  const BOOL releaseThrottle =
      _throttleTouch != nil && [touches containsObject:_throttleTouch];
  const BOOL releaseLook =
      _lookTouch != nil && [touches containsObject:_lookTouch];
  BOOL releaseButtons[kButtonCount]{};
  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    releaseButtons[index] = _buttonTouches[index] != nil &&
                            [touches containsObject:_buttonTouches[index]];
  }

  const NSUInteger weaponIndex = buttonIndex(AirfixTouchButtonWeaponNext);
  BOOL commitWeapon = NO;
  BOOL cycleWeapon = NO;
  uint8_t selectedSlot = _weaponCandidateSlot;
  if (releaseButtons[weaponIndex] && !cancelled) {
    UITouch *weaponTouch = _buttonTouches[weaponIndex];
    [self updateWeaponForTouch:weaponTouch];
    commitWeapon = _weaponSelecting && _weaponHasCandidate;
    cycleWeapon = !_weaponSelecting;
    selectedSlot = _weaponCandidateSlot;
  }

  if (releaseThrottle && !cancelled) {
    [self updateThrottleForTouch:_throttleTouch force:NO];
    if (_interactionGeneration != interactionGeneration) {
      return;
    }
  }

  if (releaseStick) {
    _stickTouch = nil;
  }
  if (releaseThrottle) {
    _throttleTouch = nil;
    _throttleDetentTracker.reset();
  }
  if (releaseLook) {
    _lookTouch = nil;
  }
  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    if (releaseButtons[index]) {
      _buttonTouches[index] = nil;
    }
  }
  if (releaseButtons[weaponIndex]) {
    _weaponGestureActive = NO;
    _weaponSelecting = NO;
    _weaponHasCandidate = NO;
  }

  if (releaseStick) {
    [self publishStickBank:0
                     pitch:0
                 forceBank:YES
                forcePitch:YES
              ownedByTouch:nil];
    if (_interactionGeneration != interactionGeneration) {
      return;
    }
  }
  if (releaseLook) {
    [self publishLookX:0 lookY:0 forceX:YES forceY:YES ownedByTouch:nil];
    if (_interactionGeneration != interactionGeneration) {
      return;
    }
  }
  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    if (releaseButtons[index] && index != weaponIndex) {
      [self setPhysicalButton:static_cast<AirfixTouchButton>(index)
                      pressed:NO
                        force:NO];
      if (_interactionGeneration != interactionGeneration) {
        return;
      }
    }
  }
  if (commitWeapon) {
    [self commitWeaponSlot:selectedSlot];
  } else if (cycleWeapon) {
    [self pulseButton:AirfixTouchButtonWeaponNext asynchronously:NO];
  }
  if (_interactionGeneration != interactionGeneration) {
    return;
  }
  [self updateVisualState];
}

- (void)updateStickForTouch:(UITouch *)touch force:(BOOL)force {
  if (touch == nil || touch != _stickTouch) {
    return;
  }
  const CGPoint point = [touch locationInView:self];
  const CGPoint center = _stickBaseView.center;
  CGFloat horizontal = point.x - center.x;
  CGFloat vertical = point.y - center.y;
  if (!std::isfinite(horizontal) || !std::isfinite(vertical) ||
      !std::isfinite(_stickTravelRadius) || _stickTravelRadius <= 0.0) {
    [self publishStickBank:0
                     pitch:0
                 forceBank:force
                forcePitch:force
              ownedByTouch:touch];
    return;
  }
  const CGFloat magnitude = std::hypot(horizontal, vertical);
  if (std::isfinite(magnitude) && magnitude > _stickTravelRadius) {
    const CGFloat scale = _stickTravelRadius / magnitude;
    horizontal *= scale;
    vertical *= scale;
  }
  [self publishStickBank:q15FromUnitValue(horizontal / _stickTravelRadius)
                   pitch:q15FromUnitValue(-vertical / _stickTravelRadius)
               forceBank:force
              forcePitch:force
            ownedByTouch:touch];
}

- (void)updateThrottleForTouch:(UITouch *)touch force:(BOOL)force {
  if (touch == nil || touch != _throttleTouch) {
    return;
  }
  const CGPoint point = [touch locationInView:_throttleTrackView];
  const CGFloat inset = std::min<CGFloat>(
      10.0, CGRectGetHeight(_throttleTrackView.bounds) * 0.15);
  const CGFloat usableHeight = std::max<CGFloat>(
      1.0, CGRectGetHeight(_throttleTrackView.bounds) - 2.0 * inset);
  const CGFloat unit =
      (CGRectGetMaxY(_throttleTrackView.bounds) - inset - point.y) /
      usableHeight;
  const int16_t value = q15FromPositiveUnitValue(unit);
  const auto hapticEvent = _throttleDetentTracker.observe(value);
  const NSUInteger interactionGeneration = _interactionGeneration;
  [self publishAxis:AirfixTouchAxisThrottleSet value:value force:force];
  if (hapticEvent.has_value() &&
      _interactionGeneration == interactionGeneration &&
      _throttleTouch == touch) {
    [self playHapticEvent:*hapticEvent];
  }
}

- (void)updateLookForTouch:(UITouch *)touch force:(BOOL)force {
  if (touch == nil || touch != _lookTouch) {
    return;
  }
  const CGPoint point = [touch locationInView:self];
  CGFloat horizontal = point.x - _lookOrigin.x;
  CGFloat vertical = point.y - _lookOrigin.y;
  if (!std::isfinite(horizontal) || !std::isfinite(vertical) ||
      !std::isfinite(_lookTravelRadius) || _lookTravelRadius <= 0.0) {
    [self publishLookX:0 lookY:0 forceX:force forceY:force ownedByTouch:touch];
    return;
  }
  horizontal = std::clamp(horizontal, -_lookTravelRadius, _lookTravelRadius);
  vertical = std::clamp(vertical, -_lookTravelRadius, _lookTravelRadius);
  [self publishLookX:q15FromUnitValue(horizontal / _lookTravelRadius)
               lookY:q15FromUnitValue(-vertical / _lookTravelRadius)
              forceX:force
              forceY:force
        ownedByTouch:touch];
}

- (void)updateWeaponForTouch:(UITouch *)touch {
  const NSUInteger weaponIndex = buttonIndex(AirfixTouchButtonWeaponNext);
  if (touch == nil || touch != _buttonTouches[weaponIndex]) {
    return;
  }
  const CGPoint point = [touch locationInView:self];
  const CGFloat horizontal = point.x - _weaponStartPoint.x;
  const CGFloat vertical = point.y - _weaponStartPoint.y;
  const CGFloat distance = std::hypot(horizontal, vertical);
  const NSTimeInterval duration =
      std::max<NSTimeInterval>(0.0, touch.timestamp - _weaponStartTimestamp);
  if (!_weaponSelecting &&
      ((std::isfinite(distance) && distance >= kWeaponSelectionDistance) ||
       duration >= kWeaponSelectionHoldDuration)) {
    _weaponSelecting = YES;
  }
  if (!_weaponSelecting) {
    return;
  }
  if (!std::isfinite(distance) || distance < kWeaponSelectionDistance) {
    [self updateVisualState];
    [self updateAccessibilityValues];
    return;
  }

  constexpr CGFloat twoPi = static_cast<CGFloat>(M_PI * 2.0);
  constexpr CGFloat sector = twoPi / static_cast<CGFloat>(kWeaponSlotCount);
  CGFloat angle = std::atan2(-vertical, horizontal);
  if (!std::isfinite(angle)) {
    angle = 0.0;
  }
  if (angle < 0.0) {
    angle += twoPi;
  }
  const NSInteger roundedSector =
      static_cast<NSInteger>(std::floor((angle + sector * 0.5) / sector));
  const BOOL hadCandidate = _weaponHasCandidate;
  const uint8_t previousCandidate = _weaponCandidateSlot;
  _weaponCandidateSlot = static_cast<uint8_t>(roundedSector % kWeaponSlotCount);
  _weaponHasCandidate = YES;
  if (!hadCandidate || previousCandidate != _weaponCandidateSlot) {
    [self playHapticEvent:airfix::input::TouchControlsHapticEvent::
                              controlSelection];
  }
  [self updateVisualState];
  [self updateAccessibilityValues];
}

- (void)publishStickBank:(int16_t)bank
                   pitch:(int16_t)pitch
               forceBank:(BOOL)forceBank
              forcePitch:(BOOL)forcePitch
            ownedByTouch:(nullable UITouch *)touch {
  if (touch != nil && _stickTouch != touch) {
    return;
  }
  const BOOL bankChanged = forceBank || _bankValue != bank;
  const BOOL pitchChanged = forcePitch || _pitchValue != pitch;
  if (!bankChanged && !pitchChanged) {
    return;
  }

  _bankValue = bank;
  _pitchValue = pitch;
  const NSUInteger generation = ++_axisGeneration;
  [self updateVisualState];
  [self updateAccessibilityValues];
  id<AirfixTouchControlsViewDelegate> delegate = self.delegate;
  if (bankChanged) {
    [delegate touchControlsView:self
                  didChangeAxis:AirfixTouchAxisBank
                          value:bank];
  }
  if (!pitchChanged) {
    return;
  }
  const BOOL ownershipIntact = touch == nil || _stickTouch == touch;
  if (_axisGeneration != generation || !ownershipIntact ||
      self.delegate != delegate) {
    return;
  }
  [delegate touchControlsView:self
                didChangeAxis:AirfixTouchAxisPitch
                        value:pitch];
}

- (void)publishLookX:(int16_t)lookX
               lookY:(int16_t)lookY
              forceX:(BOOL)forceX
              forceY:(BOOL)forceY
        ownedByTouch:(nullable UITouch *)touch {
  if (touch != nil && _lookTouch != touch) {
    return;
  }
  const BOOL xChanged = forceX || _lookXValue != lookX;
  const BOOL yChanged = forceY || _lookYValue != lookY;
  if (!xChanged && !yChanged) {
    return;
  }

  _lookXValue = lookX;
  _lookYValue = lookY;
  const NSUInteger generation = ++_axisGeneration;
  [self updateVisualState];
  [self updateAccessibilityValues];
  id<AirfixTouchControlsViewDelegate> delegate = self.delegate;
  if (xChanged) {
    [delegate touchControlsView:self
                  didChangeAxis:AirfixTouchAxisCameraLookX
                          value:lookX];
  }
  if (!yChanged) {
    return;
  }
  const BOOL ownershipIntact = touch == nil || _lookTouch == touch;
  if (_axisGeneration != generation || !ownershipIntact ||
      self.delegate != delegate) {
    return;
  }
  [delegate touchControlsView:self
                didChangeAxis:AirfixTouchAxisCameraLookY
                        value:lookY];
}

- (void)publishAxis:(AirfixTouchAxis)axis
              value:(int16_t)value
              force:(BOOL)force {
  int16_t *storedValue = nullptr;
  switch (axis) {
  case AirfixTouchAxisBank:
    storedValue = &_bankValue;
    break;
  case AirfixTouchAxisPitch:
    storedValue = &_pitchValue;
    break;
  case AirfixTouchAxisThrottleSet:
    storedValue = &_throttleValue;
    break;
  case AirfixTouchAxisCameraLookX:
    storedValue = &_lookXValue;
    break;
  case AirfixTouchAxisCameraLookY:
    storedValue = &_lookYValue;
    break;
  }
  if (storedValue == nullptr || (!force && *storedValue == value)) {
    return;
  }
  *storedValue = value;
  ++_axisGeneration;
  [self updateVisualState];
  [self updateAccessibilityValues];
  [self.delegate touchControlsView:self didChangeAxis:axis value:value];
}

- (void)setPhysicalButton:(AirfixTouchButton)button
                  pressed:(BOOL)pressed
                    force:(BOOL)force {
  if (!validButton(button)) {
    return;
  }
  const NSUInteger index = buttonIndex(button);
  _physicalButtonPressed[index] = pressed;
  [self updateEffectiveButton:button force:force];
}

- (void)updateEffectiveButton:(AirfixTouchButton)button force:(BOOL)force {
  if (!validButton(button)) {
    return;
  }
  const NSUInteger index = buttonIndex(button);
  const BOOL wasPressed = _buttonPressed[index];
  const BOOL pressed = _physicalButtonPressed[index] ||
                       _accessibilityButtonLatched[index] ||
                       _accessibilityButtonPulse[index];
  if (!force && _buttonPressed[index] == pressed) {
    return;
  }
  _buttonPressed[index] = pressed;
  [self updateVisualState];
  [self updateAccessibilityValues];
  if (!wasPressed && pressed && !_isCancellingAll) {
    const auto hapticEvent =
        airfix::input::touchControlPressHapticEvent(elementForButton(button));
    if (hapticEvent.has_value()) {
      [self playHapticEvent:*hapticEvent];
    }
  }
  [self.delegate touchControlsView:self didChangeButton:button pressed:pressed];
}

- (void)pulseButton:(AirfixTouchButton)button
     asynchronously:(BOOL)asynchronously {
  if (!validButton(button) || _isCancellingAll) {
    return;
  }
  const NSUInteger index = buttonIndex(button);
  _accessibilityButtonPulse[index] = YES;
  [self updateEffectiveButton:button force:NO];

  if (!asynchronously) {
    _accessibilityButtonPulse[index] = NO;
    [self updateEffectiveButton:button force:NO];
    return;
  }

  __weak AirfixTouchControlsView *weakSelf = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    AirfixTouchControlsView *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    strongSelf->_accessibilityButtonPulse[index] = NO;
    [strongSelf updateEffectiveButton:button force:NO];
  });
}

- (void)commitWeaponSlot:(uint8_t)slot {
  if (slot >= kWeaponSlotCount || _isCancellingAll) {
    return;
  }
  _lastWeaponSlot = slot;
  _hasSelectedWeaponSlot = YES;
  [self updateVisualState];
  [self updateAccessibilityValues];
  [self.delegate touchControlsView:self didSelectWeaponSlot:slot];
}

- (void)playHapticEvent:(airfix::input::TouchControlsHapticEvent)event {
  NSAssert(NSThread.isMainThread, @"Touch haptics belong to the main thread");
  if (!NSThread.isMainThread ||
      _hapticsMode != airfix::input::TouchControlsHapticsMode::system) {
    return;
  }
  switch (event) {
  case airfix::input::TouchControlsHapticEvent::controlSelection:
  case airfix::input::TouchControlsHapticEvent::throttleIdleDetent:
  case airfix::input::TouchControlsHapticEvent::throttleMidpointDetent:
  case airfix::input::TouchControlsHapticEvent::throttleFullDetent:
    [_hapticFeedbackRouter playSelectionFeedback];
    break;
  }
}

- (void)cancelAllTouches {
  NSAssert(NSThread.isMainThread,
           @"Touch controls must be cancelled on the main thread");
  if (_isCancellingAll) {
    return;
  }
  _isCancellingAll = YES;
  ++_interactionGeneration;
  ++_axisGeneration;

  _stickTouch = nil;
  _throttleTouch = nil;
  _lookTouch = nil;
  _weaponGestureActive = NO;
  _weaponSelecting = NO;
  _weaponHasCandidate = NO;
  _throttleDetentTracker.reset();
  [_hapticFeedbackRouter cancelFeedback];
  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    _buttonTouches[index] = nil;
    _physicalButtonPressed[index] = NO;
    _accessibilityButtonLatched[index] = NO;
    _accessibilityButtonPulse[index] = NO;
  }

  [self publishStickBank:0
                   pitch:0
               forceBank:YES
              forcePitch:YES
            ownedByTouch:nil];
  [self publishLookX:0 lookY:0 forceX:YES forceY:YES ownedByTouch:nil];
  [self publishAxis:AirfixTouchAxisThrottleSet value:_throttleValue force:YES];
  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    [self updateEffectiveButton:static_cast<AirfixTouchButton>(index)
                          force:YES];
  }

  [self.delegate touchControlsViewDidCancelAll:self];
  _isCancellingAll = NO;
}

- (void)updateVisualState {
  const BOOL stickActive =
      _stickTouch != nil || _bankValue != 0 || _pitchValue != 0;
  _stickBaseView.backgroundColor =
      stickActive
          ? panelColor(0.72)
          : panelColor(scaledRestingAlpha(0.48, _restingOpacityPercent));
  _stickBaseView.layer.borderColor =
      flightAccentColor(stickActive ? 1.0 : 0.78).CGColor;
  _stickKnobView.backgroundColor =
      stickActive
          ? flightAccentColor(0.92)
          : panelColor(scaledRestingAlpha(0.76, _restingOpacityPercent));

  CGFloat horizontal =
      static_cast<CGFloat>(_bankValue) / static_cast<CGFloat>(kQ15Maximum);
  CGFloat vertical =
      -static_cast<CGFloat>(_pitchValue) / static_cast<CGFloat>(kQ15Maximum);
  const CGFloat stickMagnitude = std::hypot(horizontal, vertical);
  if (stickMagnitude > 1.0) {
    horizontal /= stickMagnitude;
    vertical /= stickMagnitude;
  }
  _stickKnobView.center = CGPointMake(
      CGRectGetMidX(_stickBaseView.bounds) + horizontal * _stickTravelRadius,
      CGRectGetMidY(_stickBaseView.bounds) + vertical * _stickTravelRadius);

  if (!CGRectIsEmpty(_throttleTrackView.bounds)) {
    const CGFloat inset = std::min<CGFloat>(
        10.0, CGRectGetHeight(_throttleTrackView.bounds) * 0.15);
    const CGFloat usableHeight = std::max<CGFloat>(
        1.0, CGRectGetHeight(_throttleTrackView.bounds) - 2.0 * inset);
    const CGFloat throttleUnit = static_cast<CGFloat>(_throttleValue) /
                                 static_cast<CGFloat>(kQ15Maximum);
    const CGFloat thumbCenterY = CGRectGetMaxY(_throttleTrackView.bounds) -
                                 inset - throttleUnit * usableHeight;
    _throttleThumbView.center =
        CGPointMake(CGRectGetMidX(_throttleTrackView.bounds), thumbCenterY);
    _throttleFillView.frame = CGRectMake(
        CGRectGetMidX(_throttleTrackView.bounds) - 3.0, thumbCenterY, 6.0,
        std::max<CGFloat>(0.0, CGRectGetMaxY(_throttleTrackView.bounds) -
                                   inset - thumbCenterY));
  }
  _throttleTrackView.backgroundColor =
      _throttleTouch != nil
          ? panelColor(0.78)
          : panelColor(scaledRestingAlpha(0.52, _restingOpacityPercent));
  _throttleThumbView.backgroundColor =
      _throttleTouch != nil
          ? flightAccentColor(0.94)
          : panelColor(scaledRestingAlpha(0.84, _restingOpacityPercent));

  const BOOL lookActive =
      _lookTouch != nil || _lookXValue != 0 || _lookYValue != 0;
  _lookRegionView.backgroundColor =
      lookActive ? panelColor(0.28)
                 : panelColor(scaledRestingAlpha(0.12, _restingOpacityPercent));
  _lookRegionView.layer.borderColor =
      flightAccentColor(lookActive ? 0.66 : 0.20).CGColor;

  for (NSUInteger index = 0U; index < kButtonCount; ++index) {
    const AirfixTouchButton button = static_cast<AirfixTouchButton>(index);
    const BOOL active =
        _buttonPressed[index] ||
        (button == AirfixTouchButtonWeaponNext && _weaponGestureActive);
    UIColor *accent = flightAccentColor(active ? 0.96 : 0.76);
    UIColor *resting =
        panelColor(scaledRestingAlpha(0.52, _restingOpacityPercent));
    if (button == AirfixTouchButtonPrimaryFire) {
      accent = fireAccentColor(active ? 1.0 : 0.82);
      resting = [UIColor
          colorWithRed:0.20
                 green:0.065
                  blue:0.035
                 alpha:scaledRestingAlpha(0.56, _restingOpacityPercent)];
    } else if (button == AirfixTouchButtonSecondaryFire) {
      accent = secondaryAccentColor(active ? 1.0 : 0.82);
      resting = [UIColor
          colorWithRed:0.20
                 green:0.13
                  blue:0.025
                 alpha:scaledRestingAlpha(0.56, _restingOpacityPercent)];
    }
    _buttonViews[index].backgroundColor = active ? accent : resting;
    _buttonViews[index].layer.borderColor =
        (active ? UIColor.whiteColor : accent).CGColor;
    _buttonViews[index].transform = active
                                        ? CGAffineTransformMakeScale(0.94, 0.94)
                                        : CGAffineTransformIdentity;
    _buttonLabels[index].textColor = UIColor.whiteColor;
  }
  UILabel *weaponLabel =
      _buttonLabels[buttonIndex(AirfixTouchButtonWeaponNext)];
  weaponLabel.text =
      _weaponSelecting
          ? (_weaponHasCandidate
                 ? [NSString
                       stringWithFormat:@"W%u", static_cast<unsigned int>(
                                                    _weaponCandidateSlot + 1U)]
                 : @"W\u2013")
          : @"WEAP";
}

- (void)updateAccessibilityValues {
  if (_controlAccessibilityElements.count !=
      static_cast<NSUInteger>(AirfixAccessibilityControlCount)) {
    return;
  }

  for (AirfixTouchAccessibilityElement
           *element in _controlAccessibilityElements) {
    NSString *value = nil;
    switch (element.control) {
    case AirfixAccessibilityControlBank: {
      const NSInteger percent = static_cast<NSInteger>(
          std::lround(std::abs(static_cast<CGFloat>(_bankValue)) * 100.0 /
                      static_cast<CGFloat>(kQ15Maximum)));
      value =
          _bankValue == 0
              ? NSLocalizedString(@"Centered", nil)
              : [NSString
                    stringWithFormat:NSLocalizedString(@"%ld percent %@", nil),
                                     static_cast<long>(percent),
                                     _bankValue > 0
                                         ? NSLocalizedString(@"right", nil)
                                         : NSLocalizedString(@"left", nil)];
      break;
    }
    case AirfixAccessibilityControlPitch: {
      const NSInteger percent = static_cast<NSInteger>(
          std::lround(std::abs(static_cast<CGFloat>(_pitchValue)) * 100.0 /
                      static_cast<CGFloat>(kQ15Maximum)));
      value =
          _pitchValue == 0
              ? NSLocalizedString(@"Centered", nil)
              : [NSString
                    stringWithFormat:NSLocalizedString(@"%ld percent %@", nil),
                                     static_cast<long>(percent),
                                     _pitchValue > 0
                                         ? NSLocalizedString(@"up", nil)
                                         : NSLocalizedString(@"down", nil)];
      break;
    }
    case AirfixAccessibilityControlThrottle: {
      const NSInteger percent = static_cast<NSInteger>(
          std::lround(static_cast<CGFloat>(_throttleValue) * 100.0 /
                      static_cast<CGFloat>(kQ15Maximum)));
      value = [NSString stringWithFormat:NSLocalizedString(@"%ld percent", nil),
                                         static_cast<long>(percent)];
      break;
    }
    case AirfixAccessibilityControlCameraLookX: {
      const NSInteger percent = static_cast<NSInteger>(
          std::lround(std::abs(static_cast<CGFloat>(_lookXValue)) * 100.0 /
                      static_cast<CGFloat>(kQ15Maximum)));
      value =
          _lookXValue == 0
              ? NSLocalizedString(@"Centered", nil)
              : [NSString
                    stringWithFormat:NSLocalizedString(@"%ld percent %@", nil),
                                     static_cast<long>(percent),
                                     _lookXValue > 0
                                         ? NSLocalizedString(@"right", nil)
                                         : NSLocalizedString(@"left", nil)];
      break;
    }
    case AirfixAccessibilityControlCameraLookY: {
      const NSInteger percent = static_cast<NSInteger>(
          std::lround(std::abs(static_cast<CGFloat>(_lookYValue)) * 100.0 /
                      static_cast<CGFloat>(kQ15Maximum)));
      value =
          _lookYValue == 0
              ? NSLocalizedString(@"Centered", nil)
              : [NSString
                    stringWithFormat:NSLocalizedString(@"%ld percent %@", nil),
                                     static_cast<long>(percent),
                                     _lookYValue > 0
                                         ? NSLocalizedString(@"up", nil)
                                         : NSLocalizedString(@"down", nil)];
      break;
    }
    case AirfixAccessibilityControlThrottleIncrease:
      value = _buttonPressed[buttonIndex(AirfixTouchButtonThrottleIncrease)]
                  ? NSLocalizedString(@"Increasing", nil)
                  : NSLocalizedString(@"Stopped", nil);
      break;
    case AirfixAccessibilityControlThrottleDecrease:
      value = _buttonPressed[buttonIndex(AirfixTouchButtonThrottleDecrease)]
                  ? NSLocalizedString(@"Decreasing", nil)
                  : NSLocalizedString(@"Stopped", nil);
      break;
    case AirfixAccessibilityControlPrimaryFire:
      value = _buttonPressed[buttonIndex(AirfixTouchButtonPrimaryFire)]
                  ? NSLocalizedString(@"Firing", nil)
                  : NSLocalizedString(@"Stopped", nil);
      break;
    case AirfixAccessibilityControlSecondaryFire:
      value = _buttonPressed[buttonIndex(AirfixTouchButtonSecondaryFire)]
                  ? NSLocalizedString(@"Firing", nil)
                  : NSLocalizedString(@"Stopped", nil);
      break;
    case AirfixAccessibilityControlRearView:
      value = _buttonPressed[buttonIndex(AirfixTouchButtonRearView)]
                  ? NSLocalizedString(@"Active", nil)
                  : NSLocalizedString(@"Inactive", nil);
      break;
    case AirfixAccessibilityControlWeapon:
      if (_weaponSelecting) {
        value =
            _weaponHasCandidate
                ? [NSString stringWithFormat:NSLocalizedString(
                                                 @"Selecting weapon %u", nil),
                                             static_cast<unsigned int>(
                                                 _weaponCandidateSlot + 1U)]
                : NSLocalizedString(@"Choose a direction", nil);
      } else if (_hasSelectedWeaponSlot) {
        value = [NSString
            stringWithFormat:NSLocalizedString(@"Weapon %u", nil),
                             static_cast<unsigned int>(_lastWeaponSlot + 1U)];
      } else {
        value = NSLocalizedString(@"Cycle weapon", nil);
      }
      break;
    case AirfixAccessibilityControlMission:
    case AirfixAccessibilityControlCameraCycle:
    case AirfixAccessibilityControlCameraRecenter:
    case AirfixAccessibilityControlPause:
    case AirfixAccessibilityControlCount:
      break;
    }
    element.accessibilityValue = value;
  }
}

- (void)adjustAccessibilityAxis:(AirfixTouchAxis)axis
                      direction:(NSInteger)direction {
  if (_isCancellingAll ||
      (axis != AirfixTouchAxisBank && axis != AirfixTouchAxisPitch &&
       axis != AirfixTouchAxisCameraLookX &&
       axis != AirfixTouchAxisCameraLookY)) {
    return;
  }

  int16_t current = 0;
  switch (axis) {
  case AirfixTouchAxisBank:
    current = _bankValue;
    break;
  case AirfixTouchAxisPitch:
    current = _pitchValue;
    break;
  case AirfixTouchAxisCameraLookX:
    current = _lookXValue;
    break;
  case AirfixTouchAxisCameraLookY:
    current = _lookYValue;
    break;
  case AirfixTouchAxisThrottleSet:
    return;
  }
  const int32_t adjusted = std::clamp(
      static_cast<int32_t>(current) +
          (direction < 0 ? -static_cast<int32_t>(kAccessibilityAxisStep)
                         : static_cast<int32_t>(kAccessibilityAxisStep)),
      -static_cast<int32_t>(kQ15Maximum), static_cast<int32_t>(kQ15Maximum));
  const int16_t value = static_cast<int16_t>(adjusted);
  if (axis == AirfixTouchAxisBank || axis == AirfixTouchAxisPitch) {
    const int16_t bank = axis == AirfixTouchAxisBank ? value : _bankValue;
    const int16_t pitch = axis == AirfixTouchAxisPitch ? value : _pitchValue;
    [self publishStickBank:bank
                     pitch:pitch
                 forceBank:NO
                forcePitch:NO
              ownedByTouch:nil];
  } else {
    const int16_t lookX =
        axis == AirfixTouchAxisCameraLookX ? value : _lookXValue;
    const int16_t lookY =
        axis == AirfixTouchAxisCameraLookY ? value : _lookYValue;
    [self publishLookX:lookX lookY:lookY forceX:NO forceY:NO ownedByTouch:nil];
  }
}

- (void)centerAccessibilityAxis:(AirfixTouchAxis)axis {
  if (_isCancellingAll) {
    return;
  }
  switch (axis) {
  case AirfixTouchAxisBank:
    [self publishStickBank:0
                     pitch:_pitchValue
                 forceBank:YES
                forcePitch:NO
              ownedByTouch:nil];
    break;
  case AirfixTouchAxisPitch:
    [self publishStickBank:_bankValue
                     pitch:0
                 forceBank:NO
                forcePitch:YES
              ownedByTouch:nil];
    break;
  case AirfixTouchAxisCameraLookX:
    [self publishLookX:0
                 lookY:_lookYValue
                forceX:YES
                forceY:NO
          ownedByTouch:nil];
    break;
  case AirfixTouchAxisCameraLookY:
    [self publishLookX:_lookXValue
                 lookY:0
                forceX:NO
                forceY:YES
          ownedByTouch:nil];
    break;
  case AirfixTouchAxisThrottleSet:
    break;
  }
}

- (void)adjustAccessibilityThrottle:(NSInteger)direction {
  if (_isCancellingAll) {
    return;
  }
  const int32_t adjusted = std::clamp(
      static_cast<int32_t>(_throttleValue) +
          (direction < 0 ? -static_cast<int32_t>(kAccessibilityThrottleStep)
                         : static_cast<int32_t>(kAccessibilityThrottleStep)),
      0, static_cast<int32_t>(kQ15Maximum));
  [self publishAxis:AirfixTouchAxisThrottleSet
              value:static_cast<int16_t>(adjusted)
              force:NO];
}

- (void)setAccessibilityHeldButton:(AirfixTouchButton)button
                           pressed:(BOOL)pressed {
  if (_isCancellingAll || !validButton(button) || !heldButton(button)) {
    return;
  }
  if (pressed && (button == AirfixTouchButtonThrottleIncrease ||
                  button == AirfixTouchButtonThrottleDecrease)) {
    const AirfixTouchButton opposite =
        button == AirfixTouchButtonThrottleIncrease
            ? AirfixTouchButtonThrottleDecrease
            : AirfixTouchButtonThrottleIncrease;
    _accessibilityButtonLatched[buttonIndex(opposite)] = NO;
    [self updateEffectiveButton:opposite force:NO];
  }
  _accessibilityButtonLatched[buttonIndex(button)] = pressed;
  [self updateEffectiveButton:button force:NO];
}

- (void)toggleAccessibilityHeldButton:(AirfixTouchButton)button {
  if (!validButton(button) || !heldButton(button)) {
    return;
  }
  [self setAccessibilityHeldButton:button
                           pressed:!_accessibilityButtonLatched[buttonIndex(
                                       button)]];
}

- (void)activateAccessibilityEdgeButton:(AirfixTouchButton)button {
  if (!validButton(button) || heldButton(button)) {
    return;
  }
  [self pulseButton:button asynchronously:YES];
}

- (void)setHidden:(BOOL)hidden {
  if (hidden && !self.hidden) {
    [self cancelAllTouches];
  }
  [super setHidden:hidden];
}

- (void)setUserInteractionEnabled:(BOOL)userInteractionEnabled {
  if (!userInteractionEnabled && self.userInteractionEnabled) {
    [self cancelAllTouches];
  }
  [super setUserInteractionEnabled:userInteractionEnabled];
}

- (void)willMoveToWindow:(nullable UIWindow *)newWindow {
  if (newWindow == nil && self.window != nil) {
    [self cancelAllTouches];
  }
  [super willMoveToWindow:newWindow];
}

@end
