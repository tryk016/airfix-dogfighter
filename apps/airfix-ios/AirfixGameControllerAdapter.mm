#import "AirfixGameControllerAdapter.h"

#import <GameController/GameController.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>

namespace {

constexpr int16_t kControllerTriggerActuation = 16384;

[[nodiscard]] int16_t signedQ15(const float value) noexcept {
    if (!std::isfinite(value)) {
        return 0;
    }
    const float bounded = std::clamp(value, -1.0F, 1.0F);
    const auto scaled = static_cast<std::int32_t>(
        std::lround(bounded * 32767.0F));
    return static_cast<int16_t>(
        std::clamp(scaled, -32767, 32767));
}

[[nodiscard]] int16_t positiveQ15(const float value) noexcept {
    if (!std::isfinite(value)) {
        return 0;
    }
    const float bounded = std::clamp(value, 0.0F, 1.0F);
    const auto scaled = static_cast<std::int32_t>(
        std::lround(bounded * 32767.0F));
    return static_cast<int16_t>(std::clamp(scaled, 0, 32767));
}

[[nodiscard]] BOOL pausePressed(
    const AirfixGameControllerSample& sample) noexcept {
    return sample.menuPressed || sample.optionsPressed;
}

[[nodiscard]] BOOL triggerPressed(
    const AirfixGameControllerSample& sample) noexcept {
    return sample.primaryTrigger >= kControllerTriggerActuation;
}

[[nodiscard]] AirfixGameControllerSample sampleProfile(
    GCExtendedGamepad* profile) noexcept {
    AirfixGameControllerSample sample{};
    if (profile == nil) {
        return sample;
    }
    sample.bank = signedQ15(profile.leftThumbstick.xAxis.value);
    sample.pitch = signedQ15(profile.leftThumbstick.yAxis.value);
    sample.primaryTrigger = positiveQ15(profile.rightTrigger.value);
    sample.menuPressed = profile.buttonMenu.isPressed;
    if ([profile respondsToSelector:@selector(buttonOptions)]) {
        GCControllerButtonInput* options = profile.buttonOptions;
        sample.optionsPressed = options != nil && options.isPressed;
    }
    return sample;
}

} // namespace

@interface AirfixGameControllerState ()

- (instancetype)initWithConnected:(BOOL)connected
                       generation:(uint64_t)generation
                        vendorName:(NSString*)vendorName
                  productCategory:(NSString*)productCategory
    NS_DESIGNATED_INITIALIZER;

@end

@implementation AirfixGameControllerState

- (instancetype)initWithConnected:(BOOL)connected
                       generation:(uint64_t)generation
                        vendorName:(NSString*)vendorName
                  productCategory:(NSString*)productCategory {
    self = [super init];
    if (self != nil) {
        _connected = connected;
        _generation = generation;
        _vendorName = [vendorName copy];
        _productCategory = [productCategory copy];
    }
    return self;
}

@end

@interface AirfixGameControllerAdapter () {
    __strong GCController* _assignedController;
    __strong id _connectObserver;
    __strong id _disconnectObserver;
    std::atomic<std::uint64_t> _publishedGeneration;
    std::mutex _inputBridgeMutex;
    std::array<AirfixGameControllerDigitalEdge,
        AirfixGameControllerDigitalEdgeCapacity> _digitalEdges;
    AirfixGameControllerSample _bridgeBaseline;
    AirfixGameControllerSample _bridgeLatest;
    std::size_t _digitalEdgeHead;
    std::size_t _digitalEdgeCount;
    std::uint64_t _nextDigitalOrder;
    BOOL _inputOverflowed;
    std::atomic<bool> _generationExhausted;
    BOOL _running;
}
@property(nonatomic, strong, readwrite) AirfixGameControllerState* state;

- (void)handleControllerConnected:(GCController*)controller
               observedGeneration:(uint64_t)observedGeneration;
- (void)handleControllerDisconnected:(GCController*)controller
                  observedGeneration:(uint64_t)observedGeneration;
- (void)assignFirstAvailableController;
- (void)assignController:(GCController*)controller;
- (uint64_t)advanceGeneration;
- (void)clearHandlersForProfile:(nullable GCExtendedGamepad*)profile;
- (void)recordContinuousStateForGeneration:(uint64_t)generation
                                    profile:(GCExtendedGamepad*)profile;
- (void)recordTriggerForGeneration:(uint64_t)generation
                              value:(int16_t)value
                            pressed:(BOOL)pressed;
