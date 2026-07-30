#import "AirfixIOSControllerInputProfileStore.h"

#include "AirfixIOSSettingsStoreSupport.hpp"

namespace {

[[nodiscard]] airfix::ios::ControllerInputProfileStorageError
storageError(const airfix::settings::ControllerInputProfileStoreErrorKind
                 kind) noexcept {
  using airfix::ios::ControllerInputProfileStorageError;
  using airfix::settings::ControllerInputProfileStoreErrorKind;
  switch (kind) {
  case ControllerInputProfileStoreErrorKind::invalidDirectory:
    return ControllerInputProfileStorageError::storageUnavailable;
  case ControllerInputProfileStoreErrorKind::invalidProfile:
    return ControllerInputProfileStorageError::invalidProfile;
  case ControllerInputProfileStoreErrorKind::persistenceBlocked:
    return ControllerInputProfileStorageError::persistenceBlocked;
  case ControllerInputProfileStoreErrorKind::saveFailed:
    return ControllerInputProfileStorageError::saveFailed;
  case ControllerInputProfileStoreErrorKind::commitUnknown:
    return ControllerInputProfileStorageError::commitUnknown;
  }
  return ControllerInputProfileStorageError::saveFailed;
}

} // namespace

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

- (void)saveProfile:(const airfix::input::ControllerInputProfileRecord &)profile
         completion:(AirfixControllerInputProfileSaveCompletion)completion {
  NSParameterAssert(completion != nil);
  const airfix::input::ControllerInputProfileRecord candidate = profile;
  dispatch_async(airfix::ios::detail::settingsPersistenceQueue(), ^{
    airfix::ios::ControllerInputProfileSaveOutcome outcome;
    const auto directory =
        airfix::ios::detail::preparePrivateSettingsDirectory();
    if (!directory.has_value()) {
      outcome.error =
          airfix::ios::ControllerInputProfileStorageError::storageUnavailable;
    } else {
      try {
        outcome.result =
            airfix::settings::saveControllerInputProfile(*directory, candidate);
        outcome.durable = true;
      } catch (const airfix::settings::ControllerInputProfileStoreError
                   &storeError) {
        outcome.error = storageError(storeError.kind());
        if (storeError.kind() ==
            airfix::settings::ControllerInputProfileStoreErrorKind::
                commitUnknown) {
          try {
            const auto recovered =
                airfix::settings::loadControllerInputProfile(*directory);
            if (!recovered.persistenceBlocked &&
                recovered.source ==
                    airfix::settings::ControllerInputProfileLoadSource::
                        current &&
                recovered.current.status ==
                    airfix::settings::ControllerInputProfileFileStatus::valid &&
                recovered.profile.record() == candidate) {
              outcome.error =
                  airfix::ios::ControllerInputProfileStorageError::none;
              outcome.durable = true;
              outcome.commitUnknownResolved = true;
            }
          } catch (...) {
          }
        }
      } catch (...) {
        outcome.error =
            airfix::ios::ControllerInputProfileStorageError::saveFailed;
      }
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      completion(outcome);
    });
  });
}

@end
