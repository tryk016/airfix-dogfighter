#import "AirfixIOSInputCoordinator.h"

#import "AirfixIOSInputCoordinator+Private.hpp"
#import "AirfixTouchControlsView.h"

#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#include "airfix/input/ControllerInputBatchBridge.hpp"
#include "airfix/input/InputRouter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace {

constexpr CFTimeInterval kInputStepSeconds = 1.0 / 60.0;
constexpr CFTimeInterval kMaximumAccumulatedSeconds =
    kInputStepSeconds * 8.0;
constexpr std::uint64_t kDiagnosticHeartbeatTicks = 6U;
constexpr std::int32_t kControllerStickDeadzone = 4096;
constexpr std::int32_t kControllerTriggerActuation = 16384;
constexpr std::size_t kTouchAxisCount =
    static_cast<std::size_t>(AirfixTouchAxisCameraLookY) + 1U;
constexpr std::size_t kTouchButtonCount =
    static_cast<std::size_t>(AirfixTouchButtonCount);
constexpr std::size_t kTouchThrottleAxisIndex =
    static_cast<std::size_t>(AirfixTouchAxisThrottleSet);

constexpr airfix::input::SourceHandle kTouchSource{
    airfix::input::SourceKind::touch, 1U};
constexpr airfix::input::SourceHandle kControllerSource{
    airfix::input::SourceKind::controller, 1U};

[[nodiscard]] std::uint64_t diagnosticTimestamp() noexcept {
    const long double nanoseconds =
        static_cast<long double>(CACurrentMediaTime()) * 1000000000.0L;
    if (nanoseconds <= 0.0L) {
        return 0U;
    }
    if (nanoseconds >=
        static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(nanoseconds);
}

[[nodiscard]] airfix::input::Q15 q15(const int16_t value) noexcept {
    return airfix::input::clampQ15(static_cast<std::int32_t>(value));
}

[[nodiscard]] airfix::input::ControlId touchAxisControl(
    const AirfixTouchAxis axis) noexcept {
    switch (axis) {
    case AirfixTouchAxisBank:
        return airfix::input::controls::touch::bank;
    case AirfixTouchAxisPitch:
        return airfix::input::controls::touch::pitch;
    case AirfixTouchAxisThrottleSet:
        return airfix::input::controls::touch::throttleSet;
    case AirfixTouchAxisCameraLookX:
        return airfix::input::controls::touch::lookX;
    case AirfixTouchAxisCameraLookY:
        return airfix::input::controls::touch::lookY;
    }
    return {};
}

[[nodiscard]] airfix::input::ControlId touchButtonControl(
    const AirfixTouchButton button) noexcept {
    switch (button) {
    case AirfixTouchButtonThrottleIncrease:
        return airfix::input::controls::touch::throttleIncrease;
    case AirfixTouchButtonThrottleDecrease:
        return airfix::input::controls::touch::throttleDecrease;
    case AirfixTouchButtonPrimaryFire:
        return airfix::input::controls::touch::primaryFire;
    case AirfixTouchButtonSecondaryFire:
        return airfix::input::controls::touch::secondaryFire;
    case AirfixTouchButtonWeaponNext:
        return airfix::input::controls::touch::weaponNext;
    case AirfixTouchButtonRearView:
        return airfix::input::controls::touch::rearView;
    case AirfixTouchButtonCameraCycle:
        return airfix::input::controls::touch::cameraCycle;
    case AirfixTouchButtonCameraRecenter:
        return airfix::input::controls::touch::cameraRecenter;
    case AirfixTouchButtonMissionStatus:
        return airfix::input::controls::touch::missionStatus;
    case AirfixTouchButtonPause:
        return airfix::input::controls::touch::pause;
    case AirfixTouchButtonCount:
        break;
    }
    return {};
}

[[nodiscard]] airfix::input::ControllerSample controllerSample(
    const AirfixGameControllerSample& sample) noexcept {
    airfix::input::ControllerSample result{};
    result.bank = q15(sample.bank);
    result.pitch = q15(sample.pitch);
    result.lookX = q15(sample.lookX);
    result.lookY = q15(sample.lookY);
    result.primaryTriggerPressed =
        sample.primaryTrigger >= kControllerTriggerActuation;
    result.pausePressed = sample.menuPressed || sample.optionsPressed;
    result.secondaryTriggerPressed =
        sample.secondaryTrigger >= kControllerTriggerActuation;
    result.throttleUpPressed = sample.dpadUpPressed;
    result.throttleDownPressed = sample.dpadDownPressed;
    result.weaponNextPressed = sample.rightShoulderPressed;
    result.rearViewPressed = sample.leftShoulderPressed;
    result.cameraCyclePressed = sample.faceLeftPressed;
    result.missionStatusPressed = sample.faceTopPressed;
    result.uiConfirmPressed = sample.facePrimaryPressed;
    result.uiCancelPressed = sample.faceSecondaryPressed;
    result.cameraRecenterPressed = sample.rightStickClickPressed;
    return result;
}

[[nodiscard]] airfix::input::ControllerInputBatch controllerBatch(
    const AirfixGameControllerInputBatch& batch) noexcept {
    airfix::input::ControllerInputBatch result{};
    result.generation = batch.generation;
    result.startingState = controllerSample(batch.startingState);
    result.finalState = controllerSample(batch.finalState);
    result.edgeCount = static_cast<std::size_t>(batch.edgeCount);
    result.overflowed = batch.overflowed;
    const auto copyCount = std::min(
        result.edgeCount,
        airfix::input::ControllerInputBatch::edgeCapacity);
    for (std::size_t index = 0U; index < copyCount; ++index) {
        const auto& edge = batch.edges[index];
        result.edges[index] = {
            edge.generation,
            edge.order,
            static_cast<airfix::input::ControllerDigitalControl>(
                edge.control),
            static_cast<bool>(edge.pressed),
        };
    }
    return result;
}

static_assert(AirfixGameControllerDigitalEdgeCapacity ==
    airfix::input::ControllerInputBatch::edgeCapacity);
static_assert(AirfixGameControllerDigitalControlPrimaryTrigger ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::primaryTrigger));
static_assert(AirfixGameControllerDigitalControlPause ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::pause));
static_assert(AirfixGameControllerDigitalControlSecondaryTrigger ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::secondaryTrigger));
static_assert(AirfixGameControllerDigitalControlDpadUp ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::throttleUp));
static_assert(AirfixGameControllerDigitalControlDpadDown ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::throttleDown));
static_assert(AirfixGameControllerDigitalControlRightShoulder ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::weaponNext));
static_assert(AirfixGameControllerDigitalControlLeftShoulder ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::rearView));
static_assert(AirfixGameControllerDigitalControlFaceLeft ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::cameraCycle));
static_assert(AirfixGameControllerDigitalControlFaceTop ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::missionStatus));
static_assert(AirfixGameControllerDigitalControlFacePrimary ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::uiConfirm));
static_assert(AirfixGameControllerDigitalControlFaceSecondary ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::uiCancel));
static_assert(AirfixGameControllerDigitalControlRightStickClick ==
    static_cast<std::uint8_t>(
        airfix::input::ControllerDigitalControl::cameraRecenter));

} // namespace

