#import "AirfixTouchControlsView.h"

#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr int16_t kQ15Maximum = 32767;
constexpr int16_t kAccessibilityAxisStep = 8192;
constexpr CGFloat kMinimumTargetSize = 44.0;

typedef NS_ENUM(NSInteger, AirfixAccessibilityControl) {
    AirfixAccessibilityControlBank = 0,
    AirfixAccessibilityControlPitch,
    AirfixAccessibilityControlPrimaryFire,
    AirfixAccessibilityControlPause,
};

[[nodiscard]] BOOL rectanglesDiffer(
    const CGRect first, const CGRect second) noexcept {
    return !CGRectEqualToRect(first, second);
}

[[nodiscard]] BOOL insetsDiffer(
    const UIEdgeInsets first, const UIEdgeInsets second) noexcept {
    return !UIEdgeInsetsEqualToEdgeInsets(first, second);
}

[[nodiscard]] int16_t q15FromUnitValue(const CGFloat value) noexcept {
    if (!std::isfinite(value)) {
        return 0;
    }
    const CGFloat clamped = std::clamp(value, -1.0, 1.0);
    return static_cast<int16_t>(
        std::lround(clamped * static_cast<CGFloat>(kQ15Maximum)));
}

[[nodiscard]] UIColor* panelColor(const CGFloat alpha) {
    return [UIColor colorWithRed:0.035 green:0.078 blue:0.125 alpha:alpha];
}

[[nodiscard]] UIColor* flightAccentColor(const CGFloat alpha) {
    return [UIColor colorWithRed:0.32 green:0.80 blue:0.95 alpha:alpha];
}

[[nodiscard]] UIColor* fireAccentColor(const CGFloat alpha) {
    return [UIColor colorWithRed:1.0 green:0.37 blue:0.11 alpha:alpha];
}

} // namespace

@class AirfixTouchAccessibilityElement;

@interface AirfixTouchControlsView () {
    UIView* _stickBaseView;
    UIView* _stickHorizontalGuideView;
    UIView* _stickVerticalGuideView;
    UIView* _stickKnobView;
    UIView* _fireView;
    UILabel* _fireLabel;
    UIView* _pauseView;
    UILabel* _pauseLabel;

    UITouch* _stickTouch;
    UITouch* _fireTouch;
    UITouch* _pauseTouch;

    int16_t _bankValue;
    int16_t _pitchValue;
    BOOL _primaryFirePressed;
    BOOL _pausePressed;

    CGRect _stickCaptureFrame;
    CGRect _fireCaptureFrame;
    CGRect _pauseCaptureFrame;
    CGFloat _stickTravelRadius;

    CGRect _laidOutBounds;
    UIEdgeInsets _laidOutSafeAreaInsets;
    BOOL _hasCompletedLayout;
    BOOL _isCancellingAll;
    NSUInteger _stickGeneration;

    NSArray<AirfixTouchAccessibilityElement*>* _controlAccessibilityElements;
}

- (void)configureTouchControls;
- (void)configureAccessibilityElements;
- (void)enforceMinimumCaptureSize:(CGRect*)captureFrame
                           within:(CGRect)safeBounds;
- (void)updateAccessibilityFrames;
- (void)updateStickForTouch:(UITouch*)touch force:(BOOL)force;
- (void)releaseControlsForTouches:(NSSet<UITouch*>*)touches;
- (void)publishStickBank:(int16_t)bank
                   pitch:(int16_t)pitch
               forceBank:(BOOL)forceBank
              forcePitch:(BOOL)forcePitch
            ownedByTouch:(nullable UITouch*)touch;
- (void)publishButton:(AirfixTouchButton)button
               pressed:(BOOL)pressed
                 force:(BOOL)force;
- (void)updateVisualState;
- (void)updateAccessibilityValues;
- (void)adjustAccessibilityAxis:(AirfixTouchAxis)axis
                      direction:(NSInteger)direction;
- (void)centerAccessibilityAxis:(AirfixTouchAxis)axis;
- (void)activateAccessibilityButton:(AirfixTouchButton)button;
@end

@interface AirfixTouchAccessibilityElement : UIAccessibilityElement
@property(nonatomic, weak) AirfixTouchControlsView* controlsView;
@property(nonatomic) AirfixAccessibilityControl control;
- (AirfixTouchAxis)axis;
- (BOOL)isAxisControl;
- (BOOL)increaseAxis:(UIAccessibilityCustomAction*)action;
- (BOOL)decreaseAxis:(UIAccessibilityCustomAction*)action;
- (BOOL)centerAxis:(UIAccessibilityCustomAction*)action;
@end

