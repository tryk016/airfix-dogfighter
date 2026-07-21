#include "airfix/archive/UdspArchive.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <sstream>

namespace airfix::udsp {
namespace {

constexpr std::array<std::uint32_t, 16> kHashPrimes{
    3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U,
    29U, 31U, 37U, 41U, 43U, 47U, 53U, 59U,
};

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
        throw ParseError(std::string(field) + " is outside the archive");
    }
}

[[nodiscard]] std::uint32_t readU32(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, sizeof(std::uint32_t), bytes.size(), field);
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::string readName(
    const std::span<const std::uint8_t> strings,
    const std::uint32_t offset,
    const std::string_view kind) {
    if (offset >= strings.size()) {
        throw ParseError(std::string(kind) + " name offset is outside the string table");
    }

    const auto begin = strings.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, strings.end(), std::uint8_t{0});
    if (end == strings.end()) {
        throw ParseError(std::string(kind) + " name is not NUL terminated");
    }

    return {reinterpret_cast<const char*>(&*begin), static_cast<std::size_t>(end - begin)};
}

[[nodiscard]] std::int32_t legacyLower(const std::uint8_t value) noexcept {
    // UdsPack.dll passes a signed char directly to MSVCRT tolower. Preserve the
    // resulting negative contribution for bytes >= 0x80; localized filenames
    // depend on this legacy behavior.
    std::int32_t signedValue = value < 0x80U
        ? static_cast<std::int32_t>(value)
        : static_cast<std::int32_t>(value) - 0x100;
    if (signedValue >= 'A' && signedValue <= 'Z') {
        signedValue += 'a' - 'A';
    }
    return signedValue;
}

[[nodiscard]] std::string indexedField(
    const std::string_view kind,
    const std::size_t index) {
    std::ostringstream output;
    output << kind << '[' << index << ']';
    return output.str();
}

[[nodiscard]] Header readHeader(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) {
        throw ParseError("archive is shorter than the UDSP header");
    }
    if (bytes[0] != 'U' || bytes[1] != 'D' || bytes[2] != 'S' || bytes[3] != 'P') {
        throw ParseError("invalid UDSP magic");
    }

    return {
        .version = readU32(bytes, 4U, "version"),
        .directoryBytes = readU32(bytes, 8U, "directoryBytes"),
        .directoryOffset = readU32(bytes, 12U, "directoryOffset"),
        .stringBytes = readU32(bytes, 16U, "stringBytes"),
        .stringOffset = readU32(bytes, 20U, "stringOffset"),
        .fileBytes = readU32(bytes, 24U, "fileBytes"),
        .fileOffset = readU32(bytes, 28U, "fileOffset"),
    };
}

void validateLayout(const Header& header, const std::uint64_t archiveSize) {
    if (header.version != kVersion) {
        throw ParseError("unsupported UDSP version");
    }
    if (header.directoryBytes % kRecordSize != 0U || header.fileBytes % kRecordSize != 0U) {
        throw ParseError("UDSP record table size is not a multiple of 24");
    }
    if (header.directoryOffset < kHeaderSize) {
        throw ParseError("UDSP payload overlaps the header");
    }

    const auto directoryEnd = checkedAdd(
        header.directoryOffset, header.directoryBytes, "directory table");
    const auto fileEnd = checkedAdd(header.fileOffset, header.fileBytes, "file table");
    const auto stringEnd = checkedAdd(header.stringOffset, header.stringBytes, "string table");
    if (directoryEnd != header.fileOffset || fileEnd != header.stringOffset ||
        stringEnd != archiveSize) {
        throw ParseError("UDSP metadata tables are not contiguous at end of archive");
    }

    requireRange(header.directoryOffset, header.directoryBytes, archiveSize, "directory table");
    requireRange(header.fileOffset, header.fileBytes, archiveSize, "file table");
    requireRange(header.stringOffset, header.stringBytes, archiveSize, "string table");
}

void readExact(
    std::ifstream& input,
    const std::uint64_t offset,
    const std::span<std::uint8_t> output,
    const std::filesystem::path& path) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        output.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw ParseError("archive range exceeds stream limits: " + path.string());
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input || (!output.empty() && !input.read(
            reinterpret_cast<char*>(output.data()),
            static_cast<std::streamsize>(output.size())))) {
        throw ParseError("cannot read archive: " + path.string());
    }
}

} // namespace

