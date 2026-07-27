#include "airfix/archive/UdspArchive.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

class TempFile final {
public:
    explicit TempFile(const Bytes& bytes) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("airfix-udsp-test-" + std::to_string(suffix) + ".up");
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

[[nodiscard]] Bytes makeArchiveWithPayload(
    const Bytes& payload,
    const std::uint32_t flags,
    const std::uint32_t unpackedSize) {
    constexpr std::string_view directory = "Game\\Objects";
    constexpr std::string_view fileName = "plane.bin";

    Bytes archive(airfix::udsp::kHeaderSize, 0U);
    archive.insert(archive.end(), payload.begin(), payload.end());

    const auto directoryOffset = static_cast<std::uint32_t>(archive.size());
    appendRecord(
        archive,
        airfix::udsp::nameHash(directory),
        0U,
        0U,
        0U,
        1U,
        0U);

    const auto fileOffset = static_cast<std::uint32_t>(archive.size());
    appendRecord(
        archive,
        airfix::udsp::nameHash(fileName),
        static_cast<std::uint32_t>(directory.size() + 1U),
        flags,
        unpackedSize,
        static_cast<std::uint32_t>(payload.size()),
        static_cast<std::uint32_t>(airfix::udsp::kHeaderSize));

    const auto stringOffset = static_cast<std::uint32_t>(archive.size());
    archive.insert(archive.end(), directory.begin(), directory.end());
    archive.push_back(0U);
    archive.insert(archive.end(), fileName.begin(), fileName.end());
    archive.push_back(0U);

    archive[0] = 'U'; archive[1] = 'D'; archive[2] = 'S'; archive[3] = 'P';
    writeU32(archive, 4U, airfix::udsp::kVersion);
    writeU32(archive, 8U, airfix::udsp::kRecordSize);
    writeU32(archive, 12U, directoryOffset);
    writeU32(archive, 16U, static_cast<std::uint32_t>(archive.size()) - stringOffset);
    writeU32(archive, 20U, stringOffset);
    writeU32(archive, 24U, airfix::udsp::kRecordSize);
    writeU32(archive, 28U, fileOffset);
    return archive;
}

[[nodiscard]] Bytes makeArchive() {
    return makeArchiveWithPayload(Bytes{0x10U, 0x20U, 0x30U}, 0U, 3U);
}

[[nodiscard]] Bytes makeAmbiguousArchive() {
    constexpr std::string_view directory = "Game\\Objects";
    constexpr std::string_view fileName = "plane.bin";
    Bytes archive(airfix::udsp::kHeaderSize, 0U);
    const auto directoryOffset = static_cast<std::uint32_t>(archive.size());
    appendRecord(
        archive, airfix::udsp::nameHash(directory), 0U, 0U, 0U, 2U, 0U);
    const auto fileOffset = static_cast<std::uint32_t>(archive.size());
    for (std::size_t index = 0U; index < 2U; ++index) {
        appendRecord(
            archive,
            airfix::udsp::nameHash(fileName),
            static_cast<std::uint32_t>(directory.size() + 1U),
            0U,
            0U,
            0U,
            static_cast<std::uint32_t>(airfix::udsp::kHeaderSize));
    }
    const auto stringOffset = static_cast<std::uint32_t>(archive.size());
    archive.insert(archive.end(), directory.begin(), directory.end());
    archive.push_back(0U);
    archive.insert(archive.end(), fileName.begin(), fileName.end());
    archive.push_back(0U);
    archive[0] = 'U'; archive[1] = 'D'; archive[2] = 'S'; archive[3] = 'P';
    writeU32(archive, 4U, airfix::udsp::kVersion);
    writeU32(archive, 8U, airfix::udsp::kRecordSize);
    writeU32(archive, 12U, directoryOffset);
    writeU32(archive, 16U, static_cast<std::uint32_t>(archive.size()) - stringOffset);
    writeU32(archive, 20U, stringOffset);
    writeU32(archive, 24U, 2U * airfix::udsp::kRecordSize);
    writeU32(archive, 28U, fileOffset);
    return archive;
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
    catch (const airfix::udsp::ParseError&) {
        return;
    }
    throw std::runtime_error("expected ParseError");
}

[[nodiscard]] std::string streamBytes(const Bytes& bytes) {
    return {
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size(),
    };
}

void testHash() {
    require(airfix::udsp::nameHash("Game") == 0x00000E5EU, "Game hash mismatch");
    require(airfix::udsp::nameHash("GAME") == 0x00000E5EU, "hash must be ASCII case-insensitive");
    const std::string localized{
        'M', 'a', 's', 'k', 'i', 'n', 'e', 'g', 'e', 'v',
        static_cast<char>(0xE6), 'r', '.', 'g', 't', 'i',
    };
    require(airfix::udsp::nameHash(localized) == 0x0000A7DDU,
        "legacy signed-byte hash mismatch");
}

void testValidArchive() {
    const auto bytes = makeArchive();
    const auto archive = airfix::udsp::Archive::parse(bytes);
    require(archive.header().version == airfix::udsp::kVersion, "version mismatch");
    require(archive.directories().size() == 1U, "directory count mismatch");
    require(archive.files().size() == 1U, "file count mismatch");
    require(archive.directories()[0].path == "Game\\Objects", "directory name mismatch");
    require(archive.files()[0].name == "plane.bin", "file name mismatch");
    require(archive.files()[0].unpackedSize == 3U, "file size mismatch");
}

void testLogicalLookup() {
    const auto archive = airfix::udsp::Archive::parse(makeArchive());
    const auto lookup = archive.lookup("game/OBJECTS/PLANE.BIN");
    require(lookup.status == airfix::udsp::LookupStatus::unique,
        "case-insensitive logical lookup failed");
    require(lookup.directoryIndex == 0U && lookup.fileIndex == 0U,
        "logical lookup indices mismatch");
    require(archive.lookup("Game\\Objects\\missing.bin").status ==
        airfix::udsp::LookupStatus::notFound,
        "missing logical lookup mismatch");
    require(airfix::udsp::normalizeLogicalPath("Game/Objects/plane.bin") ==
        "Game\\Objects\\plane.bin",
        "logical path separator normalization mismatch");
    require(airfix::udsp::isLogicalPathValid("Game/Objects/plane.bin") &&
            airfix::udsp::isLogicalPathValid("plane.bin"),
        "valid logical path was rejected by non-throwing validation");

    for (const auto unsafe : {
             "", "\\absolute.bin", "C:\\drive.bin", "Game\\\\file.bin",
             "Game\\.\\file.bin", "Game\\..\\file.bin", "Game\\file.bin\\"}) {
        require(!airfix::udsp::isLogicalPathValid(unsafe),
            "unsafe logical path passed non-throwing validation");
        requireParseError([&] {
            (void)airfix::udsp::normalizeLogicalPath(unsafe);
        });
    }
    const std::array forbiddenPaths{
        std::string{"Game"} + static_cast<char>(0x01) + "file.bin",
        std::string{"Game"} + static_cast<char>(0x7F) + "file.bin",
    };
    for (const auto& forbidden : forbiddenPaths) {
        require(!airfix::udsp::isLogicalPathValid(forbidden),
            "control character passed non-throwing path validation");
        requireParseError([&] {
            (void)airfix::udsp::normalizeLogicalPath(forbidden);
        });
    }
    constexpr std::string_view exactLimitPath = "Game/file.bin";
    require(airfix::udsp::isLogicalPathValid(
                exactLimitPath, exactLimitPath.size()) &&
            !airfix::udsp::isLogicalPathValid(
                exactLimitPath, exactLimitPath.size() - 1U),
        "non-throwing path validation mishandled its exact size boundary");
    requireParseError([&] {
        (void)archive.lookup("Game\\Objects\\plane.bin", 8U);
    });
    const auto ambiguous = airfix::udsp::Archive::parse(makeAmbiguousArchive());
    require(ambiguous.lookup("Game\\Objects\\plane.bin").status ==
        airfix::udsp::LookupStatus::ambiguous,
        "duplicate logical lookup was not reported as ambiguous");
}

void testFileBackedArchive() {
    const auto bytes = makeArchive();
    const TempFile file(bytes);
    const auto archive = airfix::udsp::Archive::open(file.path());
    require(archive.archiveSize() == bytes.size(), "file-backed archive size mismatch");
    require(archive.directories().size() == 1U, "file-backed directory count mismatch");
    require(archive.files().size() == 1U, "file-backed file count mismatch");
}

void testEmbeddedFileBackedArchive() {
    const auto archiveBytes = makeArchive();
    constexpr std::size_t prefixSize = 19U;
    Bytes container(prefixSize, 0xA5U);
    container.insert(container.end(), archiveBytes.begin(), archiveBytes.end());
    container.insert(container.end(), 11U, 0x5AU);
    const TempFile file(container);

    const auto archive = airfix::udsp::Archive::openRegion(
        file.path(), prefixSize, archiveBytes.size());
    require(archive.archiveSize() == archiveBytes.size(), "embedded size mismatch");
    require(archive.backingOffset() == prefixSize, "embedded backing offset mismatch");
    require(archive.directories().size() == 1U, "embedded directory count mismatch");
    require(archive.files().size() == 1U, "embedded file count mismatch");

    requireParseError([&] {
        (void)airfix::udsp::Archive::openRegion(
            file.path(), prefixSize, archiveBytes.size() + 12U);
    });
}

void testBoundedFilePrefixes() {
    const auto bytes = makeArchive();
    const TempFile file(bytes);
    const auto archive = airfix::udsp::Archive::open(file.path());
    require(
        airfix::udsp::readFilePrefix(file.path(), archive, archive.files()[0], 2U) ==
            Bytes{0x10U, 0x20U},
        "uncompressed prefix mismatch");
    require(
        airfix::udsp::readFile(file.path(), archive, archive.files()[0], 3U) ==
            Bytes{0x10U, 0x20U, 0x30U},
        "uncompressed bounded read mismatch");

    const Bytes encoded{0x66U, 0x03U, 0x10U, 0x20U, 0x30U};
    const auto compressedBytes = makeArchiveWithPayload(
        encoded, airfix::udsp::kCompressedFlag, 3U);
    const TempFile compressedFile(compressedBytes);
    const auto compressedArchive = airfix::udsp::Archive::open(compressedFile.path());
    require(
        airfix::udsp::readFilePrefix(
            compressedFile.path(), compressedArchive, compressedArchive.files()[0], 2U) ==
            Bytes{0x10U, 0x20U},
        "compressed prefix mismatch");
    require(
        airfix::udsp::readFile(
            compressedFile.path(), compressedArchive, compressedArchive.files()[0], 3U) ==
            Bytes{0x10U, 0x20U, 0x30U},
        "compressed bounded read mismatch");
    requireParseError([&] {
        (void)airfix::udsp::readFile(
            compressedFile.path(), compressedArchive, compressedArchive.files()[0], 2U);
    });
}

void testSingleHandleStreams() {
    const auto archiveBytes = makeArchive();
    const TempFile poisonedLabelFile(Bytes{'N', 'O', 'T', 'U', 'D', 'S', 'P'});
    const auto poisonedLabel = poisonedLabelFile.path().string();
    std::istringstream standalone(
        streamBytes(archiveBytes), std::ios::in | std::ios::binary);
    const auto standaloneArchive = airfix::udsp::Archive::open(
        standalone, std::string_view(poisonedLabel));
    require(standaloneArchive.backingOffset() == 0U,
        "stream-backed standalone archive did not use zero prefix");
    require(
        airfix::udsp::readFilePrefix(
            standalone,
            archiveBytes.size(),
            std::string_view(poisonedLabel),
            standaloneArchive,
            0U,
            2U) == Bytes{0x10U, 0x20U},
        "single-handle standalone prefix mismatch");
    require(
        airfix::udsp::readFilePrefix(
            standalone,
            archiveBytes.size(),
            std::string_view(poisonedLabel),
            standaloneArchive,
            0U,
            0U).empty(),
        "single-handle zero-byte prefix mismatch");
    require(
        airfix::udsp::readFile(
            standalone,
            archiveBytes.size(),
            std::string_view(poisonedLabel),
            standaloneArchive,
            0U,
            3U) == Bytes{0x10U, 0x20U, 0x30U},
        "single-handle standalone read mismatch");
    requireParseError([&] {
        (void)airfix::udsp::readFile(
            standalone,
            archiveBytes.size(),
            std::string_view(poisonedLabel),
            standaloneArchive,
            0U,
            2U);
    });
    requireParseError([&] {
        (void)airfix::udsp::readFilePrefix(
            standalone,
            archiveBytes.size(),
            std::string_view(poisonedLabel),
            standaloneArchive,
            1U,
            1U);
    });

    constexpr std::size_t prefixSize = 13U;
    constexpr std::size_t suffixSize = 17U;
    Bytes container(prefixSize, 0xA5U);
    container.insert(container.end(), archiveBytes.begin(), archiveBytes.end());
    container.insert(container.end(), suffixSize, 0x5AU);
    std::istringstream embedded(
        streamBytes(container), std::ios::in | std::ios::binary);
    constexpr std::string_view sourceLabel = "memory:embedded-udsp";
    const auto embeddedArchive = airfix::udsp::Archive::openRegion(
        embedded,
        container.size(),
        sourceLabel,
        prefixSize,
        archiveBytes.size());
    require(embeddedArchive.backingOffset() == prefixSize,
        "single-handle embedded backing offset mismatch");
    require(
        airfix::udsp::readFile(
            embedded,
            container.size(),
            sourceLabel,
            embeddedArchive,
            0U,
            3U) == Bytes{0x10U, 0x20U, 0x30U},
        "single-handle embedded read mismatch");
    require(
        airfix::udsp::readFilePrefix(
            embedded,
            container.size(),
            sourceLabel,
            embeddedArchive,
            0U,
            1024U) == Bytes{0x10U, 0x20U, 0x30U},
        "embedded prefix escaped the declared payload into the suffix");
    requireParseError([&] {
        (void)airfix::udsp::readFile(
            embedded,
            container.size() + 1U,
            sourceLabel,
            embeddedArchive,
            0U,
            3U);
    });
    requireParseError([&] {
        (void)airfix::udsp::Archive::openRegion(
            embedded,
            container.size(),
            sourceLabel,
            prefixSize,
            archiveBytes.size() + suffixSize);
    });

    const Bytes encoded{0x66U, 0x03U, 0x10U, 0x20U, 0x30U};
    const auto compressedBytes = makeArchiveWithPayload(
        encoded, airfix::udsp::kCompressedFlag, 3U);
    Bytes compressedContainer(prefixSize, 0xA5U);
    compressedContainer.insert(
        compressedContainer.end(), compressedBytes.begin(), compressedBytes.end());
    compressedContainer.insert(compressedContainer.end(), suffixSize, 0x5AU);
    std::istringstream compressed(
        streamBytes(compressedContainer), std::ios::in | std::ios::binary);
    const auto compressedArchive = airfix::udsp::Archive::openRegion(
        compressed,
        compressedContainer.size(),
        sourceLabel,
        prefixSize,
        compressedBytes.size());
    require(
        airfix::udsp::readFilePrefix(
            compressed,
            compressedContainer.size(),
            sourceLabel,
            compressedArchive,
            0U,
            2U) == Bytes{0x10U, 0x20U},
        "single-handle compressed prefix mismatch");
    require(
        airfix::udsp::readFilePrefix(
            compressed,
            compressedContainer.size(),
            sourceLabel,
            compressedArchive,
            0U,
            0U).empty(),
        "single-handle compressed zero-byte prefix mismatch");
    require(
        airfix::udsp::readFile(
            compressed,
            compressedContainer.size(),
            sourceLabel,
            compressedArchive,
            0U,
            3U) == Bytes{0x10U, 0x20U, 0x30U},
        "single-handle compressed read mismatch");
    requireParseError([&] {
        (void)airfix::udsp::readFile(
            compressed,
            compressedContainer.size(),
            sourceLabel,
            compressedArchive,
            0U,
            2U);
    });
}

void testMalformedArchives() {
    {
        auto bytes = makeArchive();
        bytes[0] = 'X';
        requireParseError([&] { (void)airfix::udsp::Archive::parse(bytes); });
    }
    {
        auto bytes = makeArchive();
        writeU32(bytes, 20U, static_cast<std::uint32_t>(bytes.size() + 1U));
        requireParseError([&] { (void)airfix::udsp::Archive::parse(bytes); });
    }
    {
        auto bytes = makeArchive();
        writeU32(bytes, airfix::udsp::kHeaderSize + 20U, 1U);
        requireParseError([&] { (void)airfix::udsp::Archive::parse(bytes); });
    }
    {
        auto bytes = makeArchive();
        const auto fileOffset = static_cast<std::size_t>(
            bytes[28] | (bytes[29] << 8U) | (bytes[30] << 16U) | (bytes[31] << 24U));
        writeU32(bytes, fileOffset, 0U);
        requireParseError([&] { (void)airfix::udsp::Archive::parse(bytes); });
    }
}

void testDecompression() {
    const Bytes encoded{
        0x65U, 0x08U, 'A', 'B', 'C', 'D',
        0x66U, 0x03U, 'x', 'y', 'z',
        0x67U, 0x01U, '!',
    };
    const Bytes expected{
        'A', 'B', 'C', 'D', 'A', 'B', 'C', 'D', 'x', 'y', 'z', '!',
    };
    require(airfix::udsp::decompress(encoded, expected.size(), 1024U) == expected,
        "decompression mismatch");

    requireParseError([&] {
        const Bytes invalid{0x64U};
        (void)airfix::udsp::decompress(invalid, 0U, 1024U);
    });
    requireParseError([&] {
        const Bytes truncated{0x66U, 0x02U, 0xAAU};
        (void)airfix::udsp::decompress(truncated, 2U, 1024U);
    });
    requireParseError([&] {
        const Bytes literal{0x67U, 0x01U, 0xAAU};
        (void)airfix::udsp::decompress(literal, 2U, 1024U);
    });
    requireParseError([&] {
        const Bytes empty;
        (void)airfix::udsp::decompress(empty, 2U, 1U);
    });
}

} // namespace

int main() {
    try {
        testHash();
        testValidArchive();
        testLogicalLookup();
        testFileBackedArchive();
        testEmbeddedFileBackedArchive();
        testBoundedFilePrefixes();
        testSingleHandleStreams();
        testMalformedArchives();
        testDecompression();
        std::cout << "all UDSP tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "UDSP test failure: " << error.what() << '\n';
        return 1;
    }
}
