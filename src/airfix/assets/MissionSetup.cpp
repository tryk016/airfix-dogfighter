#include "airfix/assets/MissionSetup.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <locale>
#include <sstream>
#include <string_view>
#include <utility>

namespace airfix::assets {
namespace {

[[nodiscard]] constexpr bool isWhitespace(const std::uint8_t value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
        value == '\f' || value == '\v';
}

[[nodiscard]] constexpr bool isIdentifierStart(
    const std::uint8_t value) noexcept {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') || value == '_';
}

[[nodiscard]] constexpr bool isIdentifierPart(
    const std::uint8_t value) noexcept {
    return isIdentifierStart(value) || (value >= '0' && value <= '9');
}

class MissionSetupScanner final {
public:
    MissionSetupScanner(
        const std::span<const std::uint8_t> source,
        const MissionSetupParseLimits& limits)
        : source_(source),
          limits_(limits),
          effectivePositionLimit_(
              std::min(
                  limits.maximumStartPositions,
                  legacyMissionStartCapacity)) {
        starts_.reserve(effectivePositionLimit_);
    }

    [[nodiscard]] std::vector<MissionStartPosition> parse() {
        while (position_ < source_.size()) {
            skipTrivia();
            if (position_ >= source_.size()) {
                break;
            }
            if (source_[position_] == '"') {
                skipStringLiteral();
                continue;
            }
            if (!isIdentifierStart(source_[position_])) {
                if (source_[position_] == 0U) {
                    fail(
                        MissionSetupParseErrorCode::malformedText,
                        "AFS source contains an embedded NUL");
                }
                ++position_;
                continue;
            }

            const auto identifier = parseIdentifier();
            if (identifier == "AddStartPos") {
                parseStartPosition();
            }
        }
        return std::move(starts_);
    }

private:
    [[noreturn]] void fail(
        const MissionSetupParseErrorCode code,
        const char* const message,
        const std::size_t offset) const {
        throw MissionSetupParseError(
            code, static_cast<std::uint64_t>(offset), message);
    }

    [[noreturn]] void fail(
        const MissionSetupParseErrorCode code,
        const char* const message) const {
        fail(code, message, position_);
    }

    [[nodiscard]] bool has(const std::size_t count) const noexcept {
        return count <= source_.size() - position_;
    }

    void skipTrivia() {
        for (;;) {
            while (position_ < source_.size() &&
                   isWhitespace(source_[position_])) {
                ++position_;
            }
            if (!has(2U) || source_[position_] != '/') {
                return;
            }
            if (source_[position_ + 1U] == '/') {
                position_ += 2U;
                while (position_ < source_.size() &&
                       source_[position_] != '\n') {
                    ++position_;
                }
                continue;
            }
            if (source_[position_ + 1U] != '*') {
                return;
            }

            const auto commentOffset = position_;
            position_ += 2U;
            bool closed = false;
            while (has(2U)) {
                if (source_[position_] == '*' &&
                    source_[position_ + 1U] == '/') {
                    position_ += 2U;
                    closed = true;
                    break;
                }
                ++position_;
            }
            if (!closed) {
                fail(
                    MissionSetupParseErrorCode::malformedText,
                    "unterminated AFS block comment",
                    commentOffset);
            }
        }
    }

    [[nodiscard]] std::string_view parseIdentifier() {
        const auto begin = position_;
        ++position_;
        while (position_ < source_.size() &&
               isIdentifierPart(source_[position_])) {
            ++position_;
        }
        return {
            reinterpret_cast<const char*>(source_.data() + begin),
            position_ - begin};
    }

    void skipStringLiteral() {
        const auto stringOffset = position_;
        ++position_;
        while (position_ < source_.size()) {
            const auto value = source_[position_++];
            if (value == '"') {
                return;
            }
            if (value == '\r' || value == '\n' || value == 0U) {
                fail(
                    MissionSetupParseErrorCode::malformedText,
                    "unterminated AFS string literal",
                    stringOffset);
            }
            if (value == '\\') {
                if (position_ >= source_.size()) {
                    break;
                }
                ++position_;
            }
        }
        fail(
            MissionSetupParseErrorCode::malformedText,
            "unterminated AFS string literal",
            stringOffset);
    }

    void expect(const std::uint8_t expected, const char* const message) {
        skipTrivia();
        if (position_ >= source_.size() || source_[position_] != expected) {
            fail(MissionSetupParseErrorCode::malformedText, message);
        }
        ++position_;
    }

