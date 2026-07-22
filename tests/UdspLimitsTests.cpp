#include "airfix/archive/UdspArchive.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

struct EntrySpec {
    std::string name;
    Bytes payload;
    std::uint32_t flags{};
    std::uint32_t unpackedSize{};
};

class TempFile final {
public:
    explicit TempFile(const Bytes& bytes) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("airfix-udsp-limits-test-" + std::to_string(suffix) + ".up");
        std::ofstream output(path_, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            throw std::runtime_error("failed to create synthetic archive");
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

[[nodiscard]] std::uint32_t readU32(const Bytes& bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes.at(offset)) |
        (static_cast<std::uint32_t>(bytes.at(offset + 1U)) << 8U) |
        (static_cast<std::uint32_t>(bytes.at(offset + 2U)) << 16U) |
        (static_cast<std::uint32_t>(bytes.at(offset + 3U)) << 24U);
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

[[nodiscard]] Bytes makeArchive(
    const std::string_view directory,
    std::vector<EntrySpec> entries) {
    std::sort(entries.begin(), entries.end(), [](const EntrySpec& left, const EntrySpec& right) {
        return airfix::udsp::nameHash(left.name) < airfix::udsp::nameHash(right.name);
    });

    Bytes archive(airfix::udsp::kHeaderSize, 0U);
    std::vector<std::uint32_t> dataOffsets;
    dataOffsets.reserve(entries.size());
    for (const auto& entry : entries) {
        dataOffsets.push_back(static_cast<std::uint32_t>(archive.size()));
        archive.insert(archive.end(), entry.payload.begin(), entry.payload.end());
    }

    const auto directoryOffset = static_cast<std::uint32_t>(archive.size());
    appendRecord(
        archive,
        airfix::udsp::nameHash(directory),
        0U,
        0U,
        0U,
        static_cast<std::uint32_t>(entries.size()),
        0U);

    const auto fileOffset = static_cast<std::uint32_t>(archive.size());
    std::uint32_t nextNameOffset = static_cast<std::uint32_t>(directory.size() + 1U);
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        appendRecord(
            archive,
            airfix::udsp::nameHash(entry.name),
            nextNameOffset,
            entry.flags,
            entry.unpackedSize,
            static_cast<std::uint32_t>(entry.payload.size()),
            dataOffsets[index]);
        nextNameOffset += static_cast<std::uint32_t>(entry.name.size() + 1U);
    }

    const auto stringOffset = static_cast<std::uint32_t>(archive.size());
    archive.insert(archive.end(), directory.begin(), directory.end());
    archive.push_back(0U);
    for (const auto& entry : entries) {
        archive.insert(archive.end(), entry.name.begin(), entry.name.end());
        archive.push_back(0U);
    }

    archive[0] = 'U';
    archive[1] = 'D';
    archive[2] = 'S';
    archive[3] = 'P';
    writeU32(archive, 4U, airfix::udsp::kVersion);
    writeU32(archive, 8U, airfix::udsp::kRecordSize);
    writeU32(archive, 12U, directoryOffset);
    writeU32(
        archive,
        16U,
        static_cast<std::uint32_t>(archive.size()) - stringOffset);
    writeU32(archive, 20U, stringOffset);
    writeU32(
        archive,
        24U,
        static_cast<std::uint32_t>(entries.size() * airfix::udsp::kRecordSize));
    writeU32(archive, 28U, fileOffset);
    return archive;
}

[[nodiscard]] Bytes makeSingleEntryArchive() {
    return makeArchive(
        "dir",
        {{.name = "entry.bin", .payload = {0x10U, 0x20U, 0x30U},
          .flags = 0U, .unpackedSize = 3U}});
}

[[nodiscard]] Bytes makeMetadataOnlyArchive(const std::uint32_t stringBytes) {
    Bytes archive(airfix::udsp::kHeaderSize + stringBytes, 0U);
    archive[0] = 'U';
    archive[1] = 'D';
    archive[2] = 'S';
    archive[3] = 'P';
    writeU32(archive, 4U, airfix::udsp::kVersion);
    writeU32(archive, 8U, 0U);
    writeU32(archive, 12U, airfix::udsp::kHeaderSize);
    writeU32(archive, 16U, stringBytes);
    writeU32(archive, 20U, airfix::udsp::kHeaderSize);
    writeU32(archive, 24U, 0U);
    writeU32(archive, 28U, airfix::udsp::kHeaderSize);
    return archive;
}

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void requireParseErrorContaining(
    const std::function<void()>& action,
    const std::string_view expected) {
    try {
        action();
    }
    catch (const airfix::udsp::ParseError& error) {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos,
            "ParseError did not identify the expected limit");
        return;
    }
    throw std::runtime_error("expected ParseError");
}

