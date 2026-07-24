#import <UIKit/UIKit.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, AirfixTouchAxis) {
    AirfixTouchAxisBank = 0,
    AirfixTouchAxisPitch,
};

typedef NS_ENUM(NSInteger, AirfixTouchButton) {
    AirfixTouchButtonPrimaryFire = 0,
    AirfixTouchButtonPause,
};

@class AirfixTouchControlsView;

@protocol AirfixTouchControlsViewDelegate <NSObject>
- (void)touchControlsView:(AirfixTouchControlsView*)view
            didChangeAxis:(AirfixTouchAxis)axis
                    value:(int16_t)value;
- (void)touchControlsView:(AirfixTouchControlsView*)view
          didChangeButton:(AirfixTouchButton)button
                  pressed:(BOOL)pressed;
- (void)touchControlsViewDidCancelAll:(AirfixTouchControlsView*)view;
@end

@interface AirfixTouchControlsView : UIView

@property(nonatomic, weak, nullable) id<AirfixTouchControlsViewDelegate> delegate;

// Main-thread only. Releases every captured control and publishes neutral
// state. Lifecycle and overlay owners should call this before suspending or
// replacing gameplay UI.
- (void)cancelAllTouches;

@end

NS_ASSUME_NONNULL_END
