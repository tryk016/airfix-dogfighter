#import <MetalKit/MetalKit.h>

#ifdef __cplusplus
#include "airfix/render/RenderPresentationSettings.hpp"

#include <memory>
#include <optional>

namespace airfix::content {
struct LoadedLegacyAircraftHealthGaugeTextureSet;
struct LoadedLegacyAircraftHudIdentityStatusTextureSet;
struct LoadedLegacyAircraftHudInstrumentTextureSet;
struct LoadedLegacyAircraftHudRollingDigitsTextureSet;
struct LoadedLegacyAircraftHudWeaponPanelTextureSet;
struct LoadedLegacyWeaponCrosshairTextureSet;
struct LoadedMissionWorldRoom;
} // namespace airfix::content
namespace airfix::render {
class LegacyGameplayCameraMissionRuntime;
class PlayerActorPoseRuntime;
} // namespace airfix::render
using AirfixGameplayCameraMissionRuntimeEndpoint =
    std::weak_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>;
using AirfixPlayerActorPoseRuntimeEndpoint =
    std::optional<std::weak_ptr<airfix::render::PlayerActorPoseRuntime>>;
#endif

NS_ASSUME_NONNULL_BEGIN

@class AirfixPreparedMetalRoom;
@class AirfixMetalPresentationRequest;
@class AirfixPreparedMetalPresentation;

// Starts with a public synthetic smoke snapshot and can atomically replace it
// with one complete private room prepared from authenticated AFPACK content.
@interface AirfixMetalRenderer : NSObject <MTKViewDelegate>

@property(nonatomic, readonly) BOOL missionWorldRoomInstalled;

- (nullable instancetype)initWithMetalView:(MTKView *)metalView
                                     error:(NSError *_Nullable *_Nullable)error
    NS_DESIGNATED_INITIALIZER;

#ifdef __cplusplus
// Captures the exact live revision and drawable surface on the main thread.
// The returned immutable request strongly retains every opaque identity used
// by the worker-side preparation.
- (nullable AirfixMetalPresentationRequest *)
    captureRenderPresentationRequest:
        (const airfix::render::RenderPresentationSettings &)candidate
                               error:(NSError *_Nullable *_Nullable)error;

// Builds any replacement scene targets on a serialized worker queue. This
// method rejects the main thread and never reads the live renderer transaction
// or drawable state.
- (nullable AirfixPreparedMetalPresentation *)
    prepareCapturedRenderPresentationRequest:
        (AirfixMetalPresentationRequest *)request
                                       error:
                                           (NSError *_Nullable *_Nullable)error;

// Main-thread final revalidation followed immediately by the no-fail move
// publication. No callback, allocation, or dispatch occurs between those two
// operations. Rejection preserves the active settings and target pair.
- (BOOL)publishPreparedRenderPresentation:
            (AirfixPreparedMetalPresentation *)prepared
                                    error:(NSError *_Nullable *_Nullable)error;

- (airfix::render::RenderPresentationSettings)renderPresentationSettings;

// Builds every Metal resource without changing the renderer's published room.
// This synchronous boundary must run on a serialized preparation queue, never
// the main thread. On failure the old render snapshot and room stay unchanged.
- (nullable AirfixPreparedMetalRoom *)
     prepareLoadedMissionRoom:(airfix::content::LoadedMissionWorldRoom &&)room
             weaponCrosshairs:
                 (airfix::content::LoadedLegacyWeaponCrosshairTextureSet &&)
                     crosshairs
          aircraftHealthGauge:
              (airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet &&)
                  healthGauge
     aircraftHudRollingDigits:
         (airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet &&)
             rollingDigits
       aircraftHudInstruments:
           (airfix::content::LoadedLegacyAircraftHudInstrumentTextureSet &&)
               hudInstruments
      aircraftHudWeaponPanels:
          (airfix::content::LoadedLegacyAircraftHudWeaponPanelTextureSet &&)
              weaponPanels
    aircraftHudIdentityStatus:
        (airfix::content::LoadedLegacyAircraftHudIdentityStatusTextureSet &&)
            identityStatus
                        error:(NSError *_Nullable *_Nullable)error;

// Read-only candidate identity. An empty optional is the authenticated
// no-player path. An engaged weak pointer names only this prepared candidate;
// callers must never retain strong ownership across a main-thread boundary.
- (AirfixPlayerActorPoseRuntimeEndpoint)
    playerActorPoseRuntimeEndpointForPreparedRoom:
        (AirfixPreparedMetalRoom *)preparedRoom;

// Every private mission owns one camera runtime. An expired weak pointer is an
// invalid candidate sentinel. The simulation must not advance this endpoint
// until it can provide the complete recovered AirCraft input contract.
- (AirfixGameplayCameraMissionRuntimeEndpoint)
    gameplayCameraMissionRuntimeEndpointForPreparedRoom:
        (AirfixPreparedMetalRoom *)preparedRoom;
#endif

// Read-only main-thread validation. The coordinator consumes the exact
// ticket/revision only after this succeeds and immediately before commit.
- (BOOL)validatePreparedRoomForCommit:(AirfixPreparedMetalRoom *)preparedRoom
                                error:(NSError *_Nullable *_Nullable)error;

// No-fail, constant-time main-thread commit of a candidate already validated
// by validatePreparedRoomForCommit:. No callback or dispatch may occur between
// ticket consumption and this method.
- (void)commitValidatedPreparedRoom:(AirfixPreparedMetalRoom *)preparedRoom;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
