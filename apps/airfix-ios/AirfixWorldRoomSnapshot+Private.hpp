#pragma once

#import "AirfixWorldRoomSnapshot.h"

#include "airfix/content/WorldRoomLoader.hpp"
#include "airfix/content/WorldRoomPublicationGate.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace airfix::ios {

// Creates the Objective-C envelope by moving the complete portable payload.
// No model, texture, or source bytes are copied into Objective-C properties.
[[nodiscard]] AirfixWorldRoomSnapshot* makeWorldRoomSnapshot(
    std::string worldLogicalPath,
    std::size_t physicalRoom,
    content::WorldRoomPublicationTicket ticket,
    content::LoadedWorldRoom&& room);

// One-shot renderer handoff. A second call fails rather than returning a
// moved-from payload.
[[nodiscard]] content::LoadedWorldRoom takeLoadedWorldRoom(
    AirfixWorldRoomSnapshot* snapshot);

// Remains available after the payload has been taken so the main-thread
// coordinator can reject a stale two-phase Metal publication.
[[nodiscard]] content::WorldRoomPublicationTicket worldRoomPublicationTicket(
    AirfixWorldRoomSnapshot* snapshot);
[[nodiscard]] content::ContentRevision worldRoomResultRevision(
    AirfixWorldRoomSnapshot* snapshot);

} // namespace airfix::ios
