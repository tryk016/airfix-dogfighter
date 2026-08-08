#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface AirfixGameViewController : UIViewController
- (void)applicationWillResignActive;
- (void)applicationDidEnterBackground;
- (void)applicationWillEnterForeground;
- (void)applicationDidBecomeActive;
#if AIRFIX_IOS_SIMULATOR_SMOKE
- (void)runSimulatorSmokeWithCompletion:
    (void (^)(NSDictionary<NSString *, id> *result))completion;
#endif
@end

NS_ASSUME_NONNULL_END
