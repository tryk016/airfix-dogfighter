#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@class AirfixTouchControlsPreferencesCoordinator;
@class AirfixTouchControlsSettingsPanelViewController;
@class AirfixUIInputSnapshot;

@protocol AirfixTouchControlsSettingsPanelViewControllerDelegate <NSObject>
- (void)touchControlsSettingsPanelViewControllerDidFinish:
    (AirfixTouchControlsSettingsPanelViewController *)panel;
@end

@interface AirfixTouchControlsSettingsPanelViewController : UIViewController

@property(nonatomic, weak, nullable)
    id<AirfixTouchControlsSettingsPanelViewControllerDelegate>
        delegate;

- (instancetype)initWithCoordinator:
    (AirfixTouchControlsPreferencesCoordinator *)coordinator
    NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithNibName:(nullable NSString *)nibNameOrNil
                         bundle:(nullable NSBundle *)nibBundleOrNil
    NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

- (void)consumeUIInputSnapshot:(AirfixUIInputSnapshot *)input;

@end

NS_ASSUME_NONNULL_END
