#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, AirfixContentReadiness) {
    AirfixContentReadinessMissing,
    AirfixContentReadinessValidating,
    AirfixContentReadinessReady,
    AirfixContentReadinessRejected,
};

@class AirfixContentCoordinator;
@class AirfixWorldRoomSnapshot;

@protocol AirfixContentCoordinatorDelegate <NSObject>
- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didChangeReadiness:(AirfixContentReadiness)readiness;

@optional
// Main-thread notification.
- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didBeginLoadingWorldAtLogicalPath:(NSString*)worldLogicalPath
        physicalRoom:(NSUInteger)physicalRoom;
- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didLoadWorldRoomSnapshot:(AirfixWorldRoomSnapshot*)snapshot;
- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didFailLoadingWorldAtLogicalPath:(NSString*)worldLogicalPath
        physicalRoom:(NSUInteger)physicalRoom;
@end

// Owns the native private-content workflow. All package mutations are
// serialized internally; callers only provide a view controller for document
// presentation and map readiness into the runtime session.
@interface AirfixContentCoordinator : NSObject

@property(nonatomic, weak, nullable) id<AirfixContentCoordinatorDelegate> delegate;
@property(nonatomic, strong, readonly) UIView* controlsView;
@property(nonatomic, readonly) AirfixContentReadiness readiness;

- (instancetype)initWithPresentingViewController:(UIViewController*)viewController
    NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

- (void)start;
- (void)applicationWillResignActive;
- (void)applicationDidEnterBackground;
- (void)applicationWillEnterForeground;
- (void)applicationDidBecomeActive;

// Remembers the latest requested physical CCF room. If content is validating
// or the app is inactive, loading begins after the next ready inspection.
- (void)requestWorldAtLogicalPath:(NSString*)worldLogicalPath
                    physicalRoom:(NSUInteger)physicalRoom;

// Main-thread two-phase publication check. The caller checks after off-main
// Metal preparation and again immediately before its constant-time swap.
- (BOOL)isWorldRoomSnapshotCurrent:(AirfixWorldRoomSnapshot*)snapshot;

@end

NS_ASSUME_NONNULL_END
