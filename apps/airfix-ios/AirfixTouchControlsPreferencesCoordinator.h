#import <Foundation/Foundation.h>

#ifdef __cplusplus
#include "airfix/input/TouchControlsPreferences.hpp"
#endif

NS_ASSUME_NONNULL_BEGIN

@class AirfixIOSTouchControlsPreferencesStore;
@class AirfixTouchControlsView;

typedef NS_ENUM(NSInteger, AirfixTouchControlsPreferencesSaveResult) {
  AirfixTouchControlsPreferencesSaveResultSaved,
  AirfixTouchControlsPreferencesSaveResultInvalidCandidate,
  AirfixTouchControlsPreferencesSaveResultPersistenceUnavailable,
  AirfixTouchControlsPreferencesSaveResultSaveFailed,
  AirfixTouchControlsPreferencesSaveResultBusy,
};

typedef void (^AirfixTouchControlsPreferencesSaveResultCompletion)(
    AirfixTouchControlsPreferencesSaveResult result);

// Main-thread owner of the active and durable touch-overlay preferences. A
// successful request is persisted first and then atomically published to the
// view, so a failed write cannot leave a session-only UI surprise.
@interface AirfixTouchControlsPreferencesCoordinator : NSObject

@property(nonatomic, readonly) BOOL persistenceAvailable;
@property(nonatomic, readonly) BOOL repairRequired;
@property(nonatomic, readonly, getter=isSaving) BOOL saving;

#ifdef __cplusplus
- (nullable instancetype)
           initWithStore:(AirfixIOSTouchControlsPreferencesStore *)store
       touchControlsView:(AirfixTouchControlsView *)touchControlsView
       activePreferences:
           (const airfix::input::TouchControlsPreferences &)preferences
    persistenceAvailable:(BOOL)persistenceAvailable
          repairRequired:(BOOL)repairRequired NS_DESIGNATED_INITIALIZER;

- (airfix::input::TouchControlsPreferences)activePreferences;
- (void)requestPreferences:
            (const airfix::input::TouchControlsPreferences &)preferences
                completion:(AirfixTouchControlsPreferencesSaveResultCompletion)
                               completion;
#endif

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
