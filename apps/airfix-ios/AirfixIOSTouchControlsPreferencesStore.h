#import <Foundation/Foundation.h>

#ifdef __cplusplus
#include "airfix/input/TouchControlsPreferences.hpp"
#include "airfix/settings/TouchControlsPreferencesStore.hpp"

#include <optional>

namespace airfix::ios {

enum class TouchControlsPreferencesStorageError : std::uint8_t {
  none,
  storageUnavailable,
  invalidPreferences,
  persistenceBlocked,
  saveFailed,
  commitUnknown,
};

struct TouchControlsPreferencesLoadOutcome final {
  std::optional<settings::TouchControlsPreferencesLoadResult> result;
  TouchControlsPreferencesStorageError error{
      TouchControlsPreferencesStorageError::none};
};

struct TouchControlsPreferencesSaveOutcome final {
  std::optional<settings::TouchControlsPreferencesSaveResult> result;
  TouchControlsPreferencesStorageError error{
      TouchControlsPreferencesStorageError::none};
  bool durable{};
  bool commitUnknownResolved{};
};

} // namespace airfix::ios

typedef void (^AirfixTouchControlsPreferencesLoadCompletion)(
    airfix::ios::TouchControlsPreferencesLoadOutcome outcome);
typedef void (^AirfixTouchControlsPreferencesSaveCompletion)(
    airfix::ios::TouchControlsPreferencesSaveOutcome outcome);
#endif

NS_ASSUME_NONNULL_BEGIN

// Serial Objective-C++ adapter for the application-private AFTC store. Every
// callback runs exactly once on main; host paths and document bytes remain
// confined to the private persistence queue.
@interface AirfixIOSTouchControlsPreferencesStore : NSObject

#ifdef __cplusplus
- (void)loadWithCompletion:
    (AirfixTouchControlsPreferencesLoadCompletion)completion;
- (void)
    savePreferences:(const airfix::input::TouchControlsPreferences &)preferences
         completion:(AirfixTouchControlsPreferencesSaveCompletion)completion;
#endif

@end

NS_ASSUME_NONNULL_END
