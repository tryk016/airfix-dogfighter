#import <MetalKit/MetalKit.h>

#ifdef __cplusplus
namespace airfix::content {
struct LoadedMissionWorldRoom;
}
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
// Builds every Metal resource without changing the renderer's published room.
// This synchronous boundary must run on a serialized preparation queue, never
// the main thread. On failure the old render snapshot and room stay unchanged.
- (nullable AirfixPreparedMetalRoom*)prepareLoadedMissionRoom:
    (airfix::content::LoadedMissionWorldRoom&&)room
    error:(NSError* _Nullable* _Nullable)error;
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
