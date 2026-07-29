#import <MetalKit/MetalKit.h>

#ifdef __cplusplus
#include "airfix/render/RenderPresentationSettings.hpp"

#include <memory>
#include <optional>

namespace airfix::content {
struct LoadedMissionWorldRoom;
}
namespace airfix::render {
class LegacyGameplayCameraMissionRuntime;
class PlayerActorPoseRuntime;
}
using AirfixGameplayCameraMissionRuntimeEndpoint =
    std::weak_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>;
using AirfixPlayerActorPoseRuntimeEndpoint =
    std::optional<
        std::weak_ptr<airfix::render::PlayerActorPoseRuntime>>;
#endif

NS_ASSUME_NONNULL_BEGIN

@class AirfixPreparedMetalRoom;

// Starts with a public synthetic smoke snapshot and can atomically replace it
// with one complete private room prepared from authenticated AFPACK content.
@interface AirfixMetalRenderer : NSObject <MTKViewDelegate>

@property(nonatomic, readonly) BOOL missionWorldRoomInstalled;

- (nullable instancetype)initWithMetalView:(MTKView*)metalView
                                     error:(NSError* _Nullable* _Nullable)error
    NS_DESIGNATED_INITIALIZER;

#ifdef __cplusplus
// Main-thread renderer transaction. The complete settings snapshot and any
// replacement scene targets are prepared before one no-fail publication.
// Rejection preserves both the active settings and the last good target pair.
- (BOOL)applyRenderPresentationSettings:
    (const airfix::render::RenderPresentationSettings&)candidate
    error:(NSError* _Nullable* _Nullable)error;

- (airfix::render::RenderPresentationSettings)
    renderPresentationSettings;

// Builds every Metal resource without changing the renderer's published room.
// This synchronous boundary must run on a serialized preparation queue, never
// the main thread. On failure the old render snapshot and room stay unchanged.
- (nullable AirfixPreparedMetalRoom*)prepareLoadedMissionRoom:
    (airfix::content::LoadedMissionWorldRoom&&)room
    error:(NSError* _Nullable* _Nullable)error;

// Read-only candidate identity. An empty optional is the authenticated
// no-player path. An engaged weak pointer names only this prepared candidate;
// callers must never retain strong ownership across a main-thread boundary.
- (AirfixPlayerActorPoseRuntimeEndpoint)
    playerActorPoseRuntimeEndpointForPreparedRoom:
        (AirfixPreparedMetalRoom*)preparedRoom;

// Every private mission owns one camera runtime. An expired weak pointer is an
// invalid candidate sentinel. The simulation must not advance this endpoint
// until it can provide the complete recovered AirCraft input contract.
- (AirfixGameplayCameraMissionRuntimeEndpoint)
    gameplayCameraMissionRuntimeEndpointForPreparedRoom:
        (AirfixPreparedMetalRoom*)preparedRoom;
#endif

// Read-only main-thread validation. The coordinator consumes the exact
// ticket/revision only after this succeeds and immediately before commit.
- (BOOL)validatePreparedRoomForCommit:(AirfixPreparedMetalRoom*)preparedRoom
                                error:(NSError* _Nullable* _Nullable)error;

// No-fail, constant-time main-thread commit of a candidate already validated
// by validatePreparedRoomForCommit:. No callback or dispatch may occur between
// ticket consumption and this method.
- (void)commitValidatedPreparedRoom:(AirfixPreparedMetalRoom*)preparedRoom;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
