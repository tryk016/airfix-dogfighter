#pragma once

#import "AirfixMissionWorldRoomSnapshot.h"

#include "airfix/content/LegacyAircraftAudioClipSet.hpp"
#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/content/WorldRoomPublicationGate.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

#include <optional>

namespace airfix::ios {

// Creates the Objective-C envelope by moving the complete portable payload.
// No private logical path, start pose, provenance, model, texture, or source
// byte is copied into a public Objective-C property.
[[nodiscard]] AirfixMissionWorldRoomSnapshot* makeMissionWorldRoomSnapshot(
    content::WorldRoomPublicationTicket ticket,
    content::LoadedMissionWorldRoom&& room,
    content::LoadedLegacyAircraftAudioClips&& audioClips);

// One-shot renderer handoff. A second call fails rather than returning a
// moved-from payload.
[[nodiscard]] content::LoadedMissionWorldRoom takeLoadedMissionWorldRoom(
    AirfixMissionWorldRoomSnapshot* snapshot);
[[nodiscard]] content::LoadedLegacyAircraftAudioClips
takeLoadedLegacyAircraftAudioClips(
    AirfixMissionWorldRoomSnapshot* snapshot);

// Remains available after the payload has been taken so the main-thread
// coordinator can reject a stale two-phase Metal publication.
[[nodiscard]] content::WorldRoomPublicationTicket
missionWorldRoomPublicationTicket(
    AirfixMissionWorldRoomSnapshot* snapshot);
[[nodiscard]] content::ContentRevision missionWorldRoomResultRevision(
    AirfixMissionWorldRoomSnapshot* snapshot);
// Independent immutable copy; remains available after the one-shot large
// room payload has moved to the renderer.
[[nodiscard]] std::optional<simulation::PlayerSpawnPose>
missionWorldRoomPlayerSpawnPose(
    AirfixMissionWorldRoomSnapshot* snapshot) noexcept;

} // namespace airfix::ios
