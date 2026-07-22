#include "airfix/package/AfPack.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <string_view>

namespace airfix::afpack {
namespace {

[[nodiscard]] std::uint64_t checkedAdd(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view field) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw ParseError(std::string(field) + " overflows");
    }
    return left + right;
}

void requireRange(
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::uint64_t limit,
    const std::string_view field) {
    if (offset > limit || checkedAdd(offset, size, field) > limit) {
        throw ParseError(std::string(field) + " is outside the pack");
    }
}

[[nodiscard]] std::uint16_t readU16(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 2U, bytes.size(), field);
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 4U, bytes.size(), field);
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint64_t readU64(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 8U, bytes.size(), field);
    std::uint64_t value = 0U;
    for (std::size_t index = 0; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] Header readHeader(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) {
        throw ParseError("pack is shorter than the AFPK header");
    }
    if (bytes[0] != 'A' || bytes[1] != 'F' || bytes[2] != 'P' || bytes[3] != 'K') {
        throw ParseError("invalid AFPK magic");
    }
    return {
        .versionMajor = readU16(bytes, 4U, "versionMajor"),
        .versionMinor = readU16(bytes, 6U, "versionMinor"),
        .headerSize = readU32(bytes, 8U, "headerSize"),
        .flags = readU32(bytes, 12U, "flags"),
        .entryCount = readU32(bytes, 16U, "entryCount"),
        .reserved20 = readU32(bytes, 20U, "reserved20"),
        .entryTableOffset = readU64(bytes, 24U, "entryTableOffset"),
        .entryTableSize = readU64(bytes, 32U, "entryTableSize"),
        .stringTableOffset = readU64(bytes, 40U, "stringTableOffset"),
        .stringTableSize = readU64(bytes, 48U, "stringTableSize"),
        .dataOffset = readU64(bytes, 56U, "dataOffset"),
        .archiveSize = readU64(bytes, 64U, "archiveSize"),
        .reserved72 = readU64(bytes, 72U, "reserved72"),
    };
}

void validateLayout(const Header& header, const std::uint64_t physicalSize) {
    if (header.versionMajor != kVersionMajor || header.versionMinor != kVersionMinor) {
        throw ParseError("unsupported AFPK version");
    }
    if (header.headerSize != kHeaderSize || header.flags != 0U ||
        header.reserved20 != 0U || header.reserved72 != 0U) {
        throw ParseError("unsupported AFPK header fields");
    }
    if (header.archiveSize != physicalSize) {
        throw ParseError("AFPK declared size does not match physical size");
    }
    if (header.entryTableOffset != kHeaderSize) {
        throw ParseError("AFPK entry table must immediately follow the header");
    }
    const auto expectedEntryBytes = static_cast<std::uint64_t>(header.entryCount) *
        kEntryRecordSize;
    if (header.entryTableSize != expectedEntryBytes) {
        throw ParseError("AFPK entry table size does not match entry count");
    }
    const auto entryEnd = checkedAdd(
        header.entryTableOffset, header.entryTableSize, "entry table");
    if (entryEnd != header.stringTableOffset) {
        throw ParseError("AFPK string table is not contiguous with entry table");
    }
    const auto stringEnd = checkedAdd(
        header.stringTableOffset, header.stringTableSize, "string table");
    if (header.dataOffset < stringEnd || header.dataOffset - stringEnd >= kDataAlignment ||
        header.dataOffset % kDataAlignment != 0U) {
        throw ParseError("AFPK data region has invalid alignment padding");
    }
    requireRange(header.entryTableOffset, header.entryTableSize, physicalSize, "entry table");
    requireRange(header.stringTableOffset, header.stringTableSize, physicalSize, "string table");
    requireRange(header.dataOffset, physicalSize - header.dataOffset, physicalSize, "data region");
}

void validateLimits(
    const Header& header,
    const std::uint64_t physicalSize,
    const ParseLimits& limits) {
    if (physicalSize > limits.maxArchiveSize) {
        throw ParseError("AFPK archive exceeds configured size limit");
    }
    if (header.entryCount > limits.maxEntryCount) {
        throw ParseError("AFPK entry count exceeds configured limit");
    }
    if (header.stringTableSize > limits.maxStringTableSize) {
        throw ParseError("AFPK string table exceeds configured size limit");
    }
    const auto metadataSize = header.dataOffset - header.entryTableOffset;
    if (metadataSize > limits.maxMetadataSize) {
        throw ParseError("AFPK metadata exceeds configured size limit");
    }
}

