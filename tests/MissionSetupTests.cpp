#include "airfix/assets/MissionSetup.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using airfix::assets::CcfMetadata;
using airfix::assets::CcfRoomMetadata;
using airfix::assets::MissionSetupParseError;
using airfix::assets::MissionSetupParseErrorCode;
using airfix::assets::MissionSetupParseLimits;
using airfix::assets::MissionStartPosition;
using airfix::assets::MissionStartRoomIssueKind;
using airfix::assets::MissionStartSelectionSource;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::vector<std::uint8_t> bytes(
    const std::string_view text) {
    return {text.begin(), text.end()};
}

template <typename Function>
void requireParseError(
    const MissionSetupParseErrorCode expected,
    Function&& function,
    const std::string& label) {
    try {
        function();
        throw std::runtime_error(label + " did not fail");
    } catch (const MissionSetupParseError& error) {
        require(error.code() == expected, label + " returned the wrong code");
    }
}

[[nodiscard]] CcfRoomMetadata room(
    std::string name,
    const bool primary = false) {
    return {
        .name = std::move(name),
        .primaryBinding = primary,
    };
}

void testParserFindsExactCallsOnly() {
    const auto source = bytes(R"afs(
        // AddStartPos("comment", coord3d(9,9,9), coord3d(9,9,9));
        object LevelData {
          string fake = "AddStartPos(\"string\", coord3d(8,8,8), coord3d(8,8,8));";
          /* another AddStartPos("comment", coord3d(7,7,7), coord3d(7,7,7)); */
          AddStartPosition("not-the-function");
          AddStartPos(
            "first_room",
            coord3d(1.25, -2, +.5),
            coord3d(0, -1.5e+1, 2E-2)
          );
          AddStartPos("second\\room", coord3d(3,4,5), coord3d(6,7,8));
        }
    )afs");

    const auto starts = airfix::assets::parseMissionStartPositions(source);
    require(starts.size() == 2U, "parser returned the wrong call count");
    require(
        starts[0].roomName == "first_room" &&
        starts[0].position[0] == 1.25F &&
        starts[0].position[1] == -2.0F &&
        starts[0].position[2] == 0.5F &&
        starts[0].axisRotation[1] == -15.0F &&
        std::fabs(starts[0].axisRotation[2] - 0.02F) < 0.000001F,
        "first AddStartPos payload was decoded incorrectly");
    require(
        starts[1].roomName == "second\\room" &&
        starts[1].position == std::array<float, 3>{3.0F, 4.0F, 5.0F} &&
        starts[1].axisRotation ==
            std::array<float, 3>{6.0F, 7.0F, 8.0F},
        "second AddStartPos payload was decoded incorrectly");
    require(
        starts[0].sourceOffset < starts[1].sourceOffset,
        "source offsets do not preserve call order");
}

void testParserAcceptsNoStarts() {
    const auto source = bytes("object Empty { event Run { Print(\"ok\"); } }");
    require(
        airfix::assets::parseMissionStartPositions(source).empty(),
        "source without AddStartPos produced a record");
}

