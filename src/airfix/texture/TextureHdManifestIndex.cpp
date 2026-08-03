#include "airfix/texture/TextureHdManifestIndex.hpp"

#include "airfix/archive/UdspArchive.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace airfix::texture {
namespace {

struct ManifestFault {
    TextureHdManifestIssueKind kind{};
};

[[noreturn]] void fail(const TextureHdManifestIssueKind kind) {
    throw ManifestFault{kind};
}

struct JsonInteger {
    bool negative{};
    std::uint64_t magnitude{};
};

struct JsonValue {
    enum class Type : std::uint8_t {
        nullValue,
        string,
        integer,
        number,
        boolean,
        array,
        object,
    };

    Type type{Type::nullValue};
    std::string stringValue;
    JsonInteger integerValue;
    bool boolValue{};
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::string, JsonValue>> objectValue;
};

class JsonParser final {
public:
    JsonParser(
        const std::span<const std::uint8_t> bytes,
        const TextureHdManifestLimits& limits)
        : input_(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
          limits_(limits) {}

    [[nodiscard]] JsonValue parse() {
        skipWhitespace();
        const auto result = parseValue(1U);
        skipWhitespace();
        if (cursor_ != input_.size()) {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
        return result;
    }

private:
    void skipWhitespace() noexcept {
        while (cursor_ < input_.size()) {
            const auto value = input_[cursor_];
            if (value != ' ' && value != '\t' && value != '\r' &&
                value != '\n') {
                break;
            }
            ++cursor_;
        }
    }

    [[nodiscard]] bool consume(const char value) noexcept {
        if (cursor_ < input_.size() && input_[cursor_] == value) {
            ++cursor_;
            return true;
        }
        return false;
    }

    void require(const char value) {
        if (!consume(value)) {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
    }

    void accountItem() {
        if (itemCount_ >= limits_.maximumItemsPerLine) {
            fail(TextureHdManifestIssueKind::containerLimitExceeded);
        }
        ++itemCount_;
    }

    void appendByte(std::string& output, const char byte) {
        if (output.size() >= limits_.maximumStringBytes ||
            totalStringBytes_ >= limits_.maximumTotalStringBytesPerLine) {
            fail(TextureHdManifestIssueKind::containerLimitExceeded);
        }
        ++totalStringBytes_;
        output.push_back(byte);
    }

    void appendCodePoint(std::string& output, const std::uint32_t codePoint) {
        if (codePoint <= 0x7FU) {
            appendByte(output, static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FFU) {
            appendByte(output, static_cast<char>(0xC0U | (codePoint >> 6U)));
            appendByte(
                output,
                static_cast<char>(0x80U | (codePoint & 0x3FU)));
        } else if (codePoint <= 0xFFFFU) {
            appendByte(output, static_cast<char>(0xE0U | (codePoint >> 12U)));
            appendByte(
                output,
                static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            appendByte(
                output,
                static_cast<char>(0x80U | (codePoint & 0x3FU)));
        } else {
            appendByte(output, static_cast<char>(0xF0U | (codePoint >> 18U)));
            appendByte(
                output,
                static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
            appendByte(
                output,
                static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            appendByte(
                output,
                static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
    }

    [[nodiscard]] static int hexDigit(const char value) noexcept {
        if (value >= '0' && value <= '9') {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f') {
            return value - 'a' + 10;
        }
        if (value >= 'A' && value <= 'F') {
            return value - 'A' + 10;
        }
        return -1;
    }

    [[nodiscard]] std::uint16_t parseHexQuad() {
        if (input_.size() - cursor_ < 4U) {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
        std::uint16_t value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const auto digit = hexDigit(input_[cursor_++]);
            if (digit < 0) {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            value = static_cast<std::uint16_t>(
                (value << 4U) | static_cast<std::uint16_t>(digit));
        }
        return value;
    }

    void parseUnicodeEscape(std::string& output) {
        const auto first = parseHexQuad();
        if (first >= 0xD800U && first <= 0xDBFFU) {
            if (input_.size() - cursor_ < 2U || input_[cursor_] != '\\' ||
                input_[cursor_ + 1U] != 'u') {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            cursor_ += 2U;
            const auto second = parseHexQuad();
            if (second < 0xDC00U || second > 0xDFFFU) {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            const auto codePoint =
                0x10000U +
                ((static_cast<std::uint32_t>(first) - 0xD800U) << 10U) +
                (static_cast<std::uint32_t>(second) - 0xDC00U);
            appendCodePoint(output, codePoint);
            return;
        }
        if (first >= 0xDC00U && first <= 0xDFFFU) {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
        appendCodePoint(output, first);
    }

    void parseRawUtf8(std::string& output) {
        const auto first = static_cast<std::uint8_t>(input_[cursor_]);
        std::size_t continuationCount = 0U;
        std::uint32_t codePoint = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            continuationCount = 1U;
            codePoint = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuationCount = 2U;
            codePoint = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuationCount = 3U;
            codePoint = first & 0x07U;
        } else {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
        if (continuationCount > input_.size() - cursor_ - 1U) {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
        for (std::size_t index = 1U; index <= continuationCount; ++index) {
            const auto continuation =
                static_cast<std::uint8_t>(input_[cursor_ + index]);
            if ((continuation & 0xC0U) != 0x80U) {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            codePoint = (codePoint << 6U) | (continuation & 0x3FU);
        }
        const bool overlong =
            (continuationCount == 1U && codePoint < 0x80U) ||
            (continuationCount == 2U && codePoint < 0x800U) ||
            (continuationCount == 3U && codePoint < 0x10000U);
        if (overlong || (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            codePoint > 0x10FFFFU) {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
        for (std::size_t index = 0U; index <= continuationCount; ++index) {
            appendByte(output, input_[cursor_ + index]);
        }
        cursor_ += continuationCount + 1U;
    }

    [[nodiscard]] std::string parseString() {
        require('"');
        std::string output;
        while (cursor_ < input_.size()) {
            const auto byte = static_cast<std::uint8_t>(input_[cursor_]);
            if (byte == static_cast<std::uint8_t>('"')) {
                ++cursor_;
                return output;
            }
            if (byte == static_cast<std::uint8_t>('\\')) {
                ++cursor_;
                if (cursor_ == input_.size()) {
                    fail(TextureHdManifestIssueKind::malformedJson);
                }
                const auto escape = input_[cursor_++];
                switch (escape) {
                case '"': appendByte(output, '"'); break;
                case '\\': appendByte(output, '\\'); break;
                case '/': appendByte(output, '/'); break;
                case 'b': appendByte(output, '\b'); break;
                case 'f': appendByte(output, '\f'); break;
                case 'n': appendByte(output, '\n'); break;
                case 'r': appendByte(output, '\r'); break;
                case 't': appendByte(output, '\t'); break;
                case 'u': parseUnicodeEscape(output); break;
                default: fail(TextureHdManifestIssueKind::malformedJson);
                }
                continue;
            }
            if (byte < 0x20U) {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            if (byte <= 0x7FU) {
                appendByte(output, input_[cursor_++]);
            } else {
                parseRawUtf8(output);
            }
        }
        fail(TextureHdManifestIssueKind::malformedJson);
    }

    [[nodiscard]] JsonValue parseNumber() {
        const bool negative = consume('-');
        if (cursor_ == input_.size() || input_[cursor_] < '0' ||
            input_[cursor_] > '9') {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
        const auto integerBegin = cursor_;
        if (input_[cursor_] == '0') {
            ++cursor_;
            if (cursor_ < input_.size() && input_[cursor_] >= '0' &&
                input_[cursor_] <= '9') {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
        } else {
            while (cursor_ < input_.size() && input_[cursor_] >= '0' &&
                   input_[cursor_] <= '9') {
                ++cursor_;
            }
        }

        bool integer = true;
        if (consume('.')) {
            integer = false;
            if (cursor_ == input_.size() || input_[cursor_] < '0' ||
                input_[cursor_] > '9') {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            while (cursor_ < input_.size() && input_[cursor_] >= '0' &&
                   input_[cursor_] <= '9') {
                ++cursor_;
            }
        }
        if (cursor_ < input_.size() &&
            (input_[cursor_] == 'e' || input_[cursor_] == 'E')) {
            integer = false;
            ++cursor_;
            if (cursor_ < input_.size() &&
                (input_[cursor_] == '+' || input_[cursor_] == '-')) {
                ++cursor_;
            }
            if (cursor_ == input_.size() || input_[cursor_] < '0' ||
                input_[cursor_] > '9') {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            while (cursor_ < input_.size() && input_[cursor_] >= '0' &&
                   input_[cursor_] <= '9') {
                ++cursor_;
            }
        }

        JsonValue result;
        if (!integer) {
            result.type = JsonValue::Type::number;
            return result;
        }

        result.type = JsonValue::Type::integer;
        result.integerValue.negative = negative;
        for (std::size_t index = integerBegin; index < cursor_; ++index) {
            const auto digit =
                static_cast<std::uint64_t>(input_[index] - '0');
            if (result.integerValue.magnitude >
                (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            result.integerValue.magnitude =
                result.integerValue.magnitude * 10U + digit;
        }
        return result;
    }

    [[nodiscard]] JsonValue parseArray(const std::size_t depth) {
        JsonValue result;
        result.type = JsonValue::Type::array;
        require('[');
        skipWhitespace();
        if (consume(']')) {
            return result;
        }
        while (true) {
            accountItem();
            result.arrayValue.push_back(parseValue(depth + 1U));
            skipWhitespace();
            if (consume(']')) {
                return result;
            }
            require(',');
            skipWhitespace();
        }
    }

    [[nodiscard]] JsonValue parseObject(const std::size_t depth) {
        JsonValue result;
        result.type = JsonValue::Type::object;
        require('{');
        skipWhitespace();
        if (consume('}')) {
            return result;
        }
        std::set<std::string> keys;
        while (true) {
            accountItem();
            if (cursor_ == input_.size() || input_[cursor_] != '"') {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            auto key = parseString();
            if (!keys.insert(key).second) {
                fail(TextureHdManifestIssueKind::malformedJson);
            }
            skipWhitespace();
            require(':');
            skipWhitespace();
            auto value = parseValue(depth + 1U);
            result.objectValue.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            if (consume('}')) {
                return result;
            }
            require(',');
            skipWhitespace();
        }
    }

    [[nodiscard]] JsonValue parseValue(const std::size_t depth) {
        if (depth > limits_.maximumDepth) {
            fail(TextureHdManifestIssueKind::containerLimitExceeded);
        }
        if (cursor_ == input_.size()) {
            fail(TextureHdManifestIssueKind::malformedJson);
        }
        const auto leading = input_[cursor_];
        if (leading == '"') {
            JsonValue result;
            result.type = JsonValue::Type::string;
            result.stringValue = parseString();
            return result;
        }
        if (leading == '{') {
            return parseObject(depth);
        }
        if (leading == '[') {
            return parseArray(depth);
        }
        if (leading == '-' || (leading >= '0' && leading <= '9')) {
            return parseNumber();
        }
        if (input_.substr(cursor_, 4U) == "true") {
            cursor_ += 4U;
            JsonValue result;
            result.type = JsonValue::Type::boolean;
            result.boolValue = true;
            return result;
        }
        if (input_.substr(cursor_, 5U) == "false") {
            cursor_ += 5U;
            JsonValue result;
            result.type = JsonValue::Type::boolean;
            result.boolValue = false;
            return result;
        }
        if (input_.substr(cursor_, 4U) == "null") {
            cursor_ += 4U;
            return {};
        }
        fail(TextureHdManifestIssueKind::malformedJson);
    }

    std::string_view input_;
    const TextureHdManifestLimits& limits_;
    std::size_t cursor_{};
    std::size_t itemCount_{};
    std::size_t totalStringBytes_{};
};

[[nodiscard]] const JsonValue* optionalField(
    const JsonValue& object,
    const std::string_view name) noexcept {
    if (object.type != JsonValue::Type::object) {
        return nullptr;
    }
    const auto found = std::find_if(
        object.objectValue.begin(),
        object.objectValue.end(),
        [name](const auto& item) { return item.first == name; });
    return found == object.objectValue.end() ? nullptr : &found->second;
}

[[nodiscard]] const JsonValue& field(
    const JsonValue& object,
    const std::string_view name,
    const TextureHdManifestIssueKind issue) {
    const auto* value = optionalField(object, name);
    if (value == nullptr) {
        fail(issue);
    }
    return *value;
}

void requireObject(
    const JsonValue& value,
    const TextureHdManifestIssueKind issue) {
    if (value.type != JsonValue::Type::object) {
        fail(issue);
    }
}

void requireAllowedFields(
    const JsonValue& object,
    const std::initializer_list<std::string_view> allowed,
    const TextureHdManifestIssueKind issue) {
    requireObject(object, issue);
    for (const auto& item : object.objectValue) {
        const bool known = std::any_of(
            allowed.begin(),
            allowed.end(),
            [&](const std::string_view name) { return item.first == name; });
        if (!known) {
            fail(issue);
        }
    }
}

[[nodiscard]] const std::string& stringValue(
    const JsonValue& value,
    const TextureHdManifestIssueKind issue,
    const bool requireNonEmpty = true) {
    if (value.type != JsonValue::Type::string ||
        (requireNonEmpty && value.stringValue.empty())) {
        fail(issue);
    }
    return value.stringValue;
}

[[nodiscard]] std::uint64_t unsignedValue(
    const JsonValue& value,
    const TextureHdManifestIssueKind issue) {
    if (value.type != JsonValue::Type::integer || value.integerValue.negative) {
        fail(issue);
    }
    return value.integerValue.magnitude;
}

[[nodiscard]] std::size_t sizeValue(
    const JsonValue& value,
    const TextureHdManifestIssueKind issue) {
    const auto decoded = unsignedValue(value, issue);
    if (decoded > std::numeric_limits<std::size_t>::max()) {
        fail(issue);
    }
    return static_cast<std::size_t>(decoded);
}

[[nodiscard]] std::uint32_t u32Value(
    const JsonValue& value,
    const TextureHdManifestIssueKind issue) {
    const auto decoded = unsignedValue(value, issue);
    if (decoded > std::numeric_limits<std::uint32_t>::max()) {
        fail(issue);
    }
    return static_cast<std::uint32_t>(decoded);
}

[[nodiscard]] bool boolValue(
    const JsonValue& value,
    const TextureHdManifestIssueKind issue) {
    if (value.type != JsonValue::Type::boolean) {
        fail(issue);
    }
    return value.boolValue;
}

[[nodiscard]] crypto::Sha256Digest digestValue(const JsonValue& value) {
    const auto& text = stringValue(
        value,
        TextureHdManifestIssueKind::invalidDigest);
    if (text.size() != 64U) {
        fail(TextureHdManifestIssueKind::invalidDigest);
    }
    crypto::Sha256Digest digest{};
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        const auto high = text[index * 2U];
        const auto low = text[index * 2U + 1U];
        const auto digit = [](const char value) -> std::uint8_t {
            if (value >= '0' && value <= '9') {
                return static_cast<std::uint8_t>(value - '0');
            }
            if (value >= 'a' && value <= 'f') {
                return static_cast<std::uint8_t>(value - 'a' + 10);
            }
            fail(TextureHdManifestIssueKind::invalidDigest);
        };
        digest[index] = static_cast<std::uint8_t>(
            (digit(high) << 4U) | digit(low));
    }
    return digest;
}

[[nodiscard]] std::string normalizeIndexedLogicalPath(
    const std::string_view path,
    const std::size_t limit) {
    auto normalized = udsp::normalizeLogicalPath(path, limit);
    for (auto& character : normalized) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return normalized;
}

[[nodiscard]] std::string logicalPathValue(
    const JsonValue& value,
    const TextureHdManifestLimits& limits) {
    const auto& path = stringValue(
        value,
        TextureHdManifestIssueKind::invalidLogicalPath);
    try {
        return normalizeIndexedLogicalPath(
            path,
            limits.maximumLogicalPathBytes);
    } catch (const udsp::ParseError&) {
        fail(TextureHdManifestIssueKind::invalidLogicalPath);
    }
}

[[nodiscard]] std::string relativePathValue(
    const JsonValue& value,
    const TextureHdManifestLimits& limits) {
    const auto& path = stringValue(
        value,
        TextureHdManifestIssueKind::invalidRelativePath);
    try {
        auto normalized =
            udsp::normalizeLogicalPath(path, limits.maximumRelativePathBytes);
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        return normalized;
    } catch (const udsp::ParseError&) {
        fail(TextureHdManifestIssueKind::invalidRelativePath);
    }
}

enum class ReviewStatus : std::uint8_t {
    accepted,
    rejected,
    manualReview,
};

[[nodiscard]] ReviewStatus reviewStatusValue(
    const JsonValue& value,
    const TextureHdManifestIssueKind issue) {
    const auto& status = stringValue(value, issue);
    if (status == "accepted") {
        return ReviewStatus::accepted;
    }
    if (status == "rejected") {
        return ReviewStatus::rejected;
    }
    if (status == "manual-review") {
        return ReviewStatus::manualReview;
    }
    fail(issue);
}

[[nodiscard]] std::size_t statusIndex(const ReviewStatus status) noexcept {
    switch (status) {
    case ReviewStatus::accepted: return 0U;
    case ReviewStatus::rejected: return 1U;
    case ReviewStatus::manualReview: return 2U;
    }
    return 2U;
}

[[nodiscard]] TextureHdAlphaUsage alphaUsageValue(const JsonValue& value) {
    const auto& usage = stringValue(
        value,
        TextureHdManifestIssueKind::invalidRecord);
    if (usage == "opaque") {
        return TextureHdAlphaUsage::opaque;
    }
    if (usage == "binary") {
        return TextureHdAlphaUsage::binary;
    }
    if (usage == "translucent") {
        return TextureHdAlphaUsage::translucent;
    }
    fail(TextureHdManifestIssueKind::invalidRecord);
}

[[nodiscard]] bool validCategory(const std::string_view category) noexcept {
    constexpr std::array<std::string_view, 8U> categories{
        "world",
        "models",
        "aircraft",
        "ui-fonts",
        "effects",
        "atlases-animations",
        "technical",
        "mixed",
    };
    return std::find(categories.begin(), categories.end(), category) !=
        categories.end();
}

[[nodiscard]] std::uint32_t naturalMipCount(
    std::uint32_t width,
    std::uint32_t height) noexcept {
    std::uint32_t count = 1U;
    while (width != 1U || height != 1U) {
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
        ++count;
    }
    return count;
}

struct HeaderData {
    crypto::Sha256Digest sourceCorpusId{};
    crypto::Sha256Digest baseManifestSha256{};
    std::string reviewRunId;
    std::size_t uniqueResultCount{};
    std::size_t logicalTextureCount{};
    std::array<std::size_t, 3U> statusCounts{};
};

[[nodiscard]] HeaderData parseHeader(
    const JsonValue& root,
    const TextureHdManifestLimits& limits) {
    constexpr auto issue = TextureHdManifestIssueKind::invalidHeader;
    requireAllowedFields(
        root,
        {
            "record_type",
            "schema_version",
            "review_run_id",
            "source_corpus_id",
            "base_manifest_sha256",
            "base_pipeline_version",
            "override_sources",
            "unique_result_count",
            "logical_texture_count",
            "status_counts",
            "review_basis",
            "local_only",
        },
        issue);

    if (stringValue(field(root, "record_type", issue), issue) !=
        "hd-reviewed-corpus") {
        fail(issue);
    }
    if (u32Value(field(root, "schema_version", issue), issue) != 1U) {
        fail(TextureHdManifestIssueKind::unsupportedSchema);
    }

    HeaderData result;
    result.reviewRunId =
        stringValue(field(root, "review_run_id", issue), issue);
    result.sourceCorpusId = digestValue(field(root, "source_corpus_id", issue));
    result.baseManifestSha256 =
        digestValue(field(root, "base_manifest_sha256", issue));
    (void)stringValue(field(root, "base_pipeline_version", issue), issue);
    (void)stringValue(field(root, "review_basis", issue), issue);
    if (!boolValue(field(root, "local_only", issue), issue)) {
        fail(issue);
    }

    result.uniqueResultCount =
        sizeValue(field(root, "unique_result_count", issue), issue);
    result.logicalTextureCount =
        sizeValue(field(root, "logical_texture_count", issue), issue);
    if (result.uniqueResultCount > limits.maximumRecords) {
        fail(TextureHdManifestIssueKind::recordLimitExceeded);
    }
    if (result.logicalTextureCount > limits.maximumTotalLogicalPaths) {
        fail(TextureHdManifestIssueKind::containerLimitExceeded);
    }

    const auto& overrides = field(root, "override_sources", issue);
    if (overrides.type != JsonValue::Type::array) {
        fail(issue);
    }
    for (const auto& overrideSource : overrides.arrayValue) {
        requireAllowedFields(
            overrideSource,
            {"sha256", "pipeline_version", "result_count"},
            issue);
        (void)digestValue(field(overrideSource, "sha256", issue));
        (void)stringValue(
            field(overrideSource, "pipeline_version", issue),
            issue);
        if (sizeValue(field(overrideSource, "result_count", issue), issue) ==
            0U) {
            fail(issue);
        }
    }

    const auto& counts = field(root, "status_counts", issue);
    requireObject(counts, issue);
    std::size_t total = 0U;
    for (const auto& item : counts.objectValue) {
        const auto count = sizeValue(item.second, issue);
        if (count > std::numeric_limits<std::size_t>::max() - total) {
            fail(issue);
        }
        total += count;
        if (item.first == "accepted") {
            result.statusCounts[0U] = count;
        } else if (item.first == "rejected") {
            result.statusCounts[1U] = count;
        } else if (item.first == "manual-review") {
            result.statusCounts[2U] = count;
        } else if (count != 0U) {
            fail(issue);
        }
    }
    if (total != result.uniqueResultCount) {
        fail(TextureHdManifestIssueKind::countMismatch);
    }
    return result;
}

struct ParsedRecord {
    TextureHdManifestRecord record;
    ReviewStatus status{ReviewStatus::manualReview};
};

[[nodiscard]] ParsedRecord parseRecord(
    const JsonValue& root,
    const HeaderData& header,
    const TextureHdManifestLimits& limits) {
    constexpr auto issue = TextureHdManifestIssueKind::invalidRecord;
    requireAllowedFields(
        root,
        {
            "schema_version",
            "pipeline_version",
            "run_id",
            "source_corpus_id",
            "source_gti_sha256",
            "input_png_sha256",
            "output_png_sha256",
            "logical_paths",
            "categories",
            "category",
            "role",
            "method",
            "model",
            "model_provenance",
            "parameters",
            "source",
            "result",
            "qa",
            "status",
            "automated_status",
            "review",
        },
        issue);

    if (u32Value(field(root, "schema_version", issue), issue) != 1U) {
        fail(TextureHdManifestIssueKind::unsupportedSchema);
    }
    (void)stringValue(field(root, "pipeline_version", issue), issue);
    (void)stringValue(field(root, "run_id", issue), issue);
    if (digestValue(field(root, "source_corpus_id", issue)) !=
        header.sourceCorpusId) {
        fail(issue);
    }

    ParsedRecord parsed;
    auto& record = parsed.record;
    record.sourceGtiSha256 =
        digestValue(field(root, "source_gti_sha256", issue));
    (void)digestValue(field(root, "input_png_sha256", issue));
    record.outputPngSha256 =
        digestValue(field(root, "output_png_sha256", issue));

    const auto& logicalPaths = field(root, "logical_paths", issue);
    if (logicalPaths.type != JsonValue::Type::array ||
        logicalPaths.arrayValue.empty() ||
        logicalPaths.arrayValue.size() > limits.maximumLogicalPathsPerRecord) {
        fail(TextureHdManifestIssueKind::containerLimitExceeded);
    }
    std::set<std::string> recordPaths;
    record.logicalPaths.reserve(logicalPaths.arrayValue.size());
    for (const auto& value : logicalPaths.arrayValue) {
        auto path = logicalPathValue(value, limits);
        if (!recordPaths.insert(path).second) {
            fail(TextureHdManifestIssueKind::duplicateLogicalPath);
        }
        record.logicalPaths.push_back(std::move(path));
    }

    const auto& categories = field(root, "categories", issue);
    if (categories.type != JsonValue::Type::array ||
        categories.arrayValue.empty()) {
        fail(issue);
    }
    std::set<std::string> categorySet;
    for (const auto& value : categories.arrayValue) {
        const auto& category = stringValue(value, issue);
        if (!validCategory(category) || !categorySet.insert(category).second) {
            fail(issue);
        }
    }
    const auto& category = stringValue(field(root, "category", issue), issue);
    if (!validCategory(category) || !categorySet.contains(category)) {
        fail(issue);
    }
    (void)stringValue(field(root, "role", issue), issue);
    record.method = stringValue(field(root, "method", issue), issue);

    const auto& model = field(root, "model", issue);
    if (model.type != JsonValue::Type::nullValue &&
        model.type != JsonValue::Type::string) {
        fail(issue);
    }
    const auto& provenance = field(root, "model_provenance", issue);
    if (provenance.type != JsonValue::Type::nullValue &&
        provenance.type != JsonValue::Type::object) {
        fail(issue);
    }

    const auto& parameters = field(root, "parameters", issue);
    requireObject(parameters, issue);
    record.parameters.scale =
        u32Value(field(parameters, "scale", issue), issue);
    if (record.parameters.scale != 4U) {
        fail(issue);
    }
    const auto& edgeMode =
        stringValue(field(parameters, "edge_mode", issue), issue);
    if (edgeMode == "wrap") {
        record.parameters.edgeMode = TextureHdEdgeMode::wrap;
    } else if (edgeMode == "clamp") {
        record.parameters.edgeMode = TextureHdEdgeMode::clamp;
    } else {
        fail(issue);
    }
    record.parameters.rgbResampler = stringValue(
        field(parameters, "rgb_resampler", issue),
        issue,
        false);
    record.parameters.alphaResampler = stringValue(
        field(parameters, "alpha_resampler", issue),
        issue,
        false);
    record.parameters.transparentRgbExtension = boolValue(
        field(parameters, "transparent_rgb_extension", issue),
        issue);
    if (stringValue(field(parameters, "sample_space", issue), issue) !=
        "encoded-unclassified") {
        fail(issue);
    }

    const auto& source = field(root, "source", issue);
    requireObject(source, issue);
    record.source.width = u32Value(field(source, "width", issue), issue);
    record.source.height = u32Value(field(source, "height", issue), issue);
    record.alphaUsage = alphaUsageValue(field(source, "alpha_usage", issue));
    record.sourceMipCount =
        u32Value(field(source, "mipmaps", issue), issue);

    const auto& result = field(root, "result", issue);
    requireObject(result, issue);
    record.result.width = u32Value(field(result, "width", issue), issue);
    record.result.height = u32Value(field(result, "height", issue), issue);
    record.generatedMipCount =
        u32Value(field(result, "generated_mipmaps", issue), issue);
    record.baseTextureRelativePath =
        relativePathValue(field(result, "path", issue), limits);
    record.mipmapDirectoryRelativePath =
        relativePathValue(field(result, "mipmap_directory", issue), limits);
    (void)relativePathValue(
        field(result, "comparison_board", issue),
        limits);

    if (record.source.width == 0U || record.source.height == 0U ||
        record.source.width > limits.maximumDimension ||
        record.source.height > limits.maximumDimension ||
        record.result.width < 4U || record.result.height < 4U ||
        record.result.width > limits.maximumDimension ||
        record.result.height > limits.maximumDimension ||
        record.source.width > std::numeric_limits<std::uint32_t>::max() / 4U ||
        record.source.height >
            std::numeric_limits<std::uint32_t>::max() / 4U ||
        record.result.width != record.source.width * 4U ||
        record.result.height != record.source.height * 4U ||
        record.sourceMipCount == 0U ||
        record.sourceMipCount > limits.maximumMipLevels ||
        record.generatedMipCount == 0U ||
        record.generatedMipCount > limits.maximumMipLevels ||
        record.generatedMipCount !=
            naturalMipCount(record.result.width, record.result.height)) {
        fail(issue);
    }

    requireObject(field(root, "qa", issue), issue);
    parsed.status = reviewStatusValue(field(root, "status", issue), issue);
    if (const auto* automated = optionalField(root, "automated_status")) {
        (void)reviewStatusValue(*automated, issue);
    }

    const auto& review = field(root, "review", issue);
    requireAllowedFields(
        review,
        {"status", "basis", "review_run_id", "used_override"},
        issue);
    const auto reviewStatus =
        reviewStatusValue(field(review, "status", issue), issue);
    (void)stringValue(field(review, "basis", issue), issue);
    if (stringValue(field(review, "review_run_id", issue), issue) !=
        header.reviewRunId) {
        fail(issue);
    }
    (void)boolValue(field(review, "used_override", issue), issue);
    if (reviewStatus != parsed.status) {
        fail(issue);
    }
    return parsed;
}

struct BuildState {
    HeaderData header;
    bool hasHeader{};
    std::size_t recordsSeen{};
    std::size_t logicalPathsSeen{};
    std::array<std::size_t, 3U> actualStatusCounts{};
    std::vector<TextureHdManifestRecord> acceptedRecords;
    std::set<std::string> allLogicalPaths;
    std::set<crypto::Sha256Digest> allSourceDigests;
};

void acceptRecord(
    BuildState& state,
    ParsedRecord parsed,
    const TextureHdManifestLimits& limits) {
    if (state.recordsSeen >= limits.maximumRecords) {
        fail(TextureHdManifestIssueKind::recordLimitExceeded);
    }
    ++state.recordsSeen;
    if (parsed.record.logicalPaths.size() >
        limits.maximumTotalLogicalPaths - state.logicalPathsSeen) {
        fail(TextureHdManifestIssueKind::containerLimitExceeded);
    }
    state.logicalPathsSeen += parsed.record.logicalPaths.size();

    if (!state.allSourceDigests.insert(parsed.record.sourceGtiSha256).second) {
        fail(TextureHdManifestIssueKind::duplicateSourceDigest);
    }
    for (const auto& path : parsed.record.logicalPaths) {
        if (!state.allLogicalPaths.insert(path).second) {
            fail(TextureHdManifestIssueKind::duplicateLogicalPath);
        }
    }

    ++state.actualStatusCounts[statusIndex(parsed.status)];
    if (parsed.status == ReviewStatus::accepted) {
        state.acceptedRecords.push_back(std::move(parsed.record));
    }
}

[[nodiscard]] std::span<const std::uint8_t> lineSpan(
    const std::span<const std::uint8_t> bytes,
    const std::size_t begin,
    std::size_t end) noexcept {
    if (end > begin && bytes[end - 1U] == static_cast<std::uint8_t>('\r')) {
        --end;
    }
    return bytes.subspan(begin, end - begin);
}

} // namespace

const TextureHdManifestRecord* TextureHdManifestIndex::findByLogicalPath(
    const std::string_view logicalPath) const {
    std::string normalized;
    try {
        normalized =
            normalizeIndexedLogicalPath(logicalPath, logicalPathLimit_);
    } catch (const udsp::ParseError&) {
        return nullptr;
    }
    const auto found = std::lower_bound(
        logicalPaths_.begin(),
        logicalPaths_.end(),
        normalized,
        [](const LogicalPathEntry& entry, const std::string_view value) {
            return entry.normalizedPath < value;
        });
    if (found == logicalPaths_.end() || found->normalizedPath != normalized) {
        return nullptr;
    }
    return &records_[found->recordIndex];
}

const TextureHdManifestRecord*
TextureHdManifestIndex::findBySourceGtiSha256(
    const crypto::Sha256Digest& digest) const noexcept {
    const auto found = std::lower_bound(
        sourceDigests_.begin(),
        sourceDigests_.end(),
        digest,
        [](const DigestEntry& entry, const crypto::Sha256Digest& value) {
            return entry.digest < value;
        });
    if (found == sourceDigests_.end() || found->digest != digest) {
        return nullptr;
    }
    return &records_[found->recordIndex];
}

TextureHdManifestParseResult parseTextureHdManifest(
    const std::span<const std::uint8_t> bytes,
    const TextureHdManifestLimits& limits) {
    TextureHdManifestParseResult result;
    std::optional<std::size_t> activeLine;
    try {
        if (bytes.size() > limits.maximumInputBytes) {
            fail(TextureHdManifestIssueKind::inputLimitExceeded);
        }
        if (bytes.size() >= 3U && bytes[0U] == 0xEFU && bytes[1U] == 0xBBU &&
            bytes[2U] == 0xBFU) {
            activeLine = 1U;
            fail(TextureHdManifestIssueKind::malformedJson);
        }

        BuildState state;
        std::size_t begin = 0U;
        std::size_t lineNumber = 0U;
        while (begin < bytes.size()) {
            const auto newline = std::find(
                bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                bytes.end(),
                static_cast<std::uint8_t>('\n'));
            const auto end = static_cast<std::size_t>(newline - bytes.begin());
            ++lineNumber;
            activeLine = lineNumber;
            if (lineNumber > limits.maximumLines) {
                fail(TextureHdManifestIssueKind::lineCountLimitExceeded);
            }
            const auto line = lineSpan(bytes, begin, end);
            if (line.size() > limits.maximumLineBytes) {
                fail(TextureHdManifestIssueKind::lineLimitExceeded);
            }
            if (line.empty()) {
                fail(TextureHdManifestIssueKind::malformedJson);
            }

            const auto root = JsonParser(line, limits).parse();
            if (!state.hasHeader) {
                state.header = parseHeader(root, limits);
                state.acceptedRecords.reserve(state.header.statusCounts[0U]);
                state.hasHeader = true;
            } else {
                acceptRecord(
                    state,
                    parseRecord(root, state.header, limits),
                    limits);
            }
            begin = newline == bytes.end() ? bytes.size() : end + 1U;
        }

        activeLine.reset();
        if (!state.hasHeader) {
            fail(TextureHdManifestIssueKind::invalidHeader);
        }
        if (state.recordsSeen != state.header.uniqueResultCount ||
            state.logicalPathsSeen != state.header.logicalTextureCount ||
            state.actualStatusCounts != state.header.statusCounts) {
            fail(TextureHdManifestIssueKind::countMismatch);
        }

        TextureHdManifestIndex index;
        index.summary_ = {
            .schemaVersion = 1U,
            .scale = 4U,
            .manifestSha256 = crypto::sha256(bytes),
            .sourceCorpusId = state.header.sourceCorpusId,
            .baseManifestSha256 = state.header.baseManifestSha256,
            .declaredResultCount = state.header.uniqueResultCount,
            .declaredLogicalTextureCount = state.header.logicalTextureCount,
            .acceptedResultCount = state.acceptedRecords.size(),
        };
        index.records_ = std::move(state.acceptedRecords);
        index.logicalPathLimit_ = limits.maximumLogicalPathBytes;

        for (std::size_t recordIndex = 0U;
             recordIndex < index.records_.size();
             ++recordIndex) {
            const auto& record = index.records_[recordIndex];
            index.sourceDigests_.push_back({
                .digest = record.sourceGtiSha256,
                .recordIndex = recordIndex,
            });
            for (const auto& path : record.logicalPaths) {
                index.logicalPaths_.push_back({
                    .normalizedPath = path,
                    .recordIndex = recordIndex,
                });
            }
        }
        std::sort(
            index.logicalPaths_.begin(),
            index.logicalPaths_.end(),
            [](const auto& left, const auto& right) {
                return left.normalizedPath < right.normalizedPath;
            });
        std::sort(
            index.sourceDigests_.begin(),
            index.sourceDigests_.end(),
            [](const auto& left, const auto& right) {
                return left.digest < right.digest;
            });
        result.index = std::move(index);
    } catch (const ManifestFault& fault) {
        result.index.reset();
        result.issues.push_back({
            .kind = fault.kind,
            .lineNumber = activeLine,
        });
    }
    return result;
}

} // namespace airfix::texture