[[nodiscard]] bool isValidUtf8(const std::string_view text) noexcept {
    std::size_t index = 0U;
    while (index < text.size()) {
        const auto first = static_cast<std::uint8_t>(text[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

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
            return false;
        }
        if (continuationCount > text.size() - index - 1U) {
            return false;
        }
        for (std::size_t part = 0; part < continuationCount; ++part) {
            const auto value = static_cast<std::uint8_t>(text[index + part + 1U]);
            if ((value & 0xC0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (value & 0x3FU);
        }
        const bool overlong = (continuationCount == 1U && codePoint < 0x80U) ||
            (continuationCount == 2U && codePoint < 0x800U) ||
            (continuationCount == 3U && codePoint < 0x10000U);
        if (overlong || (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            codePoint > 0x10FFFFU) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

void validateLogicalPathImpl(const std::string_view path) {
    if (path.empty() || !isValidUtf8(path) || path.front() == '/' ||
        path.find('\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos) {
        throw ParseError("AFPK entry has an invalid path");
    }

    std::size_t start = 0U;
    while (start <= path.size()) {
        const auto separator = path.find('/', start);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        const auto component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") {
            throw ParseError("AFPK entry path is not normalized");
        }
        for (const char character : component) {
            const auto byte = static_cast<std::uint8_t>(character);
            if (byte < 0x20U || byte == 0x7FU) {
                throw ParseError("AFPK entry path contains a control byte");
            }
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1U;
    }
}

[[nodiscard]] bool isKnownKind(const EntryKind kind) noexcept {
    return kind >= EntryKind::manifest && kind <= EntryKind::convertedAsset;
}

void readExact(
    std::istream& input,
    const std::uint64_t offset,
    const std::span<std::uint8_t> output,
    const std::filesystem::path& path) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        output.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw ParseError("AFPK range exceeds stream limits: " + path.string());
    }
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input || (!output.empty() && !input.read(
            reinterpret_cast<char*>(output.data()),
            static_cast<std::streamsize>(output.size())))) {
        throw ParseError("cannot read pack: " + path.string());
    }
}

} // namespace

void validateLogicalPath(const std::string_view path) {
    validateLogicalPathImpl(path);
}

Pack Pack::parse(
    const std::span<const std::uint8_t> bytes,
    const ParseLimits& limits) {
    if (bytes.size() > limits.maxArchiveSize) {
        throw ParseError("AFPK archive exceeds configured size limit");
    }
    const auto header = readHeader(bytes);
    validateLayout(header, bytes.size());
    validateLimits(header, bytes.size(), limits);
    return parseMetadata(
        header,
        bytes.size(),
        bytes.subspan(
            static_cast<std::size_t>(header.entryTableOffset),
            static_cast<std::size_t>(header.entryTableSize)),
        bytes.subspan(
            static_cast<std::size_t>(header.stringTableOffset),
            static_cast<std::size_t>(header.stringTableSize)),
        limits);
}

Pack Pack::open(
    const std::filesystem::path& path,
    const ParseLimits& limits) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw ParseError("cannot open pack: " + path.string());
    }
    return open(input, path, limits);
}

Pack Pack::open(
    std::istream& input,
    const std::filesystem::path& sourcePath,
    const ParseLimits& limits) {
    input.clear();
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw ParseError("cannot determine pack size: " + sourcePath.string());
    }
    const auto physicalSize = static_cast<std::uint64_t>(end);
    if (physicalSize > limits.maxArchiveSize) {
        throw ParseError("AFPK archive exceeds configured size limit");
    }

    std::array<std::uint8_t, kHeaderSize> headerBytes{};
    readExact(input, 0U, headerBytes, sourcePath);
    const auto header = readHeader(headerBytes);
    validateLayout(header, physicalSize);
    validateLimits(header, physicalSize, limits);

    if (header.dataOffset > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("AFPK metadata is too large for this process");
    }
    const auto metadataSize = header.dataOffset - header.entryTableOffset;
    if (metadataSize > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("AFPK metadata is too large for this process");
    }
    std::vector<std::uint8_t> metadata(static_cast<std::size_t>(metadataSize));
    readExact(input, header.entryTableOffset, metadata, sourcePath);
    const auto metadataView = std::span<const std::uint8_t>(metadata);
    const auto stringStart = static_cast<std::size_t>(
        header.stringTableOffset - header.entryTableOffset);
    return parseMetadata(
        header,
        physicalSize,
        metadataView.first(static_cast<std::size_t>(header.entryTableSize)),
        metadataView.subspan(stringStart, static_cast<std::size_t>(header.stringTableSize)),
        limits);
}

