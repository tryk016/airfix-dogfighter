#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@class AirfixControllerInputProfileCoordinator;
@class AirfixControllerCalibrationPanelViewController;
@class AirfixUIInputSnapshot;

@protocol AirfixControllerCalibrationPanelViewControllerDelegate <NSObject>

// Closing the overlay never resumes gameplay. The host resets the menu input
// boundary and offers an explicit Resume action.
- (void)controllerCalibrationPanelViewControllerDidFinish:
    (AirfixControllerCalibrationPanelViewController *)panel;

@end

// Calibration-only editor for the four standardized stick axes. Saves are
// durable for the next process launch; the active input profile is never
// replaced while this controller exists.
@interface AirfixControllerCalibrationPanelViewController : UIViewController

@property(nonatomic, weak, nullable)
    id<AirfixControllerCalibrationPanelViewControllerDelegate>
        delegate;
@property(nonatomic, readonly) BOOL persistedDuringPresentation;

- (instancetype)initWithCoordinator:
    (AirfixControllerInputProfileCoordinator *)coordinator
    NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithNibName:(nullable NSString *)nibNameOrNil
                         bundle:(nullable NSBundle *)nibBundleOrNil
    NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

// Main-thread controller/menu projection. Preview axes are raw standardized
// Q15 values and are transformed with the shared portable calibration path.
- (void)consumeUIInputSnapshot:(AirfixUIInputSnapshot *)input;

@end

NS_ASSUME_NONNULL_END