- (void)recordPauseButtonForGeneration:(uint64_t)generation
                                options:(BOOL)options
                                pressed:(BOOL)pressed;
- (void)enqueueDigitalControlLocked:
    (AirfixGameControllerDigitalControl)control
                              pressed:(BOOL)pressed
                           generation:(uint64_t)generation;
- (void)resetInputBridgeLockedWithSample:
    (const AirfixGameControllerSample&)sample;
- (void)notifyInputFailureForGeneration:(uint64_t)generation;
- (void)publishCurrentState;
@end

@implementation AirfixGameControllerAdapter

- (instancetype)initWithDelegate:
    (nullable id<AirfixGameControllerAdapterDelegate>)delegate {
    self = [super init];
    if (self != nil) {
        _delegate = delegate;
        _publishedGeneration.store(0U, std::memory_order_relaxed);
        _generationExhausted.store(false, std::memory_order_relaxed);
        _nextDigitalOrder = 1U;
        _state = [[AirfixGameControllerState alloc]
            initWithConnected:NO
                   generation:0U
                    vendorName:@""
              productCategory:@""];

        __weak AirfixGameControllerAdapter* weakSelf = self;
        NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
        _connectObserver = [center
            addObserverForName:GCControllerDidConnectNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification* notification) {
            AirfixGameControllerAdapter* adapter = weakSelf;
            if (adapter == nil) {
                return;
            }
            const uint64_t observedGeneration =
                adapter->_publishedGeneration.load(std::memory_order_acquire);
            GCController* controller = [notification.object
                isKindOfClass:GCController.class]
                ? (GCController*)notification.object
                : nil;
            if (controller == nil) {
                return;
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                AirfixGameControllerAdapter* strongAdapter = weakSelf;
                if (strongAdapter == nil ||
                    observedGeneration !=
                        strongAdapter->_publishedGeneration.load(
                            std::memory_order_acquire)) {
                    return;
                }
                [strongAdapter handleControllerConnected:controller
                                      observedGeneration:observedGeneration];
            });
        }];
        _disconnectObserver = [center
            addObserverForName:GCControllerDidDisconnectNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification* notification) {
            AirfixGameControllerAdapter* adapter = weakSelf;
            if (adapter == nil) {
                return;
            }
            const uint64_t observedGeneration =
                adapter->_publishedGeneration.load(std::memory_order_acquire);
            GCController* controller = [notification.object
                isKindOfClass:GCController.class]
                ? (GCController*)notification.object
                : nil;
            if (controller == nil) {
                return;
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                AirfixGameControllerAdapter* strongAdapter = weakSelf;
                if (strongAdapter == nil ||
                    observedGeneration !=
                        strongAdapter->_publishedGeneration.load(
                            std::memory_order_acquire)) {
                    return;
                }
                [strongAdapter handleControllerDisconnected:controller
                                         observedGeneration:observedGeneration];
            });
        }];
    }
    return self;
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:_connectObserver];
    [NSNotificationCenter.defaultCenter removeObserver:_disconnectObserver];
    [self clearHandlersForProfile:_assignedController.extendedGamepad];
}

- (void)start {
    NSAssert(NSThread.isMainThread, @"Controller adapter is main-thread-affine");
    if (_running ||
        _generationExhausted.load(std::memory_order_acquire)) {
        return;
    }
    _running = YES;
    // A lifecycle restart invalidates notifications that were queued while the
    // adapter was stopped, even when no controller had been assigned.
    if ([self advanceGeneration] == 0U) {
        _running = NO;
        [self publishCurrentState];
        return;
    }
    [self assignFirstAvailableController];
    if (_assignedController == nil) {
        [self publishCurrentState];
    }
}

- (void)stop {
    NSAssert(NSThread.isMainThread, @"Controller adapter is main-thread-affine");
    if (!_running) {
        return;
    }
    _running = NO;
    if (_assignedController != nil) {
        [self clearHandlersForProfile:_assignedController.extendedGamepad];
        _assignedController = nil;
    }
    [self advanceGeneration];
    [self publishCurrentState];
}

