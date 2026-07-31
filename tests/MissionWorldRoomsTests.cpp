#include "airfix/assets/MissionWorldRooms.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using airfix::assets::CcNameState;
using airfix::assets::CcfMetadata;
using airfix::assets::CcfRoomMetadata;
using airfix::assets::MissionCcfRoomLoadSource;
using airfix::assets::MissionRuntimeRoom;
using airfix::assets::MissionStartPosition;
using airfix::assets::MissionWorldRoomBuildInput;
using airfix::assets::MissionWorldRoomBuildIssueKind;
using airfix::assets::MissionWorldRoomBuildLimits;
using airfix::assets::MissionWorldRoomCatalog;
using airfix::assets::MissionWorldRoomContributor;
using airfix::assets::MissionWorldStartIssueKind;
using airfix::assets::MissionWorldStartSelectionSource;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] CcfRoomMetadata room(
    std::string name,
    std::string prefix = {},
    const bool primary = false) {
    return {
        .name = std::move(name),
        .prefix = std::move(prefix),
        .primaryBinding = primary,
    };
}

[[nodiscard]] bool hasBuildIssue(
    const MissionWorldRoomCatalog& catalog,
    const MissionWorldRoomBuildIssueKind kind) {
    return catalog.issues.size() == 1U &&
        catalog.issues.front().kind == kind;
}

[[nodiscard]] CcfMetadata ccf(
    std::vector<CcfRoomMetadata> rooms,
    const bool firstDirectChildIsRoom = true) {
    CcfMetadata result;
    result.rooms = std::move(rooms);
    result.roomSections.push_back({
        .firstPhysicalRoomIndex = 0U,
        .physicalRoomCount = result.rooms.size(),
        .firstDirectChildIsRoom =
            firstDirectChildIsRoom && !result.rooms.empty(),
    });
    return result;
}

[[nodiscard]] MissionWorldRoomCatalog build(
    const std::vector<std::vector<CcfRoomMetadata>>& sourceRooms,
    const MissionWorldRoomBuildLimits& limits = {}) {
    std::vector<CcfMetadata> metadata;
    metadata.reserve(sourceRooms.size());
    for (const auto& rooms : sourceRooms) {
        metadata.push_back(ccf(rooms));
    }
    std::vector<MissionCcfRoomLoadSource> sources;
    sources.reserve(sourceRooms.size());
    for (const auto& source : metadata) {
        sources.push_back({.ccf = &source});
    }
    return airfix::assets::buildMissionWorldRoomCatalog(
        MissionWorldRoomBuildInput{.sources = sources}, limits);
}

void testOrderedLoadsMergeRoomsAndRoot() {
    const std::vector<std::vector<CcfRoomMetadata>> sourceRooms{
        {
            room("", "", true),
            room("Hangar", "first"),
            room("Outside"),
        },
        {
            room("", "", true),
            room("hANGAR", "ignored"),
            room("Tunnel"),
        },
    };
    const auto catalog = build(sourceRooms);
    require(catalog.complete(), "ordered catalog is incomplete");
    require(
        catalog.sourceCount == 2U &&
        catalog.sourcePhysicalRoomCounts ==
            std::vector<std::size_t>{3U, 3U} &&
        catalog.rooms.size() == 4U,
        "ordered catalog counts are wrong");
    require(
        catalog.rooms[0].contributors ==
            std::vector<MissionWorldRoomContributor>{{0U, 0U}, {1U, 0U}},
        "primary rooms did not bind the shared root");
    require(
        catalog.rooms[1].ccName.name ==
            std::optional<std::string>{"Tunnel"} &&
        catalog.rooms[2].ccName.name ==
            std::optional<std::string>{"Outside"} &&
        catalog.rooms[3].ccName ==
            CcNameState{
                .name = std::string{"Hangar"},
                .prefix = std::string{"first"}} &&
        catalog.rooms[3].contributors ==
            std::vector<MissionWorldRoomContributor>{{0U, 1U}, {1U, 1U}},
        "non-root creation/merge ordering is wrong");
}

