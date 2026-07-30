#import <Foundation/Foundation.h>

#ifdef __cplusplus
#include "airfix/input/ControllerInputProfile.hpp"
#endif

NS_ASSUME_NONNULL_BEGIN

@class AirfixIOSControllerInputProfileStore;

typedef NS_ENUM(NSInteger, AirfixControllerInputProfileSaveResult) {
  AirfixControllerInputProfileSaveResultSaved,
  AirfixControllerInputProfileSaveResultInvalidCandidate,
  AirfixControllerInputProfileSaveResultPersistenceUnavailable,
  AirfixControllerInputProfileSaveResultSaveFailed,
  AirfixControllerInputProfileSaveResultBusy,
};

typedef void (^AirfixControllerInputProfileSaveResultCompletion)(
    AirfixControllerInputProfileSaveResult result);

// Main-thread owner of durable controller-profile edits after startup. The
// active record is immutable for this coordinator's lifetime; a successful
// save only updates the record that a later process launch will load.
@interface AirfixControllerInputProfileCoordinator : NSObject

@property(nonatomic, readonly) BOOL persistenceAvailable;
@property(nonatomic, readonly) BOOL repairRequired;
@property(nonatomic, readonly, getter=isSaving) BOOL saving;

#ifdef __cplusplus
- (nullable instancetype)
           initWithStore:(AirfixIOSControllerInputProfileStore *)store
           activeProfile:(const airfix::input::ControllerInputProfileRecord &)
                             activeProfile
       persistentProfile:(const airfix::input::ControllerInputProfileRecord &)
                             persistentProfile
    persistenceAvailable:(BOOL)persistenceAvailable
          repairRequired:(BOOL)repairRequired NS_DESIGNATED_INITIALIZER;

- (airfix::input::ControllerInputProfileRecord)activeProfile;
- (airfix::input::ControllerInputProfileRecord)persistentProfile;

// Completion runs exactly once on main. Saved means the complete AFIP is
// durable; it never means the active input router was replaced.
- (void)requestPersistentProfile:
            (const airfix::input::ControllerInputProfileRecord &)profile
                      completion:
                          (AirfixControllerInputProfileSaveResultCompletion)
                              completion;
#endif

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