@interface AirfixInputDiagnostics ()

- (instancetype)initWithTick:(uint64_t)tick
                        bank:(int16_t)bank
                       pitch:(int16_t)pitch
              primaryPressed:(BOOL)primaryPressed
                 primaryHeld:(BOOL)primaryHeld
             primaryReleased:(BOOL)primaryReleased
         controllerConnected:(BOOL)controllerConnected
        lastMeaningfulSource:(AirfixInputSource)lastMeaningfulSource
    NS_DESIGNATED_INITIALIZER;

@end

@implementation AirfixInputDiagnostics

- (instancetype)initWithTick:(uint64_t)tick
                        bank:(int16_t)bank
                       pitch:(int16_t)pitch
              primaryPressed:(BOOL)primaryPressed
                 primaryHeld:(BOOL)primaryHeld
             primaryReleased:(BOOL)primaryReleased
         controllerConnected:(BOOL)controllerConnected
        lastMeaningfulSource:(AirfixInputSource)lastMeaningfulSource {
    self = [super init];
    if (self != nil) {
        _tick = tick;
        _bank = bank;
        _pitch = pitch;
        _primaryPressed = primaryPressed;
        _primaryHeld = primaryHeld;
        _primaryReleased = primaryReleased;
        _controllerConnected = controllerConnected;
        _lastMeaningfulSource = lastMeaningfulSource;
    }
    return self;
}

@end

@interface AirfixUIInputSnapshot ()

- (instancetype)initWithTick:(uint64_t)tick
                 navigationX:(int16_t)navigationX
                 navigationY:(int16_t)navigationY
              confirmPressed:(BOOL)confirmPressed
               cancelPressed:(BOOL)cancelPressed
          tabPreviousPressed:(BOOL)tabPreviousPressed
              tabNextPressed:(BOOL)tabNextPressed
    NS_DESIGNATED_INITIALIZER;

@end

@implementation AirfixUIInputSnapshot

- (instancetype)initWithTick:(uint64_t)tick
                 navigationX:(int16_t)navigationX
                 navigationY:(int16_t)navigationY
              confirmPressed:(BOOL)confirmPressed
               cancelPressed:(BOOL)cancelPressed
          tabPreviousPressed:(BOOL)tabPreviousPressed
              tabNextPressed:(BOOL)tabNextPressed {
    self = [super init];
    if (self != nil) {
        _tick = tick;
        _navigationX = navigationX;
        _navigationY = navigationY;
        _confirmPressed = confirmPressed;
        _cancelPressed = cancelPressed;
        _tabPreviousPressed = tabPreviousPressed;
        _tabNextPressed = tabNextPressed;
    }
    return self;
}

@end

@class AirfixIOSInputCoordinator;

@interface AirfixInputDisplayLinkTarget : NSObject
@property(nonatomic, weak) AirfixIOSInputCoordinator* coordinator;
- (void)displayLinkDidFire:(CADisplayLink*)displayLink;
@end

