#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Main-thread-only bridge for optional UIKit touch feedback. Unsupported
// hardware and the simulator are valid silent outcomes.
@interface AirfixIOSHapticFeedbackRouter : NSObject

@property(nonatomic, getter=isEnabled) BOOL enabled;

- (void)prepareSelectionFeedback;
- (void)playSelectionFeedback;
- (void)cancelFeedback;

@end

NS_ASSUME_NONNULL_END
