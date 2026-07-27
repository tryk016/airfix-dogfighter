#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

@class AirfixGameControllerAdapter;

// Immutable, presentation-safe information about the single controller
// assigned to player one. Runtime device identifiers are deliberately not
// exposed or persisted.
@interface AirfixGameControllerState : NSObject

@property(nonatomic, readonly, getter=isConnected) BOOL connected;
@property(nonatomic, readonly) uint64_t generation;
@property(nonatomic, copy, readonly) NSString* vendorName;
@property(nonatomic, copy, readonly) NSString* productCategory;

- (instancetype)init NS_UNAVAILABLE;

@end

typedef struct AirfixGameControllerSample {
    int16_t bank;
    int16_t pitch;
    int16_t lookX;
    int16_t lookY;
    int16_t primaryTrigger;
    int16_t secondaryTrigger;
    BOOL dpadUpPressed;
    BOOL dpadDownPressed;
    BOOL rightShoulderPressed;
    BOOL leftShoulderPressed;
    BOOL faceLeftPressed;
    BOOL faceTopPressed;
    BOOL facePrimaryPressed;
    BOOL faceSecondaryPressed;
    BOOL rightStickClickPressed;
    BOOL menuPressed;
    BOOL optionsPressed;
} AirfixGameControllerSample;

typedef NS_ENUM(uint8_t, AirfixGameControllerDigitalControl) {
    AirfixGameControllerDigitalControlPrimaryTrigger = 0,
    AirfixGameControllerDigitalControlPause = 1,
    AirfixGameControllerDigitalControlSecondaryTrigger = 2,
    AirfixGameControllerDigitalControlDpadUp = 3,
    AirfixGameControllerDigitalControlDpadDown = 4,
    AirfixGameControllerDigitalControlRightShoulder = 5,
    AirfixGameControllerDigitalControlLeftShoulder = 6,
    AirfixGameControllerDigitalControlFaceLeft = 7,
    AirfixGameControllerDigitalControlFaceTop = 8,
    AirfixGameControllerDigitalControlFacePrimary = 9,
    AirfixGameControllerDigitalControlFaceSecondary = 10,
    AirfixGameControllerDigitalControlRightStickClick = 11,
};

enum {
    AirfixGameControllerDigitalEdgeCapacity = 64,
};

typedef struct AirfixGameControllerDigitalEdge {
    uint64_t generation;
    uint64_t order;
    AirfixGameControllerDigitalControl control;
    BOOL pressed;
} AirfixGameControllerDigitalEdge;

// Fixed-capacity, allocation-free handoff from the Game Controller callback
// thread to the main input tick. Edges transition from startingState in array
// order and finish at finalState. overflowed invalidates the whole batch.
typedef struct AirfixGameControllerInputBatch {
    uint64_t generation;
    AirfixGameControllerSample startingState;
    AirfixGameControllerSample finalState;
    AirfixGameControllerDigitalEdge
        edges[AirfixGameControllerDigitalEdgeCapacity];
    NSUInteger edgeCount;
    BOOL overflowed;
} AirfixGameControllerInputBatch;

@protocol AirfixGameControllerAdapterDelegate <NSObject>
@optional

// Delivered on the main thread. State objects remain valid after a later
// connection change.
- (void)gameControllerAdapter:(AirfixGameControllerAdapter*)adapter
               didChangeState:(AirfixGameControllerState*)state;

// Generation exhaustion is terminal for this adapter instance. Ordinary FIFO
// overflow is reported in the next drained input batch.
- (void)gameControllerAdapter:(AirfixGameControllerAdapter*)adapter
        didFailInputForGeneration:(uint64_t)generation;

@end

// Narrow Apple Game Controller bridge. It owns no pairing UI or Bluetooth
// behavior; discovery and pairing remain system-managed. All public methods
// are main-thread-affine.
@interface AirfixGameControllerAdapter : NSObject

@property(nonatomic, weak, nullable)
    id<AirfixGameControllerAdapterDelegate> delegate;
@property(nonatomic, strong, readonly) AirfixGameControllerState* state;

- (instancetype)initWithDelegate:
    (nullable id<AirfixGameControllerAdapterDelegate>)delegate
    NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

- (void)start;
- (void)stop;

// Main-thread drain. Digital transitions remain ordered even when a complete
// press/release occurs between fixed ticks; continuous axes use finalState.
- (AirfixGameControllerInputBatch)drainInputBatch;

// Discards queued pre-boundary edges and makes the current physical state the
// new baseline. Main-thread only.
- (void)resetInputBridge;

@end

NS_ASSUME_NONNULL_END