void testLegacyRoomIdsFollowCreationOrder() {
    const auto catalog = build({
        {
            room("", "", true),
            room("Hangar"),
            room("Outside"),
        },
        {
            room("", "", true),
            room("hangar"),
            room("Tunnel"),
        },
    });
    require(
        catalog.complete() && catalog.rooms.size() == 4U,
        "room-ID catalog is incomplete");

    const std::vector<std::int32_t> expectedIds{0, 3, 2, 1};
    for (std::size_t worldRoomIndex = 0U;
         worldRoomIndex < expectedIds.size();
         ++worldRoomIndex) {
        const auto roomId =
            airfix::assets::legacyCcRoomIdForWorldRoomIndex(
                catalog, worldRoomIndex);
        require(
            roomId == std::optional{expectedIds[worldRoomIndex]},
            "world-room index mapped to the wrong legacy room ID");
        require(
            airfix::assets::worldRoomIndexForLegacyCcRoomId(
                catalog, expectedIds[worldRoomIndex]) ==
                std::optional{worldRoomIndex},
            "legacy room ID did not round-trip to its world-room index");
        require(airfix::assets::legacyCcRoomIdForWorldRoomIndex(
                    catalog.rooms.size(), worldRoomIndex)
                    == roomId
                && airfix::assets::worldRoomIndexForLegacyCcRoomId(
                       catalog.rooms.size(), expectedIds[worldRoomIndex])
                    == std::optional{worldRoomIndex},
            "compact room-count identity diverged from the build catalog");
    }

    require(
        !airfix::assets::legacyCcRoomIdForWorldRoomIndex(
            catalog, catalog.rooms.size()).has_value(),
        "out-of-range world-room index was accepted");
    require(
        !airfix::assets::worldRoomIndexForLegacyCcRoomId(
            catalog, -1).has_value() &&
        !airfix::assets::worldRoomIndexForLegacyCcRoomId(
            catalog, 4).has_value(),
        "invalid legacy room ID was accepted");
    require(
        !airfix::assets::legacyCcRoomIdForWorldRoomIndex(0U, 0U).has_value() &&
            !airfix::assets::worldRoomIndexForLegacyCcRoomId(0U, 0)
                 .has_value() &&
            !airfix::assets::legacyCcRoomIdForWorldRoomIndex(
                 std::numeric_limits<std::size_t>::max(), 0U)
                 .has_value() &&
            !airfix::assets::worldRoomIndexForLegacyCcRoomId(
                 std::numeric_limits<std::size_t>::max(), 0)
                 .has_value(),
        "invalid compact room identity was accepted");

    auto incomplete = catalog;
    incomplete.issues.push_back({
        .kind =
            MissionWorldRoomBuildIssueKind::invalidSourceMetadata,
    });
    require(
        !airfix::assets::legacyCcRoomIdForWorldRoomIndex(
            incomplete, 0U).has_value() &&
        !airfix::assets::worldRoomIndexForLegacyCcRoomId(
            incomplete, 0).has_value(),
        "incomplete catalog was accepted by room-ID mapping");

    const auto rootOnly = build({});
    require(
        airfix::assets::legacyCcRoomIdForWorldRoomIndex(
            rootOnly, 0U) == std::optional<std::int32_t>{0} &&
        airfix::assets::worldRoomIndexForLegacyCcRoomId(
            rootOnly, 0) == std::optional<std::size_t>{0U} &&
        !airfix::assets::worldRoomIndexForLegacyCcRoomId(
            rootOnly, 1).has_value(),
        "root-only room-ID mapping is wrong");
}

