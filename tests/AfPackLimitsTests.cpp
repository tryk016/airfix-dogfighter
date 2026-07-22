#include "airfix/package/AfPack.hpp"

#include "airfix/crypto/Sha256.hpp"

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

constexpr std::string_view kManifestPath = "manifest.json";
constexpr std::string_view kSourcePath = "source/Resource.up";
constexpr std::size_t kEntryCount = 2U;
constexpr std::size_t kEntryTableSize =
    kEntryCount * airfix::afpack::kEntryRecordSize;
constexpr std::size_t kStringTableOffset =
    airfix::afpack::kHeaderSize + kEntryTableSize;
constexpr std::size_t kStringTableSize = kManifestPath.size() + kSourcePath.size();

[[nodiscard]] constexpr std::size_t alignUp(const std::size_t value) {
    return (value + airfix::afpack::kDataAlignment - 1U) &
        ~(airfix::afpack::kDataAlignment - 1U);
}

constexpr std::size_t kDataOffset = alignUp(kStringTableOffset + kStringTableSize);
constexpr std::size_t kMetadataSize = kDataOffset - airfix::afpack::kHeaderSize;
constexpr std::size_t kManifestSize = 2U;
constexpr std::size_t kSourceOffset = alignUp(kDataOffset + kManifestSize);

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
    std::fill_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(recordOffset + 40U),
        32U,
        digestByte);
}

[[nodiscard]] Bytes makePack() {
    constexpr std::size_t sourceSize = 4U;
    constexpr std::size_t archiveSize = kSourceOffset + sourceSize;
    const Bytes source{0x55U, 0x44U, 0x53U, 0x50U};

    Bytes bytes(archiveSize, 0U);
    bytes[0] = 'A';
    bytes[1] = 'F';
    bytes[2] = 'P';
    bytes[3] = 'K';
    writeU16(bytes, 4U, airfix::afpack::kVersionMajor);
    writeU16(bytes, 6U, airfix::afpack::kVersionMinor);
    writeU32(bytes, 8U, static_cast<std::uint32_t>(airfix::afpack::kHeaderSize));
    writeU32(bytes, 16U, static_cast<std::uint32_t>(kEntryCount));
    writeU64(bytes, 24U, airfix::afpack::kHeaderSize);
    writeU64(bytes, 32U, kEntryTableSize);
    writeU64(bytes, 40U, kStringTableOffset);
    writeU64(bytes, 48U, kStringTableSize);
    writeU64(bytes, 56U, kDataOffset);
    writeU64(bytes, 64U, archiveSize);

    writeEntry(
        bytes,
        airfix::afpack::kHeaderSize,
        0U,
        static_cast<std::uint32_t>(kManifestPath.size()),
        airfix::afpack::EntryKind::manifest,
        kDataOffset,
        kManifestSize,
        0x11U);
    writeEntry(
        bytes,
        airfix::afpack::kHeaderSize + airfix::afpack::kEntryRecordSize,
        kManifestPath.size(),
        static_cast<std::uint32_t>(kSourcePath.size()),
        airfix::afpack::EntryKind::sourceArchive,
        kSourceOffset,
        sourceSize,
        0x22U);
    const auto sourceDigest = airfix::crypto::sha256(source);
    std::copy(sourceDigest.begin(), sourceDigest.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(
            airfix::afpack::kHeaderSize + airfix::afpack::kEntryRecordSize + 40U));

    auto cursor = bytes.begin() + static_cast<std::ptrdiff_t>(kStringTableOffset);
    cursor = std::copy(kManifestPath.begin(), kManifestPath.end(), cursor);
    std::copy(kSourcePath.begin(), kSourcePath.end(), cursor);
    bytes[kDataOffset] = '{';
    bytes[kDataOffset + 1U] = '}';
    std::copy(source.begin(), source.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(kSourceOffset));
    return bytes;
}

[[nodiscard]] airfix::afpack::ParseLimits exactLimits(const Bytes& bytes) {
    return {
        .maxArchiveSize = bytes.size(),
        .maxMetadataSize = kMetadataSize,
        .maxEntryCount = static_cast<std::uint32_t>(kEntryCount),
        .maxPathSize = static_cast<std::uint32_t>(kSourcePath.size()),
        .maxStringTableSize = kStringTableSize,
        .maxPayloadSize = 4U,
    };
}

class TempFile final {
public:
    explicit TempFile(const Bytes& bytes) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("airfix-afpack-limits-" + std::to_string(suffix) + ".afpack");
        write(bytes, std::ios::binary | std::ios::trunc);
    }

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    void append(const std::uint8_t value) {
        const Bytes byte{value};
        write(byte, std::ios::binary | std::ios::app);
    }

    void overwriteByte(const std::uint64_t offset, const std::uint8_t value) {
        std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        file.put(static_cast<char>(value));
        if (!file) {
            throw std::runtime_error("failed to mutate synthetic AFPACK");
        }
    }

