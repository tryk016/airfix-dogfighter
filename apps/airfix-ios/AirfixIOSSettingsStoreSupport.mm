#include "AirfixIOSSettingsStoreSupport.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace airfix::ios::detail {
namespace {

[[nodiscard]] std::optional<std::filesystem::path>
fileSystemPath(NSURL *const url) {
  if (url == nil || !url.fileURL) {
    return std::nullopt;
  }
  const char *const representation = url.fileSystemRepresentation;
  if (representation == nullptr || representation[0] == '\0') {
    return std::nullopt;
  }
  try {
    const std::filesystem::path path(representation);
    return path.is_absolute() ? std::optional<std::filesystem::path>(path)
                              : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] bool ensurePrivateDirectory(NSFileManager *const manager,
                                          NSURL *const url,
                                          NSDictionary *const attributes) {
  const auto path = fileSystemPath(url);
  if (manager == nil || !path.has_value()) {
    return false;
  }

  struct stat information{};
  if (::lstat(path->c_str(), &information) != 0) {
    if (errno != ENOENT) {
      return false;
    }
    NSError *createError = nil;
    if (![manager createDirectoryAtURL:url
            withIntermediateDirectories:NO
                             attributes:attributes
                                  error:&createError] ||
        createError != nil) {
      return false;
    }
  } else if (!S_ISDIR(information.st_mode) || S_ISLNK(information.st_mode)) {
    return false;
  }

  const int descriptor =
      ::open(path->c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (descriptor < 0) {
    return false;
  }

  bool valid = false;
  struct stat openedInformation{};
  struct stat finalInformation{};
  if (::fstat(descriptor, &openedInformation) == 0 &&
      S_ISDIR(openedInformation.st_mode) &&
      ::fchmod(descriptor, S_IRWXU) == 0) {
    NSError *attributeError = nil;
    NSDictionary *const currentAttributes =
        [manager attributesOfItemAtPath:url.path error:&attributeError];
    id const protection = currentAttributes[NSFileProtectionKey];
    if (attributeError == nil &&
        [protection
            isEqual:NSFileProtectionCompleteUntilFirstUserAuthentication] &&
        ::lstat(path->c_str(), &finalInformation) == 0 &&
        S_ISDIR(finalInformation.st_mode) &&
        !S_ISLNK(finalInformation.st_mode) &&
        finalInformation.st_dev == openedInformation.st_dev &&
        finalInformation.st_ino == openedInformation.st_ino) {
      valid = true;
    }
  }
  const bool closed = ::close(descriptor) == 0;
  return valid && closed;
}

} // namespace

dispatch_queue_t settingsPersistenceQueue() noexcept {
  static dispatch_queue_t queue = dispatch_queue_create(
      "com.tryk016.airfixdogfighter.settings-store", DISPATCH_QUEUE_SERIAL);
  return queue;
}

std::optional<std::filesystem::path>
preparePrivateSettingsDirectory() noexcept {
  @autoreleasepool {
    @try {
      NSFileManager *const manager = NSFileManager.defaultManager;
      NSError *supportError = nil;
      NSURL *const support =
          [manager URLForDirectory:NSApplicationSupportDirectory
                          inDomain:NSUserDomainMask
                 appropriateForURL:nil
                            create:YES
                             error:&supportError];
      if (support == nil || supportError != nil) {
        return std::nullopt;
      }

      NSURL *const parent =
          [support URLByAppendingPathComponent:@"AirfixDogfighter"
                                   isDirectory:YES];
      NSURL *const settings = [parent URLByAppendingPathComponent:@"settings"
                                                      isDirectory:YES];
      NSDictionary *const attributes = @{
        NSFilePosixPermissions : @(0700),
        NSFileProtectionKey :
            NSFileProtectionCompleteUntilFirstUserAuthentication,
      };
      const auto settingsPath = fileSystemPath(settings);
      if (!settingsPath.has_value() ||
          !ensurePrivateDirectory(manager, parent, attributes) ||
          !ensurePrivateDirectory(manager, settings, attributes)) {
        return std::nullopt;
      }

      return settingsPath;
    } @catch (NSException *exception) {
      (void)exception;
      return std::nullopt;
    }
  }
}

} // namespace airfix::ios::detail
