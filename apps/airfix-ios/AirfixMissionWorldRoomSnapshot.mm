#import "AirfixMissionWorldRoomSnapshot+Private.hpp"

#include "airfix/content/MissionWorldRoomPublication.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

namespace {

struct SnapshotStorage final {
  SnapshotStorage(
      airfix::content::WorldRoomPublicationTicket publicationTicket,
      airfix::content::LoadedMissionWorldRoom &&loadedRoom,
      airfix::content::LoadedLegacyAircraftAudioClips &&loadedAudioClips,
      airfix::content::LoadedLegacyWeaponCrosshairTextureSet &&loadedCrosshairs,
      airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet
          &&loadedHealthGauge,
      airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet
          &&loadedRollingDigits,
      airfix::content::LoadedLegacyAircraftHudInstrumentTextureSet
          &&loadedHudInstruments,
      airfix::content::LoadedLegacyAircraftHudWeaponPanelTextureSet
          &&loadedWeaponPanels,
      airfix::content::LoadedLegacyAircraftHudIdentityStatusTextureSet
          &&loadedIdentityStatus)
      : ticket(std::move(publicationTicket)),
        resultRevision(loadedRoom.revision),
        playerSpawnPose(loadedRoom.playerSpawnPose),
        room(std::move(loadedRoom)), audioClips(std::move(loadedAudioClips)),
        crosshairs(std::move(loadedCrosshairs)),
        healthGauge(std::move(loadedHealthGauge)),
        rollingDigits(std::move(loadedRollingDigits)),
        hudInstruments(std::move(loadedHudInstruments)),
        weaponPanels(std::move(loadedWeaponPanels)),
        identityStatus(std::move(loadedIdentityStatus)) {}

