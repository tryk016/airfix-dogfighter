#include "airfix/render/MissionWorldDynamicCollisionFrame.hpp"

#include <cmath>
#include <limits>

namespace airfix::render {
namespace {

constexpr double kOrthonormalTolerance = 1.0e-5;

[[nodiscard]] double dot(const Vec3& left, const Vec3& right) noexcept {
    return static_cast<double>(left.x) * right.x +
        static_cast<double>(left.y) * right.y +
        static_cast<double>(left.z) * right.z;
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool orthonormal(const Mat3& value) noexcept {
    for (std::size_t index = 0U; index < 3U; ++index) {
        if (!finite(value.columns[index]) ||
            std::abs(
                dot(value.columns[index], value.columns[index]) -
                1.0) > kOrthonormalTolerance) {
            return false;
        }
    }
    return
        std::abs(dot(value.columns[0], value.columns[1])) <=
            kOrthonormalTolerance &&
        std::abs(dot(value.columns[0], value.columns[2])) <=
            kOrthonormalTolerance &&
        std::abs(dot(value.columns[1], value.columns[2])) <=
            kOrthonormalTolerance;
}

[[nodiscard]] MissionWorldDynamicCollisionPublicationResult failure(
    const MissionWorldDynamicCollisionPublicationStatus status) noexcept {
    return {
        .status = status,
        .frame = {},
    };
}

} // namespace

MissionWorldDynamicCollisionPublicationResult
publishMissionWorldDynamicCollisionFrame(
    const MissionPlacedDynamicBspAssembly& placed,
    const PlayerActorCollisionAssembly* const player,
    const ConvertedNodeTransform& playerWorld,
    const std::uint32_t playerObjectId,
    const bool playerActive,
    const std::size_t playerWorldRoomIndex,
    const std::span<LegacyDynamicBspLineObject> outputObjects,
    const std::span<LegacyDynamicBspRoomObjectRange>
        outputRoomRanges) noexcept {
    if (!placed.complete()) {
        return failure(
            MissionWorldDynamicCollisionPublicationStatus::
                invalidPlacedAssembly);
    }
    if (outputRoomRanges.size() !=
        placed.roomObjectRanges.size()) {
        return failure(
            MissionWorldDynamicCollisionPublicationStatus::
                outputSizeMismatch);
    }
    if (player != nullptr && !player->complete()) {
        return failure(
            MissionWorldDynamicCollisionPublicationStatus::
                invalidPlayerAssembly);
    }

    const auto playerInstanceCount =
        player == nullptr ? 0U : player->instances.size();
    if (playerInstanceCount >
        std::numeric_limits<std::size_t>::max() -
            placed.objects.size()) {
        return failure(
            MissionWorldDynamicCollisionPublicationStatus::
                integerOverflow);
    }
    const auto expectedObjectCount =
        placed.objects.size() + playerInstanceCount;
    if (outputObjects.size() != expectedObjectCount) {
        return failure(
            MissionWorldDynamicCollisionPublicationStatus::
                outputSizeMismatch);
    }
    if (player != nullptr) {
        if (outputRoomRanges.empty() ||
            playerWorldRoomIndex >= outputRoomRanges.size()) {
            return failure(
                MissionWorldDynamicCollisionPublicationStatus::
                    invalidInput);
        }
        if (player->meshes.size() >
            std::numeric_limits<std::size_t>::max() -
                placed.meshes.size()) {
            return failure(
                MissionWorldDynamicCollisionPublicationStatus::
                    integerOverflow);
        }
        if (!orthonormal(playerWorld.linear) ||
            !finite(playerWorld.translation) ||
            !std::isfinite(playerWorld.rawScalar)) {
            return failure(
                MissionWorldDynamicCollisionPublicationStatus::
                    invalidTransform);
        }

        // Validate every composition and adjusted mesh index before writing
        // any caller-owned record.
        for (const auto& instance : player->instances) {
            if (instance.collisionMeshIndex >=
                    player->meshes.size() ||
                instance.collisionMeshIndex >
                    std::numeric_limits<std::size_t>::max() -
                        placed.meshes.size()) {
                return failure(
                    MissionWorldDynamicCollisionPublicationStatus::
                        invalidPlayerAssembly);
            }
            const auto composed = tryComposeNodeTransforms(
                playerWorld, instance.actorLocal);
            if (!composed.has_value() ||
                !orthonormal(composed->linear) ||
                !finite(composed->translation)) {
                return failure(
                    MissionWorldDynamicCollisionPublicationStatus::
                        invalidTransform);
            }
        }
    }

    std::size_t outputIndex = 0U;
    for (std::size_t roomIndex = 0U;
         roomIndex < placed.roomObjectRanges.size();
         ++roomIndex) {
        const auto firstObjectIndex = outputIndex;
        if (player != nullptr &&
            roomIndex == playerWorldRoomIndex) {
            for (const auto& instance : player->instances) {
                const auto composed = tryComposeNodeTransforms(
                    playerWorld, instance.actorLocal);
                outputObjects[outputIndex++] = {
                    .meshIndex =
                        placed.meshes.size() +
                        instance.collisionMeshIndex,
                    .actorObjectId = playerObjectId,
                    .active = playerActive,
                    .objectLocalToRuntime = composed->linear,
                    .runtimeTranslation = composed->translation,
                    .portalType = -1,
                    .portalWorldRoomIndex = std::nullopt,
                    .portalObjectVisible = false,
                };
            }
        }

        const auto& placedRange =
            placed.roomObjectRanges[roomIndex];
        for (std::size_t localIndex = 0U;
             localIndex < placedRange.objectCount;
             ++localIndex) {
            outputObjects[outputIndex++] =
                placed.objects[
                    placedRange.firstObjectIndex + localIndex];
        }
        outputRoomRanges[roomIndex] = {
            .firstObjectIndex = firstObjectIndex,
            .objectCount = outputIndex - firstObjectIndex,
        };
    }

    return {
        .status =
            MissionWorldDynamicCollisionPublicationStatus::published,
        .frame =
            {
                .meshes =
                    {
                        .primary = placed.meshes,
                        .secondary = player == nullptr
                            ? std::span<const LegacyDynamicBspMesh>{}
                            : std::span<const LegacyDynamicBspMesh>{
                                  player->meshes},
                    },
                .objects = outputObjects,
                .roomObjectRanges = outputRoomRanges,
            },
    };
}

} // namespace airfix::render
