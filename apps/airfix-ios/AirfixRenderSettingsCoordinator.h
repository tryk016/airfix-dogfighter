#import <Foundation/Foundation.h>

#ifdef __cplusplus
#include "airfix/render/RenderPresentationSettings.hpp"
#endif

NS_ASSUME_NONNULL_BEGIN

@class AirfixMetalRenderer;

typedef NS_ENUM(NSInteger, AirfixRenderSettingsApplyResult) {
    AirfixRenderSettingsApplyResultApplied,
    AirfixRenderSettingsApplyResultInvalidCandidate,
    AirfixRenderSettingsApplyResultPersistenceUnavailable,
    AirfixRenderSettingsApplyResultSaveFailed,
    AirfixRenderSettingsApplyResultSuperseded,
};

typedef void (^AirfixRenderSettingsApplyCompletion)(
    AirfixRenderSettingsApplyResult result);

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
@property(nonatomic, readonly) BOOL applying;

#ifdef __cplusplus
// The completion runs exactly once on the main thread. Applied means the
// complete candidate is both durable and published by Metal. Results contain
// no storage path, record contents, or renderer diagnostics.
- (void)requestPersistentSettings:
            (const airfix::render::RenderPresentationSettings&)settings
    completion:(AirfixRenderSettingsApplyCompletion)completion;

- (airfix::render::RenderPresentationSettings)
    persistentSettings;

// Returns the renderer's actual published snapshot, which may differ from the
// durable base while a stale-surface candidate is being prepared again.
- (airfix::render::RenderPresentationSettings)
    activeSettings;
#endif

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
