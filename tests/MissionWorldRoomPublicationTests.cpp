#include "airfix/content/MissionWorldRoomPublication.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using airfix::assets::MissionStartPosition;
using airfix::assets::MissionWorldStartSelectionSource;
using airfix::content::ContentRevision;
using airfix::content::LoadedMissionWorldRoom;
using airfix::content::MissionWorldRoomPublicationIssue;
using airfix::content::MissionWorldRoomPublicationIssueKind;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] ContentRevision revision(const std::uint64_t generation = 7U) {
    ContentRevision result{
        .generation = generation,
        .pack =
            {
                .size = 4'096U,
                .sha256 = {},
            },
    };
    for (std::size_t index = 0U; index < result.pack.sha256.size(); ++index) {
        result.pack.sha256[index] = static_cast<std::uint8_t>(0x20U + index);
    }
    return result;
}

[[nodiscard]] LoadedMissionWorldRoom validRootRoom() {
    LoadedMissionWorldRoom room;
    room.revision = revision();
    room.setupEntry.logicalPath = "Missions/Test/Setup.txt";
    room.setupEntry.archiveFileIndex = 0U;
    room.setupSourceFootprintBytes = 0U;
    room.startSelection = {
        .source = MissionWorldStartSelectionSource::rootRoomFallback,
        .startPositionIndex = std::nullopt,
        .worldRoomIndex = 0U,
    };
    room.semanticCcfSourceCount = 3U;
    room.uniqueCcfSourceCount = 2U;
    room.ccfCacheIndexByLoadSource = {0U, 1U, 1U};
    return room;
}

[[nodiscard]] LoadedMissionWorldRoom validTableRoom() {
    auto room = validRootRoom();
    room.startSelection = {
        .source = MissionWorldStartSelectionSource::table,
        .startPositionIndex = airfix::assets::legacyMissionStartCapacity - 1U,
        .worldRoomIndex = std::numeric_limits<std::size_t>::max(),
    };
    room.selectedStart = MissionStartPosition{
        .roomName = "Room",
        .position =
            {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::lowest(),
                0.0F,
            },
        .axisRotation =
            {
                -std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                -0.0F,
            },
        .sourceOffset = std::numeric_limits<std::uint64_t>::max(),
    };
    return room;
}

void requireIssue(const LoadedMissionWorldRoom &room,
                  const MissionWorldRoomPublicationIssue expected,
                  const std::string_view message) {
    const auto actual =
        airfix::content::validateMissionWorldRoomPublication(room, revision());
    require(actual == std::optional{expected}, message);
}

void testValidBoundaryRooms() {
    const auto root = validRootRoom();
    require(
        !airfix::content::validateMissionWorldRoomPublication(root, revision())
             .has_value(),
        "valid root fallback or zero setup footprint was rejected");

    const auto table = validTableRoom();
    require(
        !airfix::content::validateMissionWorldRoomPublication(table, revision())
             .has_value(),
        "valid table boundary values were rejected");
}

void testIdentityAndParallelProvenance() {
    {
        auto room = validRootRoom();
        const auto issue = airfix::content::validateMissionWorldRoomPublication(
            room, revision(8U));
        require(issue == std::optional{MissionWorldRoomPublicationIssue{
                             .kind = MissionWorldRoomPublicationIssueKind::
                                 revisionMismatch}},
                "revision mismatch was not rejected");
    }
    {
        auto room = validRootRoom();
        room.setupEntry.logicalPath.clear();
        requireIssue(
            room,
            {.kind =
                 MissionWorldRoomPublicationIssueKind::emptySetupLogicalPath},
            "empty setup identity was not rejected");
    }
    {
        auto room = validRootRoom();
        room.meshProvenance.push_back({});
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          meshProvenanceCountMismatch},
                     "mesh provenance mismatch was not rejected");
    }
    {
        auto room = validRootRoom();
        room.instanceProvenance.push_back({});
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          instanceProvenanceCountMismatch},
                     "instance provenance mismatch was not rejected");
    }
}

