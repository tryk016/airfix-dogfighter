#include "airfix/assets/MissionEntryResolver.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

struct TestDirectory {
    std::string path;
    std::vector<std::string> files;
};

void appendU32(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void writeU32(Bytes& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(offset + 2U) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(offset + 3U) = static_cast<std::uint8_t>(value >> 24U);
}

void appendRecord(
    Bytes& bytes,
    const std::uint32_t hash,
    const std::uint32_t nameOffset,
    const std::uint32_t field08,
    const std::uint32_t field0C,
    const std::uint32_t field10,
    const std::uint32_t field14) {
    appendU32(bytes, hash);
    appendU32(bytes, nameOffset);
    appendU32(bytes, field08);
    appendU32(bytes, field0C);
    appendU32(bytes, field10);
    appendU32(bytes, field14);
}

[[nodiscard]] Bytes makeArchive(std::vector<TestDirectory> directories) {
    std::stable_sort(
        directories.begin(), directories.end(),
        [](const auto& left, const auto& right) {
            return airfix::udsp::nameHash(left.path) <
                airfix::udsp::nameHash(right.path);
        });
    for (auto& directory : directories) {
        std::stable_sort(
            directory.files.begin(), directory.files.end(),
            [](const auto& left, const auto& right) {
                return airfix::udsp::nameHash(left) < airfix::udsp::nameHash(right);
            });
    }

    Bytes strings;
    struct EncodedDirectory {
        std::uint32_t pathOffset{};
        std::uint32_t firstFileIndex{};
        std::vector<std::uint32_t> fileNameOffsets;
    };
    std::vector<EncodedDirectory> encodedDirectories;
    encodedDirectories.reserve(directories.size());
    std::uint32_t fileIndex = 0U;
    for (const auto& directory : directories) {
        EncodedDirectory encoded{
            .pathOffset = static_cast<std::uint32_t>(strings.size()),
            .firstFileIndex = fileIndex,
            .fileNameOffsets = {},
        };
        strings.insert(strings.end(), directory.path.begin(), directory.path.end());
        strings.push_back(0U);
        encoded.fileNameOffsets.reserve(directory.files.size());
        for (const auto& file : directory.files) {
            encoded.fileNameOffsets.push_back(
                static_cast<std::uint32_t>(strings.size()));
            strings.insert(strings.end(), file.begin(), file.end());
            strings.push_back(0U);
            ++fileIndex;
        }
        encodedDirectories.push_back(std::move(encoded));
    }

    Bytes archive(airfix::udsp::kHeaderSize, 0U);
    const auto directoryOffset = static_cast<std::uint32_t>(archive.size());
    for (std::size_t index = 0U; index < directories.size(); ++index) {
        const auto& directory = directories[index];
        const auto& encoded = encodedDirectories[index];
        appendRecord(
            archive,
            airfix::udsp::nameHash(directory.path),
            encoded.pathOffset,
            0U,
            0U,
            static_cast<std::uint32_t>(directory.files.size()),
            encoded.firstFileIndex * static_cast<std::uint32_t>(airfix::udsp::kRecordSize));
    }

    const auto fileOffset = static_cast<std::uint32_t>(archive.size());
    for (std::size_t directoryIndex = 0U;
         directoryIndex < directories.size();
         ++directoryIndex) {
        const auto& directory = directories[directoryIndex];
        const auto& encoded = encodedDirectories[directoryIndex];
        for (std::size_t index = 0U; index < directory.files.size(); ++index) {
            appendRecord(
                archive,
                airfix::udsp::nameHash(directory.files[index]),
                encoded.fileNameOffsets[index],
                0U,
                0U,
                0U,
                static_cast<std::uint32_t>(airfix::udsp::kHeaderSize));
        }
    }

    const auto stringOffset = static_cast<std::uint32_t>(archive.size());
    archive.insert(archive.end(), strings.begin(), strings.end());
    archive[0] = 'U';
    archive[1] = 'D';
    archive[2] = 'S';
    archive[3] = 'P';
    writeU32(archive, 4U, airfix::udsp::kVersion);
    writeU32(
        archive,
        8U,
        static_cast<std::uint32_t>(directories.size() * airfix::udsp::kRecordSize));
    writeU32(archive, 12U, directoryOffset);
    writeU32(archive, 16U, static_cast<std::uint32_t>(strings.size()));
    writeU32(archive, 20U, stringOffset);
    writeU32(
        archive,
        24U,
        fileIndex * static_cast<std::uint32_t>(airfix::udsp::kRecordSize));
    writeU32(archive, 28U, fileOffset);
    return archive;
}

[[nodiscard]] airfix::udsp::Archive testArchive() {
    return airfix::udsp::Archive::parse(makeArchive({
        {.path = "", .files = {"Root.obje"}},
        {.path = "Dup", .files = {"Thing.obje", "Thing.obje"}},
        {.path = "Objects", .files = {"Plane.obje"}},
        {.path = "Worlds", .files = {"House.fhou"}},
    }));
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] airfix::assets::LevelObjectPlacement placement(
    const std::string& path,
    const float marker,
    const std::string& room) {
    return {
        .position = {marker, marker + 1.0F, marker + 2.0F},
        .axisRotation = {marker + 3.0F, marker + 4.0F, marker + 5.0F},
        .room = room,
        .objectPath = path,
    };
}

[[nodiscard]] airfix::assets::LevelModelPlacement modelPlacement(
    const std::string& path,
    const float marker) {
    return {
        .placement = placement(path, marker, "model-room"),
        .stateStrings = {"one", "two", "three"},
        .stateValues = {1U, 2U, 3U},
        .compatibilityValue = 4U,
    };
}

void testStatusesMetadataAndStablePlacements() {
    std::cerr << "mission-resolver checkpoint: statuses begin\n";
    const auto archive = testArchive();
    std::cerr << "mission-resolver checkpoint: archive built\n";
    airfix::assets::LevelDefinition level;
    level.worldPath = "worlds/house.FHOU";
    level.objects = {
        placement("objects/PLANE.obje", 10.0F, "hangar"),
        placement("../unsafe.obje", 20.0F, "cellar"),
        placement("ROOT.OBJE", 30.0F, "garden"),
        placement("Missing/ghost.obje", 40.0F, "hall"),
        placement("Dup/Thing.obje", 50.0F, "attic"),
    };
    level.models = {
        modelPlacement("objects/PLANE.obje", 60.0F),
        modelPlacement("Missing/model.object", 70.0F),
    };
    std::cerr << "mission-resolver checkpoint: level built\n";

    const auto result = airfix::assets::resolveMissionEntries(level, archive);
    std::cerr << "mission-resolver checkpoint: resolved\n";
    using Status = airfix::assets::MissionEntryStatus;
    require(result.issues.empty(), "valid-size resolution reported an issue");
    require(result.worldEntry.status == Status::unique &&
            result.worldEntry.logicalPath == "worlds\\house.FHOU" &&
            result.worldEntry.archiveDirectoryIndex.has_value() &&
            result.worldEntry.archiveFileIndex.has_value() &&
            result.worldEntry.archiveLogicalPath ==
                std::optional<std::string>{"Worlds\\House.fhou"},
        "world separator/case lookup metadata mismatch");
    require(result.placements.size() == level.objects.size(),
        "placement count or stable processing was lost");
    require(result.models.size() == level.models.size() &&
            result.models[0].modelIndex == 0U &&
            result.models[1].modelIndex == 1U &&
            result.models[0].model.placement.position ==
                level.models[0].placement.position &&
            result.models[0].model.stateStrings == level.models[0].stateStrings &&
            result.models[0].model.stateValues == level.models[0].stateValues &&
            result.models[0].model.compatibilityValue ==
                level.models[0].compatibilityValue &&
            result.models[0].objectEntry.status == Status::unique &&
            result.models[1].objectEntry.status == Status::notFound,
        "model placement metadata, order, or lookup status mismatch");
    std::cerr << "mission-resolver checkpoint: model assertions complete\n";

    const std::array expectedStatuses{
        Status::unique,
        Status::invalid,
        Status::unique,
        Status::notFound,
        Status::ambiguous,
    };
    for (std::size_t index = 0U; index < result.placements.size(); ++index) {
        const auto& resolved = result.placements[index];
        require(resolved.placementIndex == index,
            "placement index/order was not preserved");
        require(resolved.placement.position == level.objects[index].position &&
                resolved.placement.axisRotation == level.objects[index].axisRotation &&
                resolved.placement.room == level.objects[index].room &&
                resolved.placement.objectPath == level.objects[index].objectPath,
            "placement metadata was not copied exactly");
        require(resolved.objectEntry.status == expectedStatuses[index],
            "placement lookup status mismatch");
    }
    std::cerr << "mission-resolver checkpoint: placement assertions complete\n";
    require(result.placements[0].objectEntry.logicalPath ==
            "objects\\PLANE.obje" &&
            result.placements[0].objectEntry.archiveLogicalPath ==
                std::optional<std::string>{"Objects\\Plane.obje"},
        "object separator/case lookup metadata mismatch");
    require(result.placements[2].objectEntry.archiveLogicalPath ==
            std::optional<std::string>{"Root.obje"},
        "root-directory canonical path mismatch or invalid path stopped later entries");
    for (const auto index : {1U, 3U, 4U}) {
        require(!result.placements[index].objectEntry.archiveDirectoryIndex.has_value() &&
                !result.placements[index].objectEntry.archiveFileIndex.has_value() &&
                !result.placements[index].objectEntry.archiveLogicalPath.has_value(),
            "non-unique lookup exposed archive indices");
    }
    std::cerr << "mission-resolver checkpoint: statuses end\n";
}

void testMissingAndInvalidWorld() {
    const auto archive = testArchive();
    const auto missing = airfix::assets::resolveMissionEntries({}, archive);
    require(missing.worldEntry.status == airfix::assets::MissionEntryStatus::missing &&
            missing.worldEntry.logicalPath.empty(),
        "absent world path was not reported as missing");

    airfix::assets::LevelDefinition level;
    level.worldPath = "";
    const auto invalid = airfix::assets::resolveMissionEntries(level, archive);
    require(invalid.worldEntry.status == airfix::assets::MissionEntryStatus::invalid,
        "present empty world path was not reported as invalid");
}

void testLimitsFailClosed() {
    const auto archive = testArchive();
    airfix::assets::LevelDefinition level;
    level.worldPath = "Worlds/House.fhou";
    level.objects = {
        placement("Objects/Plane.obje", 1.0F, "one"),
        placement("Root.obje", 2.0F, "two"),
    };

    auto placementLimits = airfix::assets::MissionEntryResolutionLimits{};
    placementLimits.maximumPlacements = 1U;
    const auto tooMany = airfix::assets::resolveMissionEntries(
        level, archive, placementLimits);
    require(tooMany.worldEntry.status ==
                airfix::assets::MissionEntryStatus::unique &&
            tooMany.placements.empty() && tooMany.issues.size() == 1U &&
            tooMany.issues[0].kind ==
                airfix::assets::MissionEntryIssueKind::limitExceeded,
        "placement limit did not fail closed before processing entries");

    placementLimits.maximumPlacements = 0U;
    const auto zeroPlacements = airfix::assets::resolveMissionEntries(
        level, archive, placementLimits);
    require(zeroPlacements.placements.empty() &&
            zeroPlacements.issues.size() == 1U,
        "zero placement limit accepted a placement");

    auto pathLimits = airfix::assets::MissionEntryResolutionLimits{};
    pathLimits.maximumLogicalPathBytes = 0U;
    const auto zeroPath = airfix::assets::resolveMissionEntries(
        level, archive, pathLimits);
    require(zeroPath.worldEntry.status ==
                airfix::assets::MissionEntryStatus::invalid &&
            zeroPath.placements.size() == 2U &&
            zeroPath.placements[0].objectEntry.status ==
                airfix::assets::MissionEntryStatus::invalid &&
            zeroPath.placements[1].objectEntry.status ==
                airfix::assets::MissionEntryStatus::invalid,
        "zero path limit did not invalidate every supplied path independently");

    airfix::assets::LevelDefinition aggregate;
    aggregate.objects.push_back(placement("Root.obje", 3.0F, "object"));
    aggregate.models.push_back(modelPlacement("Objects/Plane.obje", 4.0F));
    pathLimits = {};
    pathLimits.maximumPlacements = 1U;
    const auto aggregateLimited = airfix::assets::resolveMissionEntries(
        aggregate, archive, pathLimits);
    require(aggregateLimited.placements.empty() &&
            aggregateLimited.models.empty() &&
            aggregateLimited.issues.size() == 1U,
        "combined object/model placement limit was not enforced before processing");
}

} // namespace

int main() {
    try {
        std::cerr << "mission-resolver checkpoint: main begin\n";
        testStatusesMetadataAndStablePlacements();
        std::cerr << "mission-resolver checkpoint: statuses returned\n";
        testMissingAndInvalidWorld();
        std::cerr << "mission-resolver checkpoint: missing returned\n";
        testLimitsFailClosed();
        std::cerr << "mission-resolver checkpoint: limits returned\n";
        std::cout << "all mission entry resolver tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "mission entry resolver test failure: " << error.what() << '\n';
        return 1;
    }
}
