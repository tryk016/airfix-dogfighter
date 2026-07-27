#import "AirfixMissionWorldRoomSnapshot+Private.hpp"

#include "airfix/content/MissionWorldRoomPublication.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

namespace {

struct SnapshotStorage final {
    SnapshotStorage(
        airfix::content::WorldRoomPublicationTicket publicationTicket,
        airfix::content::LoadedMissionWorldRoom&& loadedRoom)
        : ticket(std::move(publicationTicket)),
          resultRevision(loadedRoom.revision),
          playerSpawnPose(loadedRoom.playerSpawnPose),
          room(std::move(loadedRoom)) {}

    airfix::content::WorldRoomPublicationTicket ticket;
    airfix::content::ContentRevision resultRevision;
    airfix::simulation::PlayerSpawnPose playerSpawnPose;
    std::optional<airfix::content::LoadedMissionWorldRoom> room;
};

dispatch_queue_t snapshotTeardownQueue() {
    static dispatch_queue_t queue = dispatch_queue_create(
        "com.tryk016.airfixdogfighter.mission-room-snapshot-teardown",
        DISPATCH_QUEUE_SERIAL);
    return queue;
}

} // namespace

@interface AirfixMissionWorldRoomSnapshot ()

@property(nonatomic, readwrite) uint64_t requestSerial;
@property(nonatomic, readwrite) uint64_t contentGeneration;
@property(nonatomic, readwrite) uint64_t packSize;
@property(nonatomic, readwrite) NSUInteger textureCount;
@property(nonatomic, readwrite) NSUInteger meshCount;
@property(nonatomic, readwrite) NSUInteger drawCommandCount;

- (instancetype)initWithRequestSerial:(uint64_t)requestSerial
                      loadedRoomStore:(void*)loadedRoomStore;
- (void*)airfix_privateStorage;

@end

@implementation AirfixMissionWorldRoomSnapshot

- (instancetype)initWithRequestSerial:(uint64_t)requestSerial
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

AirfixMissionWorldRoomSnapshot* makeMissionWorldRoomSnapshot(
    content::WorldRoomPublicationTicket ticket,
    content::LoadedMissionWorldRoom&& room) {
    if (content::validateMissionWorldRoomPublication(
            room, ticket.expectedRevision).has_value()) {
        throw std::invalid_argument(
            "mission room snapshot failed its publication contract");
    }
    // Initialize the long-lived teardown queue on the content worker so the
    // first stale release never has to create it from a main-thread dealloc.
    (void)snapshotTeardownQueue();
    const auto requestSerial = ticket.serial;
    auto* const storage =
        new SnapshotStorage(std::move(ticket), std::move(room));
    AirfixMissionWorldRoomSnapshot* const snapshot =
        [[AirfixMissionWorldRoomSnapshot alloc]
            initWithRequestSerial:requestSerial
                 loadedRoomStore:storage];
    if (snapshot == nil) {
        throw std::runtime_error(
            "mission room snapshot could not be initialized");
    }
    return snapshot;
}

content::LoadedMissionWorldRoom takeLoadedMissionWorldRoom(
    AirfixMissionWorldRoomSnapshot* const snapshot) {
    if (snapshot == nil) {
        throw std::invalid_argument("mission room snapshot is null");
    }
    auto* const storage =
        static_cast<SnapshotStorage*>([snapshot airfix_privateStorage]);
    if (storage == nullptr || !storage->room.has_value()) {
        throw std::logic_error(
            "mission room snapshot payload was already consumed");
    }

    auto room = std::move(*storage->room);
    storage->room.reset();
    return room;
}

content::WorldRoomPublicationTicket missionWorldRoomPublicationTicket(
    AirfixMissionWorldRoomSnapshot* const snapshot) {
    if (snapshot == nil) {
        throw std::invalid_argument("mission room snapshot is null");
    }
    auto* const storage =
        static_cast<SnapshotStorage*>([snapshot airfix_privateStorage]);
    if (storage == nullptr) {
        throw std::logic_error(
            "mission room snapshot storage is unavailable");
    }
    return storage->ticket;
}

content::ContentRevision missionWorldRoomResultRevision(
    AirfixMissionWorldRoomSnapshot* const snapshot) {
    if (snapshot == nil) {
        throw std::invalid_argument("mission room snapshot is null");
    }
    auto* const storage =
        static_cast<SnapshotStorage*>([snapshot airfix_privateStorage]);
    if (storage == nullptr) {
        throw std::logic_error(
            "mission room snapshot storage is unavailable");
    }
    return storage->resultRevision;
}

std::optional<simulation::PlayerSpawnPose>
missionWorldRoomPlayerSpawnPose(
    AirfixMissionWorldRoomSnapshot* const snapshot) noexcept {
    if (snapshot == nil) {
        return std::nullopt;
    }
    auto* const storage =
        static_cast<SnapshotStorage*>([snapshot airfix_privateStorage]);
    if (storage == nullptr) {
        return std::nullopt;
    }
    return storage->playerSpawnPose;
}

} // namespace airfix::ios
