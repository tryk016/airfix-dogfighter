#import <UIKit/UIKit.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, AirfixContentReadiness) {
    AirfixContentReadinessMissing,
    AirfixContentReadinessValidating,
    AirfixContentReadinessReady,
    AirfixContentReadinessRejected,
};

typedef NS_ENUM(NSInteger, AirfixTexturePackageAvailability) {
    AirfixTexturePackageAvailabilityNotConfigured,
    AirfixTexturePackageAvailabilityValidating,
    AirfixTexturePackageAvailabilityReady,
    AirfixTexturePackageAvailabilityUnavailable,
};

typedef NS_ENUM(NSInteger, AirfixMissionTextureMode) {
    AirfixMissionTextureModeClassic,
    AirfixMissionTextureModeEnhanced,
};

@class AirfixContentCoordinator;
@class AirfixMissionWorldRoomSnapshot;

@protocol AirfixContentCoordinatorDelegate <NSObject>
- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didChangeReadiness:(AirfixContentReadiness)readiness;

@optional
// Main-thread notification.
- (void)contentCoordinatorDidBeginLoadingMission:
    (AirfixContentCoordinator*)coordinator;
- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
    didLoadMissionWorldRoomSnapshot:
        (AirfixMissionWorldRoomSnapshot*)snapshot;
- (void)contentCoordinatorDidFailLoadingMission:
    (AirfixContentCoordinator*)coordinator;
- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
    didChangeTexturePackageAvailability:
        (AirfixTexturePackageAvailability)availability;
@end

// Owns the native private-content workflow. All package mutations are
// serialized internally; callers only provide a view controller for document
// presentation and map readiness into the runtime session.
@interface AirfixContentCoordinator : NSObject

@property(nonatomic, weak, nullable) id<AirfixContentCoordinatorDelegate> delegate;
@property(nonatomic, strong, readonly) UIView* controlsView;
@property(nonatomic, readonly) AirfixContentReadiness readiness;
@property(nonatomic, readonly)
    AirfixTexturePackageAvailability texturePackageAvailability;

- (instancetype)initWithPresentingViewController:(UIViewController*)viewController
    NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

- (void)start;
- (void)applicationWillResignActive;
- (void)applicationDidEnterBackground;
- (void)applicationWillEnterForeground;
- (void)applicationDidBecomeActive;

// Changes only the requested mission-texture policy. Enhanced becomes
// effective after a completely validated private package is ready; otherwise
// a full mission reload safely uses Classic GTI textures.
- (void)requestMissionTextureMode:(AirfixMissionTextureMode)textureMode;

// Remembers an explicit private mission setup/Level pair and an optional exact
// player object-definition path. If content is validating or the app is
// inactive, loading begins after the next ready inspection. The paths are
// never inferred, displayed, logged, or exposed by the resulting public
// snapshot.
- (void)requestMissionWithSetupLogicalPath:(NSString*)setupLogicalPath
                          levelLogicalPath:(NSString*)levelLogicalPath
                       requestedStartIndex:(uint32_t)requestedStartIndex;

- (void)requestMissionWithSetupLogicalPath:(NSString*)setupLogicalPath
                          levelLogicalPath:(NSString*)levelLogicalPath
                   playerObjectLogicalPath:
                       (NSString* _Nullable)playerObjectLogicalPath
                       requestedStartIndex:(uint32_t)requestedStartIndex;

// Main-thread two-phase publication check. The caller checks after off-main
// Metal preparation and again immediately before transaction commit.
- (BOOL)isMissionWorldRoomSnapshotCurrent:
    (AirfixMissionWorldRoomSnapshot*)snapshot;

// Finalize exactly the current ticket. consume... is called after renderer
// candidate validation and immediately before its no-fail swap. abandon...
// releases only a matching failed candidate and cannot cancel a newer request.
- (BOOL)consumeMissionWorldRoomSnapshot:
    (AirfixMissionWorldRoomSnapshot*)snapshot;
- (BOOL)abandonMissionWorldRoomSnapshot:
    (AirfixMissionWorldRoomSnapshot*)snapshot;

@end

NS_ASSUME_NONNULL_END
