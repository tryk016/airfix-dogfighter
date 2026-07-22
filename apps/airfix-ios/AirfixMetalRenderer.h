#import <MetalKit/MetalKit.h>

NS_ASSUME_NONNULL_BEGIN

// Minimal public-data renderer used to exercise the complete offline Metal
// shader and indexed-draw path before private AFPACK content is available.
@interface AirfixMetalRenderer : NSObject <MTKViewDelegate>

- (nullable instancetype)initWithMetalView:(MTKView*)metalView
                                     error:(NSError* _Nullable* _Nullable)error
    NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