@interface AirfixIOSInputCoordinator ()
    <AirfixTouchControlsViewDelegate, AirfixGameControllerAdapterDelegate> {
    airfix::input::InputRouter _router;
    __weak AirfixTouchControlsView* _touchControlsView;
    __strong AirfixGameControllerAdapter* _controllerAdapter;
    __strong CADisplayLink* _displayLink;
    __strong AirfixInputDisplayLinkTarget* _displayLinkTarget;
    airfix::ios::InputFrameConsumer _frameConsumer;

    std::uint64_t _nextSequence;
    std::uint64_t _inputTick;
    std::uint64_t _lastDiagnosticPublishTick;
    std::uint64_t _resetEpoch;
    CFTimeInterval _lastDisplayTimestamp;
    CFTimeInterval _inputAccumulator;

    std::array<int16_t, kTouchAxisCount> _touchAxes;
    std::array<BOOL, kTouchButtonCount> _touchButtons;

    airfix::input::ControllerInputBatchBridge _controllerBridge;
    std::array<
        airfix::input::ControllerInputEmission,
        airfix::input::ControllerInputBatchBridge::maximumEmissionCount>
        _controllerEmissions;

    AirfixInputSource _lastMeaningfulSource;
    BOOL _started;
    BOOL _foreground;
    BOOL _suppressControllerLossPause;
    BOOL _resettingSources;
    BOOL _handlingInputFailure;
    BOOL _terminalInputFailure;
}
@property(nonatomic, strong, readwrite) AirfixInputDiagnostics* diagnostics;
@property(nonatomic, strong, readwrite)
    AirfixGameControllerState* controllerState;

- (void)ensureDisplayLink;
- (void)displayLinkDidFire:(CADisplayLink*)displayLink;
- (void)performFixedInputTick;
- (BOOL)pollController;
- (BOOL)enqueueAxis:(int16_t)value
             source:(airfix::input::SourceHandle)source
            control:(airfix::input::ControlId)control;
- (BOOL)enqueueButton:(BOOL)pressed
               source:(airfix::input::SourceHandle)source
              control:(airfix::input::ControlId)control;
- (BOOL)enqueueWeaponSelection:(uint8_t)selection
                        source:(airfix::input::SourceHandle)source
                       control:(airfix::input::ControlId)control;
- (BOOL)takeNextSequence:(std::uint64_t*)sequence;
- (void)resetAllSources;
- (void)handleInputFailureTerminal:(BOOL)terminal;
- (void)notifyPauseForReason:(AirfixInputPauseReason)reason;
- (void)notifyControllerState:(AirfixGameControllerState*)state;
- (void)notifyDiagnostics:(AirfixInputDiagnostics*)diagnostics;
- (void)notifyUIInput:(AirfixUIInputSnapshot*)input;
- (void)performOnMain:(dispatch_block_t)block;
- (BOOL)setPrivateFrameConsumer:
    (const airfix::ios::InputFrameConsumer&)consumer;
@end

@implementation AirfixInputDisplayLinkTarget

- (void)displayLinkDidFire:(CADisplayLink*)displayLink {
    [self.coordinator displayLinkDidFire:displayLink];
}

@end

@implementation AirfixIOSInputCoordinator

- (instancetype)initWithTouchControlsView:
    (AirfixTouchControlsView*)touchControlsView {
    self = [super init];
    if (self != nil) {
        NSParameterAssert(touchControlsView != nil);
        _touchControlsView = touchControlsView;
        _nextSequence = 1U;
        _lastMeaningfulSource = AirfixInputSourceNone;
        _controllerAdapter = [[AirfixGameControllerAdapter alloc]
            initWithDelegate:self];
        _controllerState = _controllerAdapter.state;
        _diagnostics = [[AirfixInputDiagnostics alloc]
            initWithTick:0U
                    bank:0
                   pitch:0
          primaryPressed:NO
             primaryHeld:NO
         primaryReleased:NO
     controllerConnected:NO
    lastMeaningfulSource:AirfixInputSourceNone];
    }
    return self;
}

- (void)dealloc {
    _touchControlsView.delegate = nil;
    [_displayLink invalidate];
    _controllerAdapter.delegate = nil;
}

- (void)start {
    NSAssert(NSThread.isMainThread, @"Input coordinator is main-thread-affine");
    if (_started) {
        return;
    }
    _started = YES;
    _foreground =
        UIApplication.sharedApplication.applicationState !=
        UIApplicationStateBackground;
    _touchControlsView.delegate = self;
    [self resetAllSources];
    if (!_frameConsumer || _terminalInputFailure) {
        if (!_terminalInputFailure) {
            [self handleInputFailureTerminal:YES];
        }
        return;
    }
    if (_foreground) {
        [_controllerAdapter start];
        if (_terminalInputFailure) {
            return;
        }
        [self ensureDisplayLink];
        _displayLink.paused = NO;
    }
}

- (void)stop {
    NSAssert(NSThread.isMainThread, @"Input coordinator is main-thread-affine");
    if (!_started) {
        return;
    }
    [self resetAllSources];
    _started = NO;
    _foreground = NO;
    _touchControlsView.delegate = nil;
    _displayLink.paused = YES;
    _suppressControllerLossPause = YES;
    [_controllerAdapter stop];
    _suppressControllerLossPause = NO;
}

- (void)applicationWillResignActive {
    NSAssert(NSThread.isMainThread, @"Input lifecycle belongs to main");
    if (!_started) {
        return;
    }
    [self resetAllSources];
    [self notifyPauseForReason:AirfixInputPauseReasonLifecycle];
}

