#import "AirfixSimulatorSmokeHarness.h"

#import "AirfixGameViewController.h"

namespace {

NSString *const kResultFilename = @"airfix-ios-simulator-smoke-v1.json";

} // namespace

@interface AirfixSimulatorSmokeHarness ()
@property(nonatomic, weak) AirfixGameViewController *gameViewController;
@property(nonatomic) BOOL started;
@end

@implementation AirfixSimulatorSmokeHarness

- (instancetype)initWithGameViewController:
    (AirfixGameViewController *)gameViewController {
  self = [super init];
  if (self != nil) {
    _gameViewController = gameViewController;
  }
  return self;
}

- (void)start {
  NSAssert(NSThread.isMainThread,
           @"Simulator smoke harness belongs to the main thread");
  if (self.started) {
    return;
  }
  self.started = YES;

  AirfixGameViewController *controller = self.gameViewController;
  if (controller == nil) {
    return;
  }
  [controller
      runSimulatorSmokeWithCompletion:^(NSDictionary<NSString *, id> *result) {
        if (![NSJSONSerialization isValidJSONObject:result]) {
          return;
        }
        NSError *serializationError = nil;
        NSData *data =
            [NSJSONSerialization dataWithJSONObject:result
                                            options:NSJSONWritingSortedKeys
                                              error:&serializationError];
        if (data == nil || serializationError != nil) {
          return;
        }
        NSURL *documents =
            [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                   inDomains:NSUserDomainMask]
                .firstObject;
        if (documents == nil) {
          return;
        }
        NSURL *destination =
            [documents URLByAppendingPathComponent:kResultFilename
                                       isDirectory:NO];
        // NSDataWritingAtomic prevents CI from accepting a
        // partially written success document.
        [data writeToURL:destination options:NSDataWritingAtomic error:nil];
      }];
}

@end
