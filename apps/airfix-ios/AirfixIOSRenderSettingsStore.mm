#import "AirfixIOSRenderSettingsStore.h"

#include "AirfixIOSSettingsStoreSupport.hpp"

namespace {

[[nodiscard]] airfix::ios::RenderSettingsStorageError
storageError(
    const airfix::settings::RenderSettingsStoreErrorKind kind) noexcept {
    using airfix::ios::RenderSettingsStorageError;
    using airfix::settings::RenderSettingsStoreErrorKind;
    switch (kind) {
    case RenderSettingsStoreErrorKind::invalidDirectory:
        return RenderSettingsStorageError::storageUnavailable;
    case RenderSettingsStoreErrorKind::invalidSettings:
        return RenderSettingsStorageError::invalidSettings;
    case RenderSettingsStoreErrorKind::persistenceBlocked:
        return RenderSettingsStorageError::persistenceBlocked;
    case RenderSettingsStoreErrorKind::saveFailed:
        return RenderSettingsStorageError::saveFailed;
    case RenderSettingsStoreErrorKind::commitUnknown:
        return RenderSettingsStorageError::commitUnknown;
    }
    return RenderSettingsStorageError::saveFailed;
}

} // namespace

@implementation AirfixIOSRenderSettingsStore

- (void)loadWithCompletion:
    (AirfixRenderSettingsLoadCompletion)completion {
    NSParameterAssert(completion != nil);
    dispatch_async(airfix::ios::detail::settingsPersistenceQueue(), ^{
        airfix::ios::RenderSettingsLoadOutcome outcome;
        const auto directory =
            airfix::ios::detail::preparePrivateSettingsDirectory();
        if (!directory.has_value()) {
            outcome.error =
                airfix::ios::RenderSettingsStorageError::
                    storageUnavailable;
        }
        else {
            try {
                outcome.result =
                    airfix::settings::loadRenderPresentationSettings(
                        *directory);
            }
            catch (...) {
                outcome.result.persistenceBlocked = true;
                outcome.error =
                    airfix::ios::RenderSettingsStorageError::
                        storageUnavailable;
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(outcome);
        });
    });
}

- (void)saveSettings:
    (const airfix::render::RenderPresentationSettings&)settings
    completion:(AirfixRenderSettingsSaveCompletion)completion {
    NSParameterAssert(completion != nil);
    const airfix::render::RenderPresentationSettings candidate =
        settings;
    dispatch_async(airfix::ios::detail::settingsPersistenceQueue(), ^{
        airfix::ios::RenderSettingsSaveOutcome outcome;
        const auto directory =
            airfix::ios::detail::preparePrivateSettingsDirectory();
        if (!directory.has_value()) {
            outcome.error =
                airfix::ios::RenderSettingsStorageError::
                    storageUnavailable;
        }
        else {
            try {
                outcome.result =
                    airfix::settings::saveRenderPresentationSettings(
                        *directory, candidate);
                outcome.durable = true;
            }
            catch (
                const airfix::settings::RenderSettingsStoreError&
                    storeError) {
                outcome.error = storageError(storeError.kind());
                if (storeError.kind() ==
                    airfix::settings::RenderSettingsStoreErrorKind::
                        commitUnknown) {
                    try {
                        const auto recovered =
                            airfix::settings::
                                loadRenderPresentationSettings(
                                    *directory);
                        if (recovered.source ==
                                airfix::settings::
                                    RenderSettingsLoadSource::current &&
                            recovered.settings == candidate) {
                            outcome.error =
                                airfix::ios::
                                    RenderSettingsStorageError::none;
                            outcome.durable = true;
                            outcome.commitUnknownResolved = true;
                        }
                    }
                    catch (...) {
                    }
                }
            }
            catch (...) {
                outcome.error =
                    airfix::ios::RenderSettingsStorageError::
                        saveFailed;
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(outcome);
        });
    });
}

@end
