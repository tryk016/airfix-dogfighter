#import "AirfixAppDelegate.h"

#import "AirfixGameViewController.h"

@interface AirfixAppDelegate ()
@property(nonatomic, strong) UIWindow* window;
@property(nonatomic, strong) AirfixGameViewController* gameViewController;
@end

@implementation AirfixAppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    (void)application;
    (void)launchOptions;

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.gameViewController = [[AirfixGameViewController alloc] init];
    self.window.rootViewController = self.gameViewController;
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationWillResignActive:(UIApplication*)application {
    (void)application;
    [self.gameViewController applicationWillResignActive];
}

- (void)applicationDidEnterBackground:(UIApplication*)application {
    (void)application;
    [self.gameViewController applicationDidEnterBackground];
}

- (void)applicationWillEnterForeground:(UIApplication*)application {
    (void)application;
    [self.gameViewController applicationWillEnterForeground];
}

@end
