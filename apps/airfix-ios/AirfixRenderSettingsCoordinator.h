#import <Foundation/Foundation.h>

#ifdef __cplusplus
#include "airfix/render/RenderPresentationSettings.hpp"
#endif

NS_ASSUME_NONNULL_BEGIN

@class AirfixMetalRenderer;

// Main-thread owner of the cross-queue render-settings transaction. Exactly
// one candidate may pass prepare/save/publication at a time; later UI requests
// are coalesced to the newest complete persistent base.
@interface AirfixRenderSettingsCoordinator : NSObject

- (instancetype)initWithRenderer:(AirfixMetalRenderer*)renderer
    NS_DESIGNATED_INITIALIZER;

- (void)start;
- (void)notifyPresentationSurfaceAvailable;

@property(nonatomic, readonly) BOOL readyForPresentation;
@property(nonatomic, readonly) BOOL persistenceAvailable;

#ifdef __cplusplus
- (void)requestPersistentSettings:
    (const airfix::render::RenderPresentationSettings&)settings;

- (airfix::render::RenderPresentationSettings)
    persistentSettings;
#endif

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
