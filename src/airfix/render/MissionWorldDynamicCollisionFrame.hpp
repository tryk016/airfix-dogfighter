#pragma once

#include "airfix/render/MissionPlacedDynamicBspAssembly.hpp"
#include "airfix/render/PlayerActorCollisionAssembly.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace airfix::render {

enum class MissionWorldDynamicCollisionPublicationStatus : std::uint8_t {
    published,
    invalidPlacedAssembly,
    invalidPlayerAssembly,
    invalidInput,
    outputSizeMismatch,
    invalidTransform,
    integerOverflow,
};

struct MissionWorldDynamicCollisionFrameView final {
    // Logical mesh indices address placedMeshes first and playerMeshes second.
    LegacyDynamicBspMeshView meshes;
    std::span<const LegacyDynamicBspLineObject> objects;
    std::span<const LegacyDynamicBspRoomObjectRange> roomObjectRanges;
};

struct MissionWorldDynamicCollisionPublicationResult final {
    MissionWorldDynamicCollisionPublicationStatus status{
        MissionWorldDynamicCollisionPublicationStatus::invalidInput};
    MissionWorldDynamicCollisionFrameView frame;

    [[nodiscard]] constexpr bool published() const noexcept {
        return status ==
            MissionWorldDynamicCollisionPublicationStatus::published;
    }
};

// Publishes the immutable mission objects together with one live player actor
// into exact-size caller-owned buffers. Mission meshes remain the logical
// prefix; player mesh indices are offset into a second immutable span.
//
// CcObject::Link prepends runtime actors after the mission scene has loaded,
// so the player's instances precede the placed-object list in its current
// room. Placed-object order is otherwise copied exactly. The player remains
// present with active=false so buffer sizes and room ranges stay stable.
//
// The complete validation pass runs before either output span is changed.
// Success is allocation-free and noexcept. Pass nullptr when the authenticated
// mission has no player collision assembly; actor inputs are then ignored.
[[nodiscard]] MissionWorldDynamicCollisionPublicationResult
publishMissionWorldDynamicCollisionFrame(
    const MissionPlacedDynamicBspAssembly& placed,
    const PlayerActorCollisionAssembly* player,
    const ConvertedNodeTransform& playerWorld,
    std::uint32_t playerObjectId,
    bool playerActive,
    std::size_t playerWorldRoomIndex,
    std::span<LegacyDynamicBspLineObject> outputObjects,
    std::span<LegacyDynamicBspRoomObjectRange> outputRoomRanges) noexcept;

} // namespace airfix::render
