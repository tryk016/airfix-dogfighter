#import <UIKit/UIKit.h>

#include <stdint.h>

#ifdef __cplusplus
#include "airfix/input/TouchControlsPreferences.hpp"
#endif

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

typedef NS_ENUM(NSInteger, AirfixTouchControlsHandedness) {
  AirfixTouchControlsHandednessRight = 0,
  AirfixTouchControlsHandednessLeft,
};

typedef NS_ENUM(NSInteger, AirfixTouchControlsDensity) {
  AirfixTouchControlsDensityAutomatic = 0,
  AirfixTouchControlsDensityCompact,
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

// Main-thread only. Changing either property first releases every active
// touch, then applies a stable semantic-preserving relayout. Left-handed mode
// mirrors geometry only; it never swaps input action identifiers.
@property(nonatomic) AirfixTouchControlsHandedness layoutHandedness;
@property(nonatomic) AirfixTouchControlsDensity layoutDensity;

#ifdef __cplusplus
// Applies one validated preference snapshot atomically on main. A geometry
// change first neutralizes every held control; opacity-only changes leave
// ownership and capture geometry intact.
- (BOOL)applyPreferences:
    (const airfix::input::TouchControlsPreferences &)preferences;
#endif

// Main-thread only. Releases every captured/accessible held control and
// publishes neutral stick, camera-look, throttle-delta, and button state. The
// latched absolute throttle target is deliberately preserved and republished.
// Lifecycle and overlay owners should call this before suspending or replacing
// gameplay UI.
- (void)cancelAllTouches;

@end

NS_ASSUME_NONNULL_END