void testMultipleAndNonLeadingRoomSections() {
    CcfMetadata sectioned;
    sectioned.rooms = {
        room("ordinary-first"),
        room("root-name", "root-prefix", true),
        room("ordinary-second"),
    };
    sectioned.roomSections = {
        {
            .firstPhysicalRoomIndex = 0U,
            .physicalRoomCount = 1U,
            .firstDirectChildIsRoom = false,
        },
        {
            .firstPhysicalRoomIndex = 1U,
            .physicalRoomCount = 2U,
            .firstDirectChildIsRoom = true,
        },
    };
    const std::vector<MissionCcfRoomLoadSource> sources{{
        .ccf = &sectioned,
        .copyPrimaryNameToRoot = true,
    }};
    const auto catalog = airfix::assets::buildMissionWorldRoomCatalog({
        .sources = sources,
    });
    require(catalog.complete(), "sectioned catalog is incomplete");
    require(
        catalog.rooms.size() == 3U &&
        catalog.rooms[0].ccName ==
            CcNameState{
                .name = std::string{"root-name"},
                .prefix = std::string{"root-prefix"}} &&
        catalog.rooms[0].contributors ==
            std::vector<MissionWorldRoomContributor>{{0U, 1U}} &&
        catalog.rooms[1].ccName.name ==
            std::optional<std::string>{"ordinary-second"} &&
        catalog.rooms[2].ccName.name ==
            std::optional<std::string>{"ordinary-first"},
        "room-section boundaries or first-child semantics were lost");
    const auto start = airfix::assets::resolveMissionStartsInWorld(
        std::vector<MissionStartPosition>{{
            .roomName = "ORDINARY-FIRST",
        }},
        catalog);
    require(
        start.complete() &&
        start.starts ==
            std::vector<airfix::assets::ResolvedMissionWorldStart>{
                {0U, 2U}},
        "physical room zero was incorrectly forced to root");
}

void testRootNamingAndRoomSectionFlags() {
    const std::vector<CcfRoomMetadata> first{
        room("root", "zone", true),
        room("child"),
    };
    const std::vector<CcfRoomMetadata> rootMatch{
        room("", "", true),
        room("ROOT", "ZONE"),
        room("new"),
    };
    const std::vector<CcfRoomMetadata> disabled{
        room("", "", false),
        room("ignored"),
    };
    const CcfMetadata firstCcf = ccf(first);
    const CcfMetadata rootMatchCcf = ccf(rootMatch);
    const CcfMetadata disabledCcf = ccf(disabled);
    const CcfMetadata emptyCcf = ccf({});
    const std::vector<MissionCcfRoomLoadSource> sources{
        {
            .ccf = &firstCcf,
            .roomSectionEnabled = true,
            .copyPrimaryNameToRoot = true,
        },
        {.ccf = &rootMatchCcf},
        {
            .ccf = &disabledCcf,
            .roomSectionEnabled = false,
            .copyPrimaryNameToRoot = true,
        },
        {.ccf = &emptyCcf},
    };
    const auto catalog = airfix::assets::buildMissionWorldRoomCatalog({
        .initialRootName = {},
        .sources = sources,
    });
    require(catalog.complete(), "root-name catalog is incomplete");
    require(
        catalog.rooms.size() == 3U &&
        catalog.rooms[0].ccName ==
            CcNameState{
                .name = std::string{"root"},
                .prefix = std::string{"zone"}} &&
        catalog.rooms[0].contributors ==
            std::vector<MissionWorldRoomContributor>{
                {0U, 0U}, {1U, 0U}, {1U, 1U}},
        "root full-name precedence is wrong");
    require(
        catalog.rooms[1].ccName.name ==
            std::optional<std::string>{"new"} &&
        catalog.rooms[2].ccName.name ==
            std::optional<std::string>{"child"},
        "disabled/empty source changed the room list");
}