void testRootFallbackMutations() {
    {
        auto room = validRootRoom();
        room.startSelection.source =
            static_cast<MissionWorldStartSelectionSource>(0xffU);
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          invalidStartSelectionSource},
                     "forged start-selection source was not rejected");
    }
    {
        auto room = validRootRoom();
        room.startSelection.startPositionIndex = 0U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          rootFallbackStartPositionIndexPresent},
                     "root fallback table index was not rejected");
    }
    {
        auto room = validRootRoom();
        room.selectedStart = MissionStartPosition{.roomName = "Room"};
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          rootFallbackSelectedStartPresent},
                     "root fallback selected start was not rejected");
    }
    {
        auto room = validRootRoom();
        room.startSelection.worldRoomIndex = 1U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          rootFallbackWorldRoomIndexNotZero},
                     "non-root fallback room was not rejected");
    }
}

void testTableMutationsAndFiniteness() {
    {
        auto room = validTableRoom();
        room.startSelection.startPositionIndex.reset();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          tableStartPositionIndexMissing},
                     "missing table index was not rejected");
    }
    {
        auto room = validTableRoom();
        room.startSelection.startPositionIndex =
            airfix::assets::legacyMissionStartCapacity;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 tableStartPositionIndexLimitExceeded,
             .sourceIndex = airfix::assets::legacyMissionStartCapacity},
            "one-past legacy start capacity was not rejected");
    }
    {
        auto room = validTableRoom();
        room.selectedStart.reset();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          tableSelectedStartMissing},
                     "missing selected table start was not rejected");
    }
    {
        auto room = validTableRoom();
        room.startSelection.worldRoomIndex = 0U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          tableWorldRoomIndexIsRoot},
                     "table selection of the root room was not rejected");
    }
    {
        auto room = validTableRoom();
        room.selectedStart->roomName.clear();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          tableSelectedStartRoomNameEmpty},
                     "empty selected table room name was not rejected");
    }
    for (std::size_t component = 0U; component < 3U; ++component) {
        auto room = validTableRoom();
        room.selectedStart->position[component] =
            std::numeric_limits<float>::infinity();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          selectedStartPositionNotFinite,
                      .componentIndex = component},
                     "non-finite selected position component was not rejected");
    }
    for (std::size_t component = 0U; component < 3U; ++component) {
        auto room = validTableRoom();
        room.selectedStart->axisRotation[component] =
            std::numeric_limits<float>::quiet_NaN();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          selectedStartAxisRotationNotFinite,
                      .componentIndex = component},
                     "non-finite selected rotation component was not rejected");
    }
}

void testCcfCountAndCacheMutations() {
    {
        auto room = validRootRoom();
        room.semanticCcfSourceCount = 0U;
        room.ccfCacheIndexByLoadSource.clear();
        room.uniqueCcfSourceCount = 0U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          semanticCcfSourceCountZero},
                     "zero semantic CCF count was not rejected");
    }
    {
        auto room = validRootRoom();
        room.semanticCcfSourceCount = 4U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          semanticCcfSourceCountMismatch},
                     "semantic CCF/vector count mismatch was not rejected");
    }
    {
        auto room = validRootRoom();
        room.uniqueCcfSourceCount = 0U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          uniqueCcfSourceCountZero},
                     "zero unique CCF count was not rejected");
    }
    {
        auto room = validRootRoom();
        room.uniqueCcfSourceCount = 4U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 uniqueCcfSourceCountExceedsSemanticCount},
            "unique CCF count greater than semantic count was not rejected");
    }
    {
        auto room = validRootRoom();
        room.ccfCacheIndexByLoadSource[2] = 2U;
        requireIssue(
            room,
            {.kind =
                 MissionWorldRoomPublicationIssueKind::ccfCacheIndexOutOfRange,
             .sourceIndex = 2U},
            "out-of-range cache index was not rejected");
    }
    {
        auto room = validRootRoom();
        room.ccfCacheIndexByLoadSource = {1U, 0U, 1U};
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          ccfCacheFirstUseOrderMismatch,
                      .sourceIndex = 0U},
                     "non-canonical cache first-use order was not rejected");
    }
    {
        auto room = validRootRoom();
        room.ccfCacheIndexByLoadSource = {0U, 0U, 0U};
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          uniqueCcfSourceCountMismatch},
                     "unrepresented unique cache index was not rejected");
    }
}

} // namespace

int main() {
    try {
        testValidBoundaryRooms();
        testIdentityAndParallelProvenance();
        testRootFallbackMutations();
        testTableMutationsAndFiniteness();
        testCcfCountAndCacheMutations();
    } catch (const std::exception &error) {
        std::cerr << "Mission world publication tests failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "Mission world publication tests passed\n";
    return 0;
}