void testParserLimitsAndMalformedInput() {
    const auto one = bytes(
        "AddStartPos(\"room\",coord3d(1,2,3),coord3d(4,5,6));");
    requireParseError(
        MissionSetupParseErrorCode::sourceLimitExceeded,
        [&] {
            MissionSetupParseLimits limits;
            limits.maximumSourceBytes = one.size() - 1U;
            (void)airfix::assets::parseMissionStartPositions(one, limits);
        },
        "source limit");
    requireParseError(
        MissionSetupParseErrorCode::roomNameLimitExceeded,
        [&] {
            MissionSetupParseLimits limits;
            limits.maximumRoomNameBytes = 3U;
            (void)airfix::assets::parseMissionStartPositions(one, limits);
        },
        "room-name limit");

    const auto two = bytes(
        "AddStartPos(\"a\",coord3d(1,2,3),coord3d(4,5,6));"
        "AddStartPos(\"b\",coord3d(1,2,3),coord3d(4,5,6));");
    requireParseError(
        MissionSetupParseErrorCode::startPositionLimitExceeded,
        [&] {
            MissionSetupParseLimits limits;
            limits.maximumStartPositions = 1U;
            (void)airfix::assets::parseMissionStartPositions(two, limits);
        },
        "start-position limit");
    requireParseError(
        MissionSetupParseErrorCode::startPositionLimitExceeded,
        [&] {
            MissionSetupParseLimits limits;
            limits.maximumStartPositions = 0U;
            (void)airfix::assets::parseMissionStartPositions(one, limits);
        },
        "zero start-position limit");

    std::string capacitySource;
    for (std::size_t index = 0U;
         index < airfix::assets::legacyMissionStartCapacity;
         ++index) {
        capacitySource +=
            "AddStartPos(\"room\",coord3d(1,2,3),coord3d(4,5,6));";
    }
    require(
        airfix::assets::parseMissionStartPositions(
            bytes(capacitySource)).size() ==
            airfix::assets::legacyMissionStartCapacity,
        "legacy start-position capacity was not accepted");
    capacitySource +=
        "AddStartPos(\"room\",coord3d(1,2,3),coord3d(4,5,6));";
    requireParseError(
        MissionSetupParseErrorCode::startPositionLimitExceeded,
        [&] {
            (void)airfix::assets::parseMissionStartPositions(
                bytes(capacitySource));
        },
        "legacy start-position capacity plus one");

    requireParseError(
        MissionSetupParseErrorCode::malformedText,
        [&] {
            const auto malformed = bytes(
                "AddStartPos(\"room\",coord3d(1,2),coord3d(4,5,6));");
            (void)airfix::assets::parseMissionStartPositions(malformed);
        },
        "malformed coord3d");
    requireParseError(
        MissionSetupParseErrorCode::invalidNumber,
        [&] {
            const auto invalid = bytes(
                "AddStartPos(\"room\",coord3d(1e999,2,3),coord3d(4,5,6));");
            (void)airfix::assets::parseMissionStartPositions(invalid);
        },
        "out-of-range number");
    requireParseError(
        MissionSetupParseErrorCode::malformedText,
        [&] {
            const auto invalid = bytes("/* unterminated");
            (void)airfix::assets::parseMissionStartPositions(invalid);
        },
        "unterminated comment");

    std::string nulInComment{"/*"};
    nulInComment.push_back('\0');
    nulInComment += "*/";
    std::string escapedNul{"\"\\", 2U};
    escapedNul.push_back('\0');
    escapedNul += "\"";
    for (const auto& embedded : {nulInComment, escapedNul}) {
        requireParseError(
            MissionSetupParseErrorCode::malformedText,
            [&] {
                (void)airfix::assets::parseMissionStartPositions(
                    bytes(embedded));
            },
            "embedded NUL");
    }
}

void testRoomResolutionAndSelection() {
    CcfMetadata ccf;
    ccf.rooms = {
        room("root", true),
        room("hangar"),
        room("outside"),
    };
    const std::vector<MissionStartPosition> starts{
        {.roomName = "outside"},
        {.roomName = "hangar"},
    };

    const auto resolution =
        airfix::assets::resolveMissionStartRoomsInCcf(starts, ccf);
    require(resolution.complete(), "valid room resolution is incomplete");
    require(
        resolution.physicalRoomCount == 3U &&
        resolution.primaryPhysicalRoomIndex == 0U &&
        resolution.starts ==
            std::vector<airfix::assets::ResolvedMissionStartPosition>{
                {0U, 2U}, {1U, 1U}},
        "room resolution did not preserve physical/start order");

    const auto first = airfix::assets::selectMissionStart(resolution, 0U);
    const auto wrapped = airfix::assets::selectMissionStart(resolution, 3U);
    const auto maximum = airfix::assets::selectMissionStart(
        resolution, std::numeric_limits<std::uint32_t>::max());
    require(
        first.has_value() &&
        first->source == MissionStartSelectionSource::table &&
        first->startPositionIndex == 0U &&
        first->physicalRoomIndex == 2U,
        "selector zero returned the wrong start");
    require(
        wrapped.has_value() &&
        wrapped->startPositionIndex == 1U &&
        wrapped->physicalRoomIndex == 1U,
        "selector did not wrap modulo the start count");
    require(
        maximum.has_value() &&
        maximum->startPositionIndex == 1U,
        "maximum selector did not use unsigned modulo");

    const auto empty =
        airfix::assets::resolveMissionStartRoomsInCcf({}, ccf);
    const auto fallback = airfix::assets::selectMissionStart(empty, 99U);
    require(
        fallback.has_value() &&
        fallback->source ==
            MissionStartSelectionSource::primaryRoomFallback &&
        !fallback->startPositionIndex.has_value() &&
        fallback->physicalRoomIndex == 0U,
        "empty start table did not select the primary room fallback");
}