void testRootRenameCollisionKeepsExistingChild() {
    const std::vector<CcfRoomMetadata> first{
        room("", "", true),
        room("shared", "child-prefix"),
    };
    const std::vector<CcfRoomMetadata> rename{
        room("SHARED", "root-prefix", true),
    };
    const std::vector<CcfRoomMetadata> later{
        room("", "", true),
        room("shared", "ROOT-PREFIX"),
    };
    const CcfMetadata firstCcf = ccf(first);
    const CcfMetadata renameCcf = ccf(rename);
    const CcfMetadata laterCcf = ccf(later);
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &firstCcf},
        {
            .ccf = &renameCcf,
            .copyPrimaryNameToRoot = true,
        },
        {.ccf = &laterCcf},
    };
    const auto catalog = airfix::assets::buildMissionWorldRoomCatalog({
        .sources = sources,
    });
    require(catalog.complete(), "root-collision catalog is incomplete");
    require(
        catalog.rooms.size() == 2U &&
        catalog.rooms[0].contributors ==
            std::vector<MissionWorldRoomContributor>{
                {0U, 0U}, {1U, 0U}, {2U, 0U}, {2U, 1U}} &&
        catalog.rooms[1].contributors ==
            std::vector<MissionWorldRoomContributor>{{0U, 1U}},
        "root rename did not preserve child or root-first lookup");
}

void testNullAndPresentEmptyCcNameRemainDistinct() {
    const std::vector<CcfRoomMetadata> source{
        room("", "", true),
        room("", ""),
    };
    const CcfMetadata sourceCcf = ccf(source);
    const std::vector<MissionCcfRoomLoadSource> sources{{
        .ccf = &sourceCcf,
    }};
    const auto voidRoot = airfix::assets::buildMissionWorldRoomCatalog({
        .initialRootName = {},
        .sources = sources,
    });
    require(
        voidRoot.complete() && voidRoot.rooms.size() == 2U &&
        !voidRoot.rooms[0].ccName.name.has_value() &&
        voidRoot.rooms[1].ccName.name ==
            std::optional<std::string>{""},
        "void root was treated as present-empty");
    const auto emptyStart = airfix::assets::resolveMissionStartsInWorld(
        std::vector<MissionStartPosition>{{.roomName = ""}},
        voidRoot);
    require(
        emptyStart.complete() &&
        emptyStart.starts ==
            std::vector<airfix::assets::ResolvedMissionWorldStart>{
                {0U, 1U}},
        "present-empty ordinary room did not resolve");

    const CcfMetadata primaryOnly = ccf({source[0]});
    const std::vector<MissionCcfRoomLoadSource> namingSources{
        {
            .ccf = &primaryOnly,
            .copyPrimaryNameToRoot = true,
        },
        {.ccf = &sourceCcf},
    };
    const auto presentEmptyRoot =
        airfix::assets::buildMissionWorldRoomCatalog({
            .sources = namingSources,
        });
    require(
        presentEmptyRoot.complete() &&
        presentEmptyRoot.rooms.size() == 1U &&
        presentEmptyRoot.rooms[0].ccName ==
            CcNameState{
                .name = std::string{},
                .prefix = std::string{}} &&
        presentEmptyRoot.rooms[0].contributors.size() == 3U,
        "present-empty root did not win a full CcName lookup");
    const auto rootExcluded = airfix::assets::resolveMissionStartsInWorld(
        std::vector<MissionStartPosition>{{.roomName = ""}},
        presentEmptyRoot);
    require(
        rootExcluded.starts.empty() &&
        rootExcluded.issues.size() == 1U &&
        rootExcluded.issues[0].kind ==
            MissionWorldStartIssueKind::missingStartRoom,
        "AddStartPos resolved a present-empty root");
}