    [[nodiscard]] std::string parseRoomName() {
        skipTrivia();
        const auto stringOffset = position_;
        if (position_ >= source_.size() || source_[position_] != '"') {
            fail(
                MissionSetupParseErrorCode::malformedText,
                "AddStartPos room name must be a string");
        }
        ++position_;

        std::string result;
        result.reserve(std::min<std::size_t>(
            limits_.maximumRoomNameBytes, 64U));
        while (position_ < source_.size()) {
            auto value = source_[position_++];
            if (value == '"') {
                return result;
            }
            if (value == '\r' || value == '\n' || value == 0U) {
                fail(
                    MissionSetupParseErrorCode::malformedText,
                    "unterminated AddStartPos room name",
                    stringOffset);
            }
            if (value == '\\') {
                if (position_ >= source_.size()) {
                    fail(
                        MissionSetupParseErrorCode::malformedText,
                        "unterminated AddStartPos room-name escape",
                        stringOffset);
                }
                const auto escaped = source_[position_++];
                switch (escaped) {
                case '\\':
                case '"':
                    value = escaped;
                    break;
                case 'n':
                    value = '\n';
                    break;
                case 'r':
                    value = '\r';
                    break;
                case 't':
                    value = '\t';
                    break;
                default:
                    fail(
                        MissionSetupParseErrorCode::malformedText,
                        "unsupported AddStartPos room-name escape",
                        position_ - 2U);
                }
            }
            if (result.size() >= limits_.maximumRoomNameBytes) {
                fail(
                    MissionSetupParseErrorCode::roomNameLimitExceeded,
                    "AddStartPos room name exceeds its byte limit",
                    stringOffset);
            }
            result.push_back(static_cast<char>(value));
        }
        fail(
            MissionSetupParseErrorCode::malformedText,
            "unterminated AddStartPos room name",
            stringOffset);
    }

    [[nodiscard]] float parseNumber() {
        skipTrivia();
        const auto begin = position_;
        if (position_ < source_.size() &&
            (source_[position_] == '+' || source_[position_] == '-')) {
            ++position_;
        }

        bool hasDigits = false;
        while (position_ < source_.size() &&
               source_[position_] >= '0' && source_[position_] <= '9') {
            hasDigits = true;
            ++position_;
        }
        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            while (position_ < source_.size() &&
                   source_[position_] >= '0' && source_[position_] <= '9') {
                hasDigits = true;
                ++position_;
            }
        }
        if (!hasDigits) {
            fail(
                MissionSetupParseErrorCode::invalidNumber,
                "AddStartPos contains an invalid number",
                begin);
        }
        if (position_ < source_.size() &&
            (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (position_ < source_.size() &&
                (source_[position_] == '+' || source_[position_] == '-')) {
                ++position_;
            }
            const auto exponentBegin = position_;
            while (position_ < source_.size() &&
                   source_[position_] >= '0' && source_[position_] <= '9') {
                ++position_;
            }
            if (position_ == exponentBegin) {
                fail(
                    MissionSetupParseErrorCode::invalidNumber,
                    "AddStartPos contains an invalid exponent",
                    begin);
            }
        }

        std::string normalized{
            reinterpret_cast<const char*>(source_.data() + begin),
            position_ - begin};
        if (!normalized.empty() && normalized.front() == '+') {
            normalized.erase(normalized.begin());
        }
        if (!normalized.empty() && normalized.front() == '.') {
            normalized.insert(normalized.begin(), '0');
        } else if (normalized.size() >= 2U &&
                   normalized[0] == '-' && normalized[1] == '.') {
            normalized.insert(normalized.begin() + 1, '0');
        }
        float result{};
        std::istringstream conversion{normalized};
        conversion.imbue(std::locale::classic());
        conversion >> std::noskipws >> result;
        if (conversion.fail() ||
            conversion.peek() != std::char_traits<char>::eof() ||
            !std::isfinite(result)) {
            fail(
                MissionSetupParseErrorCode::invalidNumber,
                "AddStartPos number is not a finite float",
                begin);
        }
        return result;
    }

    [[nodiscard]] std::array<float, 3> parseCoord3d() {
        skipTrivia();
        if (position_ >= source_.size() ||
            !isIdentifierStart(source_[position_]) ||
            parseIdentifier() != "coord3d") {
            fail(
                MissionSetupParseErrorCode::malformedText,
                "AddStartPos expects coord3d");
        }
        expect('(', "coord3d is missing '('");
        std::array<float, 3> result{};
        result[0] = parseNumber();
        expect(',', "coord3d is missing its first comma");
        result[1] = parseNumber();
        expect(',', "coord3d is missing its second comma");
        result[2] = parseNumber();
        expect(')', "coord3d is missing ')'");
        return result;
    }

    void parseStartPosition() {
        const auto callOffset = position_ - std::string_view{
            "AddStartPos"}.size();
        if (starts_.size() >= effectivePositionLimit_) {
            fail(
                MissionSetupParseErrorCode::startPositionLimitExceeded,
                "AFS contains too many AddStartPos calls",
                callOffset);
        }

        expect('(', "AddStartPos is missing '('");
        auto roomName = parseRoomName();
        expect(',', "AddStartPos is missing its first comma");
        const auto position = parseCoord3d();
        expect(',', "AddStartPos is missing its second comma");
        const auto rotation = parseCoord3d();
        expect(')', "AddStartPos is missing ')'");
        expect(';', "AddStartPos is missing ';'");

        starts_.push_back({
            .roomName = std::move(roomName),
            .position = position,
            .axisRotation = rotation,
            .sourceOffset = static_cast<std::uint64_t>(callOffset),
        });
    }