- (AirfixGameControllerInputBatch)drainInputBatch {
    NSAssert(NSThread.isMainThread, @"Controller adapter is main-thread-affine");
    AirfixGameControllerInputBatch batch{};
    const uint64_t generation =
        _publishedGeneration.load(std::memory_order_acquire);
    std::scoped_lock lock(_inputBridgeMutex);
    if (_running &&
        !_generationExhausted.load(std::memory_order_acquire) &&
        generation ==
            _publishedGeneration.load(std::memory_order_acquire)) {
        GCExtendedGamepad* profile = _assignedController.extendedGamepad;
        if (profile != nil) {
            _bridgeLatest.bank =
                signedQ15(profile.leftThumbstick.xAxis.value);
            _bridgeLatest.pitch =
                signedQ15(profile.leftThumbstick.yAxis.value);
        }
    }

    batch.generation = generation;
    batch.startingState = _bridgeBaseline;
    batch.finalState = _bridgeLatest;
    batch.overflowed = _inputOverflowed;
    if (!_inputOverflowed) {
        batch.edgeCount = _digitalEdgeCount;
        for (std::size_t index = 0U; index < _digitalEdgeCount; ++index) {
            batch.edges[index] = _digitalEdges[
                (_digitalEdgeHead + index) %
                static_cast<std::size_t>(
                    AirfixGameControllerDigitalEdgeCapacity)];
        }
    }
    _bridgeBaseline = _bridgeLatest;
    _digitalEdgeHead = 0U;
    _digitalEdgeCount = 0U;
    _inputOverflowed = NO;
    return batch;
}

- (void)resetInputBridge {
    NSAssert(NSThread.isMainThread, @"Controller adapter is main-thread-affine");
    const uint64_t generation =
        _publishedGeneration.load(std::memory_order_acquire);
    std::scoped_lock lock(_inputBridgeMutex);
    const AirfixGameControllerSample sample =
        _running &&
            !_generationExhausted.load(std::memory_order_acquire) &&
            generation ==
                _publishedGeneration.load(std::memory_order_acquire)
        ? sampleProfile(_assignedController.extendedGamepad)
        : AirfixGameControllerSample{};
    [self resetInputBridgeLockedWithSample:sample];
}

- (void)handleControllerConnected:(GCController*)controller
               observedGeneration:(uint64_t)observedGeneration {
    NSAssert(NSThread.isMainThread, @"Controller callbacks must reach main");
    if (!_running || controller == nil ||
        observedGeneration !=
            _publishedGeneration.load(std::memory_order_acquire) ||
        _assignedController != nil || controller.extendedGamepad == nil) {
        return;
    }
    [self assignController:controller];
}

- (void)handleControllerDisconnected:(GCController*)controller
                  observedGeneration:(uint64_t)observedGeneration {
    NSAssert(NSThread.isMainThread, @"Controller callbacks must reach main");
    if (!_running || controller == nil ||
        observedGeneration !=
            _publishedGeneration.load(std::memory_order_acquire) ||
        controller != _assignedController) {
        return;
    }

    [self clearHandlersForProfile:_assignedController.extendedGamepad];
    _assignedController = nil;
    (void)[self advanceGeneration];
    [self publishCurrentState];

    // A connected replacement may already be present. Publishing the loss
    // first lets the coordinator cancel the old source before a replacement
    // submits its complete state through the reconnect neutral gate.
    [self assignFirstAvailableController];
}

- (void)assignFirstAvailableController {
    NSAssert(NSThread.isMainThread, @"Controller adapter is main-thread-affine");
    if (!_running || _assignedController != nil) {
        return;
    }
    for (GCController* controller in GCController.controllers) {
        if (controller.extendedGamepad != nil) {
            [self assignController:controller];
            return;
        }
    }
}