void testStartResolutionExcludesRootAndSelectsChild() {
    const std::vector<CcfRoomMetadata> source{
        room("root", "zone", true),
        room("root", "other"),
        room("Hangar"),
    };
    const CcfMetadata sourceCcf = ccf(source);
    const std::vector<MissionCcfRoomLoadSource> sources{{
        .ccf = &sourceCcf,
        .copyPrimaryNameToRoot = true,
    }};
    const auto catalog = airfix::assets::buildMissionWorldRoomCatalog({
        .sources = sources,
    });
    require(catalog.complete(), "start catalog is incomplete");

    const std::vector<MissionStartPosition> starts{
        {.roomName = "ROOT"},
        {.roomName = "hangar"},
    };
    const auto resolution =
        airfix::assets::resolveMissionStartsInWorld(starts, catalog);
    require(
        resolution.complete() &&
        resolution.starts ==
            std::vector<airfix::assets::ResolvedMissionWorldStart>{
                {0U, 2U}, {1U, 1U}},
        "start lookup selected root or wrong child");

    const auto first =
        airfix::assets::selectMissionWorldStart(resolution, 0U);
    const auto wrapped =
        airfix::assets::selectMissionWorldStart(resolution, 3U);
    const auto maximum = airfix::assets::selectMissionWorldStart(
        resolution, std::numeric_limits<std::uint32_t>::max());
    require(
        first.has_value() &&
        first->source == MissionWorldStartSelectionSource::table &&
        first->startPositionIndex == 0U &&
        first->worldRoomIndex == 2U &&
        wrapped.has_value() &&
        wrapped->startPositionIndex == 1U &&
        wrapped->worldRoomIndex == 1U &&
        maximum.has_value() &&
        maximum->startPositionIndex == 1U,
        "world start modulo selection is wrong");

    const auto empty =
        airfix::assets::resolveMissionStartsInWorld({}, catalog);
    const auto fallback =
        airfix::assets::selectMissionWorldStart(empty, 99U);
    require(
        fallback.has_value() &&
        fallback->source ==
            MissionWorldStartSelectionSource::rootRoomFallback &&
        !fallback->startPositionIndex.has_value() &&
        fallback->worldRoomIndex == 0U,
        "empty table did not select the root fallback");
}

void testBuildRejectsInvalidInputAndLimits() {
    const auto wrongFirst = build({{room("not-primary")}});
    require(
        hasBuildIssue(
            wrongFirst,
            MissionWorldRoomBuildIssueKind::invalidPrimaryBinding),
        "non-primary first room was accepted");
    const auto secondPrimary = build({{
        room("", "", true),
        room("also", "", true),
    }});
    require(
        hasBuildIssue(
            secondPrimary,
            MissionWorldRoomBuildIssueKind::invalidPrimaryBinding),
        "second primary room was accepted");

    CcfMetadata missingSections;
    missingSections.rooms = {room("", "", true)};
    const std::vector<MissionCcfRoomLoadSource> invalidSources{{
        .ccf = &missingSections,
    }};
    require(
        hasBuildIssue(
            airfix::assets::buildMissionWorldRoomCatalog({
                .sources = invalidSources,
            }),
            MissionWorldRoomBuildIssueKind::invalidSourceMetadata),
        "rooms without section metadata were accepted");
    const std::vector<MissionCcfRoomLoadSource> nullSource{{}};
    require(
        hasBuildIssue(
            airfix::assets::buildMissionWorldRoomCatalog({
                .sources = nullSource,
            }),
            MissionWorldRoomBuildIssueKind::invalidSourceMetadata),
        "null source metadata was accepted");

    std::string nul{"a"};
    nul.push_back('\0');
    nul.push_back('b');
    require(
        hasBuildIssue(
            build({{room("", "", true), room(nul)}}),
            MissionWorldRoomBuildIssueKind::embeddedNul),
        "embedded NUL was accepted");
    require(
        hasBuildIssue(
            build({{
                room("", "", true),
                room(std::string(1U, static_cast<char>(0x80))),
            }}),
            MissionWorldRoomBuildIssueKind::unsupportedNonAscii),
        "non-ASCII name was accepted");

    const std::vector<std::vector<CcfRoomMetadata>> one{{
        room("", "", true),
        room("a"),
    }};
    auto limits = MissionWorldRoomBuildLimits{};
    limits.maximumSources = 0U;
    require(
        hasBuildIssue(
            build(one, limits),
            MissionWorldRoomBuildIssueKind::sourceLimitExceeded),
        "source limit was ignored");
    CcfMetadata twoEmptySections;
    twoEmptySections.roomSections.resize(2U);
    const std::vector<MissionCcfRoomLoadSource> sectionSources{{
        .ccf = &twoEmptySections,
    }};
    limits = {};
    limits.maximumRoomSections = 1U;
    require(
        hasBuildIssue(
            airfix::assets::buildMissionWorldRoomCatalog(
                {.sources = sectionSources}, limits),
            MissionWorldRoomBuildIssueKind::roomSectionLimitExceeded),
        "room-section limit was ignored");
    limits = {};
    limits.maximumContributors = 1U;
    require(
        hasBuildIssue(
            build(one, limits),
            MissionWorldRoomBuildIssueKind::contributorLimitExceeded),
        "contributor limit was ignored");
    limits = {};
    limits.maximumRuntimeRooms = 1U;
    require(
        hasBuildIssue(
            build(one, limits),
            MissionWorldRoomBuildIssueKind::runtimeRoomLimitExceeded),
        "runtime-room limit was ignored");
    limits = {};
    limits.maximumNameComponentBytes = 0U;
    require(
        hasBuildIssue(
            build(one, limits),
            MissionWorldRoomBuildIssueKind::nameComponentLimitExceeded),
        "name-component limit was ignored");
    limits = {};
    limits.maximumRetainedNameBytes = 1U;
    require(
        hasBuildIssue(
            build(one, limits),
            MissionWorldRoomBuildIssueKind::retainedNameLimitExceeded),
        "retained-name limit was ignored");

    limits = {};
    limits.maximumContributors = 2U;
    limits.maximumRuntimeRooms = 2U;
    limits.maximumNameComponentBytes = 1U;
    limits.maximumRetainedNameBytes = 2U;
    require(
        build(one, limits).complete(),
        "exact build limits were rejected");

    limits = {};
    limits.maximumSources = 0U;
    limits.maximumRuntimeRooms = 1U;
    const auto rootOnly = build({}, limits);
    require(
        rootOnly.complete() && rootOnly.rooms.size() == 1U,
        "root-only world at exact zero-source limit was rejected");
}