private:
    void write(const Bytes& bytes, const std::ios::openmode mode) {
        std::ofstream output(path_, mode);
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            throw std::runtime_error("failed to create synthetic AFPACK");
        }
    }

    std::filesystem::path path_;
};

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

void requireParseErrorContaining(
    const std::function<void()>& action,
    const std::string_view expected) {
    try {
        action();
    }
    catch (const airfix::afpack::ParseError& error) {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos,
            "ParseError did not identify the rejected limit");
        return;
    }
    throw std::runtime_error("expected ParseError");
}

void testDefaultLimits() {
    const airfix::afpack::ParseLimits limits;
    require(limits.maxArchiveSize == 512U * 1024U * 1024U,
        "default archive limit mismatch");
    require(limits.maxMetadataSize == 8U * 1024U * 1024U,
        "default metadata limit mismatch");
    require(limits.maxEntryCount == 4096U, "default entry count limit mismatch");
    require(limits.maxPathSize == 1024U, "default path limit mismatch");
    require(limits.maxStringTableSize == 4U * 1024U * 1024U,
        "default string table limit mismatch");
    require(limits.maxPayloadSize == 384U * 1024U * 1024U,
        "default payload limit mismatch");
}

void testExactAndLimitPlusOne() {
    const auto bytes = makePack();
    const auto exact = exactLimits(bytes);
    const auto pack = airfix::afpack::Pack::parse(bytes, exact);
    require(pack.entries().size() == kEntryCount, "exact limits rejected valid pack");

    auto limits = exact;
    --limits.maxArchiveSize;
    requireParseErrorContaining(
        [&] { (void)airfix::afpack::Pack::parse(bytes, limits); }, "archive");

    limits = exact;
    --limits.maxMetadataSize;
    requireParseErrorContaining(
        [&] { (void)airfix::afpack::Pack::parse(bytes, limits); }, "metadata");

    limits = exact;
    --limits.maxEntryCount;
    requireParseErrorContaining(
        [&] { (void)airfix::afpack::Pack::parse(bytes, limits); }, "entry count");

    limits = exact;
    --limits.maxPathSize;
    requireParseErrorContaining(
        [&] { (void)airfix::afpack::Pack::parse(bytes, limits); }, "path");

    limits = exact;
    --limits.maxStringTableSize;
    requireParseErrorContaining(
        [&] { (void)airfix::afpack::Pack::parse(bytes, limits); }, "string table");

    limits = exact;
    --limits.maxPayloadSize;
    requireParseErrorContaining(
        [&] { (void)airfix::afpack::Pack::parse(bytes, limits); }, "payload");
}

void testOversizedMetadataRejectedBeforeFileRead() {
    auto bytes = makePack();
    std::fill_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(airfix::afpack::kHeaderSize),
        airfix::afpack::kEntryRecordSize,
        0U);
    const TempFile file(bytes);
    auto limits = exactLimits(bytes);
    --limits.maxMetadataSize;
    requireParseErrorContaining(
        [&] { (void)airfix::afpack::Pack::open(file.path(), limits); }, "metadata");
}

void testBoundedFileBackedRead() {
    const auto bytes = makePack();
    const TempFile file(bytes);
    const auto pack = airfix::afpack::Pack::open(file.path(), exactLimits(bytes));

    const auto payload = pack.readEntry(file.path(), 1U, 4U);
    require(payload == Bytes({0x55U, 0x44U, 0x53U, 0x50U}),
        "bounded entry read returned wrong payload");
    requireParseError([&] { (void)pack.readEntry(file.path(), 2U, 4U); });
    requireParseError([&] { (void)pack.readEntry(file.path(), 1U, 3U); });
}

void testReadRejectsChangedSourceSize() {
    const auto bytes = makePack();
    TempFile file(bytes);
    const auto pack = airfix::afpack::Pack::open(file.path(), exactLimits(bytes));
    file.append(0xFFU);
    requireParseErrorContaining(
        [&] { (void)pack.readEntry(file.path(), 1U, 4U); }, "size mismatch");
}

void testReadRejectsSameSizePayloadMutation() {
    const auto bytes = makePack();
    TempFile file(bytes);
    const auto pack = airfix::afpack::Pack::open(file.path(), exactLimits(bytes));
    file.overwriteByte(kSourceOffset, 0x54U);
    requireParseErrorContaining(
        [&] { (void)pack.readEntry(file.path(), 1U, 4U); }, "SHA-256 mismatch");
}

} // namespace

int main() {
    try {
        testDefaultLimits();
        testExactAndLimitPlusOne();
        testOversizedMetadataRejectedBeforeFileRead();
        testBoundedFileBackedRead();
        testReadRejectsChangedSourceSize();
        testReadRejectsSameSizePayloadMutation();
        std::cout << "all AFPACK limit tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "AFPACK limit test failure: " << error.what() << '\n';
        return 1;
    }
}
