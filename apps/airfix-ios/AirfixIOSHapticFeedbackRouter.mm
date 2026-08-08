#import "AirfixIOSHapticFeedbackRouter.h"

#import <UIKit/UIKit.h>

@interface AirfixIOSHapticFeedbackRouter ()
@property(nonatomic, strong, nullable)
    UISelectionFeedbackGenerator *selectionGenerator;
@end

@implementation AirfixIOSHapticFeedbackRouter

- (void)setEnabled:(BOOL)enabled {
  NSAssert(NSThread.isMainThread,
           @"Haptic feedback belongs to the main thread");
  if (!NSThread.isMainThread || _enabled == enabled) {
    return;
  }
  _enabled = enabled;
  if (!enabled) {
    [self cancelFeedback];
  }
}

- (void)prepareSelectionFeedback {
  NSAssert(NSThread.isMainThread,
           @"Haptic feedback belongs to the main thread");
  if (!NSThread.isMainThread || !self.enabled) {
    return;
  }
  if (self.selectionGenerator == nil) {
    self.selectionGenerator = [[UISelectionFeedbackGenerator alloc] init];
  }
  [self.selectionGenerator prepare];
}

- (void)playSelectionFeedback {
  NSAssert(NSThread.isMainThread,
           @"Haptic feedback belongs to the main thread");
  if (!NSThread.isMainThread || !self.enabled) {
    return;
  }
  [self prepareSelectionFeedback];
  [self.selectionGenerator selectionChanged];
  [self.selectionGenerator prepare];
}

- (void)cancelFeedback {
  NSAssert(NSThread.isMainThread,
           @"Haptic feedback belongs to the main thread");
  if (!NSThread.isMainThread) {
    return;
  }
  self.selectionGenerator = nil;
}

@end
