#include "airfix/package/AfPackManifest.hpp"
#include "airfix/package/AfPackWriter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using airfix::afpack::Entry;
using airfix::afpack::EntryKind;

constexpr std::string_view kDigest11 =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr std::string_view kDigest22 =
    "2222222222222222222222222222222222222222222222222222222222222222";

class TempDirectory final {
public:
    TempDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("airfix-afpack-manifest-test-" + std::to_string(suffix));
        if (!std::filesystem::create_directory(path_)) {
            throw std::runtime_error("failed to create manifest test directory");
        }
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] Entry makeEntry(
    std::string path,
    const EntryKind kind,
    const std::uint64_t size,
    const std::uint8_t digestByte) {
    Entry entry;
    entry.path = std::move(path);
    entry.kind = kind;
    entry.storedSize = size;
    entry.contentSize = size;
    entry.sha256.fill(digestByte);
    return entry;
}

[[nodiscard]] std::vector<Entry> validTable() {
    return {
        makeEntry("localization/English.up", EntryKind::localization, 3U, 0x11U),
        makeEntry("manifest.json", EntryKind::manifest, 900U, 0x33U),
        makeEntry("source/Resource.up", EntryKind::sourceArchive, 5U, 0x22U),
    };
}

[[nodiscard]] std::string validManifest() {
    return std::string{
        "{\n"
        "  \"schema\": \"airfix.afpack.manifest\",\n"
        "  \"version\": 1,\n"
        "  \"game\": {\"id\": \"airfix-dogfighter\", \"sourceVersion\": \"1.01\"},\n"
        "  \"converter\": {\"version\": \"0.1.0\", \"commit\": \"0123456789abcdef\"},\n"
        "  \"locale\": \"en\",\n"
        "  \"capabilities\": {\"music\": false, \"multiplayer\": false, \"editors\": false},\n"
        "  \"entries\": [\n"
        "    {\"path\": \"localization/English.up\", \"kind\": \"localization\", "
        "\"size\": 3, \"sha256\": \""} + std::string(kDigest11) +
        "\"},\n"
        "    {\"path\": \"source/Resource.up\", \"kind\": \"source-archive\", "
        "\"size\": 5, \"sha256\": \"" + std::string(kDigest22) +
        "\"}\n"
        "  ]\n"
        "}\n";
}

[[nodiscard]] std::vector<std::uint8_t> bytes(const std::string_view text) {
    return {text.begin(), text.end()};
}

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void requireManifestError(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const airfix::afpack::ManifestError&) {
        return;
    }
    throw std::runtime_error("expected ManifestError");
}

void requireManifestErrorContaining(
    const std::function<void()>& action,
    const std::string_view expected) {
    try {
        action();
    }
    catch (const airfix::afpack::ManifestError& error) {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos) {
            return;
        }
        throw std::runtime_error(
            "ManifestError did not contain '" + std::string(expected) + "': " + error.what());
    }
    throw std::runtime_error(
        "expected ManifestError containing: " + std::string(expected));
}

void writeFile(
    const std::filesystem::path& path,
    const std::span<const std::uint8_t> contents) {
    std::ofstream output(path, std::ios::binary);
    if (!contents.empty()) {
        output.write(
            reinterpret_cast<const char*>(contents.data()),
            static_cast<std::streamsize>(contents.size()));
    }
    if (!output) {
        throw std::runtime_error("failed to write manifest test input");
    }
}

void replaceOnce(std::string& text, const std::string_view from, const std::string_view to) {
    const auto offset = text.find(from);
    if (offset == std::string::npos) {
        throw std::runtime_error("test replacement source was not found");
    }
    text.replace(offset, from.size(), to);
}

void expectRejected(std::string text, const std::vector<Entry>& table = validTable()) {
    requireManifestError([&] {
        (void)airfix::afpack::parseManifest(bytes(text), table);
    });
}