@implementation AirfixTouchAccessibilityElement

- (AirfixTouchAxis)axis {
    switch (self.control) {
    case AirfixAccessibilityControlBank:
        return AirfixTouchAxisBank;
    case AirfixAccessibilityControlPitch:
        return AirfixTouchAxisPitch;
    case AirfixAccessibilityControlPrimaryFire:
    case AirfixAccessibilityControlPause:
        return AirfixTouchAxisBank;
    }
    return AirfixTouchAxisBank;
}

- (BOOL)isAxisControl {
    return self.control == AirfixAccessibilityControlBank ||
        self.control == AirfixAccessibilityControlPitch;
}

- (BOOL)accessibilityActivate {
    switch (self.control) {
    case AirfixAccessibilityControlPrimaryFire:
        [self.controlsView
            activateAccessibilityButton:AirfixTouchButtonPrimaryFire];
        return YES;
    case AirfixAccessibilityControlPause:
        [self.controlsView activateAccessibilityButton:AirfixTouchButtonPause];
        return YES;
    case AirfixAccessibilityControlBank:
    case AirfixAccessibilityControlPitch:
        return NO;
    }
    return NO;
}

- (void)accessibilityIncrement {
    if ([self isAxisControl]) {
        [self.controlsView adjustAccessibilityAxis:self.axis direction:1];
    }
}

- (void)accessibilityDecrement {
    if ([self isAxisControl]) {
        [self.controlsView adjustAccessibilityAxis:self.axis direction:-1];
    }
}

- (BOOL)increaseAxis:(UIAccessibilityCustomAction*)action {
    (void)action;
    [self accessibilityIncrement];
    return YES;
}

- (BOOL)decreaseAxis:(UIAccessibilityCustomAction*)action {
    (void)action;
    [self accessibilityDecrement];
    return YES;
}

- (BOOL)centerAxis:(UIAccessibilityCustomAction*)action {
    (void)action;
    if (![self isAxisControl]) {
        return NO;
    }
    [self.controlsView centerAccessibilityAxis:self.axis];
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

- (instancetype)initWithCoder:(NSCoder*)coder {
    self = [super initWithCoder:coder];
    if (self != nil) {
        [self configureTouchControls];
    }
    return self;
}

- (void)configureTouchControls {
    self.backgroundColor = UIColor.clearColor;
    self.opaque = NO;
    self.multipleTouchEnabled = YES;
    self.isAccessibilityElement = NO;

    _stickBaseView = [[UIView alloc] initWithFrame:CGRectZero];
    _stickHorizontalGuideView = [[UIView alloc] initWithFrame:CGRectZero];
    _stickVerticalGuideView = [[UIView alloc] initWithFrame:CGRectZero];
    _stickKnobView = [[UIView alloc] initWithFrame:CGRectZero];
    _fireView = [[UIView alloc] initWithFrame:CGRectZero];
    _fireLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _pauseView = [[UIView alloc] initWithFrame:CGRectZero];
    _pauseLabel = [[UILabel alloc] initWithFrame:CGRectZero];

    NSArray<UIView*>* inertViews = @[
        _stickBaseView,
        _stickHorizontalGuideView,
        _stickVerticalGuideView,
        _stickKnobView,
        _fireView,
        _fireLabel,
        _pauseView,
        _pauseLabel,
    ];
    for (UIView* view in inertViews) {
        view.userInteractionEnabled = NO;
        view.isAccessibilityElement = NO;
    }

    _stickBaseView.backgroundColor = panelColor(0.46);
    _stickBaseView.layer.borderColor = flightAccentColor(0.60).CGColor;
    _stickBaseView.layer.borderWidth = 1.5;
    _stickBaseView.clipsToBounds = NO;

    _stickHorizontalGuideView.backgroundColor = flightAccentColor(0.25);
    _stickVerticalGuideView.backgroundColor = flightAccentColor(0.25);

    _stickKnobView.backgroundColor = panelColor(0.90);
    _stickKnobView.layer.borderColor = flightAccentColor(0.90).CGColor;
    _stickKnobView.layer.borderWidth = 2.0;

    _fireView.backgroundColor =
        [UIColor colorWithRed:0.20 green:0.065 blue:0.035 alpha:0.72];
    _fireView.layer.borderColor = fireAccentColor(0.78).CGColor;
    _fireView.layer.borderWidth = 2.0;

    _fireLabel.text = @"FIRE";
    _fireLabel.textAlignment = NSTextAlignmentCenter;
    _fireLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.92];
    _fireLabel.adjustsFontSizeToFitWidth = YES;
    _fireLabel.minimumScaleFactor = 0.75;

    _pauseView.backgroundColor = panelColor(0.72);
    _pauseView.layer.borderColor =
        [UIColor colorWithWhite:1.0 alpha:0.56].CGColor;
    _pauseView.layer.borderWidth = 1.5;

    _pauseLabel.text = @"\u2016";
    _pauseLabel.textAlignment = NSTextAlignmentCenter;
    _pauseLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.92];
    _pauseLabel.adjustsFontSizeToFitWidth = YES;

    [_stickBaseView addSubview:_stickHorizontalGuideView];
    [_stickBaseView addSubview:_stickVerticalGuideView];
    [_stickBaseView addSubview:_stickKnobView];
    [_fireView addSubview:_fireLabel];
    [_pauseView addSubview:_pauseLabel];
    [self addSubview:_stickBaseView];
    [self addSubview:_fireView];
    [self addSubview:_pauseView];

    [self configureAccessibilityElements];
    [self updateVisualState];
    [self updateAccessibilityValues];
}

