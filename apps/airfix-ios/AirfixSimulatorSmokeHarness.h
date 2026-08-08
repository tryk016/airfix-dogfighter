#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@class AirfixGameViewController;

// Exists only in the explicitly enabled iPhoneSimulator CI target. The
// production and iphoneos targets do not compile or link this class.
@interface AirfixSimulatorSmokeHarness : NSObject
- (instancetype)initWithGameViewController:
    (AirfixGameViewController *)gameViewController NS_DESIGNATED_INITIALIZER;
- (void)armWatchdog;
- (void)noteRendererInitializationCompleted;
- (void)noteDrawWillBegin;
- (void)noteDrawDidReturn;
- (void)start;
- (instancetype)init NS_UNAVAILABLE;
@end

NS_ASSUME_NONNULL_END
