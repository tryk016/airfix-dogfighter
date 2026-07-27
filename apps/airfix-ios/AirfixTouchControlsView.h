#import <UIKit/UIKit.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, AirfixTouchAxis) {
  AirfixTouchAxisBank = 0,
  AirfixTouchAxisPitch,
  AirfixTouchAxisThrottleSet,
  AirfixTouchAxisCameraLookX,
  AirfixTouchAxisCameraLookY,
};

typedef NS_ENUM(NSInteger, AirfixTouchButton) {
  AirfixTouchButtonThrottleIncrease = 0,
  AirfixTouchButtonThrottleDecrease,
  AirfixTouchButtonPrimaryFire,
  AirfixTouchButtonSecondaryFire,
  AirfixTouchButtonWeaponNext,
  AirfixTouchButtonRearView,
  AirfixTouchButtonCameraCycle,
  AirfixTouchButtonCameraRecenter,
  AirfixTouchButtonMissionStatus,
  AirfixTouchButtonPause,
  AirfixTouchButtonCount,
};

@class AirfixTouchControlsView;

@protocol AirfixTouchControlsViewDelegate <NSObject>
- (void)touchControlsView:(AirfixTouchControlsView *)view
            didChangeAxis:(AirfixTouchAxis)axis
                    value:(int16_t)value;
- (void)touchControlsView:(AirfixTouchControlsView *)view
          didChangeButton:(AirfixTouchButton)button
                  pressed:(BOOL)pressed;
- (void)touchControlsView:(AirfixTouchControlsView *)view
      didSelectWeaponSlot:(uint8_t)slot;
- (void)touchControlsViewDidCancelAll:(AirfixTouchControlsView *)view;
@end

@interface AirfixTouchControlsView : UIView

@property(nonatomic, weak, nullable) id<AirfixTouchControlsViewDelegate>
    delegate;

// Main-thread only. Releases every captured/accessible held control and
// publishes neutral stick, camera-look, throttle-delta, and button state. The
// latched absolute throttle target is deliberately preserved and republished.
// Lifecycle and overlay owners should call this before suspending or replacing
// gameplay UI.
- (void)cancelAllTouches;

@end

NS_ASSUME_NONNULL_END
