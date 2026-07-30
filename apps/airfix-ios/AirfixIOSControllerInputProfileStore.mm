#import "AirfixIOSControllerInputProfileStore.h"

#include "AirfixIOSSettingsStoreSupport.hpp"

@implementation AirfixIOSControllerInputProfileStore

- (void)loadWithCompletion:
    (AirfixControllerInputProfileLoadCompletion)completion {
  NSParameterAssert(completion != nil);
  dispatch_async(airfix::ios::detail::settingsPersistenceQueue(), ^{
    airfix::ios::ControllerInputProfileLoadOutcome outcome;
    const auto directory =
        airfix::ios::detail::preparePrivateSettingsDirectory();
    if (!directory.has_value()) {
      outcome.error =
          airfix::ios::ControllerInputProfileStorageError::storageUnavailable;
    } else {
      try {
        outcome.result =
            airfix::settings::loadControllerInputProfile(*directory);
      } catch (...) {
        outcome.error =
            airfix::ios::ControllerInputProfileStorageError::storageUnavailable;
      }
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      completion(outcome);
    });
  });
}

@end
