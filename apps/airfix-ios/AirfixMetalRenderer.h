#import <MetalKit/MetalKit.h>

#ifdef __cplusplus
namespace airfix::content {
struct LoadedWorldRoom;
}
#endif

NS_ASSUME_NONNULL_BEGIN

@class AirfixPreparedMetalRoom;

// Starts with a public synthetic smoke snapshot and can atomically replace it
// with one complete private room prepared from authenticated AFPACK content.
@interface AirfixMetalRenderer : NSObject <MTKViewDelegate>

@property(nonatomic, readonly) BOOL worldRoomInstalled;

- (nullable instancetype)initWithMetalView:(MTKView*)metalView
                                     error:(NSError* _Nullable* _Nullable)error
    NS_DESIGNATED_INITIALIZER;

#ifdef __cplusplus
// Builds every Metal resource without changing the renderer's published room.
// This synchronous boundary must run on a serialized preparation queue, never
// the main thread. On failure the old render snapshot and room stay unchanged.
- (nullable AirfixPreparedMetalRoom*)prepareLoadedRoom:
    (airfix::content::LoadedWorldRoom&&)room
    error:(NSError* _Nullable* _Nullable)error;
#endif

// Performs only owner/device/one-shot/transition validation and one atomic
// snapshot assignment. It must run on the main thread after the coordinator
// revalidates the request serial and content revision.
- (BOOL)publishPreparedRoom:(AirfixPreparedMetalRoom*)preparedRoom
                      error:(NSError* _Nullable* _Nullable)error;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
