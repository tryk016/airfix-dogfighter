#include "airfix/package/AfPackManifest.hpp"

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace airfix::afpack {
namespace {

struct JsonInteger {
    bool negative{};
    std::uint64_t magnitude{};
};

struct JsonValue {
    enum class Type {
        string,
        integer,
        boolean,
        array,
        object,
    };

    Type type{};
    std::string stringValue;
    JsonInteger integerValue;
    bool boolValue{};
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::string, JsonValue>> objectValue;
};

class JsonParser final {
public:
    JsonParser(const std::span<const std::uint8_t> bytes, const ManifestLimits& limits)
        : input_(reinterpret_cast<const char*>(bytes.data()), bytes.size()), limits_(limits) {}

    [[nodiscard]] JsonValue parse() {
        skipWhitespace();
        const auto result = parseValue(1U);
        skipWhitespace();
        if (cursor_ != input_.size()) {
            fail("trailing bytes after JSON value");
        }
        return result;
    }

private:
    [[noreturn]] void fail(const std::string_view message) const {
        throw ManifestError("manifest JSON: " + std::string(message));
    }

    void skipWhitespace() noexcept {
        while (cursor_ < input_.size()) {
            const auto value = input_[cursor_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
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

    void require(const char value, const std::string_view message) {
        if (!consume(value)) {
            fail(message);
        }
    }

    void accountItem() {
        if (itemCount_ >= limits_.maxItems) {
            fail("container item limit exceeded");
        }
        ++itemCount_;
    }

    void appendByte(std::string& output, const char byte) {
        if (output.size() >= limits_.maxStringBytes ||
            totalStringBytes_ >= limits_.maxTotalStringBytes) {
            fail("string byte limit exceeded");
        }
        ++totalStringBytes_;
        output.push_back(byte);
    }

    void appendCodePoint(std::string& output, const std::uint32_t codePoint) {
        if (codePoint <= 0x7FU) {
            appendByte(output, static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7FFU) {
            appendByte(output, static_cast<char>(0xC0U | (codePoint >> 6U)));
            appendByte(output, static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        else if (codePoint <= 0xFFFFU) {
            appendByte(output, static_cast<char>(0xE0U | (codePoint >> 12U)));
            appendByte(output, static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            appendByte(output, static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        else {
            appendByte(output, static_cast<char>(0xF0U | (codePoint >> 18U)));
            appendByte(output, static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
            appendByte(output, static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            appendByte(output, static_cast<char>(0x80U | (codePoint & 0x3FU)));
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
            fail("truncated Unicode escape");
        }
        std::uint16_t value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const auto digit = hexDigit(input_[cursor_++]);
            if (digit < 0) {
                fail("invalid Unicode escape");
            }
            value = static_cast<std::uint16_t>((value << 4U) |
                static_cast<std::uint16_t>(digit));
        }
        return value;
    }

    void parseUnicodeEscape(std::string& output) {
        const auto first = parseHexQuad();
        if (first >= 0xD800U && first <= 0xDBFFU) {
            if (input_.size() - cursor_ < 2U || input_[cursor_] != '\\' ||
                input_[cursor_ + 1U] != 'u') {
                fail("high surrogate is not followed by a low surrogate");
            }
            cursor_ += 2U;
            const auto second = parseHexQuad();
            if (second < 0xDC00U || second > 0xDFFFU) {
                fail("invalid low surrogate");
            }
            const auto codePoint = 0x10000U +
                ((static_cast<std::uint32_t>(first) - 0xD800U) << 10U) +
                (static_cast<std::uint32_t>(second) - 0xDC00U);
            appendCodePoint(output, codePoint);
            return;
        }
        if (first >= 0xDC00U && first <= 0xDFFFU) {
            fail("unpaired low surrogate");
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
        }
        else if (first >= 0xE0U && first <= 0xEFU) {
            continuationCount = 2U;
            codePoint = first & 0x0FU;
        }
        else if (first >= 0xF0U && first <= 0xF4U) {
            continuationCount = 3U;
            codePoint = first & 0x07U;
        }
        else {
            fail("invalid UTF-8 leading byte");
        }
        if (continuationCount > input_.size() - cursor_ - 1U) {
            fail("truncated UTF-8 sequence");
        }
        for (std::size_t index = 1U; index <= continuationCount; ++index) {
            const auto continuation = static_cast<std::uint8_t>(input_[cursor_ + index]);
            if ((continuation & 0xC0U) != 0x80U) {
                fail("invalid UTF-8 continuation byte");
            }
            codePoint = (codePoint << 6U) | (continuation & 0x3FU);
        }
        const bool overlong = (continuationCount == 1U && codePoint < 0x80U) ||
            (continuationCount == 2U && codePoint < 0x800U) ||
            (continuationCount == 3U && codePoint < 0x10000U);
        if (overlong || (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            codePoint > 0x10FFFFU) {
            fail("invalid UTF-8 code point");
        }
        for (std::size_t index = 0U; index <= continuationCount; ++index) {
            appendByte(output, input_[cursor_ + index]);
        }
        cursor_ += continuationCount + 1U;
    }

    [[nodiscard]] std::string parseString() {
        require('"', "expected string");
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
                    fail("truncated string escape");
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
                default: fail("invalid string escape");
                }
                continue;
            }
            if (byte < 0x20U) {
                fail("unescaped control byte in string");
            }
            if (byte <= 0x7FU) {
                appendByte(output, input_[cursor_++]);
            }
            else {
                parseRawUtf8(output);
            }
        }
        fail("unterminated string");
    }

    [[nodiscard]] JsonInteger parseInteger() {
        JsonInteger result;
        result.negative = consume('-');
        if (cursor_ == input_.size() || input_[cursor_] < '0' || input_[cursor_] > '9') {
            fail("invalid integer");
        }
        if (input_[cursor_] == '0' && cursor_ + 1U < input_.size() &&
            input_[cursor_ + 1U] >= '0' && input_[cursor_ + 1U] <= '9') {
            fail("integer has a leading zero");
        }
        while (cursor_ < input_.size() && input_[cursor_] >= '0' &&
            input_[cursor_] <= '9') {
            const auto digit = static_cast<std::uint64_t>(input_[cursor_] - '0');
            if (result.magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
                fail("integer overflows 64 bits");
            }
            result.magnitude = result.magnitude * 10U + digit;
            ++cursor_;
        }
        if (cursor_ < input_.size() && (input_[cursor_] == '.' || input_[cursor_] == 'e' ||
            input_[cursor_] == 'E')) {
            fail("non-integer JSON number is unsupported");
        }
        return result;
    }

    [[nodiscard]] JsonValue parseArray(const std::size_t depth) {
        JsonValue result;
        result.type = JsonValue::Type::array;
        require('[', "expected array");
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
            require(',', "expected comma in array");
            skipWhitespace();
        }
    }

    [[nodiscard]] JsonValue parseObject(const std::size_t depth) {
        JsonValue result;
        result.type = JsonValue::Type::object;
        require('{', "expected object");
        skipWhitespace();
        if (consume('}')) {
            return result;
        }
        std::set<std::string> keys;
        while (true) {
            accountItem();
            if (cursor_ == input_.size() || input_[cursor_] != '"') {
                fail("object key must be a string");
            }
            auto key = parseString();
            if (!keys.insert(key).second) {
                fail("duplicate object key");
            }
            skipWhitespace();
            require(':', "expected colon after object key");
            skipWhitespace();
            auto value = parseValue(depth + 1U);
            result.objectValue.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            if (consume('}')) {
                return result;
            }
            require(',', "expected comma in object");
            skipWhitespace();
        }
    }

    [[nodiscard]] JsonValue parseValue(const std::size_t depth) {
        if (depth > limits_.maxDepth) {
            fail("nesting depth limit exceeded");
        }
        if (cursor_ == input_.size()) {
            fail("expected JSON value");
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
            JsonValue result;
            result.type = JsonValue::Type::integer;
            result.integerValue = parseInteger();
            return result;
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
        fail("unsupported or malformed JSON value");
    }

    std::string_view input_;
    const ManifestLimits& limits_;
    std::size_t cursor_{};
    std::size_t itemCount_{};
    std::size_t totalStringBytes_{};
};

[[noreturn]] void schemaError(const std::string_view message) {
    throw ManifestError("manifest schema: " + std::string(message));
}

void requireType(
    const JsonValue& value,
    const JsonValue::Type expected,
    const std::string_view context) {
    if (value.type != expected) {
        schemaError(std::string(context) + " has the wrong JSON type");
    }
}

void requireFields(
    const JsonValue& value,
    const std::initializer_list<std::string_view> expected,
    const std::string_view context) {
    requireType(value, JsonValue::Type::object, context);
    for (const auto& field : value.objectValue) {
        if (std::find(expected.begin(), expected.end(), field.first) == expected.end()) {
            schemaError(std::string(context) + " has unknown field: " + field.first);
        }
    }
    for (const auto name : expected) {
        if (std::none_of(value.objectValue.begin(), value.objectValue.end(),
                [&](const auto& field) { return field.first == name; })) {
            schemaError(std::string(context) + " is missing field: " + std::string(name));
        }
    }
}

[[nodiscard]] const JsonValue& field(const JsonValue& object, const std::string_view name) {
    const auto found = std::find_if(
        object.objectValue.begin(), object.objectValue.end(),
        [&](const auto& candidate) { return candidate.first == name; });
    if (found == object.objectValue.end()) {
        schemaError("internal missing-field validation failure");
    }
    return found->second;
}

[[nodiscard]] const std::string& stringField(
    const JsonValue& object,
    const std::string_view name,
    const std::string_view context) {
    const auto& value = field(object, name);
    requireType(value, JsonValue::Type::string, context);
    return value.stringValue;
}

[[nodiscard]] std::uint64_t unsignedField(
    const JsonValue& object,
    const std::string_view name,
    const std::string_view context) {
    const auto& value = field(object, name);
    requireType(value, JsonValue::Type::integer, context);
    if (value.integerValue.negative) {
        schemaError(std::string(context) + " must be unsigned");
    }
    return value.integerValue.magnitude;
}

[[nodiscard]] bool boolField(
    const JsonValue& object,
    const std::string_view name,
    const std::string_view context) {
    const auto& value = field(object, name);
    requireType(value, JsonValue::Type::boolean, context);
    return value.boolValue;
}

[[nodiscard]] bool isVisibleAsciiToken(
    const std::string_view value,
    const std::size_t maximum) noexcept {
    if (value.empty() || value.size() > maximum) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) {
        const auto byte = static_cast<std::uint8_t>(character);
        return byte >= 0x21U && byte <= 0x7EU;
    });
}

[[nodiscard]] ManifestLocale parseLocale(const std::string_view locale) {
    if (locale == "da") return ManifestLocale::da;
    if (locale == "en") return ManifestLocale::en;
    if (locale == "no") return ManifestLocale::no;
    if (locale == "sv") return ManifestLocale::sv;
    schemaError("locale is unsupported");
}

[[nodiscard]] std::string localizationPath(const ManifestLocale locale) {
    switch (locale) {
    case ManifestLocale::da: return "localization/Dansk.up";
    case ManifestLocale::en: return "localization/English.up";
    case ManifestLocale::no: return "localization/Norsk.up";
    case ManifestLocale::sv: return "localization/Svenska.up";
    }
    schemaError("invalid typed locale");
}

[[nodiscard]] EntryKind parseKind(const std::string_view kind) {
    if (kind == "source-archive") return EntryKind::sourceArchive;
    if (kind == "localization") return EntryKind::localization;
    schemaError("entry kind is not allowed in manifest v1");
}

[[nodiscard]] std::array<std::uint8_t, 32> parseDigest(const std::string_view text) {
    if (text.size() != 64U) {
        schemaError("entry SHA-256 must contain 64 lowercase hexadecimal characters");
    }
    std::array<std::uint8_t, 32> result{};
    const auto nibble = [](const char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        return -1;
    };
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto high = nibble(text[index * 2U]);
        const auto low = nibble(text[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            schemaError("entry SHA-256 must contain 64 lowercase hexadecimal characters");
        }
        result[index] = static_cast<std::uint8_t>((high << 4U) | low);
    }
    return result;
}

void validateAllowlist(const Manifest& manifest) {
    if (manifest.entries.size() != 2U) {
        schemaError("manifest v1 requires exactly two content entries");
    }
    const auto expectedLocalization = localizationPath(manifest.locale);
    std::size_t sourceCount = 0U;
    std::size_t localizationCount = 0U;
    std::set<std::string> paths;
    for (const auto& entry : manifest.entries) {
        if (!paths.insert(entry.path).second) {
            schemaError("manifest contains a duplicate entry path");
        }
        if (entry.path == "manifest.json") {
            schemaError("manifest entry list must exclude manifest.json");
        }
        if (entry.path == "source/Resource.up" && entry.kind == EntryKind::sourceArchive) {
            ++sourceCount;
        }
        else if (entry.path == expectedLocalization &&
            entry.kind == EntryKind::localization) {
            ++localizationCount;
        }
        else {
            schemaError("entry is outside the manifest v1 allowlist");
        }
    }
    if (sourceCount != 1U || localizationCount != 1U) {
        schemaError("manifest v1 source/localization entry set is incomplete");
    }
}

void validateTable(
    const Manifest& manifest,
    const std::span<const Entry> packEntries) {
    if (packEntries.size() != 3U) {
        schemaError("pack table must contain exactly manifest and two v1 content entries");
    }
    std::set<std::string> tablePaths;
    std::size_t manifestCount = 0U;
    for (const auto& tableEntry : packEntries) {
        if (!tablePaths.insert(tableEntry.path).second) {
            schemaError("pack table contains a duplicate path");
        }
        if (tableEntry.path == "manifest.json" && tableEntry.kind == EntryKind::manifest) {
            ++manifestCount;
            continue;
        }
        const auto match = std::find_if(
            manifest.entries.begin(), manifest.entries.end(),
            [&](const ManifestEntry& entry) { return entry.path == tableEntry.path; });
        if (match == manifest.entries.end()) {
            schemaError("pack table contains an entry absent from the manifest");
        }
        if (match->kind != tableEntry.kind || match->contentSize != tableEntry.contentSize ||
            match->sha256 != tableEntry.sha256) {
            schemaError("manifest entry disagrees with the pack table");
        }
    }
    if (manifestCount != 1U) {
        schemaError("pack table must contain exactly one manifest.json manifest entry");
    }
    for (const auto& entry : manifest.entries) {
        if (tablePaths.find(entry.path) == tablePaths.end()) {
            schemaError("manifest entry is absent from the pack table");
        }
    }
}

} // namespace

std::string_view localeName(const ManifestLocale locale) noexcept {
    switch (locale) {
    case ManifestLocale::da: return "da";
    case ManifestLocale::en: return "en";
    case ManifestLocale::no: return "no";
    case ManifestLocale::sv: return "sv";
    }
    return {};
}

Manifest parseManifest(
    const std::span<const std::uint8_t> bytes,
    const std::span<const Entry> packEntries,
    const ManifestLimits& limits) {
    if (bytes.empty()) {
        throw ManifestError("manifest is empty");
    }
    if (bytes.size() > limits.maxInputBytes) {
        throw ManifestError("manifest exceeds configured input size limit");
    }
    if (limits.maxDepth == 0U || limits.maxStringBytes == 0U ||
        limits.maxTotalStringBytes == 0U || limits.maxItems == 0U ||
        limits.maxEntries < 2U) {
        throw ManifestError("manifest parser limits are unusable");
    }

    const auto root = JsonParser(bytes, limits).parse();
    requireFields(root,
        {"schema", "version", "game", "converter", "locale", "capabilities", "entries"},
        "root");

    Manifest manifest;
    manifest.schema = stringField(root, "schema", "schema");
    manifest.version = unsignedField(root, "version", "version");
    if (manifest.schema != "airfix.afpack.manifest" || manifest.version != 1U) {
        schemaError("unsupported schema or version");
    }

    const auto& game = field(root, "game");
    requireFields(game, {"id", "sourceVersion"}, "game");
    manifest.gameId = stringField(game, "id", "game.id");
    manifest.sourceVersion = stringField(game, "sourceVersion", "game.sourceVersion");
    if (manifest.gameId != "airfix-dogfighter" || manifest.sourceVersion != "1.01") {
        schemaError("unsupported game identity or source version");
    }

    const auto& converter = field(root, "converter");
    requireFields(converter, {"version", "commit"}, "converter");
    manifest.converterVersion = stringField(converter, "version", "converter.version");
    manifest.converterCommit = stringField(converter, "commit", "converter.commit");
    if (!isVisibleAsciiToken(manifest.converterVersion, 64U) ||
        !isVisibleAsciiToken(manifest.converterCommit, 128U)) {
        schemaError("converter version/commit must be bounded visible ASCII tokens");
    }

    manifest.locale = parseLocale(stringField(root, "locale", "locale"));

    const auto& capabilities = field(root, "capabilities");
    requireFields(capabilities, {"music", "multiplayer", "editors"}, "capabilities");
    manifest.capabilities = {
        .music = boolField(capabilities, "music", "capabilities.music"),
        .multiplayer = boolField(capabilities, "multiplayer", "capabilities.multiplayer"),
        .editors = boolField(capabilities, "editors", "capabilities.editors"),
    };
    if (manifest.capabilities.music || manifest.capabilities.multiplayer ||
        manifest.capabilities.editors) {
        schemaError("manifest v1 capabilities must all be false");
    }

    const auto& entries = field(root, "entries");
    requireType(entries, JsonValue::Type::array, "entries");
    if (entries.arrayValue.size() > limits.maxEntries) {
        schemaError("manifest entry count exceeds configured limit");
    }
    manifest.entries.reserve(entries.arrayValue.size());
    for (const auto& value : entries.arrayValue) {
        requireFields(value, {"path", "kind", "size", "sha256"}, "entry");
        ManifestEntry entry;
        entry.path = stringField(value, "path", "entry.path");
        entry.kind = parseKind(stringField(value, "kind", "entry.kind"));
        entry.contentSize = unsignedField(value, "size", "entry.size");
        entry.sha256 = parseDigest(stringField(value, "sha256", "entry.sha256"));
        manifest.entries.push_back(std::move(entry));
    }

    validateAllowlist(manifest);
    validateTable(manifest, packEntries);
    return manifest;
}

} // namespace airfix::afpack
