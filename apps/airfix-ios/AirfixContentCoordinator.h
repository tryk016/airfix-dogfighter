#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, AirfixContentReadiness) {
    AirfixContentReadinessMissing,
    AirfixContentReadinessValidating,
    AirfixContentReadinessReady,
    AirfixContentReadinessRejected,
};

@class AirfixContentCoordinator;

@protocol AirfixContentCoordinatorDelegate <NSObject>
- (void)contentCoordinator:(AirfixContentCoordinator*)coordinator
        didChangeReadiness:(AirfixContentReadiness)readiness;
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

@end

NS_ASSUME_NONNULL_END