- (void)configureAccessibilityElements {
    AirfixTouchAccessibilityElement* bank =
        [[AirfixTouchAccessibilityElement alloc]
            initWithAccessibilityContainer:self];
    bank.controlsView = self;
    bank.control = AirfixAccessibilityControlBank;
    bank.accessibilityLabel = @"Bank";
    bank.accessibilityHint =
        @"Swipe up or down to bank right or left. Actions also center bank.";
    bank.accessibilityTraits = UIAccessibilityTraitAdjustable;
    bank.accessibilityCustomActions = @[
        [[UIAccessibilityCustomAction alloc]
            initWithName:@"Bank right"
                  target:bank
                selector:@selector(increaseAxis:)],
        [[UIAccessibilityCustomAction alloc]
            initWithName:@"Bank left"
                  target:bank
                selector:@selector(decreaseAxis:)],
        [[UIAccessibilityCustomAction alloc]
            initWithName:@"Center bank"
                  target:bank
                selector:@selector(centerAxis:)],
    ];

    AirfixTouchAccessibilityElement* pitch =
        [[AirfixTouchAccessibilityElement alloc]
            initWithAccessibilityContainer:self];
    pitch.controlsView = self;
    pitch.control = AirfixAccessibilityControlPitch;
    pitch.accessibilityLabel = @"Pitch";
    pitch.accessibilityHint =
        @"Swipe up or down to pitch up or down. Actions also center pitch.";
    pitch.accessibilityTraits = UIAccessibilityTraitAdjustable;
    pitch.accessibilityCustomActions = @[
        [[UIAccessibilityCustomAction alloc]
            initWithName:@"Pitch up"
                  target:pitch
                selector:@selector(increaseAxis:)],
        [[UIAccessibilityCustomAction alloc]
            initWithName:@"Pitch down"
                  target:pitch
                selector:@selector(decreaseAxis:)],
        [[UIAccessibilityCustomAction alloc]
            initWithName:@"Center pitch"
                  target:pitch
                selector:@selector(centerAxis:)],
    ];

    AirfixTouchAccessibilityElement* fire =
        [[AirfixTouchAccessibilityElement alloc]
            initWithAccessibilityContainer:self];
    fire.controlsView = self;
    fire.control = AirfixAccessibilityControlPrimaryFire;
    fire.accessibilityLabel = @"Primary fire";
    fire.accessibilityHint = @"Double-tap to fire.";
    fire.accessibilityTraits = UIAccessibilityTraitButton;

    AirfixTouchAccessibilityElement* pause =
        [[AirfixTouchAccessibilityElement alloc]
            initWithAccessibilityContainer:self];
    pause.controlsView = self;
    pause.control = AirfixAccessibilityControlPause;
    pause.accessibilityLabel = @"Pause or resume";
    pause.accessibilityHint =
        @"Double-tap to pause or resume gameplay.";
    pause.accessibilityTraits = UIAccessibilityTraitButton;

    _controlAccessibilityElements = @[ bank, pitch, fire, pause ];
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
        // Touch coordinates are tied to the previous geometry. Neutralize
        // before publishing any replacement capture regions.
        [self cancelAllTouches];
    }

    _laidOutBounds = self.bounds;
    _laidOutSafeAreaInsets = safeInsets;
    _hasCompletedLayout = YES;

    CGRect safeBounds = UIEdgeInsetsInsetRect(self.bounds, safeInsets);
    if (CGRectIsEmpty(safeBounds) || CGRectIsNull(safeBounds)) {
        safeBounds = self.bounds;
    }

    const CGFloat safeWidth = CGRectGetWidth(safeBounds);
    const CGFloat safeHeight = CGRectGetHeight(safeBounds);
    const BOOL compact = safeHeight <= 375.0 || safeWidth < 700.0;
    const CGFloat outerMargin = compact ? 14.0 : 22.0;

    CGFloat stickDiameter = compact ? 108.0 : 132.0;
    stickDiameter = std::max(
        88.0,
        std::min(stickDiameter, safeHeight - (outerMargin * 2.0)));
    const CGFloat knobDiameter = compact ? 42.0 : 50.0;
    const CGFloat fireDiameter = compact ? 76.0 : 92.0;
    const CGFloat pauseSize = compact ? 48.0 : 52.0;

    const CGPoint stickCenter = CGPointMake(
        CGRectGetMinX(safeBounds) + outerMargin + (stickDiameter * 0.5),
        CGRectGetMaxY(safeBounds) - outerMargin - (stickDiameter * 0.5));
    const CGRect stickFrame = CGRectMake(
        stickCenter.x - (stickDiameter * 0.5),
        stickCenter.y - (stickDiameter * 0.5),
        stickDiameter,
        stickDiameter);
    _stickBaseView.bounds =
        CGRectMake(0.0, 0.0, stickDiameter, stickDiameter);
    _stickBaseView.center = stickCenter;
    _stickBaseView.layer.cornerRadius = stickDiameter * 0.5;

    const CGFloat guideInset = compact ? 15.0 : 18.0;
    _stickHorizontalGuideView.frame = CGRectMake(
        guideInset,
        std::floor((stickDiameter - 1.0) * 0.5),
        stickDiameter - (guideInset * 2.0),
        1.0);
    _stickVerticalGuideView.frame = CGRectMake(
        std::floor((stickDiameter - 1.0) * 0.5),
        guideInset,
        1.0,
        stickDiameter - (guideInset * 2.0));
    _stickKnobView.bounds =
        CGRectMake(0.0, 0.0, knobDiameter, knobDiameter);
    _stickKnobView.layer.cornerRadius = knobDiameter * 0.5;
    _stickTravelRadius =
        std::max(1.0, (stickDiameter - knobDiameter) * 0.5 - 5.0);

    const CGPoint fireCenter = CGPointMake(
        CGRectGetMaxX(safeBounds) - outerMargin - (fireDiameter * 0.5),
        CGRectGetMaxY(safeBounds) - outerMargin - (fireDiameter * 0.5));
    const CGRect fireFrame = CGRectMake(
        fireCenter.x - (fireDiameter * 0.5),
        fireCenter.y - (fireDiameter * 0.5),
        fireDiameter,
        fireDiameter);
    _fireView.bounds = CGRectMake(0.0, 0.0, fireDiameter, fireDiameter);
    _fireView.center = fireCenter;
    _fireView.layer.cornerRadius = fireDiameter * 0.5;
    _fireLabel.frame =
        CGRectInset(_fireView.bounds, fireDiameter * 0.17, fireDiameter * 0.17);
    _fireLabel.font = [UIFont systemFontOfSize:(compact ? 15.0 : 18.0)
                                       weight:UIFontWeightBold];

    const CGPoint pauseCenter = CGPointMake(
        CGRectGetMaxX(safeBounds) - outerMargin - (pauseSize * 0.5),
        CGRectGetMinY(safeBounds) + outerMargin + (pauseSize * 0.5));
    const CGRect pauseFrame = CGRectMake(
        pauseCenter.x - (pauseSize * 0.5),
        pauseCenter.y - (pauseSize * 0.5),
        pauseSize,
        pauseSize);
    _pauseView.bounds = CGRectMake(0.0, 0.0, pauseSize, pauseSize);
    _pauseView.center = pauseCenter;
    _pauseView.layer.cornerRadius = compact ? 13.0 : 15.0;
    _pauseLabel.frame = CGRectInset(_pauseView.bounds, 11.0, 9.0);
    _pauseLabel.font = [UIFont systemFontOfSize:(compact ? 22.0 : 25.0)
                                        weight:UIFontWeightSemibold];

    _stickCaptureFrame = CGRectIntersection(
        safeBounds, CGRectInset(stickFrame, -14.0, -14.0));
    _fireCaptureFrame = CGRectIntersection(
        safeBounds, CGRectInset(fireFrame, -12.0, -12.0));
    _pauseCaptureFrame = CGRectIntersection(
        safeBounds, CGRectInset(pauseFrame, -4.0, -4.0));

    [self enforceMinimumCaptureSize:&_stickCaptureFrame within:safeBounds];
    [self enforceMinimumCaptureSize:&_fireCaptureFrame within:safeBounds];
    [self enforceMinimumCaptureSize:&_pauseCaptureFrame within:safeBounds];
    [self updateAccessibilityFrames];
    [self updateVisualState];
}

