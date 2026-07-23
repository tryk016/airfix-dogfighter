#import "AirfixWorldRoomSnapshot+Private.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

namespace {

struct SnapshotStorage final {
    SnapshotStorage(
        airfix::content::WorldRoomPublicationTicket publicationTicket,
        airfix::content::LoadedWorldRoom&& loadedRoom)
        : ticket(std::move(publicationTicket)),
          resultRevision(loadedRoom.revision),
          room(std::move(loadedRoom)) {}

    airfix::content::WorldRoomPublicationTicket ticket;
    airfix::content::ContentRevision resultRevision;
    std::optional<airfix::content::LoadedWorldRoom> room;
};

dispatch_queue_t snapshotTeardownQueue() {
    static dispatch_queue_t queue = dispatch_queue_create(
        "com.tryk016.airfixdogfighter.world-room-snapshot-teardown",
        DISPATCH_QUEUE_SERIAL);
    return queue;
}

} // namespace

@interface AirfixWorldRoomSnapshot ()

@property(nonatomic, copy, readwrite) NSString* worldLogicalPath;
@property(nonatomic, readwrite) NSUInteger physicalRoom;
@property(nonatomic, readwrite) uint64_t requestSerial;
@property(nonatomic, readwrite) uint64_t contentGeneration;
@property(nonatomic, readwrite) uint64_t packSize;
@property(nonatomic, readwrite) NSUInteger textureCount;
@property(nonatomic, readwrite) NSUInteger meshCount;
@property(nonatomic, readwrite) NSUInteger drawCommandCount;

- (instancetype)initWithWorldLogicalPath:(NSString*)worldLogicalPath
                             physicalRoom:(NSUInteger)physicalRoom
                            requestSerial:(uint64_t)requestSerial
                          loadedRoomStore:(void*)loadedRoomStore;
- (void*)airfix_privateStorage;

@end

@implementation AirfixWorldRoomSnapshot

- (instancetype)initWithWorldLogicalPath:(NSString*)worldLogicalPath
                             physicalRoom:(NSUInteger)physicalRoom
                            requestSerial:(uint64_t)requestSerial
                          loadedRoomStore:(void*)loadedRoomStore {
    self = [super init];
    if (self == nil) {
        delete static_cast<SnapshotStorage*>(loadedRoomStore);
        return nil;
    }

    auto* const storage = static_cast<SnapshotStorage*>(loadedRoomStore);
    if (storage == nullptr || !storage->room.has_value()) {
        delete storage;
        return nil;
    }

    _airfixPrivateStorage = storage;
    _worldLogicalPath = [worldLogicalPath copy];
    _physicalRoom = physicalRoom;
    _requestSerial = requestSerial;
    _contentGeneration = storage->room->revision.generation;
    _packSize = storage->room->revision.pack.size;
    _textureCount = storage->room->textures.size();
    _meshCount = storage->room->submission.meshUploads.size();
    _drawCommandCount = storage->room->submission.commands.size();
    return self;
}

- (void)dealloc {
    // A stale publication can lose its last Objective-C reference on the main
    // queue before its move-only room is consumed. Detach in constant time and
    // destroy the potentially large CPU payload on a dedicated serial worker.
    auto* const storage =
        static_cast<SnapshotStorage*>(_airfixPrivateStorage);
    _airfixPrivateStorage = nullptr;
    if (storage != nullptr) {
        dispatch_async(snapshotTeardownQueue(), ^{
            delete storage;
        });
    }
}

- (void*)airfix_privateStorage {
    return _airfixPrivateStorage;
}

@end

namespace airfix::ios {

AirfixWorldRoomSnapshot* makeWorldRoomSnapshot(
    std::string worldLogicalPath,
    const std::size_t physicalRoom,
    content::WorldRoomPublicationTicket ticket,
    content::LoadedWorldRoom&& room) {
    if (room.revision != ticket.expectedRevision) {
        throw std::invalid_argument(
            "world room snapshot revision does not match its publication ticket");
    }
    // Initialize the long-lived teardown queue on the content worker so the
    // first stale release never has to create it from a main-thread dealloc.
    (void)snapshotTeardownQueue();
    NSString* const logicalPath = [[NSString alloc]
        initWithBytes:worldLogicalPath.data()
               length:worldLogicalPath.size()
             encoding:NSUTF8StringEncoding];
    if (logicalPath == nil) {
        throw std::invalid_argument(
            "world logical path is not valid UTF-8 at snapshot publication");
    }

    const auto requestSerial = ticket.serial;
    auto* const storage =
        new SnapshotStorage(std::move(ticket), std::move(room));
    AirfixWorldRoomSnapshot* const snapshot = [[AirfixWorldRoomSnapshot alloc]
        initWithWorldLogicalPath:logicalPath
                   physicalRoom:static_cast<NSUInteger>(physicalRoom)
                  requestSerial:requestSerial
                loadedRoomStore:storage];
    if (snapshot == nil) {
        throw std::runtime_error("world room snapshot could not be initialized");
    }
    return snapshot;
}

content::LoadedWorldRoom takeLoadedWorldRoom(
    AirfixWorldRoomSnapshot* const snapshot) {
    if (snapshot == nil) {
        throw std::invalid_argument("world room snapshot is null");
    }
    auto* const storage =
        static_cast<SnapshotStorage*>([snapshot airfix_privateStorage]);
    if (storage == nullptr || !storage->room.has_value()) {
        throw std::logic_error("world room snapshot payload was already consumed");
    }

    auto room = std::move(*storage->room);
    storage->room.reset();
    return room;
}

content::WorldRoomPublicationTicket worldRoomPublicationTicket(
    AirfixWorldRoomSnapshot* const snapshot) {
    if (snapshot == nil) {
        throw std::invalid_argument("world room snapshot is null");
    }
    auto* const storage =
        static_cast<SnapshotStorage*>([snapshot airfix_privateStorage]);
    if (storage == nullptr) {
        throw std::logic_error("world room snapshot storage is unavailable");
    }
    return storage->ticket;
}

content::ContentRevision worldRoomResultRevision(
    AirfixWorldRoomSnapshot* const snapshot) {
    if (snapshot == nil) {
        throw std::invalid_argument("world room snapshot is null");
    }
    auto* const storage =
        static_cast<SnapshotStorage*>([snapshot airfix_privateStorage]);
    if (storage == nullptr) {
        throw std::logic_error("world room snapshot storage is unavailable");
    }
    return storage->resultRevision;
}

} // namespace airfix::ios
