#import "AirfixIOSTouchControlsPreferencesStore.h"

#include "AirfixIOSSettingsStoreSupport.hpp"

namespace {

[[nodiscard]] airfix::ios::TouchControlsPreferencesStorageError
storageError(const airfix::settings::TouchControlsPreferencesStoreErrorKind
                 kind) noexcept {
  using airfix::ios::TouchControlsPreferencesStorageError;
  using airfix::settings::TouchControlsPreferencesStoreErrorKind;
  switch (kind) {
  case TouchControlsPreferencesStoreErrorKind::invalidDirectory:
    return TouchControlsPreferencesStorageError::storageUnavailable;
  case TouchControlsPreferencesStoreErrorKind::invalidPreferences:
    return TouchControlsPreferencesStorageError::invalidPreferences;
  case TouchControlsPreferencesStoreErrorKind::persistenceBlocked:
    return TouchControlsPreferencesStorageError::persistenceBlocked;
  case TouchControlsPreferencesStoreErrorKind::saveFailed:
    return TouchControlsPreferencesStorageError::saveFailed;
  case TouchControlsPreferencesStoreErrorKind::commitUnknown:
    return TouchControlsPreferencesStorageError::commitUnknown;
  }
  return TouchControlsPreferencesStorageError::saveFailed;
}

} // namespace

@implementation AirfixIOSTouchControlsPreferencesStore

- (void)loadWithCompletion:
    (AirfixTouchControlsPreferencesLoadCompletion)completion {
  NSParameterAssert(completion != nil);
  dispatch_async(airfix::ios::detail::settingsPersistenceQueue(), ^{
    airfix::ios::TouchControlsPreferencesLoadOutcome outcome;
    const auto directory =
        airfix::ios::detail::preparePrivateSettingsDirectory();
    if (!directory.has_value()) {
      outcome.error =
          airfix::ios::TouchControlsPreferencesStorageError::storageUnavailable;
    } else {
      try {
        outcome.result =
            airfix::settings::loadTouchControlsPreferences(*directory);
      } catch (...) {
        outcome.error = airfix::ios::TouchControlsPreferencesStorageError::
            storageUnavailable;
      }
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      completion(outcome);
    });
  });
}

- (void)
    savePreferences:(const airfix::input::TouchControlsPreferences &)preferences
         completion:(AirfixTouchControlsPreferencesSaveCompletion)completion {
  NSParameterAssert(completion != nil);
  const airfix::input::TouchControlsPreferences candidate = preferences;
  dispatch_async(airfix::ios::detail::settingsPersistenceQueue(), ^{
    airfix::ios::TouchControlsPreferencesSaveOutcome outcome;
    const auto directory =
        airfix::ios::detail::preparePrivateSettingsDirectory();
    if (!directory.has_value()) {
      outcome.error =
          airfix::ios::TouchControlsPreferencesStorageError::storageUnavailable;
    } else {
      try {
        outcome.result = airfix::settings::saveTouchControlsPreferences(
            *directory, candidate);
        outcome.durable = true;
      } catch (const airfix::settings::TouchControlsPreferencesStoreError
                   &storeError) {
        outcome.error = storageError(storeError.kind());
        if (storeError.kind() ==
            airfix::settings::TouchControlsPreferencesStoreErrorKind::
                commitUnknown) {
          try {
            const auto recovered =
                airfix::settings::loadTouchControlsPreferences(*directory);
            if (!recovered.persistenceBlocked &&
                recovered.source ==
                    airfix::settings::TouchControlsPreferencesLoadSource::
                        current &&
                recovered.current.status ==
                    airfix::settings::TouchControlsPreferencesFileStatus::
                        valid &&
                recovered.preferences == candidate) {
              outcome.error =
                  airfix::ios::TouchControlsPreferencesStorageError::none;
              outcome.durable = true;
              outcome.commitUnknownResolved = true;
            }
          } catch (...) {
          }
        }
      } catch (...) {
        outcome.error =
            airfix::ios::TouchControlsPreferencesStorageError::saveFailed;
      }
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      completion(outcome);
    });
  });
}

@end
