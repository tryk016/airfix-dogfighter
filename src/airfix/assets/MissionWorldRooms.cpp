#include "airfix/assets/MissionWorldRooms.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace airfix::assets {
namespace {

enum class NameInspection : std::uint8_t {
    valid,
    tooLong,
    embeddedNul,
    unsupportedNonAscii,
};

[[nodiscard]] NameInspection inspectComponent(
    const std::string_view value,
    const std::size_t maximumBytes) noexcept {
    if (value.size() > maximumBytes) {
        return NameInspection::tooLong;
    }
    for (const char character : value) {
        const auto byte = static_cast<std::uint8_t>(character);
        if (byte == 0U) {
            return NameInspection::embeddedNul;
        }
        if (byte >= 0x80U) {
            return NameInspection::unsupportedNonAscii;
        }
    }
    return NameInspection::valid;
}

[[nodiscard]] char asciiLower(const char value) noexcept {
    const auto byte = static_cast<std::uint8_t>(value);
    if (byte >= static_cast<std::uint8_t>('A') &&
        byte <= static_cast<std::uint8_t>('Z')) {
        return static_cast<char>(byte + ('a' - 'A'));
    }
    return value;
}

[[nodiscard]] std::string asciiFold(const std::string_view value) {
    std::string result(value);
    std::transform(
        result.begin(), result.end(), result.begin(), asciiLower);
    return result;
}

[[nodiscard]] bool asciiCaseEqual(
    const std::string_view left,
    const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (asciiLower(left[index]) != asciiLower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ccNameEqual(
    const CcNameState& left,
    const CcNameState& right) noexcept {
    return left.name.has_value() && right.name.has_value() &&
        left.prefix.has_value() && right.prefix.has_value() &&
        asciiCaseEqual(*left.name, *right.name) &&
        asciiCaseEqual(*left.prefix, *right.prefix);
}

[[nodiscard]] bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] std::size_t componentBytes(
    const std::optional<std::string>& component) noexcept {
    return component.has_value() ? component->size() : 0U;
}

[[nodiscard]] std::size_t ccNameBytes(const CcNameState& value) noexcept {
    return componentBytes(value.name) + componentBytes(value.prefix);
}

void failBuild(
    MissionWorldRoomCatalog& result,
    const MissionWorldRoomBuildIssueKind kind,
    const std::optional<std::size_t> sourceIndex = std::nullopt,
    const std::optional<std::size_t> physicalRoomIndex = std::nullopt) {
    result.rooms.clear();
    result.issues.push_back({
        .kind = kind,
        .sourceIndex = sourceIndex,
        .physicalRoomIndex = physicalRoomIndex,
    });
}

[[nodiscard]] MissionWorldRoomBuildIssueKind buildIssueFor(
    const NameInspection inspection) noexcept {
    switch (inspection) {
    case NameInspection::tooLong:
        return MissionWorldRoomBuildIssueKind::nameComponentLimitExceeded;
    case NameInspection::embeddedNul:
        return MissionWorldRoomBuildIssueKind::embeddedNul;
    case NameInspection::unsupportedNonAscii:
        return MissionWorldRoomBuildIssueKind::unsupportedNonAscii;
    case NameInspection::valid:
        break;
    }
    return MissionWorldRoomBuildIssueKind::integerOverflow;
}

[[nodiscard]] MissionWorldStartIssueKind startIssueFor(
    const NameInspection inspection) noexcept {
    switch (inspection) {
    case NameInspection::tooLong:
        return MissionWorldStartIssueKind::nameComponentLimitExceeded;
    case NameInspection::embeddedNul:
        return MissionWorldStartIssueKind::embeddedNul;
    case NameInspection::unsupportedNonAscii:
        return MissionWorldStartIssueKind::unsupportedNonAscii;
    case NameInspection::valid:
        break;
    }
    return MissionWorldStartIssueKind::catalogIncomplete;
}

[[nodiscard]] NameInspection inspectCcName(
    const CcNameState& value,
    const std::size_t maximumBytes) noexcept {
    for (const auto* component : {&value.name, &value.prefix}) {
        if (!component->has_value()) {
            continue;
        }
        const auto inspection = inspectComponent(**component, maximumBytes);
        if (inspection != NameInspection::valid) {
            return inspection;
        }
    }
    return NameInspection::valid;
}

[[nodiscard]] bool validateCatalog(
    const MissionWorldRoomCatalog& catalog,
    const MissionWorldStartResolutionLimits& limits) {
    if (!catalog.complete() ||
        catalog.sourceCount != catalog.sourcePhysicalRoomCounts.size() ||
        catalog.rooms.size() > limits.maximumRuntimeRooms) {
        return false;
    }

    std::size_t contributorCount = 0U;
    std::size_t retainedNameBytes = 0U;
    std::set<std::pair<std::size_t, std::size_t>> seenContributors;
    for (std::size_t roomIndex = 0U;
         roomIndex < catalog.rooms.size();
         ++roomIndex) {
        const auto& room = catalog.rooms[roomIndex];
        std::size_t nextRetained = 0U;
        if (!checkedAdd(
                retainedNameBytes,
                ccNameBytes(room.ccName),
                nextRetained) ||
            (roomIndex != 0U && room.ccName.name.has_value() &&
             !checkedAdd(
                 nextRetained,
                 room.ccName.name->size(),
                 nextRetained)) ||
            nextRetained > limits.maximumRetainedNameBytes) {
            return false;
        }
        retainedNameBytes = nextRetained;
        if (inspectCcName(
                room.ccName,
                limits.maximumNameComponentBytes) !=
            NameInspection::valid) {
            return false;
        }
        if (roomIndex != 0U) {
            if (!room.ccName.name.has_value() ||
                room.contributors.empty()) {
                return false;
            }
        }

        std::optional<std::pair<std::size_t, std::size_t>> previous;
        for (const auto& contributor : room.contributors) {
            if (contributor.sourceIndex >= catalog.sourceCount ||
                contributor.physicalRoomIndex >=
                    catalog.sourcePhysicalRoomCounts[
                        contributor.sourceIndex] ||
                !seenContributors.emplace(
                    contributor.sourceIndex,
                    contributor.physicalRoomIndex).second) {
                return false;
            }
            const auto current = std::pair{
                contributor.sourceIndex,
                contributor.physicalRoomIndex};
            if (previous.has_value() && current < *previous) {
                return false;
            }
            previous = current;
            if (contributorCount >= limits.maximumContributors) {
                return false;
            }
            ++contributorCount;
        }
    }
    return true;
}

} // namespace

MissionWorldRoomCatalog buildMissionWorldRoomCatalog(
    const MissionWorldRoomBuildInput& input,
    const MissionWorldRoomBuildLimits& limits) {
    MissionWorldRoomCatalog result;
    result.sourceCount = input.sources.size();
    result.initialRootName = input.initialRootName;
    if (input.sources.size() > limits.maximumSources) {
        failBuild(
            result,
            MissionWorldRoomBuildIssueKind::sourceLimitExceeded);
        return result;
    }
    if (limits.maximumRuntimeRooms == 0U) {
        failBuild(
            result,
            MissionWorldRoomBuildIssueKind::runtimeRoomLimitExceeded);
        return result;
    }

    const auto initialInspection = inspectCcName(
        input.initialRootName, limits.maximumNameComponentBytes);
    if (initialInspection != NameInspection::valid) {
        failBuild(result, buildIssueFor(initialInspection));
        return result;
    }

    std::size_t retainedNameBytes = ccNameBytes(input.initialRootName);
    if (retainedNameBytes > limits.maximumRetainedNameBytes) {
        failBuild(
            result,
            MissionWorldRoomBuildIssueKind::retainedNameLimitExceeded);
        return result;
    }

    result.sourcePhysicalRoomCounts.reserve(input.sources.size());
    std::size_t preflightRoomSections = 0U;
    std::size_t preflightContributors = 0U;
    for (std::size_t sourceIndex = 0U;
         sourceIndex < input.sources.size();
         ++sourceIndex) {
        const auto& source = input.sources[sourceIndex];
        if (source.ccf == nullptr) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::invalidSourceMetadata,
                sourceIndex);
            return result;
        }
        const auto& ccf = *source.ccf;
        result.sourcePhysicalRoomCounts.push_back(ccf.rooms.size());
        if (!source.roomSectionEnabled) {
            continue;
        }