- (void)applicationDidEnterBackground {
    NSAssert(NSThread.isMainThread, @"Input lifecycle belongs to main");
    if (!_started) {
        return;
    }
    _foreground = NO;
    _displayLink.paused = YES;
    [self resetAllSources];
    _suppressControllerLossPause = YES;
    [_controllerAdapter stop];
    _suppressControllerLossPause = NO;
}

- (void)applicationWillEnterForeground {
    NSAssert(NSThread.isMainThread, @"Input lifecycle belongs to main");
    if (!_started) {
        return;
    }
    _foreground = YES;
    [self resetAllSources];
    if (_terminalInputFailure) {
        return;
    }
    [self notifyPauseForReason:AirfixInputPauseReasonLifecycle];
    [_controllerAdapter start];
    if (_terminalInputFailure) {
        return;
    }
    [self ensureDisplayLink];
    _lastDisplayTimestamp = 0.0;
    _inputAccumulator = 0.0;
    _displayLink.paused = NO;
}

- (void)resetForGameplayBoundary {
    NSAssert(NSThread.isMainThread, @"Gameplay input boundary belongs to main");
    [self resetAllSources];
}

- (AirfixNativeInputContext)inputContext {
    NSAssert(NSThread.isMainThread, @"Input context belongs to main");
    switch (_router.context()) {
    case airfix::input::InputContext::gameplay:
        return AirfixNativeInputContextGameplay;
    case airfix::input::InputContext::menu:
        return AirfixNativeInputContextMenu;
    case airfix::input::InputContext::modal:
        return AirfixNativeInputContextModal;
    case airfix::input::InputContext::controlEditor:
        return AirfixNativeInputContextControlEditor;
    case airfix::input::InputContext::count:
        break;
    }
    NSAssert(NO, @"Portable input context is invalid");
    return AirfixNativeInputContextGameplay;
}

- (void)setInputContext:(AirfixNativeInputContext)inputContext {
    NSAssert(NSThread.isMainThread, @"Input context belongs to main");
    airfix::input::InputContext portableContext{};
    switch (inputContext) {
    case AirfixNativeInputContextGameplay:
        portableContext = airfix::input::InputContext::gameplay;
        break;
    case AirfixNativeInputContextMenu:
        portableContext = airfix::input::InputContext::menu;
        break;
    case AirfixNativeInputContextModal:
        portableContext = airfix::input::InputContext::modal;
        break;
    case AirfixNativeInputContextControlEditor:
        portableContext = airfix::input::InputContext::controlEditor;
        break;
    default:
        NSAssert(NO, @"Native input context is invalid");
        return;
    }
    if (_router.context() == portableContext) {
        return;
    }
    [self resetAllSources];
    _router.setContext(portableContext);
}

- (BOOL)isOperational {
    NSAssert(NSThread.isMainThread, @"Input status belongs to main");
    return !_terminalInputFailure;
}

- (void)applicationDidBecomeActive {
    NSAssert(NSThread.isMainThread, @"Input lifecycle belongs to main");
    if (!_started) {
        return;
    }
    if (_terminalInputFailure) {
        return;
    }
    // Foreground/active is deliberately not a resume signal.
    _foreground = YES;
    [self ensureDisplayLink];
    _displayLink.paused = NO;
}

- (void)ensureDisplayLink {
    if (_displayLink != nil) {
        return;
    }
    _displayLinkTarget = [[AirfixInputDisplayLinkTarget alloc] init];
    _displayLinkTarget.coordinator = self;
    _displayLink = [CADisplayLink
        displayLinkWithTarget:_displayLinkTarget
                     selector:@selector(displayLinkDidFire:)];
    if (@available(iOS 15.0, *)) {
        _displayLink.preferredFrameRateRange =
            CAFrameRateRangeMake(60.0F, 60.0F, 60.0F);
    }
    else {
        _displayLink.preferredFramesPerSecond = 60;
    }
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop
                      forMode:NSRunLoopCommonModes];
}

- (void)displayLinkDidFire:(CADisplayLink*)displayLink {
    NSAssert(NSThread.isMainThread, @"Input ticks belong to main");
    if (!_started || !_foreground || _terminalInputFailure) {
        return;
    }

    CFTimeInterval elapsed = kInputStepSeconds;
    if (_lastDisplayTimestamp > 0.0) {
        elapsed = std::clamp(
            displayLink.timestamp - _lastDisplayTimestamp,
            0.0,
            kMaximumAccumulatedSeconds);
    }
    _lastDisplayTimestamp = displayLink.timestamp;
    _inputAccumulator = std::min(
        _inputAccumulator + elapsed, kMaximumAccumulatedSeconds);

    NSUInteger ticks = 0U;
    while (_inputAccumulator + std::numeric_limits<double>::epsilon() >=
            kInputStepSeconds &&
        ticks < 8U) {
        _inputAccumulator -= kInputStepSeconds;
        [self performFixedInputTick];
        ++ticks;
    }
}