std::uint32_t nameHash(const std::string_view name) noexcept {
    const auto first = name.size() > 15U ? name.size() - 15U : 0U;
    std::uint32_t hash = static_cast<std::uint32_t>(name.size());

    for (std::size_t index = first; index < name.size(); ++index) {
        const auto primeIndex = name.size() - index;
        const auto character = legacyLower(static_cast<std::uint8_t>(name[index]));
        const auto contribution = character * static_cast<std::int32_t>(kHashPrimes[primeIndex]);
        hash += static_cast<std::uint32_t>(contribution);
    }
    return hash;
}

Archive Archive::parse(const std::span<const std::uint8_t> bytes) {
    const auto header = readHeader(bytes);
    validateLayout(header, bytes.size());
    return parseMetadata(
        header,
        bytes.size(),
        bytes.subspan(header.directoryOffset, header.directoryBytes),
        bytes.subspan(header.fileOffset, header.fileBytes),
        bytes.subspan(header.stringOffset, header.stringBytes));
}

Archive Archive::parseMetadata(
    Header header,
    const std::uint64_t archiveSize,
    const std::span<const std::uint8_t> directoryTable,
    const std::span<const std::uint8_t> fileTable,
    const std::span<const std::uint8_t> stringTable) {
    Archive archive;
    archive.archiveSize_ = archiveSize;
    archive.header_ = header;
    const auto directoryCount = header.directoryBytes / kRecordSize;
    const auto fileCount = header.fileBytes / kRecordSize;
    archive.directories_.reserve(directoryCount);
    archive.files_.reserve(fileCount);

    std::uint32_t previousHash = 0U;
    for (std::size_t index = 0; index < directoryCount; ++index) {
        const auto offset = index * kRecordSize;
        const auto field = indexedField("directory", index);
        DirectoryEntry entry{
            .hash = readU32(directoryTable, offset, field),
            .nameOffset = readU32(directoryTable, offset + 4U, field),
            .unknown08 = readU32(directoryTable, offset + 8U, field),
            .unknown0C = readU32(directoryTable, offset + 12U, field),
            .fileCount = readU32(directoryTable, offset + 16U, field),
            .fileTableByteOffset = readU32(directoryTable, offset + 20U, field),
            .firstFileIndex = 0U,
            .path = {},
        };
        entry.path = readName(stringTable, entry.nameOffset, field);
        if (entry.hash != nameHash(entry.path)) {
            throw ParseError(field + " has an invalid name hash");
        }
        if (index != 0U && entry.hash < previousHash) {
            throw ParseError("directory table is not sorted by name hash");
        }
        previousHash = entry.hash;
        if (entry.fileTableByteOffset % kRecordSize != 0U) {
            throw ParseError(field + " file range is not record aligned");
        }
        const auto byteCount = static_cast<std::uint64_t>(entry.fileCount) * kRecordSize;
        requireRange(entry.fileTableByteOffset, byteCount, header.fileBytes, field + " file range");
        entry.firstFileIndex = entry.fileTableByteOffset / kRecordSize;
        archive.directories_.push_back(std::move(entry));
    }

    for (std::size_t index = 0; index < fileCount; ++index) {
        const auto offset = index * kRecordSize;
        const auto field = indexedField("file", index);
        FileEntry entry{
            .hash = readU32(fileTable, offset, field),
            .nameOffset = readU32(fileTable, offset + 4U, field),
            .flags = readU32(fileTable, offset + 8U, field),
            .unpackedSize = readU32(fileTable, offset + 12U, field),
            .storedSize = readU32(fileTable, offset + 16U, field),
            .dataOffset = readU32(fileTable, offset + 20U, field),
            .name = {},
        };
        entry.name = readName(stringTable, entry.nameOffset, field);
        if (entry.hash != nameHash(entry.name)) {
            throw ParseError(field + " has an invalid name hash");
        }
        if ((entry.flags & ~kCompressedFlag) != 0U) {
            throw ParseError(field + " uses unknown flags");
        }
        if (!entry.isCompressed() && entry.unpackedSize != entry.storedSize) {
            throw ParseError(field + " has inconsistent uncompressed sizes");
        }
        if (entry.dataOffset < kHeaderSize) {
            throw ParseError(field + " data overlaps the header");
        }
        requireRange(entry.dataOffset, entry.storedSize, header.directoryOffset, field + " data");
        archive.files_.push_back(std::move(entry));
    }

    for (std::size_t directoryIndex = 0;
         directoryIndex < archive.directories_.size();
         ++directoryIndex) {
        const auto& directory = archive.directories_[directoryIndex];
        const auto first = static_cast<std::size_t>(directory.firstFileIndex);
        const auto end = first + static_cast<std::size_t>(directory.fileCount);
        for (auto index = first + (first < end ? 1U : 0U); index < end; ++index) {
            if (archive.files_[index].hash < archive.files_[index - 1U].hash) {
                throw ParseError(
                    indexedField("directory", directoryIndex) +
                    " file range is not sorted by name hash");
            }
        }
    }

    return archive;
}