        std::size_t nextSectionCount = 0U;
        if (!checkedAdd(
                preflightRoomSections,
                ccf.roomSections.size(),
                nextSectionCount)) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::integerOverflow,
                sourceIndex);
            return result;
        }
        if (nextSectionCount > limits.maximumRoomSections) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::roomSectionLimitExceeded,
                sourceIndex);
            return result;
        }
        preflightRoomSections = nextSectionCount;

        std::size_t nextContributorCount = 0U;
        if (!checkedAdd(
                preflightContributors,
                ccf.rooms.size(),
                nextContributorCount)) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::integerOverflow,
                sourceIndex);
            return result;
        }
        if (nextContributorCount > limits.maximumContributors) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::contributorLimitExceeded,
                sourceIndex);
            return result;
        }
        preflightContributors = nextContributorCount;

        std::size_t expectedFirstRoom = 0U;
        for (const auto& section : ccf.roomSections) {
            if (section.firstPhysicalRoomIndex != expectedFirstRoom ||
                section.physicalRoomCount >
                    ccf.rooms.size() - expectedFirstRoom ||
                (section.firstDirectChildIsRoom &&
                 section.physicalRoomCount == 0U)) {
                failBuild(
                    result,
                    MissionWorldRoomBuildIssueKind::
                        invalidSourceMetadata,
                    sourceIndex);
                return result;
            }
            for (std::size_t localRoomIndex = 0U;
                 localRoomIndex < section.physicalRoomCount;
                 ++localRoomIndex) {
                const std::size_t physicalRoomIndex =
                    expectedFirstRoom + localRoomIndex;
                const bool expectedPrimary =
                    section.firstDirectChildIsRoom &&
                    localRoomIndex == 0U;
                if (ccf.rooms[physicalRoomIndex].primaryBinding !=
                    expectedPrimary) {
                    failBuild(
                        result,
                        MissionWorldRoomBuildIssueKind::
                            invalidPrimaryBinding,
                        sourceIndex,
                        physicalRoomIndex);
                    return result;
                }
            }
            expectedFirstRoom += section.physicalRoomCount;
        }
        if (expectedFirstRoom != ccf.rooms.size()) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::invalidSourceMetadata,
                sourceIndex);
            return result;
        }
    }

    MissionRuntimeRoom root{
        .ccName = input.initialRootName,
        .contributors = {},
    };
    std::vector<MissionRuntimeRoom> createdRooms;
    std::map<std::string, std::size_t> roomByFoldedName;
    std::size_t contributorCount = 0U;

    const auto publishPhysicalRoom = [&](
        const MissionCcfRoomLoadSource& source,
        const std::size_t sourceIndex,
        const std::size_t physicalIndex,
        const bool primaryBinding) {
        if (contributorCount >= limits.maximumContributors) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::contributorLimitExceeded,
                sourceIndex,
                physicalIndex);
            return false;
        }

        const auto& physical = source.ccf->rooms[physicalIndex];
        const CcNameState physicalName{
            .name = physical.name,
            .prefix = physical.prefix,
        };
        const auto inspection = inspectCcName(
            physicalName, limits.maximumNameComponentBytes);
        if (inspection != NameInspection::valid) {
            failBuild(
                result,
                buildIssueFor(inspection),
                sourceIndex,
                physicalIndex);
            return false;
        }

        const MissionWorldRoomContributor contributor{
            .sourceIndex = sourceIndex,
            .physicalRoomIndex = physicalIndex,
        };
        if (primaryBinding) {
            root.contributors.push_back(contributor);
            ++contributorCount;
            if (source.copyPrimaryNameToRoot) {
                const auto previousRootBytes = ccNameBytes(root.ccName);
                retainedNameBytes -= previousRootBytes;
                std::size_t nextRetained = 0U;
                if (!checkedAdd(
                        retainedNameBytes,
                        ccNameBytes(physicalName),
                        nextRetained)) {
                    failBuild(
                        result,
                        MissionWorldRoomBuildIssueKind::integerOverflow,
                        sourceIndex,
                        physicalIndex);
                    return false;
                }
                if (nextRetained >
                    limits.maximumRetainedNameBytes) {
                    failBuild(
                        result,
                        MissionWorldRoomBuildIssueKind::
                            retainedNameLimitExceeded,
                        sourceIndex,
                        physicalIndex);
                    return false;
                }
                retainedNameBytes = nextRetained;
                root.ccName = physicalName;
            }
            return true;
        }

        if (ccNameEqual(root.ccName, physicalName)) {
            root.contributors.push_back(contributor);
            ++contributorCount;
            return true;
        }

        const std::string foldedName = asciiFold(physical.name);
        const auto existing = roomByFoldedName.find(foldedName);
        if (existing != roomByFoldedName.end()) {
            createdRooms[existing->second].contributors.push_back(
                contributor);
            ++contributorCount;
            return true;
        }

        if (createdRooms.size() >=
            limits.maximumRuntimeRooms - 1U) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::runtimeRoomLimitExceeded,
                sourceIndex,
                physicalIndex);
            return false;
        }
        std::size_t nextRetained = 0U;
        if (!checkedAdd(
                retainedNameBytes,
                ccNameBytes(physicalName),
                nextRetained) ||
            !checkedAdd(
                nextRetained,
                foldedName.size(),
                nextRetained)) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::integerOverflow,
                sourceIndex,
                physicalIndex);
            return false;
        }
        if (nextRetained > limits.maximumRetainedNameBytes) {
            failBuild(
                result,
                MissionWorldRoomBuildIssueKind::
                    retainedNameLimitExceeded,
                sourceIndex,
                physicalIndex);
            return false;
        }
        retainedNameBytes = nextRetained;
        const std::size_t creationIndex = createdRooms.size();
        roomByFoldedName.emplace(foldedName, creationIndex);
        createdRooms.push_back({
            .ccName = physicalName,
            .contributors = {contributor},
        });
        ++contributorCount;
        return true;
    };

    for (std::size_t sourceIndex = 0U;
         sourceIndex < input.sources.size();
         ++sourceIndex) {
        const auto& source = input.sources[sourceIndex];
        if (!source.roomSectionEnabled) {
            continue;
        }
        for (const auto& section : source.ccf->roomSections) {
            for (std::size_t localRoomIndex = 0U;
                 localRoomIndex < section.physicalRoomCount;
                 ++localRoomIndex) {
                const std::size_t physicalRoomIndex =
                    section.firstPhysicalRoomIndex + localRoomIndex;
                const bool primaryBinding =
                    section.firstDirectChildIsRoom &&
                    localRoomIndex == 0U;
                if (!publishPhysicalRoom(
                        source,
                        sourceIndex,
                        physicalRoomIndex,
                        primaryBinding)) {
                    return result;
                }
            }
        }
    }

    result.rooms.reserve(createdRooms.size() + 1U);
    result.rooms.push_back(std::move(root));
    for (auto room = createdRooms.rbegin();
         room != createdRooms.rend();
         ++room) {
        result.rooms.push_back(std::move(*room));
    }
    return result;
}

