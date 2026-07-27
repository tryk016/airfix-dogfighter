#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

// Objective-C ownership envelope for one immutable, authenticated aggregate
// mission room. Public callers can inspect only bounded, non-sensitive
// metadata; the move-only C++ payload and source provenance are available
// solely through the private Objective-C++ bridge.
@interface AirfixMissionWorldRoomSnapshot : NSObject {
@private
    void* _airfixPrivateStorage;
}

@property(nonatomic, readonly) uint64_t requestSerial;
@property(nonatomic, readonly) uint64_t contentGeneration;
@property(nonatomic, readonly) uint64_t packSize;
@property(nonatomic, readonly) NSUInteger textureCount;
@property(nonatomic, readonly) NSUInteger meshCount;
@property(nonatomic, readonly) NSUInteger drawCommandCount;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
