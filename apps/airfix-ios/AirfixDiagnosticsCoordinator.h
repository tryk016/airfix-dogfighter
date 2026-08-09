#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, AirfixDiagnosticContentState) {
  AirfixDiagnosticContentStateMissing,
  AirfixDiagnosticContentStateValidating,
  AirfixDiagnosticContentStateReady,
  AirfixDiagnosticContentStateRejected,
};

typedef NS_ENUM(NSInteger, AirfixDiagnosticMissionFailureStage) {
  AirfixDiagnosticMissionFailureStageContent,
  AirfixDiagnosticMissionFailureStageHandoff,
  AirfixDiagnosticMissionFailureStageMetalPreparation,
  AirfixDiagnosticMissionFailureStagePlayerSpawn,
  AirfixDiagnosticMissionFailureStagePublication,
  AirfixDiagnosticMissionFailureStagePlayerPose,
  AirfixDiagnosticMissionFailureStageCamera,
  AirfixDiagnosticMissionFailureStageAudio,
};

typedef NS_ENUM(NSInteger, AirfixDiagnosticLifecycleEvent) {
  AirfixDiagnosticLifecycleEventResignActive,
  AirfixDiagnosticLifecycleEventBackground,
  AirfixDiagnosticLifecycleEventForeground,
  AirfixDiagnosticLifecycleEventActive,
};

typedef NS_ENUM(NSInteger, AirfixDiagnosticPauseReason) {
  AirfixDiagnosticPauseReasonUser,
  AirfixDiagnosticPauseReasonSettings,
  AirfixDiagnosticPauseReasonLifecycle,
  AirfixDiagnosticPauseReasonControllerDisconnected,
  AirfixDiagnosticPauseReasonInputOverflow,
  AirfixDiagnosticPauseReasonInputFailure,
  AirfixDiagnosticPauseReasonAudioInterruption,
  AirfixDiagnosticPauseReasonAudioRoute,
  AirfixDiagnosticPauseReasonAudioServices,
};

typedef NS_ENUM(NSInteger, AirfixDiagnosticInputSource) {
  AirfixDiagnosticInputSourceNone,
  AirfixDiagnosticInputSourceTouch,
  AirfixDiagnosticInputSourceController,
};

// Creates the user-visible Documents/Imports and Documents/Diagnostics
// directories. In the Files application these appear below the app's
// "Airfix Dogfighter" container. The journal is intentionally path-free:
// callers can provide only controlled enums, booleans, and numeric metrics.
@interface AirfixDiagnosticsCoordinator : NSObject

@property(nonatomic, readonly, getter=isReady) BOOL ready;

- (void)recordRendererInitializationSucceeded:(BOOL)succeeded;
- (void)recordContentState:(AirfixDiagnosticContentState)state;
- (void)recordMissionLoadStarted;
- (void)recordMissionReadyWithMeshCount:(NSUInteger)meshCount
                           textureCount:(NSUInteger)textureCount
                          drawCallCount:(NSUInteger)drawCallCount;
- (void)recordMissionLoadFailedAtStage:
    (AirfixDiagnosticMissionFailureStage)stage;
- (void)recordGameplayResumed;
- (void)recordGameplayPausedForReason:(AirfixDiagnosticPauseReason)reason;
- (void)recordLifecycleEvent:(AirfixDiagnosticLifecycleEvent)event;
- (void)recordControllerConnected:(BOOL)connected;
- (void)recordInputSampleWithTick:(uint64_t)tick
                             bank:(int16_t)bank
                            pitch:(int16_t)pitch
                         throttle:(int16_t)throttle
                      firePressed:(BOOL)firePressed
                         fireHeld:(BOOL)fireHeld
                     fireReleased:(BOOL)fireReleased
              controllerConnected:(BOOL)controllerConnected
                           source:(AirfixDiagnosticInputSource)source
                   simulationStep:(uint64_t)simulationStep
                   simulationHash:(uint64_t)simulationHash;
- (void)recordMemoryWarning;

@end

NS_ASSUME_NONNULL_END
