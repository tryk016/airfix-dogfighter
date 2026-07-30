#import <Foundation/Foundation.h>

#ifdef __cplusplus
#include "airfix/render/RenderPresentationSettings.hpp"
#include "airfix/settings/RenderPresentationSettingsStore.hpp"

#include <optional>

namespace airfix::ios {

enum class RenderSettingsStorageError : std::uint8_t {
    none,
    storageUnavailable,
    invalidSettings,
    persistenceBlocked,
    saveFailed,
    commitUnknown,
};

struct RenderSettingsLoadOutcome final {
    airfix::settings::RenderSettingsLoadResult result;
    RenderSettingsStorageError error{RenderSettingsStorageError::none};
};

struct RenderSettingsSaveOutcome final {
    std::optional<airfix::settings::RenderSettingsSaveResult> result;
    RenderSettingsStorageError error{RenderSettingsStorageError::none};
    bool durable{};
    bool commitUnknownResolved{};
};

} // namespace airfix::ios

typedef void (^AirfixRenderSettingsLoadCompletion)(
    airfix::ios::RenderSettingsLoadOutcome outcome);
typedef void (^AirfixRenderSettingsSaveCompletion)(
    airfix::ios::RenderSettingsSaveOutcome outcome);
#endif

NS_ASSUME_NONNULL_BEGIN

// Serial Objective-C++ adapter for the application-private settings leaf.
// Foundation URL/path details and store exceptions never cross this boundary.
@interface AirfixIOSRenderSettingsStore : NSObject

#ifdef __cplusplus
- (void)loadWithCompletion:
    (AirfixRenderSettingsLoadCompletion)completion;

- (void)saveSettings:
    (const airfix::render::RenderPresentationSettings&)settings
    completion:(AirfixRenderSettingsSaveCompletion)completion;
#endif

@end

NS_ASSUME_NONNULL_END
