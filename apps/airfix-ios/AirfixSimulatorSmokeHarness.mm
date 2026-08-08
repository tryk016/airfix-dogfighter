#import "AirfixSimulatorSmokeHarness.h"

#import "AirfixGameViewController.h"

#import <dispatch/dispatch.h>

namespace {

NSString *const kResultFilename = @"airfix-ios-simulator-smoke-v1.json";
constexpr int64_t kWatchdogNanoseconds = 42 * NSEC_PER_SEC;

} // namespace

@interface AirfixSimulatorSmokeHarness ()
@property(nonatomic, weak) AirfixGameViewController *gameViewController;
@property(nonatomic) BOOL started;
@property(nonatomic, strong) dispatch_queue_t publicationQueue;
@property(nonatomic) BOOL resultPublished;
@property(nonatomic) BOOL firstDrawBegan;
@property(nonatomic) BOOL firstDrawReturned;
@property(nonatomic, copy) NSString *watchdogFailureStage;
- (void)enqueueResult:(NSDictionary<NSString *, id> *)result;
- (void)publishResultOnPublicationQueue:(NSDictionary<NSString *, id> *)result;
@end

@implementation AirfixSimulatorSmokeHarness

- (instancetype)initWithGameViewController:
    (AirfixGameViewController *)gameViewController {
  self = [super init];
  if (self != nil) {
    _gameViewController = gameViewController;
    _publicationQueue = dispatch_queue_create(
        "com.tryk016.airfixdogfighter.simulator-smoke-result",
        DISPATCH_QUEUE_SERIAL);
    _watchdogFailureStage = @"renderer-initialization-timeout";
  }
  return self;
}

- (void)armWatchdog {
  NSAssert(NSThread.isMainThread,
           @"Simulator smoke watchdog belongs to the main thread");
  NSLog(@"Airfix simulator smoke milestone watchdog-armed");
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kWatchdogNanoseconds),
                 self.publicationQueue, ^{
                   NSLog(@"Airfix simulator smoke watchdog-fired %@",
                         self.watchdogFailureStage);
                   [self publishResultOnPublicationQueue:@{
                     @"schema" : @"airfix.ios-simulator-smoke",
                     @"version" : @1,
                     @"status" : @"fail",
                     @"failureStage" : self.watchdogFailureStage,
                   }];
                 });
}

- (void)noteRendererInitializationCompleted {
  dispatch_async(self.publicationQueue, ^{
    if (!self.resultPublished) {
      self.watchdogFailureStage = @"smoke-sequence-timeout";
      NSLog(@"Airfix simulator smoke milestone renderer-initialized");
    }
  });
}

- (void)noteDrawWillBegin {
  dispatch_async(self.publicationQueue, ^{
    if (!self.resultPublished) {
      self.watchdogFailureStage = @"draw-call-timeout";
      if (!self.firstDrawBegan) {
        self.firstDrawBegan = YES;
        NSLog(@"Airfix simulator smoke milestone first-draw-begin");
      }
    }
  });
}

- (void)noteDrawDidReturn {
  dispatch_async(self.publicationQueue, ^{
    if (!self.resultPublished) {
      self.watchdogFailureStage = @"smoke-sequence-timeout";
      if (!self.firstDrawReturned) {
        self.firstDrawReturned = YES;
        NSLog(@"Airfix simulator smoke milestone first-draw-return");
      }
    }
  });
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
        [self enqueueResult:result];
      }];
}

- (void)enqueueResult:(NSDictionary<NSString *, id> *)result {
  dispatch_async(self.publicationQueue, ^{
    [self publishResultOnPublicationQueue:result];
  });
}

- (void)publishResultOnPublicationQueue:(NSDictionary<NSString *, id> *)result {
  if (self.resultPublished) {
    return;
  }
  self.resultPublished = YES;
  if (![NSJSONSerialization isValidJSONObject:result]) {
    NSLog(@"Airfix simulator smoke result-serialization-failed");
    return;
  }
  NSError *serializationError = nil;
  NSData *data = [NSJSONSerialization dataWithJSONObject:result
                                                 options:NSJSONWritingSortedKeys
                                                   error:&serializationError];
  if (data == nil || serializationError != nil) {
    NSLog(@"Airfix simulator smoke result-serialization-failed");
    return;
  }

  NSError *directoryError = nil;
  NSFileManager *fileManager = [NSFileManager defaultManager];
  NSURL *documents = [fileManager URLForDirectory:NSDocumentDirectory
                                         inDomain:NSUserDomainMask
                                appropriateForURL:nil
                                           create:YES
                                            error:&directoryError];
  if (documents == nil || directoryError != nil) {
    NSLog(@"Airfix simulator smoke result-write-failed");
    return;
  }
  NSURL *destination = [documents URLByAppendingPathComponent:kResultFilename
                                                  isDirectory:NO];
  NSError *writeError = nil;
  // NSDataWritingAtomic prevents CI from accepting a partially written result.
  if (![data writeToURL:destination
                options:NSDataWritingAtomic
                  error:&writeError] ||
      writeError != nil) {
    NSLog(@"Airfix simulator smoke result-write-failed");
  } else {
    NSLog(@"Airfix simulator smoke milestone result-written");
  }
}

@end