void testDefaultLimits() {
    const airfix::udsp::ParseLimits limits;
    require(limits.maxArchiveSize == 512ULL * 1024ULL * 1024ULL,
        "default archive limit mismatch");
    require(limits.maxMetadataSize == 8ULL * 1024ULL * 1024ULL,
        "default metadata limit mismatch");
    require(limits.maxDirectoryCount == 16'384ULL,
        "default directory-count limit mismatch");
    require(limits.maxFileCount == 65'536ULL,
        "default file-count limit mismatch");
    require(limits.maxStringTableSize == 4ULL * 1024ULL * 1024ULL,
        "default string-table limit mismatch");
    require(limits.maxNameSize == 1'024ULL, "default name limit mismatch");
    require(limits.maxTotalDecodedNameBytes == 16ULL * 1024ULL * 1024ULL,
        "default decoded-name limit mismatch");
    require(limits.maxStoredEntrySize == 256ULL * 1024ULL * 1024ULL,
        "default stored-entry limit mismatch");
    require(limits.maxUnpackedEntrySize == 512ULL * 1024ULL * 1024ULL,
        "default unpacked-entry limit mismatch");
    require(limits.maxTotalUnpackedSize == 1ULL * 1024ULL * 1024ULL * 1024ULL,
        "default total-unpacked limit mismatch");
}

void testArchiveAndMetadataSizeBoundaries() {
    const auto bytes = makeSingleEntryArchive();
    airfix::udsp::ParseLimits limits;
    limits.maxArchiveSize = bytes.size();
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxArchiveSize;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "archive");

    constexpr std::uint32_t metadataBytes = 1024U * 1024U;
    const auto metadataArchive = makeMetadataOnlyArchive(metadataBytes);
    limits = {};
    limits.maxArchiveSize = metadataArchive.size();
    limits.maxMetadataSize = metadataBytes;
    limits.maxStringTableSize = metadataBytes;
    const TempFile file(metadataArchive);
    (void)airfix::udsp::Archive::open(file.path(), limits);
    --limits.maxMetadataSize;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::open(file.path(), limits); }, "metadata");
}

void testTableAndStringBoundaries() {
    const auto bytes = makeSingleEntryArchive();
    const auto baseline = airfix::udsp::Archive::parse(bytes);

    airfix::udsp::ParseLimits limits;
    limits.maxDirectoryCount = baseline.directories().size();
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxDirectoryCount;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "directory count");

    limits = {};
    limits.maxFileCount = baseline.files().size();
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxFileCount;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "file count");

    limits = {};
    limits.maxStringTableSize = baseline.header().stringBytes;
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxStringTableSize;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "string table");
}

void testNameAndEntrySizeBoundaries() {
    const auto bytes = makeSingleEntryArchive();

    airfix::udsp::ParseLimits limits;
    limits.maxNameSize = std::string_view("entry.bin").size();
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxNameSize;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "name");

    limits = {};
    limits.maxStoredEntrySize = 3U;
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxStoredEntrySize;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "stored size");

    limits = {};
    limits.maxUnpackedEntrySize = 3U;
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxUnpackedEntrySize;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "unpacked size");

    limits = {};
    limits.maxTotalUnpackedSize = 3U;
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxTotalUnpackedSize;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "total unpacked");
}