void testRoomResolutionFailsClosed() {
    const std::vector<MissionStartPosition> starts{
        {.roomName = "duplicate"},
    };
    CcfMetadata duplicate;
    duplicate.rooms = {
        room("root", true),
        room("duplicate"),
        room("duplicate"),
    };
    const auto ambiguous =
        airfix::assets::resolveMissionStartRoomsInCcf(starts, duplicate);
    require(
        !ambiguous.complete() && ambiguous.starts.empty() &&
        ambiguous.issues.size() == 1U &&
        ambiguous.issues[0].kind ==
            MissionStartRoomIssueKind::ambiguousStartRoom &&
        ambiguous.issues[0].startPositionIndex == 0U &&
        !airfix::assets::selectMissionStart(ambiguous, 0U).has_value(),
        "ambiguous room resolution did not fail closed");

    CcfMetadata missing;
    missing.rooms = {room("root", true)};
    const auto notFound =
        airfix::assets::resolveMissionStartRoomsInCcf(starts, missing);
    require(
        !notFound.complete() && notFound.starts.empty() &&
        notFound.issues.size() == 1U &&
        notFound.issues[0].kind ==
            MissionStartRoomIssueKind::missingStartRoom,
        "missing room resolution returned the wrong issue");

    CcfMetadata noPrimary;
    noPrimary.rooms = {room("duplicate")};
    const auto missingPrimary =
        airfix::assets::resolveMissionStartRoomsInCcf({}, noPrimary);
    require(
        !missingPrimary.complete() &&
        missingPrimary.issues.size() == 1U &&
        missingPrimary.issues[0].kind ==
            MissionStartRoomIssueKind::missingPrimaryRoom,
        "missing primary room was accepted");

    CcfMetadata twoPrimary;
    twoPrimary.rooms = {
        room("a", true),
        room("b", true),
    };
    const auto ambiguousPrimary =
        airfix::assets::resolveMissionStartRoomsInCcf({}, twoPrimary);
    require(
        !ambiguousPrimary.complete() &&
        ambiguousPrimary.issues.size() == 1U &&
        ambiguousPrimary.issues[0].kind ==
            MissionStartRoomIssueKind::ambiguousPrimaryRoom,
        "ambiguous primary room was accepted");

    const std::vector<MissionStartPosition> tooMany(
        airfix::assets::legacyMissionStartCapacity + 1U,
        MissionStartPosition{.roomName = "a"});
    const auto limited =
        airfix::assets::resolveMissionStartRoomsInCcf(tooMany, twoPrimary);
    require(
        !limited.complete() && limited.starts.empty() &&
        limited.issues.front().kind ==
            MissionStartRoomIssueKind::startPositionLimitExceeded,
        "oversized constructed start table was accepted");

    airfix::assets::MissionStartRoomResolution forged{
        .physicalRoomCount = 1U,
        .primaryPhysicalRoomIndex = 0U,
        .starts = {{0U, 1U}},
    };
    require(
        !airfix::assets::selectMissionStart(forged, 0U).has_value(),
        "selector accepted an out-of-range physical room");

    forged.starts = {{1U, 0U}};
    require(
        !airfix::assets::selectMissionStart(forged, 0U).has_value(),
        "selector accepted a non-canonical start index");
}

static_assert(std::is_trivially_copyable_v<
    airfix::assets::MissionStartSelection>);

} // namespace

int main() {
    try {
        testParserFindsExactCallsOnly();
        testParserAcceptsNoStarts();
        testParserLimitsAndMalformedInput();
        testRoomResolutionAndSelection();
        testRoomResolutionFailsClosed();
        std::cout << "Mission setup tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mission setup tests failed: " << error.what() << '\n';
        return 1;
    }
}
