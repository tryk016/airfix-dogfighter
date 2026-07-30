#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@class AirfixRenderSettingsCoordinator;
@class AirfixRenderSettingsPanelViewController;
@class AirfixUIInputSnapshot;

@protocol AirfixRenderSettingsPanelViewControllerDelegate <NSObject>

// Closing the overlay is a UI boundary only. The host remains responsible for
// resetting input and deliberately leaves gameplay paused.
- (void)renderSettingsPanelViewControllerDidFinish:
    (AirfixRenderSettingsPanelViewController *)panel;

@end

// Child-view-controller overlay for the four settings already defined by
// ADR-0014. It owns only draft UI state; the coordinator remains the sole owner
// of prepare, durable save, and Metal publication.
@interface AirfixRenderSettingsPanelViewController : UIViewController

@property(nonatomic, weak, nullable)
    id<AirfixRenderSettingsPanelViewControllerDelegate>
        delegate;

- (instancetype)initWithCoordinator:
    (AirfixRenderSettingsCoordinator *)coordinator NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithNibName:(nullable NSString *)nibNameOrNil
                         bundle:(nullable NSBundle *)nibBundleOrNil
    NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

// Main-thread controller/menu projection. Axis navigation uses hysteresis and
// requires a return to neutral before another row or value change.
- (void)consumeUIInputSnapshot:(AirfixUIInputSnapshot *)input;

@end

NS_ASSUME_NONNULL_END