MissionWorldStartResolution resolveMissionStartsInWorld(
    const std::span<const MissionStartPosition> starts,
    const MissionWorldRoomCatalog& catalog,
    const MissionWorldStartResolutionLimits& limits) {
    MissionWorldStartResolution result;
    if (!validateCatalog(catalog, limits)) {
        result.issues.push_back({
            .kind = MissionWorldStartIssueKind::catalogIncomplete,
            .startPositionIndex = std::nullopt,
        });
        return result;
    }
    result.worldRoomCount = catalog.rooms.size();

    if (starts.size() > legacyMissionStartCapacity) {
        result.issues.push_back({
            .kind =
                MissionWorldStartIssueKind::startPositionLimitExceeded,
            .startPositionIndex = legacyMissionStartCapacity,
        });
        return result;
    }

    struct RoomMatch {
        std::size_t worldRoomIndex{};
        bool ambiguous{};
    };
    std::map<std::string, RoomMatch> roomByFoldedName;
    for (std::size_t roomIndex = 1U;
         roomIndex < catalog.rooms.size();
         ++roomIndex) {
        const auto& name = *catalog.rooms[roomIndex].ccName.name;
        const auto [iterator, inserted] = roomByFoldedName.emplace(
            asciiFold(name), RoomMatch{.worldRoomIndex = roomIndex});
        if (!inserted) {
            iterator->second.ambiguous = true;
        }
    }

    result.starts.reserve(starts.size());
    for (std::size_t startIndex = 0U;
         startIndex < starts.size();
         ++startIndex) {
        const auto inspection = inspectComponent(
            starts[startIndex].roomName,
            limits.maximumNameComponentBytes);
        if (inspection != NameInspection::valid) {
            result.starts.clear();
            result.issues.push_back({
                .kind = startIssueFor(inspection),
                .startPositionIndex = startIndex,
            });
            return result;
        }
        const auto match = roomByFoldedName.find(
            asciiFold(starts[startIndex].roomName));
        if (match == roomByFoldedName.end()) {
            result.starts.clear();
            result.issues.push_back({
                .kind = MissionWorldStartIssueKind::missingStartRoom,
                .startPositionIndex = startIndex,
            });
            return result;
        }
        if (match->second.ambiguous) {
            result.starts.clear();
            result.issues.push_back({
                .kind = MissionWorldStartIssueKind::ambiguousStartRoom,
                .startPositionIndex = startIndex,
            });
            return result;
        }
        result.starts.push_back({
            .startPositionIndex = startIndex,
            .worldRoomIndex = match->second.worldRoomIndex,
        });
    }
    return result;
}