    std::span<const std::uint8_t> source_;
    const MissionSetupParseLimits& limits_;
    std::size_t effectivePositionLimit_{};
    std::size_t position_{};
    std::vector<MissionStartPosition> starts_;
};

} // namespace

MissionSetupParseError::MissionSetupParseError(
    const MissionSetupParseErrorCode code,
    const std::uint64_t offset,
    const char* const message)
    : std::runtime_error(message), code_(code), offset_(offset) {}

std::vector<MissionStartPosition> parseMissionStartPositions(
    const std::span<const std::uint8_t> source,
    const MissionSetupParseLimits& limits) {
    if (source.size() > limits.maximumSourceBytes) {
        throw MissionSetupParseError(
            MissionSetupParseErrorCode::sourceLimitExceeded,
            static_cast<std::uint64_t>(limits.maximumSourceBytes),
            "AFS source exceeds its byte limit");
    }
    const auto nul = std::find(source.begin(), source.end(), 0U);
    if (nul != source.end()) {
        throw MissionSetupParseError(
            MissionSetupParseErrorCode::malformedText,
            static_cast<std::uint64_t>(
                std::distance(source.begin(), nul)),
            "AFS source contains an embedded NUL");
    }
    MissionSetupScanner scanner(source, limits);
    return scanner.parse();
}

MissionStartRoomResolution resolveMissionStartRoomsInCcf(
    const std::span<const MissionStartPosition> starts,
    const CcfMetadata& ccf) {
    MissionStartRoomResolution result;
    result.physicalRoomCount = ccf.rooms.size();
    if (starts.size() <= legacyMissionStartCapacity) {
        result.starts.reserve(starts.size());
    } else {
        result.issues.push_back({
            .kind =
                MissionStartRoomIssueKind::startPositionLimitExceeded,
            .startPositionIndex = legacyMissionStartCapacity,
        });
    }

    std::size_t primaryCount = 0U;
    for (std::size_t index = 0U; index < ccf.rooms.size(); ++index) {
        if (ccf.rooms[index].primaryBinding) {
            ++primaryCount;
            result.primaryPhysicalRoomIndex = index;
        }
    }
    if (primaryCount == 0U) {
        result.issues.push_back({
            .kind = MissionStartRoomIssueKind::missingPrimaryRoom,
            .startPositionIndex = std::nullopt,
        });
        result.primaryPhysicalRoomIndex.reset();
    } else if (primaryCount != 1U) {
        result.issues.push_back({
            .kind = MissionStartRoomIssueKind::ambiguousPrimaryRoom,
            .startPositionIndex = std::nullopt,
        });
        result.primaryPhysicalRoomIndex.reset();
    }

    for (std::size_t startIndex = 0U;
         startIndex < starts.size() &&
         starts.size() <= legacyMissionStartCapacity;
         ++startIndex) {
        std::size_t matchCount = 0U;
        std::size_t matchedRoomIndex = 0U;
        for (std::size_t roomIndex = 0U;
             roomIndex < ccf.rooms.size();
             ++roomIndex) {
            if (ccf.rooms[roomIndex].name == starts[startIndex].roomName) {
                ++matchCount;
                matchedRoomIndex = roomIndex;
            }
        }
        if (matchCount == 1U) {
            result.starts.push_back({
                .startPositionIndex = startIndex,
                .physicalRoomIndex = matchedRoomIndex,
            });
        } else {
            result.issues.push_back({
                .kind = matchCount == 0U
                    ? MissionStartRoomIssueKind::missingStartRoom
                    : MissionStartRoomIssueKind::ambiguousStartRoom,
                .startPositionIndex = startIndex,
            });
        }
    }

    if (!result.issues.empty()) {
        result.starts.clear();
    }
    return result;
}

std::optional<MissionStartSelection> selectMissionStart(
    const MissionStartRoomResolution& resolution,
    const std::uint32_t requestedIndex) noexcept {
    if (!resolution.complete()) {
        return std::nullopt;
    }
    if (*resolution.primaryPhysicalRoomIndex >=
            resolution.physicalRoomCount ||
        resolution.starts.size() > legacyMissionStartCapacity) {
        return std::nullopt;
    }
    for (std::size_t index = 0U;
         index < resolution.starts.size();
         ++index) {
        const auto& start = resolution.starts[index];
        if (start.startPositionIndex != index ||
            start.physicalRoomIndex >= resolution.physicalRoomCount) {
            return std::nullopt;
        }
    }
    if (resolution.starts.empty()) {
        return MissionStartSelection{
            .source = MissionStartSelectionSource::primaryRoomFallback,
            .startPositionIndex = std::nullopt,
            .physicalRoomIndex =
                *resolution.primaryPhysicalRoomIndex,
        };
    }

    const auto selected =
        static_cast<std::size_t>(requestedIndex) % resolution.starts.size();
    return MissionStartSelection{
        .source = MissionStartSelectionSource::table,
        .startPositionIndex =
            resolution.starts[selected].startPositionIndex,
        .physicalRoomIndex =
            resolution.starts[selected].physicalRoomIndex,
    };
}

} // namespace airfix::assets