- (void)assignController:(GCController*)controller {
    NSAssert(NSThread.isMainThread, @"Controller adapter is main-thread-affine");
    GCExtendedGamepad* profile = controller.extendedGamepad;
    if (!_running || _assignedController != nil || profile == nil) {
        return;
    }

    const uint64_t assignmentGeneration = [self advanceGeneration];
    if (assignmentGeneration == 0U) {
        _running = NO;
        [self publishCurrentState];
        return;
    }
    _assignedController = controller;
    [self resetInputBridge];

    __weak AirfixGameControllerAdapter* weakSelf = self;
    __weak GCController* weakController = controller;
    profile.valueChangedHandler = ^(
        GCExtendedGamepad* changedProfile,
        GCControllerElement* changedElement) {
        (void)changedElement;
        AirfixGameControllerAdapter* adapter = weakSelf;
        GCController* assigned = weakController;
        if (adapter == nil || assigned == nil ||
            assigned != changedProfile.controller) {
            return;
        }
        [adapter recordContinuousStateForGeneration:assignmentGeneration
                                            profile:changedProfile];
    };
    profile.rightTrigger.valueChangedHandler = ^(
        GCControllerButtonInput* button, float value, BOOL pressed) {
        (void)pressed;
        AirfixGameControllerAdapter* adapter = weakSelf;
        GCController* assigned = weakController;
        if (adapter == nil || assigned == nil ||
            button != assigned.extendedGamepad.rightTrigger) {
            return;
        }
        const int16_t triggerValue = positiveQ15(value);
        const BOOL isTriggerPressed =
            triggerValue >= kControllerTriggerActuation;
        [adapter recordTriggerForGeneration:assignmentGeneration
                                      value:triggerValue
                                    pressed:isTriggerPressed];
    };
    profile.buttonMenu.pressedChangedHandler = ^(
        GCControllerButtonInput* button, float value, BOOL pressed) {
        (void)value;
        AirfixGameControllerAdapter* adapter = weakSelf;
        GCController* assigned = weakController;
        if (adapter == nil || assigned == nil ||
            button != assigned.extendedGamepad.buttonMenu) {
            return;
        }
        [adapter recordPauseButtonForGeneration:assignmentGeneration
                                        options:NO
                                        pressed:pressed];
    };
    if ([profile respondsToSelector:@selector(buttonOptions)]) {
        GCControllerButtonInput* optionsButton = profile.buttonOptions;
        __weak GCControllerButtonInput* weakOptionsButton = optionsButton;
        optionsButton.pressedChangedHandler = ^(
            GCControllerButtonInput* button, float value, BOOL pressed) {
            (void)value;
            AirfixGameControllerAdapter* adapter = weakSelf;
            GCController* assigned = weakController;
            GCControllerButtonInput* installedButton = weakOptionsButton;
            if (adapter == nil || assigned == nil ||
                installedButton == nil || button != installedButton) {
                return;
            }
            [adapter recordPauseButtonForGeneration:assignmentGeneration
                                            options:YES
                                            pressed:pressed];
        };
    }
    [self publishCurrentState];
}

- (uint64_t)advanceGeneration {
    NSAssert(NSThread.isMainThread, @"Controller generation belongs to main");
    const uint64_t current =
        _publishedGeneration.load(std::memory_order_relaxed);
    if (current == std::numeric_limits<uint64_t>::max()) {
        if (!_generationExhausted.exchange(
                true, std::memory_order_acq_rel)) {
            _running = NO;
            {
                std::scoped_lock lock(_inputBridgeMutex);
                _inputOverflowed = YES;
                _digitalEdgeHead = 0U;
                _digitalEdgeCount = 0U;
            }
            [self notifyInputFailureForGeneration:current];
        }
        return 0U;
    }
    const uint64_t next = current + 1U;
    _publishedGeneration.store(next, std::memory_order_release);
    {
        std::scoped_lock lock(_inputBridgeMutex);
        [self resetInputBridgeLockedWithSample:AirfixGameControllerSample{}];
    }
    return next;
}

- (void)clearHandlersForProfile:(nullable GCExtendedGamepad*)profile {
    if (profile == nil) {
        return;
    }
    profile.valueChangedHandler = nil;
    profile.rightTrigger.valueChangedHandler = nil;
    profile.buttonMenu.pressedChangedHandler = nil;
    if ([profile respondsToSelector:@selector(buttonOptions)]) {
        profile.buttonOptions.pressedChangedHandler = nil;
    }
}

- (void)recordContinuousStateForGeneration:(uint64_t)generation
                                    profile:(GCExtendedGamepad*)profile {
    std::scoped_lock lock(_inputBridgeMutex);
    if (_generationExhausted.load(std::memory_order_acquire) ||
        generation !=
            _publishedGeneration.load(std::memory_order_acquire)) {
        return;
    }
    // The profile callback is only a latest-value path for continuous axes.
    // Digital state is sourced exclusively from the concrete button callback
    // parameters, so a delayed profile callback cannot collapse or reorder a
    // complete press/release pair.
    _bridgeLatest.bank = signedQ15(profile.leftThumbstick.xAxis.value);
    _bridgeLatest.pitch = signedQ15(profile.leftThumbstick.yAxis.value);
}