- (void)performFixedInputTick {
    NSAssert(NSThread.isMainThread, @"Input ticks belong to main");
    if (_terminalInputFailure) {
        return;
    }
    if (_inputTick == std::numeric_limits<std::uint64_t>::max()) {
        [self handleInputFailureTerminal:YES];
        return;
    }
    if (![self pollController]) {
        return;
    }

    const auto frame = _router.tick(++_inputTick);
    const std::uint64_t frameEpoch = _resetEpoch;
    try {
        // Keep the active callable alive across reentrant sink replacement.
        // A completed fixed tick is offered once and is never replayed.
        airfix::ios::InputFrameConsumer activeConsumer(_frameConsumer);
        if (!activeConsumer) {
            [self handleInputFailureTerminal:YES];
            return;
        }
        activeConsumer(frame);
    }
    catch (...) {
        airfix::ios::InputFrameConsumer failedConsumer;
        _frameConsumer.swap(failedConsumer);
        [self handleInputFailureTerminal:YES];
        return;
    }
    if (_terminalInputFailure || _resetEpoch != frameEpoch) {
        // The consumer crossed a gameplay/lifecycle boundary. Do not publish
        // diagnostics or a pause request derived from the pre-reset frame.
        return;
    }

    const auto inputContext = _router.context();
    if (inputContext == airfix::input::InputContext::menu ||
        inputContext == airfix::input::InputContext::modal) {
        AirfixUIInputSnapshot* const input =
            [[AirfixUIInputSnapshot alloc]
                initWithTick:frame.simulationTick
                 navigationX:frame.analog(
                     airfix::input::AnalogAxis::uiNavigateX)
                 navigationY:frame.analog(
                     airfix::input::AnalogAxis::uiNavigateY)
              confirmPressed:frame.pressed(
                  airfix::input::DigitalAction::uiConfirm)
               cancelPressed:frame.pressed(
                  airfix::input::DigitalAction::uiCancel)
          tabPreviousPressed:frame.pressed(
              airfix::input::DigitalAction::uiTabPrevious)
              tabNextPressed:frame.pressed(
                  airfix::input::DigitalAction::uiTabNext)];
        [self notifyUIInput:input];
        if (_terminalInputFailure || _resetEpoch != frameEpoch) {
            // The UI delegate crossed an input/lifecycle boundary. The
            // already-delivered frame must not produce later callbacks.
            return;
        }
    }

    const BOOL userPause =
        frame.pressed(airfix::input::DigitalAction::globalPause);

    const int16_t bank = frame.analog(
        airfix::input::AnalogAxis::flightBank);
    const int16_t pitch = frame.analog(
        airfix::input::AnalogAxis::flightPitch);
    const BOOL primaryPressed = frame.pressed(
        airfix::input::DigitalAction::combatPrimaryFire);
    const BOOL primaryHeld = frame.held(
        airfix::input::DigitalAction::combatPrimaryFire);
    const BOOL primaryReleased = frame.released(
        airfix::input::DigitalAction::combatPrimaryFire);
    const BOOL controllerConnected = self.controllerState.isConnected;

    AirfixInputDiagnostics* previous = self.diagnostics;
    const BOOL meaningfulChange =
        previous.bank != bank ||
        previous.pitch != pitch ||
        previous.primaryHeld != primaryHeld ||
        primaryPressed ||
        primaryReleased ||
        previous.isControllerConnected != controllerConnected ||
        previous.lastMeaningfulSource != _lastMeaningfulSource;
    const BOOL heartbeatDue =
        _inputTick - _lastDiagnosticPublishTick >= kDiagnosticHeartbeatTicks;
    if (!meaningfulChange && !heartbeatDue) {
        if (userPause) {
            [self resetAllSources];
            [self notifyPauseForReason:AirfixInputPauseReasonUserControl];
        }
        return;
    }

    self.diagnostics = [[AirfixInputDiagnostics alloc]
        initWithTick:_inputTick
                bank:bank
               pitch:pitch
      primaryPressed:primaryPressed
         primaryHeld:primaryHeld
     primaryReleased:primaryReleased
 controllerConnected:controllerConnected
lastMeaningfulSource:_lastMeaningfulSource];
    _lastDiagnosticPublishTick = _inputTick;
    [self notifyDiagnostics:self.diagnostics];

    if (userPause) {
        // The triggering frame (including any primary-fire edge) has already
        // reached the consumer. Reset before allowing the host to toggle pause.
        [self resetAllSources];
        [self notifyPauseForReason:AirfixInputPauseReasonUserControl];
    }
}

- (BOOL)pollController {
    if (!self.controllerState.isConnected) {
        return YES;
    }
    const AirfixGameControllerInputBatch nativeBatch =
        [_controllerAdapter drainInputBatch];
    if (nativeBatch.generation != self.controllerState.generation) {
        [self handleInputFailureTerminal:NO];
        return NO;
    }

    const auto portableBatch = controllerBatch(nativeBatch);
    const bool fullState =
        _controllerBridge.currentGeneration() != portableBatch.generation;
    const auto result = _controllerBridge.process(
        portableBatch, _controllerEmissions);
    if (!result.accepted()) {
        [self handleInputFailureTerminal:NO];
        return NO;
    }

    BOOL meaningful = NO;
    for (std::size_t index = 0U; index < result.emissionCount; ++index) {
        const auto& emission = _controllerEmissions[index];
        BOOL accepted = NO;
        switch (emission.kind) {
        case airfix::input::PhysicalEventKind::analog:
            accepted = [self enqueueAxis:
                static_cast<int16_t>(emission.value)
                                      source:kControllerSource
                                     control:emission.control];
            break;
        case airfix::input::PhysicalEventKind::digital:
            accepted = [self enqueueButton:emission.value != 0
                                    source:kControllerSource
                                   control:emission.control];
            break;
        case airfix::input::PhysicalEventKind::weaponSelection:
        case airfix::input::PhysicalEventKind::count:
            [self handleInputFailureTerminal:NO];
            return NO;
        }
        if (!accepted) {
            [self handleInputFailureTerminal:_nextSequence == 0U];
            return NO;
        }
    }

    meaningful = !fullState && result.emissionCount != 0U;
    if (meaningful) {
        _lastMeaningfulSource = AirfixInputSourceController;
    }
    return YES;
}