void testValidManifest() {
    const auto table = validTable();
    const auto parsed = airfix::afpack::parseManifest(bytes(validManifest()), table);
    require(parsed.schema == "airfix.afpack.manifest", "schema was not retained");
    require(parsed.version == 1U, "version was not retained");
    require(parsed.gameId == "airfix-dogfighter", "game id was not retained");
    require(parsed.locale == airfix::afpack::ManifestLocale::en, "locale mismatch");
    require(airfix::afpack::localeName(parsed.locale) == "en", "locale name mismatch");
    require(parsed.entries.size() == 2U, "typed entry count mismatch");
    require(parsed.entries[1].sha256 == table[2].sha256, "typed digest mismatch");

    auto escaped = validManifest();
    replaceOnce(escaped, "airfix.afpack.manifest", "airfix.afpack.manife\\u0073t");
    replaceOnce(escaped, "\"locale\": \"en\"", "\"locale\": \"\\u0065n\"");
    (void)airfix::afpack::parseManifest(bytes(escaped), table);

    struct LocaleCase {
        std::string_view code;
        std::string_view archive;
        airfix::afpack::ManifestLocale typed;
    };
    constexpr std::array locales{
        LocaleCase{"da", "Dansk", airfix::afpack::ManifestLocale::da},
        LocaleCase{"en", "English", airfix::afpack::ManifestLocale::en},
        LocaleCase{"no", "Norsk", airfix::afpack::ManifestLocale::no},
        LocaleCase{"sv", "Svenska", airfix::afpack::ManifestLocale::sv},
    };
    for (const auto& locale : locales) {
        auto localeText = validManifest();
        replaceOnce(localeText, "\"locale\": \"en\"",
            "\"locale\": \"" + std::string(locale.code) + "\"");
        replaceOnce(localeText, "localization/English.up",
            "localization/" + std::string(locale.archive) + ".up");
        auto localeTable = validTable();
        localeTable[0].path = "localization/" + std::string(locale.archive) + ".up";
        const auto localeManifest = airfix::afpack::parseManifest(
            bytes(localeText), localeTable);
        require(localeManifest.locale == locale.typed, "accepted locale type mismatch");
        require(airfix::afpack::localeName(locale.typed) == locale.code,
            "accepted locale name mismatch");
    }
}

void testWriterPackRoundTrip() {
    TempDirectory directory;
    const auto resourcePath = directory.path() / "Resource.up";
    const auto localizationPath = directory.path() / "English.up";
    const auto outputPath = directory.path() / "roundtrip.afpack";
    constexpr std::array<std::uint8_t, 5> resource{0x55U, 0x44U, 0x53U, 0x50U, 0x01U};
    constexpr std::array<std::uint8_t, 3> localization{0x44U, 0x41U, 0x01U};
    writeFile(resourcePath, resource);
    writeFile(localizationPath, localization);

    const auto written = airfix::afpack::writePack({
        .outputPath = outputPath,
        .manifest = {
            .sourceVersion = "1.01",
            .converterVersion = "manifest-test",
            .converterCommit = "0123456789abcdef",
            .locale = "en",
        },
        .entries = {
            {
                .logicalPath = "source/Resource.up",
                .kind = EntryKind::sourceArchive,
                .sourcePath = resourcePath,
            },
            {
                .logicalPath = "localization/English.up",
                .kind = EntryKind::localization,
                .sourcePath = localizationPath,
            },
        },
    });
    require(written.entries.size() == 3U, "writer round-trip entry count mismatch");

    const auto pack = airfix::afpack::Pack::open(outputPath);
    const auto manifest = std::find_if(
        pack.entries().begin(), pack.entries().end(),
        [](const Entry& entry) { return entry.kind == EntryKind::manifest; });
    require(manifest != pack.entries().end(), "writer round-trip manifest entry missing");
    const auto manifestIndex = static_cast<std::size_t>(
        std::distance(pack.entries().begin(), manifest));
    const auto payload = pack.readEntry(
        outputPath,
        manifestIndex,
        airfix::afpack::ManifestLimits{}.maxInputBytes);
    const auto parsed = airfix::afpack::parseManifest(payload, pack.entries());
    require(parsed.locale == airfix::afpack::ManifestLocale::en,
        "writer round-trip locale mismatch");
    require(parsed.entries.size() == 2U, "writer round-trip content count mismatch");
    require(parsed.converterVersion == "manifest-test",
        "writer round-trip converter version mismatch");
}

void testStrictSchema() {
    {
        auto text = validManifest();
        replaceOnce(text, "airfix.afpack.manifest", "wrong.schema");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"version\": 1", "\"version\": 2");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "airfix-dogfighter", "other-game");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"sourceVersion\": \"1.01\"", "\"sourceVersion\": \"1.00\"");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"locale\": \"en\"", "\"locale\": \"pl\"");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"music\": false", "\"music\": true");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"commit\": \"0123456789abcdef\"", "\"commit\": \"\"");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"version\": 1,", "\"version\": 1, \"extra\": false,");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"id\": \"airfix-dogfighter\"",
            "\"id\": \"airfix-dogfighter\", \"id\": \"airfix-dogfighter\"");
        expectRejected(std::move(text));
    }
}

