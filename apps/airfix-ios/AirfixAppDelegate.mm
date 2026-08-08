#import "AirfixAppDelegate.h"

#import "AirfixGameViewController.h"

@interface AirfixAppDelegate ()
@property(nonatomic, strong) AirfixGameViewController *gameViewController;
@end

@implementation AirfixAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  (void)application;
  (void)launchOptions;

  self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
  self.gameViewController = [[AirfixGameViewController alloc] init];
  self.window.rootViewController = self.gameViewController;
  [self.window makeKeyAndVisible];
#if AIRFIX_IOS_SIMULATOR_SMOKE
  NSLog(@"Airfix simulator smoke milestone launch-finished");
#endif
  return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application {
  (void)application;
#if AIRFIX_IOS_SIMULATOR_SMOKE
  NSLog(@"Airfix simulator smoke milestone lifecycle-will-resign-active");
#endif
  [self.gameViewController applicationWillResignActive];
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
  (void)application;
#if AIRFIX_IOS_SIMULATOR_SMOKE
  NSLog(@"Airfix simulator smoke milestone lifecycle-did-enter-background");
#endif
  [self.gameViewController applicationDidEnterBackground];
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
  (void)application;
#if AIRFIX_IOS_SIMULATOR_SMOKE
  NSLog(@"Airfix simulator smoke milestone lifecycle-will-enter-foreground");
#endif
  [self.gameViewController applicationWillEnterForeground];
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
  (void)application;
#if AIRFIX_IOS_SIMULATOR_SMOKE
  NSLog(@"Airfix simulator smoke milestone lifecycle-did-become-active");
#endif
  [self.gameViewController applicationDidBecomeActive];
}

@end