- (BOOL)enqueueAxis:(int16_t)value
             source:(airfix::input::SourceHandle)source
            control:(airfix::input::ControlId)control {
    std::uint64_t sequence = 0U;
    if (![self takeNextSequence:&sequence]) {
        return NO;
    }
    const auto event = airfix::input::PhysicalEvent::axis(
        sequence, diagnosticTimestamp(), source, control, q15(value));
    return _router.enqueue(event);
}

- (BOOL)enqueueButton:(BOOL)pressed
               source:(airfix::input::SourceHandle)source
              control:(airfix::input::ControlId)control {
    std::uint64_t sequence = 0U;
    if (![self takeNextSequence:&sequence]) {
        return NO;
    }
    const auto event = airfix::input::PhysicalEvent::button(
        sequence, diagnosticTimestamp(), source, control, pressed);
    return _router.enqueue(event);
}

- (BOOL)enqueueWeaponSelection:(uint8_t)selection
                        source:(airfix::input::SourceHandle)source
                       control:(airfix::input::ControlId)control {
    if (selection >= airfix::input::weaponSlotCount) {
        return NO;
    }
    std::uint64_t sequence = 0U;
    if (![self takeNextSequence:&sequence]) {
        return NO;
    }
    const auto event = airfix::input::PhysicalEvent::selectWeapon(
        sequence, diagnosticTimestamp(), source, control, selection);
    return _router.enqueue(event);
}

- (BOOL)takeNextSequence:(std::uint64_t*)sequence {
    NSAssert(NSThread.isMainThread, @"Global input sequence belongs to main");
    NSParameterAssert(sequence != nullptr);
    if (_nextSequence == 0U || _terminalInputFailure) {
        return NO;
    }
    if (_nextSequence == std::numeric_limits<std::uint64_t>::max()) {
        _nextSequence = 0U;
        return NO;
    }
    *sequence = _nextSequence;
    ++_nextSequence;
    return YES;
}

- (void)resetAllSources {
    NSAssert(NSThread.isMainThread, @"Input reset belongs to main");
    if (_resettingSources) {
        return;
    }
    BOOL resetEpochExhausted = NO;
    if (_resetEpoch == std::numeric_limits<std::uint64_t>::max()) {
        if (!_terminalInputFailure) {
            _terminalInputFailure = YES;
            resetEpochExhausted = YES;
        }
    }
    else {
        ++_resetEpoch;
    }
    _resettingSources = YES;
    [_touchControlsView cancelAllTouches];
    const int16_t latchedTouchThrottle =
        _touchAxes[kTouchThrottleAxisIndex];
    _router.cancelSource(kTouchSource);
    _router.cancelSource(kControllerSource);
    [_controllerAdapter resetInputBridge];
    _router.lifecycleReset();
    _touchAxes.fill(0);
    _touchAxes[kTouchThrottleAxisIndex] = latchedTouchThrottle;
    _touchButtons.fill(NO);
    _controllerBridge.reset();
    _lastMeaningfulSource = AirfixInputSourceNone;
    self.diagnostics = [[AirfixInputDiagnostics alloc]
        initWithTick:_inputTick
                bank:0
               pitch:0
      primaryPressed:NO
         primaryHeld:NO
     primaryReleased:NO
 controllerConnected:self.controllerState.isConnected
lastMeaningfulSource:AirfixInputSourceNone];
    _lastDiagnosticPublishTick = _inputTick;
    _lastDisplayTimestamp = 0.0;
    _inputAccumulator = 0.0;
    _resettingSources = NO;
    if (!_terminalInputFailure && latchedTouchThrottle != 0 &&
        ![self enqueueAxis:latchedTouchThrottle
                    source:kTouchSource
                   control:airfix::input::controls::touch::throttleSet]) {
        [self handleInputFailureTerminal:YES];
        return;
    }
    if (resetEpochExhausted) {
        _displayLink.paused = YES;
        if (!_handlingInputFailure) {
            _handlingInputFailure = YES;
            [self notifyPauseForReason:
                AirfixInputPauseReasonInputPipelineFailure];
            _handlingInputFailure = NO;
        }
    }
}

