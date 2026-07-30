#import <Foundation/Foundation.h>

#ifdef __cplusplus
#include "airfix/input/ControllerInputProfile.hpp"
#include "airfix/settings/ControllerInputProfileStore.hpp"

#include <optional>

namespace airfix::ios {

enum class ControllerInputProfileStorageError : std::uint8_t {
  none,
  storageUnavailable,
};

struct ControllerInputProfileLoadOutcome final {
  std::optional<settings::ControllerInputProfileLoadResult> result;
  ControllerInputProfileStorageError error{
      ControllerInputProfileStorageError::none};
};

} // namespace airfix::ios

typedef void (^AirfixControllerInputProfileLoadCompletion)(
    airfix::ios::ControllerInputProfileLoadOutcome outcome);
#endif

NS_ASSUME_NONNULL_BEGIN

// Serial Objective-C++ adapter for the application-private AFIP store. All
// callbacks run exactly once on main. Foundation paths, document bytes,
// checksums, and store exception strings never cross this boundary.
@interface AirfixIOSControllerInputProfileStore : NSObject

#ifdef __cplusplus
- (void)loadWithCompletion:
    (AirfixControllerInputProfileLoadCompletion)completion;
#endif

@end

NS_ASSUME_NONNULL_END
