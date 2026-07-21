#include "airfix/archive/UdspArchive.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
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

[[nodiscard]] Bytes makeArchive() {
    constexpr std::string_view directory = "Game\\Objects";
    constexpr std::string_view fileName = "plane.bin";
    const Bytes payload{0x10U, 0x20U, 0x30U};

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
        0U,
        static_cast<std::uint32_t>(payload.size()),
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

void testFileBackedArchive() {
    const auto bytes = makeArchive();
    const TempFile file(bytes);
    const auto archive = airfix::udsp::Archive::open(file.path());
    require(archive.archiveSize() == bytes.size(), "file-backed archive size mismatch");
    require(archive.directories().size() == 1U, "file-backed directory count mismatch");
    require(archive.files().size() == 1U, "file-backed file count mismatch");
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
        testFileBackedArchive();
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