- (void)handleInputFailureTerminal:(BOOL)terminal {
    NSAssert(NSThread.isMainThread, @"Input failure handling belongs to main");
    if (_handlingInputFailure || _terminalInputFailure) {
        return;
    }
    _handlingInputFailure = YES;
    _terminalInputFailure = terminal;
    [self resetAllSources];
    const BOOL effectiveTerminal = _terminalInputFailure;
    if (effectiveTerminal) {
        _displayLink.paused = YES;
    }
    [self notifyPauseForReason:
        effectiveTerminal
        ? AirfixInputPauseReasonInputPipelineFailure
        : AirfixInputPauseReasonInputOverflow];
    _handlingInputFailure = NO;
}

- (void)notifyPauseForReason:(AirfixInputPauseReason)reason {
    id<AirfixIOSInputCoordinatorDelegate> delegate = self.delegate;
    SEL selector =
        @selector(inputCoordinator:didRequestPauseForReason:);
    if ([delegate respondsToSelector:selector]) {
        try {
            [delegate inputCoordinator:self didRequestPauseForReason:reason];
        }
        catch (...) {
            // Forced reset already happened; never unwind into UIKit.
        }
    }
}

- (void)notifyControllerState:(AirfixGameControllerState*)state {
    id<AirfixIOSInputCoordinatorDelegate> delegate = self.delegate;
    SEL selector =
        @selector(inputCoordinator:didChangeControllerState:);
    if ([delegate respondsToSelector:selector]) {
        try {
            [delegate inputCoordinator:self didChangeControllerState:state];
        }
        catch (...) {
            // Controller metadata cannot compromise the input pump.
        }
    }
}

- (void)notifyDiagnostics:(AirfixInputDiagnostics*)diagnostics {
    id<AirfixIOSInputCoordinatorDelegate> delegate = self.delegate;
    SEL selector =
        @selector(inputCoordinator:didUpdateDiagnostics:);
    if ([delegate respondsToSelector:selector]) {
        try {
            [delegate inputCoordinator:self didUpdateDiagnostics:diagnostics];
        }
        catch (...) {
            // Diagnostics are best effort and never affect routing.
        }
    }
}

- (void)notifyUIInput:(AirfixUIInputSnapshot*)input {
    id<AirfixIOSInputCoordinatorDelegate> delegate = self.delegate;
    SEL selector =
        @selector(inputCoordinator:didUpdateUIInput:);
    if ([delegate respondsToSelector:selector]) {
        try {
            [delegate inputCoordinator:self didUpdateUIInput:input];
        }
        catch (...) {
            // UI navigation is best effort and never compromises routing.
        }
    }
}

