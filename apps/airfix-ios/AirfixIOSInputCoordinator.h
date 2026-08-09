#import <Foundation/Foundation.h>

#import "AirfixGameControllerAdapter.h"

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

@class AirfixIOSInputCoordinator;
@class AirfixTouchControlsView;

typedef NS_ENUM(NSInteger, AirfixInputSource) {
    AirfixInputSourceNone,
    AirfixInputSourceTouch,
    AirfixInputSourceController,
};

typedef NS_ENUM(NSInteger, AirfixInputPauseReason) {
    // Pause/menu was pressed. The host may treat this as an explicit
    // pause/resume toggle.
    AirfixInputPauseReasonUserControl,
    // Controller loss is a forced pause, never a resume toggle.
    AirfixInputPauseReasonControllerDisconnected,
    // Lifecycle transitions are forced pauses.
    AirfixInputPauseReasonLifecycle,
    // A bounded event queue overflowed. Sources were reset and the host may
    // explicitly resume after presenting the pause.
    AirfixInputPauseReasonInputOverflow,
    // A monotonic counter or the exactly-once frame pipeline failed. This
    // coordinator is no longer operational and must not be resumed.
    AirfixInputPauseReasonInputPipelineFailure,
};

typedef NS_ENUM(NSInteger, AirfixNativeInputContext) {
    AirfixNativeInputContextGameplay,
    AirfixNativeInputContextMenu,
    AirfixNativeInputContextModal,
    AirfixNativeInputContextControlEditor,
};

// Immutable Objective-C projection of the most recent portable InputFrame.
// It is intentionally narrow: simulation code continues to consume the C++
// frame, while UIKit can render input diagnostics without seeing C++ types.
@interface AirfixInputDiagnostics : NSObject

@property(nonatomic, readonly) uint64_t tick;
@property(nonatomic, readonly) int16_t bank;
@property(nonatomic, readonly) int16_t pitch;
@property(nonatomic, readonly) int16_t throttle;
@property(nonatomic, readonly) BOOL primaryPressed;
@property(nonatomic, readonly) BOOL primaryHeld;
@property(nonatomic, readonly) BOOL primaryReleased;
@property(nonatomic, readonly, getter=isControllerConnected)
    BOOL controllerConnected;
@property(nonatomic, readonly) AirfixInputSource lastMeaningfulSource;

- (instancetype)init NS_UNAVAILABLE;

@end

// Immutable Objective-C projection of one already-delivered portable
// InputFrame while the router is in a menu or modal context. Gameplay fields
// and platform timestamps are intentionally not exposed.
@interface AirfixUIInputSnapshot : NSObject

@property(nonatomic, readonly) uint64_t tick;
@property(nonatomic, readonly) int16_t navigationX;
@property(nonatomic, readonly) int16_t navigationY;
@property(nonatomic, readonly) BOOL confirmPressed;
@property(nonatomic, readonly) BOOL cancelPressed;
@property(nonatomic, readonly) BOOL tabPreviousPressed;
@property(nonatomic, readonly) BOOL tabNextPressed;
@property(nonatomic, readonly, getter=isControllerConnected)
    BOOL controllerConnected;
@property(nonatomic, readonly) int16_t controllerLeftStickX;
@property(nonatomic, readonly) int16_t controllerLeftStickY;
@property(nonatomic, readonly) int16_t controllerRightStickX;
@property(nonatomic, readonly) int16_t controllerRightStickY;

- (instancetype)init NS_UNAVAILABLE;

@end

@protocol AirfixIOSInputCoordinatorDelegate <NSObject>
@optional

// All callbacks are delivered on main. Only UserControl may be interpreted as
// an explicit toggle; every other reason must only force a pause.
- (void)inputCoordinator:(AirfixIOSInputCoordinator*)coordinator
       didRequestPauseForReason:(AirfixInputPauseReason)reason;

- (void)inputCoordinator:(AirfixIOSInputCoordinator*)coordinator
        didChangeControllerState:(AirfixGameControllerState*)state;

// Published on meaningful changes and otherwise at most as a 10 Hz heartbeat.
- (void)inputCoordinator:(AirfixIOSInputCoordinator*)coordinator
        didUpdateDiagnostics:(AirfixInputDiagnostics*)diagnostics;

// Delivered on main after the exact portable InputFrame has reached its
// consumer and only while the active input context is menu or modal. The four
// optional preview values are raw standardized Q15 stick axes; no device
// identity is exposed. A reentrant input/lifecycle reset suppresses this
// callback for the stale frame.
- (void)inputCoordinator:(AirfixIOSInputCoordinator*)coordinator
        didUpdateUIInput:(AirfixUIInputSnapshot*)input;

@end

// Main-thread-affine owner of the portable InputRouter and the native touch and
// Apple controller adapters. Its fixed 60 Hz pump is independent of MTKView,
// so input cancellation and diagnostics continue while rendering is paused.
@interface AirfixIOSInputCoordinator : NSObject

@property(nonatomic, weak, nullable) id<AirfixIOSInputCoordinatorDelegate> delegate;
@property(nonatomic, strong, readonly) AirfixInputDiagnostics* diagnostics;
@property(nonatomic, strong, readonly)
    AirfixGameControllerState* controllerState;
// Main-thread status. NO is terminal for this coordinator instance; gameplay
// must remain paused and a new coordinator is required.
@property(nonatomic, readonly, getter=isOperational) BOOL operational;
@property(nonatomic, readonly) AirfixNativeInputContext inputContext;

- (instancetype)initWithTouchControlsView:
    (AirfixTouchControlsView*)touchControlsView NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

- (void)start;
- (void)stop;

- (void)applicationWillResignActive;
- (void)applicationDidEnterBackground;
- (void)applicationWillEnterForeground;
- (void)applicationDidBecomeActive;

// Changes the semantic binding context after cancelling every physical source.
// Controls that remain held must return to neutral before they can act again.
// This method never resumes gameplay and must be called on main.
- (void)setInputContext:(AirfixNativeInputContext)inputContext;

// Clears all physical sources and closes the router's neutral gate at content,
// room, mission, or explicit gameplay boundaries. It never resumes gameplay
// and must be called on main.
- (void)resetForGameplayBoundary;

@end

NS_ASSUME_NONNULL_END