void testAliasedDecodedNameBoundary() {
    auto bytes = makeArchive(
        "d",
        {
            {.name = "same", .payload = {}, .flags = 0U, .unpackedSize = 0U},
            {.name = "same", .payload = {}, .flags = 0U, .unpackedSize = 0U},
        });
    const auto fileOffset = static_cast<std::size_t>(readU32(bytes, 28U));
    const auto sharedNameOffset = readU32(bytes, fileOffset + 4U);
    writeU32(bytes, fileOffset + airfix::udsp::kRecordSize + 4U, sharedNameOffset);

    constexpr std::uint64_t decodedBytes = 1U + (2U * 4U);
    airfix::udsp::ParseLimits limits;
    limits.maxTotalDecodedNameBytes = decodedBytes;
    const auto archive = airfix::udsp::Archive::parse(bytes, limits);
    require(archive.files().size() == 2U, "aliased-name record count mismatch");
    require(archive.files()[0].name == archive.files()[1].name,
        "aliased-name decode mismatch");

    --limits.maxTotalDecodedNameBytes;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); },
        "total decoded name bytes");
}

void testWideAggregateUnpackedBoundary() {
    constexpr auto maximumEntry = std::numeric_limits<std::uint32_t>::max();
    const auto bytes = makeArchive(
        "d",
        {
            {.name = "a", .payload = {}, .flags = airfix::udsp::kCompressedFlag,
             .unpackedSize = maximumEntry},
            {.name = "b", .payload = {}, .flags = airfix::udsp::kCompressedFlag,
             .unpackedSize = maximumEntry},
        });
    constexpr std::uint64_t aggregate =
        static_cast<std::uint64_t>(maximumEntry) * 2ULL;
    airfix::udsp::ParseLimits limits;
    limits.maxUnpackedEntrySize = maximumEntry;
    limits.maxTotalUnpackedSize = aggregate;
    (void)airfix::udsp::Archive::parse(bytes, limits);
    --limits.maxTotalUnpackedSize;
    requireParseErrorContaining(
        [&] { (void)airfix::udsp::Archive::parse(bytes, limits); }, "total unpacked");
}

void testOpenRegionBoundaries() {
    const auto archiveBytes = makeSingleEntryArchive();
    constexpr std::size_t prefixSize = 17U;
    constexpr std::size_t suffixSize = 9U;
    Bytes container(prefixSize, 0xA5U);
    container.insert(container.end(), archiveBytes.begin(), archiveBytes.end());
    container.insert(container.end(), suffixSize, 0x5AU);
    const TempFile file(container);

    airfix::udsp::ParseLimits limits;
    limits.maxArchiveSize = archiveBytes.size();
    const auto archive = airfix::udsp::Archive::openRegion(
        file.path(), prefixSize, archiveBytes.size(), limits);
    require(archive.backingOffset() == prefixSize, "openRegion backing offset mismatch");

    --limits.maxArchiveSize;
    requireParseErrorContaining(
        [&] {
            (void)airfix::udsp::Archive::openRegion(
                file.path(), prefixSize, archiveBytes.size(), limits);
        },
        "archive");

    limits = {};
    requireParseErrorContaining(
        [&] {
            (void)airfix::udsp::Archive::openRegion(
                file.path(), prefixSize, archiveBytes.size() + suffixSize + 1U, limits);
        },
        "containing-file region");
}

} // namespace

int main() {
    try {
        testDefaultLimits();
        testArchiveAndMetadataSizeBoundaries();
        testTableAndStringBoundaries();
        testNameAndEntrySizeBoundaries();
        testAliasedDecodedNameBoundary();
        testWideAggregateUnpackedBoundary();
        testOpenRegionBoundaries();
        std::cout << "all UDSP limit tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "UDSP limit test failure: " << error.what() << '\n';
        return 1;
    }
}
