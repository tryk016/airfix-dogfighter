#import "AirfixIOSInputCoordinator.h"

#import "AirfixIOSInputCoordinator+Private.hpp"
#import "AirfixTouchControlsView.h"

#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#include "airfix/input/InputRouter.hpp"

#include <algorithm>
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
constexpr std::int32_t kControllerAxisDelta = 1024;
constexpr std::int32_t kControllerTriggerActuation = 16384;

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

[[nodiscard]] int16_t stableStick(const int16_t value) noexcept {
    const auto wide = static_cast<std::int32_t>(value);
    return std::abs(wide) < kControllerStickDeadzone ? 0 : value;
}

[[nodiscard]] bool shouldSubmitAxis(
    const int16_t previous, const int16_t current) noexcept {
    if (previous == current) {
        return false;
    }
    if (previous == 0 || current == 0) {
        return true;
    }
    return std::abs(
        static_cast<std::int32_t>(current) -
        static_cast<std::int32_t>(previous)) >= kControllerAxisDelta;
}

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

    int16_t _touchBank;
    int16_t _touchPitch;
    BOOL _touchPrimaryPressed;
    BOOL _touchPausePressed;

    BOOL _hasControllerSample;
    std::uint64_t _controllerSampleGeneration;
    std::uint64_t _lastControllerEdgeOrder;
    int16_t _controllerBank;
    int16_t _controllerPitch;
    BOOL _controllerTriggerPressed;
    BOOL _controllerPausePressed;

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
- (BOOL)takeNextSequence:(std::uint64_t*)sequence;
- (void)resetAllSources;
- (void)handleInputFailureTerminal:(BOOL)terminal;
- (void)notifyPauseForReason:(AirfixInputPauseReason)reason;
- (void)notifyControllerState:(AirfixGameControllerState*)state;
- (void)notifyDiagnostics:(AirfixInputDiagnostics*)diagnostics;
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
    const AirfixGameControllerInputBatch batch =
        [_controllerAdapter drainInputBatch];
    if (batch.overflowed ||
        batch.edgeCount >
            static_cast<NSUInteger>(
                AirfixGameControllerDigitalEdgeCapacity) ||
        batch.generation == 0U ||
        batch.generation != self.controllerState.generation) {
        [self handleInputFailureTerminal:NO];
        return NO;
    }

    const int16_t bank = stableStick(batch.finalState.bank);
    const int16_t pitch = stableStick(batch.finalState.pitch);
    const BOOL startingTrigger =
        batch.startingState.primaryTrigger >= kControllerTriggerActuation;
    const BOOL startingPause =
        batch.startingState.menuPressed ||
        batch.startingState.optionsPressed;
    const BOOL finalTrigger =
        batch.finalState.primaryTrigger >= kControllerTriggerActuation;
    const BOOL finalPause =
        batch.finalState.menuPressed || batch.finalState.optionsPressed;
    const BOOL fullState =
        !_hasControllerSample ||
        _controllerSampleGeneration != batch.generation;
    BOOL meaningful = NO;

    if (fullState || shouldSubmitAxis(_controllerBank, bank)) {
        const BOOL changed = !fullState || bank != 0;
        if (![self enqueueAxis:bank
                        source:kControllerSource
                       control:
            airfix::input::controls::controller::leftStickX]) {
            [self handleInputFailureTerminal:_nextSequence == 0U];
            return NO;
        }
        _controllerBank = bank;
        meaningful = meaningful || changed;
    }
    if (fullState || shouldSubmitAxis(_controllerPitch, pitch)) {
        const BOOL changed = !fullState || pitch != 0;
        if (![self enqueueAxis:pitch
                        source:kControllerSource
                       control:
            airfix::input::controls::controller::leftStickY]) {
            [self handleInputFailureTerminal:_nextSequence == 0U];
            return NO;
        }
        _controllerPitch = pitch;
        meaningful = meaningful || changed;
    }

    if (fullState) {
        if (![self enqueueAxis:(startingTrigger ? 32767 : 0)
                        source:kControllerSource
                       control:
            airfix::input::controls::controller::rightTrigger] ||
            ![self enqueueButton:startingPause
                          source:kControllerSource
                         control:
            airfix::input::controls::controller::menu]) {
            [self handleInputFailureTerminal:_nextSequence == 0U];
            return NO;
        }
        _controllerSampleGeneration = batch.generation;
        _lastControllerEdgeOrder = 0U;
        _controllerTriggerPressed = startingTrigger;
        _controllerPausePressed = startingPause;
        meaningful = meaningful || startingTrigger || startingPause;
    }
    else if (
        _controllerTriggerPressed != startingTrigger ||
        _controllerPausePressed != startingPause) {
        // The adapter and coordinator must agree on the state from which the
        // drained edge sequence begins; otherwise replay could duplicate or
        // omit a transition.
        [self handleInputFailureTerminal:NO];
        return NO;
    }

    for (NSUInteger index = 0U; index < batch.edgeCount; ++index) {
        const AirfixGameControllerDigitalEdge edge = batch.edges[index];
        if (edge.generation != batch.generation || edge.order == 0U ||
            edge.order <= _lastControllerEdgeOrder) {
            [self handleInputFailureTerminal:NO];
            return NO;
        }
        _lastControllerEdgeOrder = edge.order;
        switch (edge.control) {
        case AirfixGameControllerDigitalControlPrimaryTrigger:
            if (_controllerTriggerPressed == edge.pressed ||
                ![self enqueueAxis:(edge.pressed ? 32767 : 0)
                            source:kControllerSource
                           control:
                airfix::input::controls::controller::rightTrigger]) {
                [self handleInputFailureTerminal:_nextSequence == 0U];
                return NO;
            }
            _controllerTriggerPressed = edge.pressed;
            break;
        case AirfixGameControllerDigitalControlPause:
            if (_controllerPausePressed == edge.pressed ||
                ![self enqueueButton:edge.pressed
                              source:kControllerSource
                             control:
                airfix::input::controls::controller::menu]) {
                [self handleInputFailureTerminal:_nextSequence == 0U];
                return NO;
            }
            _controllerPausePressed = edge.pressed;
            break;
        default:
            [self handleInputFailureTerminal:NO];
            return NO;
        }
        meaningful = YES;
    }

    // Synchronized final-state reconciliation covers a platform state change
    // observed during the drain. It emits only when the FIFO did not already
    // arrive at that state, so edges are never duplicated.
    if (_controllerTriggerPressed != finalTrigger) {
        if (![self enqueueAxis:(finalTrigger ? 32767 : 0)
                        source:kControllerSource
                       control:
            airfix::input::controls::controller::rightTrigger]) {
            [self handleInputFailureTerminal:_nextSequence == 0U];
            return NO;
        }
        _controllerTriggerPressed = finalTrigger;
        meaningful = YES;
    }
    if (_controllerPausePressed != finalPause) {
        if (![self enqueueButton:finalPause
                          source:kControllerSource
                         control:
            airfix::input::controls::controller::menu]) {
            [self handleInputFailureTerminal:_nextSequence == 0U];
            return NO;
        }
        _controllerPausePressed = finalPause;
        meaningful = YES;
    }
    _hasControllerSample = YES;
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
    _router.cancelSource(kTouchSource);
    _router.cancelSource(kControllerSource);
    [_controllerAdapter resetInputBridge];
    _router.lifecycleReset();
    _touchBank = 0;
    _touchPitch = 0;
    _touchPrimaryPressed = NO;
    _touchPausePressed = NO;
    _hasControllerSample = NO;
    _controllerSampleGeneration = self.controllerState.generation;
    _lastControllerEdgeOrder = 0U;
    _controllerBank = 0;
    _controllerPitch = 0;
    _controllerTriggerPressed = NO;
    _controllerPausePressed = NO;
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
        if (coordinator->_resettingSources ||
            coordinator->_terminalInputFailure) {
            return;
        }
        switch (axis) {
        case AirfixTouchAxisBank: {
            if (coordinator->_touchBank == value) {
                return;
            }
            const int16_t previous = coordinator->_touchBank;
            coordinator->_touchBank = value;
            if (![coordinator enqueueAxis:value
                                   source:kTouchSource
                                  control:
                airfix::input::controls::touch::bank]) {
                [coordinator handleInputFailureTerminal:
                    coordinator->_nextSequence == 0U];
                return;
            }
            if ((value == 0 && previous != 0) ||
                std::abs(static_cast<std::int32_t>(value)) >=
                    kControllerStickDeadzone) {
                coordinator->_lastMeaningfulSource = AirfixInputSourceTouch;
            }
            break;
        }
        case AirfixTouchAxisPitch: {
            if (coordinator->_touchPitch == value) {
                return;
            }
            const int16_t previous = coordinator->_touchPitch;
            coordinator->_touchPitch = value;
            if (![coordinator enqueueAxis:value
                                   source:kTouchSource
                                  control:
                airfix::input::controls::touch::pitch]) {
                [coordinator handleInputFailureTerminal:
                    coordinator->_nextSequence == 0U];
                return;
            }
            if ((value == 0 && previous != 0) ||
                std::abs(static_cast<std::int32_t>(value)) >=
                    kControllerStickDeadzone) {
                coordinator->_lastMeaningfulSource = AirfixInputSourceTouch;
            }
            break;
        }
        }
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
        switch (button) {
        case AirfixTouchButtonPrimaryFire:
            if (coordinator->_touchPrimaryPressed == pressed) {
                return;
            }
            coordinator->_touchPrimaryPressed = pressed;
            if (![coordinator enqueueButton:pressed
                                     source:kTouchSource
                                    control:
                airfix::input::controls::touch::primaryFire]) {
                [coordinator handleInputFailureTerminal:
                    coordinator->_nextSequence == 0U];
                return;
            }
            break;
        case AirfixTouchButtonPause:
            if (coordinator->_touchPausePressed == pressed) {
                return;
            }
            coordinator->_touchPausePressed = pressed;
            if (![coordinator enqueueButton:pressed
                                     source:kTouchSource
                                    control:
                airfix::input::controls::touch::pause]) {
                [coordinator handleInputFailureTerminal:
                    coordinator->_nextSequence == 0U];
                return;
            }
            break;
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
        coordinator->_router.cancelSource(kTouchSource);
        coordinator->_touchBank = 0;
        coordinator->_touchPitch = 0;
        coordinator->_touchPrimaryPressed = NO;
        coordinator->_touchPausePressed = NO;
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
    _hasControllerSample = NO;
    _controllerSampleGeneration = state.generation;
    _lastControllerEdgeOrder = 0U;
    _controllerBank = 0;
    _controllerPitch = 0;
    _controllerTriggerPressed = NO;
    _controllerPausePressed = NO;

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