  airfix::content::WorldRoomPublicationTicket ticket;
  airfix::content::ContentRevision resultRevision;
  airfix::simulation::PlayerSpawnPose playerSpawnPose;
  std::optional<airfix::content::LoadedMissionWorldRoom> room;
  std::optional<airfix::content::LoadedLegacyAircraftAudioClips> audioClips;
  std::optional<airfix::content::LoadedLegacyWeaponCrosshairTextureSet>
      crosshairs;
  std::optional<airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet>
      healthGauge;
  std::optional<airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet>
      rollingDigits;
  std::optional<airfix::content::LoadedLegacyAircraftHudInstrumentTextureSet>
      hudInstruments;
  std::optional<airfix::content::LoadedLegacyAircraftHudWeaponPanelTextureSet>
      weaponPanels;
  std::optional<
      airfix::content::LoadedLegacyAircraftHudIdentityStatusTextureSet>
      identityStatus;
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
@property(nonatomic, readwrite) NSUInteger audioClipCount;

- (instancetype)initWithRequestSerial:(uint64_t)requestSerial
                      loadedRoomStore:(void *)loadedRoomStore;
- (void *)airfix_privateStorage;

@end

@implementation AirfixMissionWorldRoomSnapshot

- (instancetype)initWithRequestSerial:(uint64_t)requestSerial
                      loadedRoomStore:(void *)loadedRoomStore {
  self = [super init];
  if (self == nil) {
    delete static_cast<SnapshotStorage *>(loadedRoomStore);
    return nil;
  }

  auto *const storage = static_cast<SnapshotStorage *>(loadedRoomStore);
  if (storage == nullptr || !storage->room.has_value() ||
      !storage->audioClips.has_value() || !storage->crosshairs.has_value() ||
      !storage->healthGauge.has_value() ||
      !storage->rollingDigits.has_value() ||
      !storage->hudInstruments.has_value() ||
      !storage->weaponPanels.has_value() ||
      !storage->identityStatus.has_value()) {
    delete storage;
    return nil;
  }

  _airfixPrivateStorage = storage;
  _requestSerial = requestSerial;
  _contentGeneration = storage->room->revision.generation;
  _packSize = storage->room->revision.pack.size;
  _textureCount = storage->room->textures.size() +
                  storage->crosshairs->textures.size() +
                  storage->healthGauge->textures.size() +
                  storage->rollingDigits->textures.size() +
                  storage->hudInstruments->textures.size() +
                  storage->weaponPanels->textures.size() +
                  storage->identityStatus->textures.size();
  _meshCount = storage->room->submission.meshUploads.size();
  _drawCommandCount = storage->room->submission.commands.size();
  _audioClipCount = storage->audioClips->clips.size();
  return self;
}

- (void)dealloc {
  // A stale publication can lose its last Objective-C reference on the main
  // queue before its move-only room is consumed. Detach in constant time and
  // destroy the potentially large CPU payload on a dedicated serial worker.
  auto *const storage = static_cast<SnapshotStorage *>(_airfixPrivateStorage);
  _airfixPrivateStorage = nullptr;
  if (storage != nullptr) {
    dispatch_async(snapshotTeardownQueue(), ^{
      delete storage;
    });
  }
}

- (void *)airfix_privateStorage {
  return _airfixPrivateStorage;
}

@end

namespace airfix::ios {

AirfixMissionWorldRoomSnapshot *makeMissionWorldRoomSnapshot(
    content::WorldRoomPublicationTicket ticket,
    content::LoadedMissionWorldRoom &&room,
    content::LoadedLegacyAircraftAudioClips &&audioClips,
    content::LoadedLegacyWeaponCrosshairTextureSet &&crosshairs,
    content::LoadedLegacyAircraftHealthGaugeTextureSet &&healthGauge,
    content::LoadedLegacyAircraftHudRollingDigitsTextureSet &&rollingDigits,
    content::LoadedLegacyAircraftHudInstrumentTextureSet &&hudInstruments,
    content::LoadedLegacyAircraftHudWeaponPanelTextureSet &&weaponPanels,
    content::LoadedLegacyAircraftHudIdentityStatusTextureSet &&identityStatus) {
  if (content::validateMissionWorldRoomPublication(room,
                                                   ticket.expectedRevision)
          .has_value()) {
    throw std::invalid_argument(
        "mission room snapshot failed its publication contract");
  }
  if (!audioClips.valid() || audioClips.revision != ticket.expectedRevision ||
      audioClips.revision != room.revision) {
    throw std::invalid_argument(
        "mission audio snapshot failed its publication contract");
  }
  if (!crosshairs.valid() || crosshairs.revision != ticket.expectedRevision ||
      crosshairs.revision != room.revision) {
    throw std::invalid_argument(
        "mission crosshair snapshot failed its publication contract");
  }
  if (!healthGauge.valid() || healthGauge.revision != ticket.expectedRevision ||
      healthGauge.revision != room.revision) {
    throw std::invalid_argument(
        "mission health gauge snapshot failed its publication contract");
  }
  if (!rollingDigits.valid() ||
      rollingDigits.revision != ticket.expectedRevision ||
      rollingDigits.revision != room.revision) {
    throw std::invalid_argument(
        "mission rolling digit snapshot failed its publication contract");
  }
  if (!hudInstruments.valid() ||
      hudInstruments.revision != ticket.expectedRevision ||
      hudInstruments.revision != room.revision) {
    throw std::invalid_argument(
        "mission HUD instrument snapshot failed its publication contract");
  }
  if (!weaponPanels.valid() ||
      weaponPanels.revision != ticket.expectedRevision ||
      weaponPanels.revision != room.revision) {
    throw std::invalid_argument(
        "mission HUD weapon panel snapshot failed its publication contract");
  }
  if (!identityStatus.valid() ||
      identityStatus.revision != ticket.expectedRevision ||
      identityStatus.revision != room.revision) {
    throw std::invalid_argument(
        "mission HUD identity/status snapshot failed its publication contract");
  }
  // Initialize the long-lived teardown queue on the content worker so the
  // first stale release never has to create it from a main-thread dealloc.
  (void)snapshotTeardownQueue();
  const auto requestSerial = ticket.serial;
  auto *const storage = new SnapshotStorage(
      std::move(ticket), std::move(room), std::move(audioClips),
      std::move(crosshairs), std::move(healthGauge), std::move(rollingDigits),
      std::move(hudInstruments), std::move(weaponPanels),
      std::move(identityStatus));
  AirfixMissionWorldRoomSnapshot *const snapshot =
      [[AirfixMissionWorldRoomSnapshot alloc]
          initWithRequestSerial:requestSerial
                loadedRoomStore:storage];
  if (snapshot == nil) {
    throw std::runtime_error("mission room snapshot could not be initialized");
  }
  return snapshot;
}

content::LoadedMissionWorldRoom
takeLoadedMissionWorldRoom(AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr || !storage->room.has_value()) {
    throw std::logic_error(
        "mission room snapshot payload was already consumed");
  }

