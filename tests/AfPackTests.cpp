#include "airfix/package/AfPack.hpp"
#include "airfix/package/AfPackWriter.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
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

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kFirstRecord = airfix::afpack::kHeaderSize;
constexpr std::size_t kSecondRecord = kFirstRecord + airfix::afpack::kEntryRecordSize;
constexpr std::string_view kManifestPath = "manifest.json";
constexpr std::string_view kSourcePath = "source/Resource.up";

class TempFile final {
public:
    explicit TempFile(const Bytes& bytes) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("airfix-afpack-test-" + std::to_string(suffix) + ".afpack");
        std::ofstream output(path_, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            throw std::runtime_error("failed to create synthetic AFPACK");
        }
    }

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class TempDirectory final {
public:
    TempDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("airfix-afpack-writer-test-" + std::to_string(suffix));
        if (!std::filesystem::create_directory(path_)) {
            throw std::runtime_error("failed to create AFPACK test directory");
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

void writeU16(Bytes& bytes, const std::size_t offset, const std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(Bytes& bytes, const std::size_t offset, const std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        bytes.at(offset + byte) = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

void writeU64(Bytes& bytes, const std::size_t offset, const std::uint64_t value) {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        bytes.at(offset + byte) = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

[[nodiscard]] constexpr std::size_t alignUp(const std::size_t value) {
    return (value + airfix::afpack::kDataAlignment - 1U) &
        ~(airfix::afpack::kDataAlignment - 1U);
}

void writeEntry(
    Bytes& bytes,
    const std::size_t recordOffset,
    const std::uint64_t pathOffset,
    const std::uint32_t pathSize,
    const airfix::afpack::EntryKind kind,
    const std::uint64_t dataOffset,
    const std::uint64_t dataSize,
    const std::uint8_t digestByte) {
    writeU64(bytes, recordOffset, pathOffset);
    writeU32(bytes, recordOffset + 8U, pathSize);
    writeU16(bytes, recordOffset + 12U, static_cast<std::uint16_t>(kind));
    writeU64(bytes, recordOffset + 16U, dataOffset);
    writeU64(bytes, recordOffset + 24U, dataSize);
    writeU64(bytes, recordOffset + 32U, dataSize);
    for (std::size_t byte = 0U; byte < 32U; ++byte) {
        bytes.at(recordOffset + 40U + byte) = digestByte;
    }
}

[[nodiscard]] Bytes makePack() {
    const Bytes manifest{'{', '}'};
    const Bytes source{0x55U, 0x44U, 0x53U, 0x50U};
    constexpr std::size_t entryCount = 2U;
    constexpr std::size_t entryTableSize = entryCount * airfix::afpack::kEntryRecordSize;
    constexpr std::size_t stringTableOffset =
        airfix::afpack::kHeaderSize + entryTableSize;
    constexpr std::size_t stringTableSize = kManifestPath.size() + kSourcePath.size();
    constexpr std::size_t dataOffset = alignUp(stringTableOffset + stringTableSize);
    const auto sourceOffset = alignUp(dataOffset + manifest.size());
    const auto archiveSize = sourceOffset + source.size();

    Bytes bytes(archiveSize, 0U);
    bytes[0] = 'A';
    bytes[1] = 'F';
    bytes[2] = 'P';
    bytes[3] = 'K';
    writeU16(bytes, 4U, airfix::afpack::kVersionMajor);
    writeU16(bytes, 6U, airfix::afpack::kVersionMinor);
    writeU32(bytes, 8U, static_cast<std::uint32_t>(airfix::afpack::kHeaderSize));
    writeU32(bytes, 16U, static_cast<std::uint32_t>(entryCount));
    writeU64(bytes, 24U, airfix::afpack::kHeaderSize);
    writeU64(bytes, 32U, entryTableSize);
    writeU64(bytes, 40U, stringTableOffset);
    writeU64(bytes, 48U, stringTableSize);
    writeU64(bytes, 56U, dataOffset);
    writeU64(bytes, 64U, archiveSize);

    writeEntry(
        bytes,
        kFirstRecord,
        0U,
        static_cast<std::uint32_t>(kManifestPath.size()),
        airfix::afpack::EntryKind::manifest,
        dataOffset,
        manifest.size(),
        0x11U);
    writeEntry(
        bytes,
        kSecondRecord,
        kManifestPath.size(),
        static_cast<std::uint32_t>(kSourcePath.size()),
        airfix::afpack::EntryKind::sourceArchive,
        sourceOffset,
        source.size(),
        0x22U);

    auto cursor = bytes.begin() + static_cast<std::ptrdiff_t>(stringTableOffset);
    cursor = std::copy(kManifestPath.begin(), kManifestPath.end(), cursor);
    std::copy(kSourcePath.begin(), kSourcePath.end(), cursor);
    std::copy(manifest.begin(), manifest.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset));
    std::copy(source.begin(), source.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(sourceOffset));
    return bytes;
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireParseError(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const airfix::afpack::ParseError&) {
        return;
    }
    throw std::runtime_error("expected ParseError");
}

void requireError(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("expected writer error");
}

void writeFile(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("failed to create AFPACK writer input");
    }
}

[[nodiscard]] Bytes readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("failed to open AFPACK writer output");
    }
    const auto size = input.tellg();
    if (size < 0) {
        throw std::runtime_error("failed to size AFPACK writer output");
    }
    Bytes bytes(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() && !input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("failed to read AFPACK writer output");
    }
    return bytes;
}

void testValidPack() {
    const auto bytes = makePack();
    const auto pack = airfix::afpack::Pack::parse(bytes);
    require(pack.archiveSize() == bytes.size(), "archive size mismatch");
    require(pack.header().entryCount == 2U, "entry count mismatch");
    require(pack.entries().size() == 2U, "parsed entry count mismatch");
    require(pack.entries()[0].path == kManifestPath, "manifest path mismatch");
    require(pack.entries()[0].kind == airfix::afpack::EntryKind::manifest,
        "manifest kind mismatch");
    require(pack.entries()[1].path == kSourcePath, "source path mismatch");
    require(pack.entries()[1].contentSize == 4U, "source size mismatch");
}

void testFileBackedPack() {
    const auto bytes = makePack();
    const TempFile file(bytes);
    const auto pack = airfix::afpack::Pack::open(file.path());
    require(pack.archiveSize() == bytes.size(), "file-backed size mismatch");
    require(pack.entries().size() == 2U, "file-backed entry count mismatch");
}

void testMalformedPacks() {
    {
        auto bytes = makePack();
        bytes[0] = 'X';
        requireParseError([&] { (void)airfix::afpack::Pack::parse(bytes); });
    }
    {
        auto bytes = makePack();
        writeU16(bytes, 4U, airfix::afpack::kVersionMajor + 1U);
        requireParseError([&] { (void)airfix::afpack::Pack::parse(bytes); });
    }
    {
        auto bytes = makePack();
        const auto stringTableOffset = airfix::afpack::kHeaderSize +
            (2U * airfix::afpack::kEntryRecordSize) + kManifestPath.size();
        constexpr std::string_view traversal = "../evil/asset.binx";
        std::copy(traversal.begin(), traversal.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(stringTableOffset));
        requireParseError([&] { (void)airfix::afpack::Pack::parse(bytes); });
    }
    {
        auto bytes = makePack();
        const auto stringTableOffset = airfix::afpack::kHeaderSize +
            (2U * airfix::afpack::kEntryRecordSize) + kManifestPath.size();
        bytes[stringTableOffset] = 0xC0U;
        requireParseError([&] { (void)airfix::afpack::Pack::parse(bytes); });
    }
    {
        auto bytes = makePack();
        std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(kSecondRecord + 40U),
            32U, 0U);
        requireParseError([&] { (void)airfix::afpack::Pack::parse(bytes); });
    }
    {
        auto bytes = makePack();
        writeU16(bytes, kFirstRecord + 12U,
            static_cast<std::uint16_t>(airfix::afpack::EntryKind::sourceArchive));
        requireParseError([&] { (void)airfix::afpack::Pack::parse(bytes); });
    }
    {
        auto bytes = makePack();
        const auto firstDataOffset = static_cast<std::uint64_t>(alignUp(
            airfix::afpack::kHeaderSize + 2U * airfix::afpack::kEntryRecordSize +
            kManifestPath.size() + kSourcePath.size()));
        writeU64(bytes, kSecondRecord + 16U, firstDataOffset);
        requireParseError([&] { (void)airfix::afpack::Pack::parse(bytes); });
    }
}