- (void)enforceMinimumCaptureSize:(CGRect*)captureFrame
                           within:(CGRect)safeBounds {
    if (captureFrame == nullptr) {
        return;
    }
    CGRect frame = *captureFrame;
    const CGFloat width =
        std::max(kMinimumTargetSize, CGRectGetWidth(frame));
    const CGFloat height =
        std::max(kMinimumTargetSize, CGRectGetHeight(frame));
    frame = CGRectMake(
        CGRectGetMidX(frame) - (width * 0.5),
        CGRectGetMidY(frame) - (height * 0.5),
        width,
        height);

    if (CGRectGetMinX(frame) < CGRectGetMinX(safeBounds)) {
        frame.origin.x = CGRectGetMinX(safeBounds);
    }
    if (CGRectGetMaxX(frame) > CGRectGetMaxX(safeBounds)) {
        frame.origin.x = CGRectGetMaxX(safeBounds) - CGRectGetWidth(frame);
    }
    if (CGRectGetMinY(frame) < CGRectGetMinY(safeBounds)) {
        frame.origin.y = CGRectGetMinY(safeBounds);
    }
    if (CGRectGetMaxY(frame) > CGRectGetMaxY(safeBounds)) {
        frame.origin.y = CGRectGetMaxY(safeBounds) - CGRectGetHeight(frame);
    }
    *captureFrame = CGRectIntersection(frame, safeBounds);
}