- (void)performOnMain:(dispatch_block_t)block {
    if (NSThread.isMainThread) {
        block();
    }
    else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

- (BOOL)setPrivateFrameConsumer:
    (const airfix::ios::InputFrameConsumer&)consumer {
    NSAssert(NSThread.isMainThread, @"Input frame consumer belongs to main");
    if (_terminalInputFailure) {
        return NO;
    }
    if (!consumer) {
        airfix::ios::InputFrameConsumer failedConsumer;
        _frameConsumer.swap(failedConsumer);
        [self handleInputFailureTerminal:YES];
        return NO;
    }
    try {
        airfix::ios::InputFrameConsumer replacement(consumer);
        if (!replacement) {
            airfix::ios::InputFrameConsumer failedConsumer;
            _frameConsumer.swap(failedConsumer);
            [self handleInputFailureTerminal:YES];
            return NO;
        }
        _frameConsumer.swap(replacement);
        return YES;
    }
    catch (...) {
        airfix::ios::InputFrameConsumer failedConsumer;
        _frameConsumer.swap(failedConsumer);
        [self handleInputFailureTerminal:YES];
        return NO;
    }
}

#pragma mark - Touch controls

- (void)touchControlsView:(AirfixTouchControlsView*)view
            didChangeAxis:(AirfixTouchAxis)axis
                    value:(int16_t)value {
    __weak AirfixIOSInputCoordinator* weakSelf = self;
    [self performOnMain:^{
        AirfixIOSInputCoordinator* coordinator = weakSelf;
        if (coordinator == nil || view != coordinator->_touchControlsView) {
            return;
        }
        if (coordinator->_terminalInputFailure) {
            return;
        }
        if (coordinator->_resettingSources) {
            if (axis == AirfixTouchAxisThrottleSet) {
                coordinator->_touchAxes[kTouchThrottleAxisIndex] = value;
            }
            return;
        }
        const auto control = touchAxisControl(axis);
        const auto axisIndex = static_cast<std::size_t>(axis);
        if (!control.valid() || axisIndex >= coordinator->_touchAxes.size()) {
            NSAssert(NO, @"Touch axis is invalid");
            return;
        }
        if (coordinator->_touchAxes[axisIndex] == value) {
            return;
        }
        coordinator->_touchAxes[axisIndex] = value;
        if (![coordinator enqueueAxis:value
                               source:kTouchSource
                              control:control]) {
            [coordinator handleInputFailureTerminal:
                coordinator->_nextSequence == 0U];
            return;
        }
        coordinator->_lastMeaningfulSource = AirfixInputSourceTouch;
    }];
}

- (void)touchControlsView:(AirfixTouchControlsView*)view
          didChangeButton:(AirfixTouchButton)button
                  pressed:(BOOL)pressed {
    __weak AirfixIOSInputCoordinator* weakSelf = self;
    [self performOnMain:^{
        AirfixIOSInputCoordinator* coordinator = weakSelf;
        if (coordinator == nil || view != coordinator->_touchControlsView) {
            return;
        }
        if (coordinator->_resettingSources ||
            coordinator->_terminalInputFailure) {
            return;
        }
        const auto control = touchButtonControl(button);
        const auto buttonIndex = static_cast<std::size_t>(button);
        if (!control.valid() ||
            buttonIndex >= coordinator->_touchButtons.size()) {
            NSAssert(NO, @"Touch button is invalid");
            return;
        }
        if (coordinator->_touchButtons[buttonIndex] == pressed) {
            return;
        }
        coordinator->_touchButtons[buttonIndex] = pressed;
        if (![coordinator enqueueButton:pressed
                                 source:kTouchSource
                                control:control]) {
            [coordinator handleInputFailureTerminal:
                coordinator->_nextSequence == 0U];
            return;
        }
        coordinator->_lastMeaningfulSource = AirfixInputSourceTouch;
    }];
}

- (void)touchControlsView:(AirfixTouchControlsView*)view
      didSelectWeaponSlot:(uint8_t)slot {
    __weak AirfixIOSInputCoordinator* weakSelf = self;
    [self performOnMain:^{
        AirfixIOSInputCoordinator* coordinator = weakSelf;
        if (coordinator == nil || view != coordinator->_touchControlsView) {
            return;
        }
        if (coordinator->_resettingSources ||
            coordinator->_terminalInputFailure) {
            return;
        }
        if (slot >= airfix::input::weaponSlotCount) {
            NSAssert(NO, @"Touch weapon slot is invalid");
            return;
        }
        if (![coordinator enqueueWeaponSelection:slot
                                         source:kTouchSource
                                        control:
            airfix::input::controls::touch::weaponSelection]) {
            [coordinator handleInputFailureTerminal:
                coordinator->_nextSequence == 0U];
            return;
        }
        coordinator->_lastMeaningfulSource = AirfixInputSourceTouch;
    }];
}

- (void)touchControlsViewDidCancelAll:(AirfixTouchControlsView*)view {
    __weak AirfixIOSInputCoordinator* weakSelf = self;
    [self performOnMain:^{
        AirfixIOSInputCoordinator* coordinator = weakSelf;
        if (coordinator == nil || view != coordinator->_touchControlsView) {
            return;
        }
        if (coordinator->_resettingSources) {
            return;
        }
        const int16_t latchedTouchThrottle =
            coordinator->_touchAxes[kTouchThrottleAxisIndex];
        coordinator->_router.cancelSource(kTouchSource);
        coordinator->_touchAxes.fill(0);
        coordinator->_touchAxes[kTouchThrottleAxisIndex] =
            latchedTouchThrottle;
        coordinator->_touchButtons.fill(NO);
        if (!coordinator->_terminalInputFailure &&
            latchedTouchThrottle != 0 &&
            ![coordinator enqueueAxis:latchedTouchThrottle
                               source:kTouchSource
                              control:
                airfix::input::controls::touch::throttleSet]) {
            [coordinator handleInputFailureTerminal:YES];
        }
    }];
}

#pragma mark - Controller adapter

- (void)gameControllerAdapter:(AirfixGameControllerAdapter*)adapter
               didChangeState:(AirfixGameControllerState*)state {
    NSAssert(NSThread.isMainThread, @"Controller state belongs to main");
    if (adapter != _controllerAdapter) {
        return;
    }
    const BOOL wasConnected = self.controllerState.isConnected;
    self.controllerState = state;
    if (!wasConnected && state.isConnected) {
        // Every controller assignment starts behind its own neutral gate,
        // including a first hot-connect after the global gate has opened.
        _router.cancelSource(kControllerSource);
    }
    _controllerBridge.reset();

    if (wasConnected && !state.isConnected) {
        [self resetAllSources];
        if (!_suppressControllerLossPause && !_terminalInputFailure) {
            [self notifyPauseForReason:
                AirfixInputPauseReasonControllerDisconnected];
        }
    }
    [self notifyControllerState:state];
}

- (void)gameControllerAdapter:(AirfixGameControllerAdapter*)adapter
        didFailInputForGeneration:(uint64_t)generation {
    NSAssert(NSThread.isMainThread, @"Controller failure belongs to main");
    if (adapter != _controllerAdapter ||
        generation != self.controllerState.generation) {
        return;
    }
    [self handleInputFailureTerminal:YES];
}

@end

namespace airfix::ios {
namespace detail {

bool installInputFrameConsumer(
    AirfixIOSInputCoordinator* coordinator,
    const InputFrameConsumer& consumer) noexcept {
    if (!NSThread.isMainThread || coordinator == nil) {
        return false;
    }
    try {
        return [coordinator setPrivateFrameConsumer:consumer];
    }
    catch (...) {
        return false;
    }
}

void reportInputFrameConsumerFailure(
    AirfixIOSInputCoordinator* coordinator) noexcept {
    if (!NSThread.isMainThread || coordinator == nil) {
        return;
    }
    try {
        [coordinator handleInputFailureTerminal:YES];
    }
    catch (...) {
        // The bridge is noexcept even if a host callback is not.
    }
}

} // namespace detail
} // namespace airfix::ios