std::optional<MissionWorldStartSelection> selectMissionWorldStart(
    const MissionWorldStartResolution& resolution,
    const std::uint32_t requestedIndex) noexcept {
    if (!resolution.complete() ||
        resolution.starts.size() > legacyMissionStartCapacity) {
        return std::nullopt;
    }
    for (std::size_t index = 0U;
         index < resolution.starts.size();
         ++index) {
        const auto& start = resolution.starts[index];
        if (start.startPositionIndex != index ||
            start.worldRoomIndex == 0U ||
            start.worldRoomIndex >= resolution.worldRoomCount) {
            return std::nullopt;
        }
    }

    if (resolution.starts.empty()) {
        return MissionWorldStartSelection{
            .source =
                MissionWorldStartSelectionSource::rootRoomFallback,
            .startPositionIndex = std::nullopt,
            .worldRoomIndex = 0U,
        };
    }
    const std::size_t selectedIndex =
        static_cast<std::size_t>(requestedIndex) %
        resolution.starts.size();
    return MissionWorldStartSelection{
        .source = MissionWorldStartSelectionSource::table,
        .startPositionIndex = selectedIndex,
        .worldRoomIndex =
            resolution.starts[selectedIndex].worldRoomIndex,
    };
}

} // namespace airfix::assets