  auto room = std::move(*storage->room);
  storage->room.reset();
  return room;
}

content::LoadedLegacyAircraftAudioClips takeLoadedLegacyAircraftAudioClips(
    AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr || !storage->audioClips.has_value()) {
    throw std::logic_error(
        "mission audio snapshot payload was already consumed");
  }

  auto audioClips = std::move(*storage->audioClips);
  storage->audioClips.reset();
  return audioClips;
}

content::LoadedLegacyWeaponCrosshairTextureSet
takeLoadedLegacyWeaponCrosshairTextures(
    AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr || !storage->crosshairs.has_value()) {
    throw std::logic_error(
        "mission crosshair snapshot payload was already consumed");
  }

  auto crosshairs = std::move(*storage->crosshairs);
  storage->crosshairs.reset();
  return crosshairs;
}

content::LoadedLegacyAircraftHealthGaugeTextureSet
takeLoadedLegacyAircraftHealthGaugeTextures(
    AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr || !storage->healthGauge.has_value()) {
    throw std::logic_error(
        "mission health gauge snapshot payload was already consumed");
  }

  auto healthGauge = std::move(*storage->healthGauge);
  storage->healthGauge.reset();
  return healthGauge;
}

content::LoadedLegacyAircraftHudRollingDigitsTextureSet
takeLoadedLegacyAircraftHudRollingDigitTextures(
    AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr || !storage->rollingDigits.has_value()) {
    throw std::logic_error(
        "mission rolling digit snapshot payload was already consumed");
  }

  auto rollingDigits = std::move(*storage->rollingDigits);
  storage->rollingDigits.reset();
  return rollingDigits;
}

content::LoadedLegacyAircraftHudInstrumentTextureSet
takeLoadedLegacyAircraftHudInstrumentTextures(
    AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr || !storage->hudInstruments.has_value()) {
    throw std::logic_error(
        "mission HUD instrument snapshot payload was already consumed");
  }

  auto hudInstruments = std::move(*storage->hudInstruments);
  storage->hudInstruments.reset();
  return hudInstruments;
}

content::LoadedLegacyAircraftHudWeaponPanelTextureSet
takeLoadedLegacyAircraftHudWeaponPanelTextures(
    AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr || !storage->weaponPanels.has_value()) {
    throw std::logic_error(
        "mission HUD weapon panel snapshot payload was already consumed");
  }

  auto weaponPanels = std::move(*storage->weaponPanels);
  storage->weaponPanels.reset();
  return weaponPanels;
}

content::LoadedLegacyAircraftHudIdentityStatusTextureSet
takeLoadedLegacyAircraftHudIdentityStatusTextures(
    AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr || !storage->identityStatus.has_value()) {
    throw std::logic_error(
        "mission HUD identity/status snapshot payload was already consumed");
  }

  auto identityStatus = std::move(*storage->identityStatus);
  storage->identityStatus.reset();
  return identityStatus;
}

content::WorldRoomPublicationTicket missionWorldRoomPublicationTicket(
    AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr) {
    throw std::logic_error("mission room snapshot storage is unavailable");
  }
  return storage->ticket;
}

content::ContentRevision
missionWorldRoomResultRevision(AirfixMissionWorldRoomSnapshot *const snapshot) {
  if (snapshot == nil) {
    throw std::invalid_argument("mission room snapshot is null");
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr) {
    throw std::logic_error("mission room snapshot storage is unavailable");
  }
  return storage->resultRevision;
}

std::optional<simulation::PlayerSpawnPose> missionWorldRoomPlayerSpawnPose(
    AirfixMissionWorldRoomSnapshot *const snapshot) noexcept {
  if (snapshot == nil) {
    return std::nullopt;
  }
  auto *const storage =
      static_cast<SnapshotStorage *>([snapshot airfix_privateStorage]);
  if (storage == nullptr) {
    return std::nullopt;
  }
  return storage->playerSpawnPose;
}

} // namespace airfix::ios