void testStartResolutionAndSelectorFailClosed() {
    const auto catalog = build({{
        room("", "", true),
        room("room"),
    }});
    require(catalog.complete(), "failure-test catalog is incomplete");

    const auto missing = airfix::assets::resolveMissionStartsInWorld(
        std::vector<MissionStartPosition>{{.roomName = "missing"}},
        catalog);
    require(
        !missing.complete() && missing.starts.empty() &&
        missing.issues.size() == 1U &&
        missing.issues[0].kind ==
            MissionWorldStartIssueKind::missingStartRoom,
        "missing start did not fail closed");

    const std::vector<MissionStartPosition> tooMany(
        airfix::assets::legacyMissionStartCapacity + 1U,
        MissionStartPosition{.roomName = "room"});
    const auto limited =
        airfix::assets::resolveMissionStartsInWorld(tooMany, catalog);
    require(
        limited.issues.size() == 1U &&
        limited.issues[0].kind ==
            MissionWorldStartIssueKind::startPositionLimitExceeded,
        "oversized start table was accepted");

    auto invalidCatalog = catalog;
    invalidCatalog.rooms[1].ccName.name =
        std::string(1U, static_cast<char>(0x80));
    const auto dependency =
        airfix::assets::resolveMissionStartsInWorld({}, invalidCatalog);
    require(
        dependency.issues.size() == 1U &&
        dependency.issues[0].kind ==
            MissionWorldStartIssueKind::catalogIncomplete,
        "forged catalog was accepted");

    auto duplicateCatalog = catalog;
    duplicateCatalog.rooms.push_back(MissionRuntimeRoom{
        .ccName = {
            .name = std::string{"ROOM"},
            .prefix = std::string{"other"},
        },
        .contributors = {{0U, 0U}},
    });
    duplicateCatalog.sourcePhysicalRoomCounts[0] = 3U;
    duplicateCatalog.rooms.back().contributors[0].physicalRoomIndex = 2U;
    const auto ambiguous = airfix::assets::resolveMissionStartsInWorld(
        std::vector<MissionStartPosition>{{.roomName = "room"}},
        duplicateCatalog);
    require(
        ambiguous.starts.empty() &&
        ambiguous.issues.size() == 1U &&
        ambiguous.issues[0].kind ==
            MissionWorldStartIssueKind::ambiguousStartRoom,
        "forged duplicate child was not treated as ambiguous");

    std::string nul{"r"};
    nul.push_back('\0');
    nul += "oom";
    const auto embeddedNul = airfix::assets::resolveMissionStartsInWorld(
        std::vector<MissionStartPosition>{{.roomName = nul}},
        catalog);
    require(
        embeddedNul.issues.size() == 1U &&
        embeddedNul.issues[0].kind ==
            MissionWorldStartIssueKind::embeddedNul,
        "start name with embedded NUL was accepted");
    const auto nonAscii = airfix::assets::resolveMissionStartsInWorld(
        std::vector<MissionStartPosition>{{
            .roomName =
                std::string(1U, static_cast<char>(0x80)),
        }},
        catalog);
    require(
        nonAscii.issues.size() == 1U &&
        nonAscii.issues[0].kind ==
            MissionWorldStartIssueKind::unsupportedNonAscii,
        "non-ASCII start name was accepted");
    auto resolutionLimits =
        airfix::assets::MissionWorldStartResolutionLimits{};
    resolutionLimits.maximumNameComponentBytes = 3U;
    const auto nameLimited = airfix::assets::resolveMissionStartsInWorld(
        std::vector<MissionStartPosition>{{.roomName = "room"}},
        catalog,
        resolutionLimits);
    require(
        nameLimited.issues.size() == 1U &&
        nameLimited.issues[0].kind ==
            MissionWorldStartIssueKind::catalogIncomplete,
        "catalog name outside resolver limit was accepted");
    resolutionLimits = {};
    resolutionLimits.maximumRetainedNameBytes = 7U;
    const auto aggregateNameLimited =
        airfix::assets::resolveMissionStartsInWorld(
            std::vector<MissionStartPosition>{},
            catalog,
            resolutionLimits);
    require(
        aggregateNameLimited.issues.size() == 1U &&
        aggregateNameLimited.issues[0].kind ==
            MissionWorldStartIssueKind::catalogIncomplete,
        "aggregate resolver name budget was ignored");

    airfix::assets::MissionWorldStartResolution forged{
        .worldRoomCount = 2U,
        .starts = {{0U, 0U}},
    };
    require(
        !airfix::assets::selectMissionWorldStart(forged, 0U).has_value(),
        "selector accepted root as a table room");
    forged.starts = {{0U, 2U}};
    require(
        !airfix::assets::selectMissionWorldStart(forged, 0U).has_value(),
        "selector accepted an out-of-range room");
    forged.starts = {{1U, 1U}};
    require(
        !airfix::assets::selectMissionWorldStart(forged, 0U).has_value(),
        "selector accepted a non-canonical start index");
}

static_assert(std::is_trivially_copyable_v<
    airfix::assets::MissionWorldStartSelection>);

} // namespace

int main() {
    try {
        testOrderedLoadsMergeRoomsAndRoot();
        testLegacyRoomIdsFollowCreationOrder();
        testMultipleAndNonLeadingRoomSections();
        testRootNamingAndRoomSectionFlags();
        testRootRenameCollisionKeepsExistingChild();
        testNullAndPresentEmptyCcNameRemainDistinct();
        testStartResolutionExcludesRootAndSelectsChild();
        testBuildRejectsInvalidInputAndLimits();
        testStartResolutionAndSelectorFailClosed();
        std::cout << "Mission world-room tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mission world-room tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