- (void)updateAccessibilityFrames {
    if (_controlAccessibilityElements.count != 4U) {
        return;
    }

    const CGFloat stickHalfHeight =
        std::max(kMinimumTargetSize, CGRectGetHeight(_stickCaptureFrame) * 0.5);
    CGRect pitchFrame = _stickCaptureFrame;
    pitchFrame.size.height = stickHalfHeight;
    CGRect bankFrame = _stickCaptureFrame;
    bankFrame.origin.y = CGRectGetMaxY(_stickCaptureFrame) - stickHalfHeight;
    bankFrame.size.height = stickHalfHeight;

    _controlAccessibilityElements[AirfixAccessibilityControlBank]
        .accessibilityFrameInContainerSpace = bankFrame;
    _controlAccessibilityElements[AirfixAccessibilityControlPitch]
        .accessibilityFrameInContainerSpace = pitchFrame;
    _controlAccessibilityElements[AirfixAccessibilityControlPrimaryFire]
        .accessibilityFrameInContainerSpace = _fireCaptureFrame;
    _controlAccessibilityElements[AirfixAccessibilityControlPause]
        .accessibilityFrameInContainerSpace = _pauseCaptureFrame;
}

- (BOOL)pointInside:(CGPoint)point withEvent:(nullable UIEvent*)event {
    if (![super pointInside:point withEvent:event]) {
        return NO;
    }
    return CGRectContainsPoint(_stickCaptureFrame, point) ||
        CGRectContainsPoint(_fireCaptureFrame, point) ||
        CGRectContainsPoint(_pauseCaptureFrame, point);
}