void testDeterministicWriter() {
    TempDirectory directory;
    const auto resourcePath = directory.path() / "Resource.up";
    const auto localizationPath = directory.path() / "English.up";
    writeFile(resourcePath, Bytes{0x01U, 0x02U, 0x03U, 0x04U, 0x05U});
    writeFile(localizationPath, Bytes{0xA0U, 0xB0U, 0xC0U});

    const auto makeRequest = [&](const std::filesystem::path& output) {
        return airfix::afpack::WriteRequest{
            .outputPath = output,
            .manifest = {
                .sourceVersion = "1.01",
                .converterVersion = "test",
                .converterCommit = "0123456789abcdef",
                .locale = "en",
            },
            .entries = {
                {
                    .logicalPath = "source/Resource.up",
                    .kind = airfix::afpack::EntryKind::sourceArchive,
                    .sourcePath = resourcePath,
                },
                {
                    .logicalPath = "localization/English.up",
                    .kind = airfix::afpack::EntryKind::localization,
                    .sourcePath = localizationPath,
                },
            },
        };
    };

    const auto firstPath = directory.path() / "first.afpack";
    const auto secondPath = directory.path() / "second.afpack";
    const auto firstResult = airfix::afpack::writePack(makeRequest(firstPath));
    const auto secondResult = airfix::afpack::writePack(makeRequest(secondPath));
    require(firstResult.entries.size() == 3U, "writer entry count mismatch");
    require(firstResult.archiveSize == secondResult.archiveSize,
        "deterministic writer size mismatch");
    require(readFile(firstPath) == readFile(secondPath),
        "deterministic writer byte mismatch");

    const auto pack = airfix::afpack::Pack::open(firstPath);
    pack.verifyPayloads(firstPath);
    require(pack.entries()[0].path == "localization/English.up",
        "writer localization sort mismatch");
    require(pack.entries()[1].path == "manifest.json", "writer manifest sort mismatch");
    require(pack.entries()[2].path == "source/Resource.up", "writer source sort mismatch");

    const auto packBytes = readFile(firstPath);
    const auto manifest = std::find_if(
        pack.entries().begin(), pack.entries().end(),
        [](const airfix::afpack::Entry& entry) {
            return entry.kind == airfix::afpack::EntryKind::manifest;
        });
    require(manifest != pack.entries().end(), "writer manifest entry missing");
    const std::string manifestText(
        reinterpret_cast<const char*>(packBytes.data() +
            static_cast<std::size_t>(manifest->dataOffset)),
        static_cast<std::size_t>(manifest->storedSize));
    require(manifestText.find("\"music\": false") != std::string::npos,
        "accepted no-CD package did not declare music absent");
    require(manifestText.find("\"music\": true") == std::string::npos,
        "accepted no-CD package incorrectly declared music available");

    requireError([&] { (void)airfix::afpack::writePack(makeRequest(firstPath)); });
    auto traversalRequest = makeRequest(directory.path() / "traversal.afpack");
    traversalRequest.entries[0].logicalPath = "../Resource.up";
    requireError([&] { (void)airfix::afpack::writePack(traversalRequest); });

    auto corrupted = readFile(firstPath);
    corrupted.back() ^= 0xFFU;
    const auto corruptedPath = directory.path() / "corrupted.afpack";
    writeFile(corruptedPath, corrupted);
    const auto corruptedPack = airfix::afpack::Pack::open(corruptedPath);
    requireParseError([&] { corruptedPack.verifyPayloads(corruptedPath); });
}

} // namespace

int main() {
    try {
        testValidPack();
        testFileBackedPack();
        testMalformedPacks();
        testDeterministicWriter();
        std::cout << "all AFPACK tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "AFPACK test failure: " << error.what() << '\n';
        return 1;
    }
}
