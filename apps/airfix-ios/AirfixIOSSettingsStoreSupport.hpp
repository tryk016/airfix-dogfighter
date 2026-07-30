#pragma once

#import <Foundation/Foundation.h>

#include <filesystem>
#include <optional>

namespace airfix::ios::detail {

// Every store that owns files in the shared application-private settings leaf
// must use this queue. The portable stores require one serialized owner for a
// directory; using one native queue also prevents first-use directory creation
// and permission hardening from racing.
[[nodiscard]] dispatch_queue_t settingsPersistenceQueue() noexcept;

// Resolves and hardens:
//   Application Support/AirfixDogfighter/settings
// The returned path is absolute. Linked/wrong-type entries, protection
// failures, and Foundation/POSIX exceptions fail closed without exposing the
// host path.
[[nodiscard]] std::optional<std::filesystem::path>
preparePrivateSettingsDirectory() noexcept;

} // namespace airfix::ios::detail