- (nullable UIView*)hitTest:(CGPoint)point withEvent:(nullable UIEvent*)event {
    if (self.hidden || self.alpha < 0.01 || !self.userInteractionEnabled) {
        return nil;
    }
    return [self pointInside:point withEvent:event] ? self : nil;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches
           withEvent:(nullable UIEvent*)event {
    (void)event;
    for (UITouch* touch in touches) {
        const CGPoint point = [touch locationInView:self];
        if (_pauseTouch == nil &&
            CGRectContainsPoint(_pauseCaptureFrame, point)) {
            _pauseTouch = touch;
            [self publishButton:AirfixTouchButtonPause
                        pressed:YES
                          force:YES];
        }
        else if (_fireTouch == nil &&
                 CGRectContainsPoint(_fireCaptureFrame, point)) {
            _fireTouch = touch;
            [self publishButton:AirfixTouchButtonPrimaryFire
                        pressed:YES
                          force:YES];
        }
        else if (_stickTouch == nil &&
                 CGRectContainsPoint(_stickCaptureFrame, point)) {
            _stickTouch = touch;
            [self updateStickForTouch:touch force:YES];
        }
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches
           withEvent:(nullable UIEvent*)event {
    (void)event;
    if (_stickTouch != nil && [touches containsObject:_stickTouch]) {
        [self updateStickForTouch:_stickTouch force:NO];
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches
           withEvent:(nullable UIEvent*)event {
    (void)event;
    [self releaseControlsForTouches:touches];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches
               withEvent:(nullable UIEvent*)event {
    (void)event;
    // UIKit can cancel one touch while leaving other fingers active. Releasing
    // only controls owned by this set preserves valid stick-plus-fire input.
    [self releaseControlsForTouches:touches];
}

- (void)releaseControlsForTouches:(NSSet<UITouch*>*)touches {
    if (touches.count == 0U) {
        return;
    }

    const BOOL releaseStick =
        _stickTouch != nil && [touches containsObject:_stickTouch];
    const BOOL releaseFire =
        _fireTouch != nil && [touches containsObject:_fireTouch];
    const BOOL releasePause =
        _pauseTouch != nil && [touches containsObject:_pauseTouch];

    // Drop every affected ownership claim before publishing. A delegate may
    // synchronously invoke global cancellation from the first callback.
    if (releaseStick) {
        _stickTouch = nil;
    }
    if (releaseFire) {
        _fireTouch = nil;
    }
    if (releasePause) {
        _pauseTouch = nil;
    }

    if (releaseStick) {
        [self publishStickBank:0
                         pitch:0
                     forceBank:YES
                    forcePitch:YES
                  ownedByTouch:nil];
    }
    if (releaseFire) {
        [self publishButton:AirfixTouchButtonPrimaryFire
                    pressed:NO
                      force:NO];
    }
    if (releasePause) {
        [self publishButton:AirfixTouchButtonPause
                    pressed:NO
                      force:NO];
    }
}

- (void)updateStickForTouch:(UITouch*)touch force:(BOOL)force {
    if (touch == nil || touch != _stickTouch) {
        return;
    }

    const CGPoint point = [touch locationInView:self];
    const CGPoint center = _stickBaseView.center;
    CGFloat horizontal = point.x - center.x;
    CGFloat vertical = point.y - center.y;
    const BOOL validGeometry =
        std::isfinite(horizontal) && std::isfinite(vertical) &&
        std::isfinite(_stickTravelRadius) && _stickTravelRadius > 0.0;
    if (!validGeometry) {
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

    const int16_t bank = q15FromUnitValue(horizontal / _stickTravelRadius);
    const int16_t pitch = q15FromUnitValue(-vertical / _stickTravelRadius);
    [self publishStickBank:bank
                     pitch:pitch
                 forceBank:force
                forcePitch:force
              ownedByTouch:touch];
}

- (void)publishStickBank:(int16_t)bank
                   pitch:(int16_t)pitch
               forceBank:(BOOL)forceBank
              forcePitch:(BOOL)forcePitch
            ownedByTouch:(nullable UITouch*)touch {
    if (touch != nil && _stickTouch != touch) {
        return;
    }

    const BOOL bankChanged = forceBank || _bankValue != bank;
    const BOOL pitchChanged = forcePitch || _pitchValue != pitch;
    if (!bankChanged && !pitchChanged) {
        return;
    }

    // Store the pair atomically from the delegate's perspective, then refresh
    // visuals once. The generation prevents a callback from publishing the
    // stale second half after it synchronously cancels or mutates the overlay.
    _bankValue = bank;
    _pitchValue = pitch;
    const NSUInteger generation = ++_stickGeneration;
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
    if (_stickGeneration != generation || !ownershipIntact ||
        self.delegate != delegate) {
        return;
    }
    [delegate touchControlsView:self
                 didChangeAxis:AirfixTouchAxisPitch
                         value:pitch];
}

- (void)publishButton:(AirfixTouchButton)button
               pressed:(BOOL)pressed
                 force:(BOOL)force {
    BOOL* storedPressed = nullptr;
    switch (button) {
    case AirfixTouchButtonPrimaryFire:
        storedPressed = &_primaryFirePressed;
        break;
    case AirfixTouchButtonPause:
        storedPressed = &_pausePressed;
        break;
    }
    if (storedPressed == nullptr) {
        return;
    }
    if (!force && *storedPressed == pressed) {
        return;
    }
    *storedPressed = pressed;
    [self updateVisualState];

    id<AirfixTouchControlsViewDelegate> delegate = self.delegate;
    [delegate touchControlsView:self
               didChangeButton:button
                       pressed:pressed];
}

- (void)cancelAllTouches {
    NSAssert(
        NSThread.isMainThread,
        @"Touch controls must be cancelled on the main thread");
    if (_isCancellingAll) {
        return;
    }
    _isCancellingAll = YES;
    ++_stickGeneration;

    _stickTouch = nil;
    _fireTouch = nil;
    _pauseTouch = nil;

    [self publishStickBank:0
                     pitch:0
                 forceBank:YES
                forcePitch:YES
              ownedByTouch:nil];
    [self publishButton:AirfixTouchButtonPrimaryFire pressed:NO force:YES];
    [self publishButton:AirfixTouchButtonPause pressed:NO force:YES];

    id<AirfixTouchControlsViewDelegate> delegate = self.delegate;
    [delegate touchControlsViewDidCancelAll:self];
    _isCancellingAll = NO;
}

- (void)updateVisualState {
    const BOOL stickActive =
        _stickTouch != nil || _bankValue != 0 || _pitchValue != 0;
    _stickBaseView.backgroundColor =
        stickActive ? panelColor(0.68) : panelColor(0.46);
    _stickBaseView.layer.borderColor =
        flightAccentColor(stickActive ? 0.95 : 0.60).CGColor;
    _stickKnobView.backgroundColor =
        stickActive ? flightAccentColor(0.88) : panelColor(0.90);
    _stickKnobView.layer.borderColor =
        (stickActive
             ? [UIColor colorWithWhite:1.0 alpha:0.96]
             : flightAccentColor(0.90))
            .CGColor;

    CGFloat horizontal =
        static_cast<CGFloat>(_bankValue) / static_cast<CGFloat>(kQ15Maximum);
    CGFloat vertical =
        -static_cast<CGFloat>(_pitchValue) / static_cast<CGFloat>(kQ15Maximum);
    const CGFloat magnitude = std::hypot(horizontal, vertical);
    if (magnitude > 1.0) {
        horizontal /= magnitude;
        vertical /= magnitude;
    }
    _stickKnobView.center = CGPointMake(
        CGRectGetMidX(_stickBaseView.bounds) +
            (horizontal * _stickTravelRadius),
        CGRectGetMidY(_stickBaseView.bounds) + (vertical * _stickTravelRadius));

    _fireView.backgroundColor = _primaryFirePressed
        ? fireAccentColor(0.94)
        : [UIColor colorWithRed:0.20 green:0.065 blue:0.035 alpha:0.72];
    _fireView.layer.borderColor =
        fireAccentColor(_primaryFirePressed ? 1.0 : 0.78).CGColor;
    _fireView.transform = _primaryFirePressed
        ? CGAffineTransformMakeScale(0.94, 0.94)
        : CGAffineTransformIdentity;
    _fireLabel.textColor = _primaryFirePressed
        ? UIColor.whiteColor
        : [UIColor colorWithWhite:1.0 alpha:0.92];

    _pauseView.backgroundColor =
        _pausePressed ? flightAccentColor(0.82) : panelColor(0.72);
    _pauseView.layer.borderColor =
        [UIColor colorWithWhite:1.0 alpha:(_pausePressed ? 0.96 : 0.56)]
            .CGColor;
    _pauseView.transform = _pausePressed
        ? CGAffineTransformMakeScale(0.94, 0.94)
        : CGAffineTransformIdentity;
}

- (void)updateAccessibilityValues {
    if (_controlAccessibilityElements.count != 4U) {
        return;
    }

    const NSInteger bankPercent = static_cast<NSInteger>(std::lround(
        std::abs(static_cast<CGFloat>(_bankValue)) * 100.0 /
        static_cast<CGFloat>(kQ15Maximum)));
    NSString* bankValue = @"Centered";
    if (_bankValue > 0) {
        bankValue = [NSString stringWithFormat:@"%ld percent right",
            static_cast<long>(bankPercent)];
    }
    else if (_bankValue < 0) {
        bankValue = [NSString stringWithFormat:@"%ld percent left",
            static_cast<long>(bankPercent)];
    }
    _controlAccessibilityElements[AirfixAccessibilityControlBank]
        .accessibilityValue = bankValue;

    const NSInteger pitchPercent = static_cast<NSInteger>(std::lround(
        std::abs(static_cast<CGFloat>(_pitchValue)) * 100.0 /
        static_cast<CGFloat>(kQ15Maximum)));
    NSString* pitchValue = @"Centered";
    if (_pitchValue > 0) {
        pitchValue = [NSString stringWithFormat:@"%ld percent up",
            static_cast<long>(pitchPercent)];
    }
    else if (_pitchValue < 0) {
        pitchValue = [NSString stringWithFormat:@"%ld percent down",
            static_cast<long>(pitchPercent)];
    }
    _controlAccessibilityElements[AirfixAccessibilityControlPitch]
        .accessibilityValue = pitchValue;
}

- (void)adjustAccessibilityAxis:(AirfixTouchAxis)axis
                      direction:(NSInteger)direction {
    if (_isCancellingAll) {
        return;
    }
    if (axis != AirfixTouchAxisBank && axis != AirfixTouchAxisPitch) {
        return;
    }

    const int32_t current =
        axis == AirfixTouchAxisBank ? _bankValue : _pitchValue;
    const int32_t adjusted = std::clamp(
        current +
            (direction < 0 ? -static_cast<int32_t>(kAccessibilityAxisStep)
                           : static_cast<int32_t>(kAccessibilityAxisStep)),
        -static_cast<int32_t>(kQ15Maximum),
        static_cast<int32_t>(kQ15Maximum));
    const int16_t bank = axis == AirfixTouchAxisBank
        ? static_cast<int16_t>(adjusted)
        : _bankValue;
    const int16_t pitch = axis == AirfixTouchAxisPitch
        ? static_cast<int16_t>(adjusted)
        : _pitchValue;
    [self publishStickBank:bank
                     pitch:pitch
                 forceBank:NO
                forcePitch:NO
              ownedByTouch:nil];
}

- (void)centerAccessibilityAxis:(AirfixTouchAxis)axis {
    if (_isCancellingAll) {
        return;
    }
    if (axis == AirfixTouchAxisBank) {
        [self publishStickBank:0
                         pitch:_pitchValue
                     forceBank:YES
                    forcePitch:NO
                  ownedByTouch:nil];
    }
    else if (axis == AirfixTouchAxisPitch) {
        [self publishStickBank:_bankValue
                         pitch:0
                     forceBank:NO
                    forcePitch:YES
                  ownedByTouch:nil];
    }
}

- (void)activateAccessibilityButton:(AirfixTouchButton)button {
    if (_isCancellingAll) {
        return;
    }
    [self publishButton:button pressed:YES force:YES];

    __weak AirfixTouchControlsView* weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        AirfixTouchControlsView* strongSelf = weakSelf;
        if (strongSelf == nil) {
            return;
        }
        const BOOL physicalTouchActive =
            button == AirfixTouchButtonPrimaryFire
            ? strongSelf->_fireTouch != nil
            : strongSelf->_pauseTouch != nil;
        if (!physicalTouchActive) {
            [strongSelf publishButton:button pressed:NO force:NO];
        }
    });
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

- (void)willMoveToWindow:(nullable UIWindow*)newWindow {
    if (newWindow == nil && self.window != nil) {
        [self cancelAllTouches];
    }
    [super willMoveToWindow:newWindow];
}

@end