Pack Pack::parseMetadata(
    Header header,
    const std::uint64_t archiveSize,
    const std::span<const std::uint8_t> entryTable,
    const std::span<const std::uint8_t> stringTable,
    const ParseLimits& limits) {
    Pack pack;
    pack.header_ = header;
    pack.archiveSize_ = archiveSize;
    pack.entries_.reserve(header.entryCount);

    std::set<std::string> paths;
    std::size_t manifestCount = 0U;
    std::string previousPath;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> dataRanges;
    dataRanges.reserve(header.entryCount);

    for (std::size_t index = 0U; index < header.entryCount; ++index) {
        const auto offset = index * kEntryRecordSize;
        Entry entry{
            .pathOffset = readU64(entryTable, offset, "entry.pathOffset"),
            .pathSize = readU32(entryTable, offset + 8U, "entry.pathSize"),
            .kind = static_cast<EntryKind>(readU16(entryTable, offset + 12U, "entry.kind")),
            .flags = readU16(entryTable, offset + 14U, "entry.flags"),
            .dataOffset = readU64(entryTable, offset + 16U, "entry.dataOffset"),
            .storedSize = readU64(entryTable, offset + 24U, "entry.storedSize"),
            .contentSize = readU64(entryTable, offset + 32U, "entry.contentSize"),
            .sha256 = {},
            .reserved72 = readU64(entryTable, offset + 72U, "entry.reserved72"),
            .path = {},
        };
        std::copy_n(entryTable.begin() + static_cast<std::ptrdiff_t>(offset + 40U),
            entry.sha256.size(), entry.sha256.begin());

        if (entry.pathSize > limits.maxPathSize) {
            throw ParseError("AFPK entry path exceeds configured size limit");
        }
        requireRange(entry.pathOffset, entry.pathSize, stringTable.size(), "entry path");
        const auto pathBytes = stringTable.subspan(
            static_cast<std::size_t>(entry.pathOffset), entry.pathSize);
        entry.path.assign(reinterpret_cast<const char*>(pathBytes.data()), pathBytes.size());
        validateLogicalPath(entry.path);
        if (!previousPath.empty() && entry.path <= previousPath) {
            throw ParseError("AFPK entries are not strictly path sorted");
        }
        previousPath = entry.path;
        if (!paths.insert(entry.path).second) {
            throw ParseError("AFPK contains a duplicate path");
        }
        if (!isKnownKind(entry.kind) || entry.flags != 0U || entry.reserved72 != 0U) {
            throw ParseError("AFPK entry uses unsupported fields");
        }
        if (entry.storedSize > limits.maxPayloadSize ||
            entry.contentSize > limits.maxPayloadSize) {
            throw ParseError("AFPK entry payload exceeds configured size limit");
        }
        if (entry.storedSize != entry.contentSize) {
            throw ParseError("AFPK v1 entries must be stored without outer compression");
        }
        if (entry.dataOffset < header.dataOffset || entry.dataOffset % kDataAlignment != 0U) {
            throw ParseError("AFPK entry data offset is invalid");
        }
        requireRange(entry.dataOffset, entry.storedSize, archiveSize, "entry data");
        if (std::all_of(entry.sha256.begin(), entry.sha256.end(),
                [](const std::uint8_t value) { return value == 0U; })) {
            throw ParseError("AFPK entry has an empty SHA-256 digest");
        }
        if (entry.kind == EntryKind::manifest) {
            ++manifestCount;
            if (entry.path != "manifest.json") {
                throw ParseError("AFPK manifest entry must be named manifest.json");
            }
        }
        dataRanges.emplace_back(
            entry.dataOffset,
            checkedAdd(entry.dataOffset, entry.storedSize, "entry data"));
        pack.entries_.push_back(std::move(entry));
    }

    if (manifestCount != 1U) {
        throw ParseError("AFPK must contain exactly one manifest.json entry");
    }
    std::sort(dataRanges.begin(), dataRanges.end());
    for (std::size_t index = 1U; index < dataRanges.size(); ++index) {
        if (dataRanges[index].first < dataRanges[index - 1U].second) {
            throw ParseError("AFPK entry data ranges overlap");
        }
    }
    return pack;
}

std::vector<std::uint8_t> Pack::readEntry(
    const std::filesystem::path& path,
    const std::size_t index,
    const std::uint64_t maxBytes) const {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw ParseError("cannot open pack: " + path.string());
    }
    return readEntry(input, path, index, maxBytes);
}

std::vector<std::uint8_t> Pack::readEntry(
    std::istream& input,
    const std::filesystem::path& sourcePath,
    const std::size_t index,
    const std::uint64_t maxBytes) const {
    if (index >= entries_.size()) {
        throw ParseError("AFPACK entry index is outside the pack");
    }
    const auto& entry = entries_[index];
    if (entry.storedSize > maxBytes) {
        throw ParseError("AFPACK entry exceeds requested read limit");
    }
    requireRange(entry.dataOffset, entry.storedSize, archiveSize_, "entry data");
    if (entry.storedSize > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("AFPACK entry is too large for this process");
    }
    if (entry.dataOffset > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max()) ||
        entry.storedSize > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamsize>::max())) {
        throw ParseError("AFPACK entry range exceeds stream limits");
    }

    input.clear();
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) != archiveSize_) {
        throw ParseError("AFPACK payload source size mismatch: " + sourcePath.string());
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(entry.storedSize));
    readExact(input, entry.dataOffset, bytes, sourcePath);
    if (crypto::sha256(bytes) != entry.sha256) {
        throw ParseError("AFPACK SHA-256 mismatch: " + entry.path);
    }
    return bytes;
}

void Pack::verifyPayloads(const std::filesystem::path& path) const {
    std::error_code sizeError;
    const auto physicalSize = std::filesystem::file_size(path, sizeError);
    if (sizeError || physicalSize != archiveSize_) {
        throw ParseError("AFPACK payload source size mismatch: " + path.string());
    }
    for (const auto& entry : entries_) {
        const auto digest = crypto::sha256FileRegion(path, entry.dataOffset, entry.storedSize);
        if (digest != entry.sha256) {
            throw ParseError("AFPACK SHA-256 mismatch: " + entry.path);
        }
    }
}

} // namespace airfix::afpack
