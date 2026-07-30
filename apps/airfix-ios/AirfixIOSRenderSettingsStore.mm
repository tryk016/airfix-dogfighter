#import "AirfixIOSRenderSettingsStore.h"

#include <cerrno>
#include <filesystem>
#include <optional>
#include <sys/stat.h>

namespace {

[[nodiscard]] std::optional<std::filesystem::path>
fileSystemPath(NSURL* url) noexcept {
    if (url == nil || !url.fileURL) {
        return std::nullopt;
    }
    const char* representation = url.fileSystemRepresentation;
    if (representation == nullptr || representation[0] == '\0') {
        return std::nullopt;
    }
    try {
        const std::filesystem::path path(representation);
        return path.is_absolute()
            ? std::optional<std::filesystem::path>(path)
            : std::nullopt;
    }
    catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool securePrivateDirectory(
    const std::filesystem::path& path) noexcept {
    struct stat information {};
    if (::lstat(path.c_str(), &information) != 0 ||
        !S_ISDIR(information.st_mode) ||
        S_ISLNK(information.st_mode)) {
        return false;
    }
    return ::chmod(path.c_str(), S_IRWXU) == 0;
}

[[nodiscard]] std::optional<std::filesystem::path>
prepareSettingsDirectory() noexcept {
    @autoreleasepool {
        @try {
            NSFileManager* manager = NSFileManager.defaultManager;
            NSError* supportError = nil;
            NSURL* support = [manager
                URLForDirectory:NSApplicationSupportDirectory
                       inDomain:NSUserDomainMask
              appropriateForURL:nil
                         create:YES
                          error:&supportError];
            if (support == nil || supportError != nil) {
                return std::nullopt;
            }

            NSURL* parent = [support
                URLByAppendingPathComponent:@"AirfixDogfighter"
                                isDirectory:YES];
            NSURL* settings = [parent
                URLByAppendingPathComponent:@"settings"
                                isDirectory:YES];
            NSDictionary* attributes = @{
                NSFilePosixPermissions : @(0700),
                NSFileProtectionKey :
                    NSFileProtectionCompleteUntilFirstUserAuthentication,
            };
            NSError* createError = nil;
            if (![manager
                    createDirectoryAtURL:settings
             withIntermediateDirectories:YES
                              attributes:attributes
                                   error:&createError] ||
                createError != nil) {
                return std::nullopt;
            }

            NSError* parentAttributeError = nil;
            NSError* settingsAttributeError = nil;
            if (![manager setAttributes:attributes
                           ofItemAtPath:parent.path
                                  error:&parentAttributeError] ||
                parentAttributeError != nil ||
                ![manager setAttributes:attributes
                           ofItemAtPath:settings.path
                                  error:&settingsAttributeError] ||
                settingsAttributeError != nil) {
                return std::nullopt;
            }

            const auto parentPath = fileSystemPath(parent);
            const auto settingsPath = fileSystemPath(settings);
            if (!parentPath.has_value() ||
                !settingsPath.has_value() ||
                !securePrivateDirectory(*parentPath) ||
                !securePrivateDirectory(*settingsPath)) {
                return std::nullopt;
            }

            // Settings contain no owner assets and are intentionally local to
            // this private reconstruction. Do not copy them into device backup.
            NSError* exclusionError = nil;
            if (![settings setResourceValue:@YES
                                     forKey:NSURLIsExcludedFromBackupKey
                                      error:&exclusionError] ||
                exclusionError != nil) {
                return std::nullopt;
            }
            return settingsPath;
        }
        @catch (NSException* exception) {
            (void)exception;
            return std::nullopt;
        }
    }
}

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

@interface AirfixIOSRenderSettingsStore () {
    dispatch_queue_t _queue;
}
@end

@implementation AirfixIOSRenderSettingsStore

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        _queue = dispatch_queue_create(
            "com.tryk016.airfixdogfighter.render-settings-store",
            DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

- (void)loadWithCompletion:
    (AirfixRenderSettingsLoadCompletion)completion {
    NSParameterAssert(completion != nil);
    dispatch_async(_queue, ^{
        airfix::ios::RenderSettingsLoadOutcome outcome;
        const auto directory = prepareSettingsDirectory();
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
    dispatch_async(_queue, ^{
        airfix::ios::RenderSettingsSaveOutcome outcome;
        const auto directory = prepareSettingsDirectory();
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