Archive Archive::open(const std::filesystem::path& path) {
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        throw ParseError("cannot determine archive size: " + path.string());
    }
    return openRegion(path, 0U, size);
}

Archive Archive::openRegion(
    const std::filesystem::path& path,
    const std::uint64_t offset,
    const std::uint64_t size) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw ParseError("cannot open archive: " + path.string());
    }

    const auto end = input.tellg();
    if (end < 0) {
        throw ParseError("cannot determine archive size: " + path.string());
    }
    const auto physicalSize = static_cast<std::uint64_t>(end);
    requireRange(offset, size, physicalSize, "UDSP containing-file region");
    if (size < kHeaderSize) {
        throw ParseError("archive region is shorter than the UDSP header");
    }

    std::array<std::uint8_t, kHeaderSize> headerBytes{};
    readExact(input, offset, headerBytes, path);
    const auto header = readHeader(headerBytes);
    validateLayout(header, size);

    const auto metadataSize = size - header.directoryOffset;
    if (metadataSize > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("UDSP metadata is too large for this process");
    }
    std::vector<std::uint8_t> metadata(static_cast<std::size_t>(metadataSize));
    readExact(
        input,
        checkedAdd(offset, header.directoryOffset, "UDSP metadata absolute offset"),
        metadata,
        path);

    const auto directoryStart = std::size_t{0};
    const auto fileStart = static_cast<std::size_t>(header.fileOffset - header.directoryOffset);
    const auto stringStart = static_cast<std::size_t>(header.stringOffset - header.directoryOffset);
    const auto metadataView = std::span<const std::uint8_t>(metadata);
    auto archive = parseMetadata(
        header,
        size,
        metadataView.subspan(directoryStart, header.directoryBytes),
        metadataView.subspan(fileStart, header.fileBytes),
        metadataView.subspan(stringStart, header.stringBytes));
    archive.backingOffset_ = offset;
    return archive;
}

std::vector<std::uint8_t> decompress(
    const std::span<const std::uint8_t> encoded,
    const std::size_t expectedSize,
    const std::size_t outputLimit) {
    if (expectedSize > outputLimit) {
        throw ParseError("decompressed size exceeds the configured limit");
    }

    std::vector<std::uint8_t> output;
    output.reserve(expectedSize);
    std::size_t inputIndex = 0U;

    const auto requireOutput = [&](const std::size_t additional) {
        if (additional > expectedSize - output.size()) {
            throw ParseError("compressed block exceeds the declared output size");
        }
    };

    while (inputIndex < encoded.size()) {
        const auto opcode = encoded[inputIndex++];
        if (opcode == 0x65U) {
            if (encoded.size() - inputIndex < 5U) {
                throw ParseError("truncated UDSP repeated-pattern block");
            }
            const auto count = encoded[inputIndex++];
            const std::array<std::uint8_t, 4> pattern{
                encoded[inputIndex], encoded[inputIndex + 1U],
                encoded[inputIndex + 2U], encoded[inputIndex + 3U],
            };
            inputIndex += pattern.size();
            const auto outputBytes = ((static_cast<std::size_t>(count) + 3U) / 4U) * 4U;
            requireOutput(outputBytes);
            for (std::size_t index = 0; index < outputBytes; ++index) {
                output.push_back(pattern[index % pattern.size()]);
            }
        }
        else if (opcode == 0x66U || opcode == 0x67U) {
            if (inputIndex >= encoded.size()) {
                throw ParseError("truncated UDSP literal-block header");
            }
            const auto count = static_cast<std::size_t>(encoded[inputIndex++]);
            if (count > encoded.size() - inputIndex) {
                throw ParseError("truncated UDSP literal block");
            }
            requireOutput(count);
            output.insert(
                output.end(),
                encoded.begin() + static_cast<std::ptrdiff_t>(inputIndex),
                encoded.begin() + static_cast<std::ptrdiff_t>(inputIndex + count));
            inputIndex += count;
        }
        else {
            throw ParseError("unknown UDSP compression opcode");
        }
    }

    if (output.size() != expectedSize) {
        throw ParseError("decompressed size does not match the file record");
    }
    return output;
}

} // namespace airfix::udsp