void testAllowlistAndAudit() {
    {
        auto text = validManifest();
        replaceOnce(text, "localization/English.up", "localization/Dansk.up");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"kind\": \"source-archive\"", "\"kind\": \"video\"");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "source/Resource.up", "localization/English.up");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"size\": 5", "\"size\": 6");
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        replaceOnce(text, kDigest22, kDigest11);
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        text[text.find(kDigest22)] = 'A';
        expectRejected(std::move(text));
    }
    {
        auto text = validManifest();
        text[text.find(kDigest22)] = 'z';
        expectRejected(std::move(text));
    }
    {
        auto table = validTable();
        table[2].kind = EntryKind::localization;
        expectRejected(validManifest(), table);
    }
    {
        auto table = validTable();
        table.push_back(makeEntry("video/intro.avi", EntryKind::video, 1U, 0x44U));
        expectRejected(validManifest(), table);
    }
    {
        auto table = validTable();
        table[2].path = table[0].path;
        expectRejected(validManifest(), table);
    }
}

void testMalformedAndLimits() {
    expectRejected(validManifest() + "x");
    {
        auto text = validManifest();
        replaceOnce(text, "\"locale\": \"en\"", "\"locale\": \"\\uD800\"");
        expectRejected(std::move(text));
    }
    {
        auto text = bytes(validManifest());
        const auto offset = std::find(text.begin(), text.end(), static_cast<std::uint8_t>('a'));
        require(offset != text.end(), "UTF-8 test marker missing");
        *offset = 0xC0U;
        requireManifestErrorContaining([&] {
            (void)airfix::afpack::parseManifest(text, validTable());
        }, "invalid UTF-8 leading byte");
    }
    {
        auto text = validManifest();
        replaceOnce(text, "\"size\": 5", "\"size\": 18446744073709551616");
        requireManifestErrorContaining([&] {
            (void)airfix::afpack::parseManifest(bytes(text), validTable());
        }, "integer overflows 64 bits");
    }
    {
        const auto text = validManifest();
        auto limits = airfix::afpack::ManifestLimits{};
        limits.maxInputBytes = text.size() - 1U;
        requireManifestErrorContaining([&] {
            (void)airfix::afpack::parseManifest(bytes(text), validTable(), limits);
        }, "manifest exceeds configured input size limit");
    }
    {
        const auto nested = bytes("[[[[]]]]");
        auto limits = airfix::afpack::ManifestLimits{};
        limits.maxDepth = 3U;
        requireManifestErrorContaining([&] {
            (void)airfix::afpack::parseManifest(nested, validTable(), limits);
        }, "nesting depth limit exceeded");
    }
    {
        const auto text = validManifest();
        auto limits = airfix::afpack::ManifestLimits{};
        limits.maxItems = 2U;
        requireManifestErrorContaining([&] {
            (void)airfix::afpack::parseManifest(bytes(text), validTable(), limits);
        }, "container item limit exceeded");
    }
    {
        const auto text = validManifest();
        auto limits = airfix::afpack::ManifestLimits{};
        limits.maxStringBytes = 8U;
        requireManifestErrorContaining([&] {
            (void)airfix::afpack::parseManifest(bytes(text), validTable(), limits);
        }, "string byte limit exceeded");
    }
    {
        const auto text = validManifest();
        auto limits = airfix::afpack::ManifestLimits{};
        limits.maxTotalStringBytes = 16U;
        requireManifestErrorContaining([&] {
            (void)airfix::afpack::parseManifest(bytes(text), validTable(), limits);
        }, "string byte limit exceeded");
    }
    {
        auto text = validManifest();
        const auto thirdEntry = std::string{
            ",\n    {\"path\": \"source/Resource.up\", \"kind\": \"source-archive\", "
            "\"size\": 5, \"sha256\": \""} + std::string(kDigest22) + "\"}";
        replaceOnce(text, "\n  ]\n}", thirdEntry + "\n  ]\n}");
        auto limits = airfix::afpack::ManifestLimits{};
        limits.maxEntries = 2U;
        requireManifestErrorContaining([&] {
            (void)airfix::afpack::parseManifest(bytes(text), validTable(), limits);
        }, "manifest entry count exceeds configured limit");
    }
}

} // namespace

int main() {
    try {
        testValidManifest();
        testWriterPackRoundTrip();
        testStrictSchema();
        testAllowlistAndAudit();
        testMalformedAndLimits();
        std::cout << "all AFPACK manifest tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "AFPACK manifest test failure: " << error.what() << '\n';
        return 1;
    }
}