- (void)recordTriggerForGeneration:(uint64_t)generation
                              value:(int16_t)value
                            pressed:(BOOL)pressed {
    std::scoped_lock lock(_inputBridgeMutex);
    if (_generationExhausted.load(std::memory_order_acquire) ||
        generation !=
            _publishedGeneration.load(std::memory_order_acquire)) {
        return;
    }
    const BOOL wasPressed = triggerPressed(_bridgeLatest);
    _bridgeLatest.primaryTrigger = value;
    if (wasPressed != pressed) {
        [self enqueueDigitalControlLocked:
                AirfixGameControllerDigitalControlPrimaryTrigger
                                  pressed:pressed
                               generation:generation];
    }
}

- (void)recordPauseButtonForGeneration:(uint64_t)generation
                                options:(BOOL)options
                                pressed:(BOOL)pressed {
    std::scoped_lock lock(_inputBridgeMutex);
    if (_generationExhausted.load(std::memory_order_acquire) ||
        generation !=
            _publishedGeneration.load(std::memory_order_acquire)) {
        return;
    }
    const BOOL wasPressed = pausePressed(_bridgeLatest);
    if (options) {
        _bridgeLatest.optionsPressed = pressed;
    }
    else {
        _bridgeLatest.menuPressed = pressed;
    }
    const BOOL isPressed = pausePressed(_bridgeLatest);
    if (wasPressed != isPressed) {
        [self enqueueDigitalControlLocked:
                AirfixGameControllerDigitalControlPause
                                  pressed:isPressed
                               generation:generation];
    }
}

- (void)enqueueDigitalControlLocked:
    (AirfixGameControllerDigitalControl)control
                              pressed:(BOOL)pressed
                           generation:(uint64_t)generation {
    if (_inputOverflowed) {
        return;
    }
    if (_digitalEdgeCount ==
            static_cast<std::size_t>(
                AirfixGameControllerDigitalEdgeCapacity) ||
        _nextDigitalOrder == 0U ||
        _nextDigitalOrder == std::numeric_limits<std::uint64_t>::max()) {
        _inputOverflowed = YES;
        _digitalEdgeHead = 0U;
        _digitalEdgeCount = 0U;
        return;
    }

    const std::size_t destination =
        (_digitalEdgeHead + _digitalEdgeCount) %
        static_cast<std::size_t>(
            AirfixGameControllerDigitalEdgeCapacity);
    _digitalEdges[destination] = {
        generation,
        _nextDigitalOrder,
        control,
        pressed,
    };
    ++_digitalEdgeCount;
    ++_nextDigitalOrder;
}

- (void)resetInputBridgeLockedWithSample:
    (const AirfixGameControllerSample&)sample {
    _digitalEdges.fill({});
    _bridgeBaseline = sample;
    _bridgeLatest = sample;
    _digitalEdgeHead = 0U;
    _digitalEdgeCount = 0U;
    _nextDigitalOrder = 1U;
    _inputOverflowed = NO;
}

- (void)notifyInputFailureForGeneration:(uint64_t)generation {
    NSAssert(NSThread.isMainThread, @"Controller failure publication belongs to main");
    id<AirfixGameControllerAdapterDelegate> delegate = self.delegate;
    SEL selector =
        @selector(gameControllerAdapter:didFailInputForGeneration:);
    if ([delegate respondsToSelector:selector]) {
        try {
            [delegate gameControllerAdapter:self
                didFailInputForGeneration:generation];
        }
        catch (...) {
            // Never unwind a C++ delegate failure through Game Controller.
        }
    }
}

- (void)publishCurrentState {
    NSAssert(NSThread.isMainThread, @"Controller publication belongs to main");
    GCController* controller = _assignedController;
    const BOOL connected =
        _running && controller != nil && controller.extendedGamepad != nil;
    NSString* vendorName = connected ? (controller.vendorName ?: @"Controller") : @"";
    NSString* productCategory =
        connected ? (controller.productCategory ?: @"") : @"";
    self.state = [[AirfixGameControllerState alloc]
        initWithConnected:connected
               generation:_publishedGeneration.load(std::memory_order_acquire)
                vendorName:vendorName
          productCategory:productCategory];
    id<AirfixGameControllerAdapterDelegate> delegate = self.delegate;
    SEL selector = @selector(gameControllerAdapter:didChangeState:);
    if ([delegate respondsToSelector:selector]) {
        try {
            [delegate gameControllerAdapter:self didChangeState:self.state];
        }
        catch (...) {
            // State publication is diagnostic; adapter integrity comes first.
        }
    }
}

@end
