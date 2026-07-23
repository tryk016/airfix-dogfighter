#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

// Objective-C ownership envelope for one immutable, fully loaded portable
// world room. Public callers can inspect only bounded metadata; the move-only
// C++ payload is available through the private Objective-C++ bridge.
@interface AirfixWorldRoomSnapshot : NSObject {
@private
    void* _airfixPrivateStorage;
}

@property(nonatomic, copy, readonly) NSString* worldLogicalPath;
@property(nonatomic, readonly) NSUInteger physicalRoom;
@property(nonatomic, readonly) uint64_t requestSerial;
@property(nonatomic, readonly) uint64_t contentGeneration;
@property(nonatomic, readonly) uint64_t packSize;
@property(nonatomic, readonly) NSUInteger textureCount;
@property(nonatomic, readonly) NSUInteger meshCount;
@property(nonatomic, readonly) NSUInteger drawCommandCount;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
