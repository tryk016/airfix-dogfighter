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

void requireStreamRangeRepresentable(
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::string_view field) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw ParseError(std::string(field) + " offset exceeds stream limits");
    }
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        throw ParseError(std::string(field) + " read size exceeds stream limits");
    }
}

[[nodiscard]] std::string_view readName(
    const std::span<const std::uint8_t> strings,
    const std::uint32_t offset,
    const std::string_view kind,
    const std::uint64_t maxNameSize) {
    if (offset >= strings.size()) {
        throw ParseError(std::string(kind) + " name offset is outside the string table");
    }

    const auto begin = strings.begin() + static_cast<std::ptrdiff_t>(offset);
    auto end = begin;
    std::uint64_t nameSize = 0U;
    while (end != strings.end() && *end != std::uint8_t{0}) {
        if (nameSize == maxNameSize) {
            throw ParseError(std::string(kind) + " name exceeds the configured limit");
        }
        ++end;
        ++nameSize;
    }
    if (end == strings.end()) {
        throw ParseError(std::string(kind) + " name is not NUL terminated");
    }

    return {reinterpret_cast<const char*>(&*begin), static_cast<std::size_t>(end - begin)};
}

[[nodiscard]] std::string decodeName(
    const std::span<const std::uint8_t> strings,
    const std::uint32_t offset,
    const std::string_view kind,
    const ParseLimits& limits,
    std::uint64_t& totalDecodedNameBytes) {
    const auto name = readName(strings, offset, kind, limits.maxNameSize);
    const auto nextTotal = checkedAdd(
        totalDecodedNameBytes, name.size(), "UDSP total decoded name bytes");
    if (nextTotal > limits.maxTotalDecodedNameBytes) {
        throw ParseError("UDSP total decoded name bytes exceeds the configured limit");
    }
    totalDecodedNameBytes = nextTotal;
    return std::string(name);
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

[[nodiscard]] bool legacyEqual(
    const std::string_view left,
    const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (legacyLower(static_cast<std::uint8_t>(left[index])) !=
            legacyLower(static_cast<std::uint8_t>(right[index]))) {
            return false;
        }
    }
    return true;
}

enum class LogicalPathError : std::uint8_t {
    none,
    size,
    forbiddenCharacter,
    boundarySeparator,
    unsafeComponent,
};

[[nodiscard]] LogicalPathError inspectLogicalPath(
    const std::string_view logicalPath,
    const std::size_t pathLimit) noexcept {
    if (logicalPath.empty() || logicalPath.size() > pathLimit) {
        return LogicalPathError::size;
    }
    for (const auto character : logicalPath) {
        const auto value = static_cast<std::uint8_t>(character);
        if (value < 0x20U || value == 0x7FU || character == ':') {
            return LogicalPathError::forbiddenCharacter;
        }
    }
    const auto isSeparator = [](const char character) noexcept {
        return character == '\\' || character == '/';
    };
    if (isSeparator(logicalPath.front()) || isSeparator(logicalPath.back())) {
        return LogicalPathError::boundarySeparator;
    }
    std::size_t componentBegin = 0U;
    for (std::size_t index = 0U; index <= logicalPath.size(); ++index) {
        if (index != logicalPath.size() && !isSeparator(logicalPath[index])) {
            continue;
        }
        const auto component = logicalPath.substr(
            componentBegin, index - componentBegin);
        if (component.empty() || component == "." || component == "..") {
            return LogicalPathError::unsafeComponent;
        }
        componentBegin = index + 1U;
    }
    return LogicalPathError::none;
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

void validateLimits(
    const Header& header,
    const std::uint64_t archiveSize,
    const ParseLimits& limits) {
    if (archiveSize > limits.maxArchiveSize) {
        throw ParseError("UDSP archive exceeds the configured size limit");
    }

    const auto metadataSize = archiveSize - header.directoryOffset;
    if (metadataSize > limits.maxMetadataSize) {
        throw ParseError("UDSP metadata exceeds the configured size limit");
    }

    const auto directoryCount = header.directoryBytes / kRecordSize;
    if (directoryCount > limits.maxDirectoryCount) {
        throw ParseError("UDSP directory count exceeds the configured limit");
    }
    const auto fileCount = header.fileBytes / kRecordSize;
    if (fileCount > limits.maxFileCount) {
        throw ParseError("UDSP file count exceeds the configured limit");
    }
    if (header.stringBytes > limits.maxStringTableSize) {
        throw ParseError("UDSP string table exceeds the configured size limit");
    }
}

void readExact(
    std::istream& input,
    const std::uint64_t offset,
    const std::span<std::uint8_t> output,
    const std::string_view sourceLabel) {
    requireStreamRangeRepresentable(offset, output.size(), "archive range");
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input || (!output.empty() && !input.read(
            reinterpret_cast<char*>(output.data()),
            static_cast<std::streamsize>(output.size())))) {
        throw ParseError("cannot read archive: " + std::string(sourceLabel));
    }
}

[[nodiscard]] std::uint64_t currentStreamSize(
    std::istream& input,
    const std::string_view sourceLabel) {
    input.clear();
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw ParseError("cannot determine archive size: " + std::string(sourceLabel));
    }
    return static_cast<std::uint64_t>(end);
}

void requireCurrentStreamSize(
    std::istream& input,
    const std::uint64_t containingFileSize,
    const std::string_view sourceLabel) {
    if (currentStreamSize(input, sourceLabel) != containingFileSize) {
        throw ParseError(
            "archive stream size changed or does not match containingFileSize: " +
            std::string(sourceLabel));
    }
}

[[nodiscard]] const FileEntry& validateFileRead(
    const std::uint64_t containingFileSize,
    const Archive& archive,
    const std::size_t fileIndex) {
    if (fileIndex >= archive.files().size()) {
        throw ParseError("UDSP file index is outside the archive");
    }
    requireRange(
        archive.backingOffset(),
        archive.archiveSize(),
        containingFileSize,
        "UDSP archive backing region");
    if (archive.header().directoryOffset > archive.archiveSize()) {
        throw ParseError("UDSP payload boundary is outside the archive");
    }

    const auto& entry = archive.files()[fileIndex];
    requireRange(
        entry.dataOffset,
        entry.storedSize,
        archive.header().directoryOffset,
        "UDSP file data");
    const auto absoluteOffset = checkedAdd(
        archive.backingOffset(), entry.dataOffset, "UDSP file absolute offset");
    requireRange(
        absoluteOffset,
        entry.storedSize,
        checkedAdd(
            archive.backingOffset(), archive.archiveSize(), "UDSP archive backing end"),
        "UDSP file backing data");
    return entry;
}

[[nodiscard]] std::size_t fileIndexForEntry(
    const Archive& archive,
    const FileEntry& entry) {
    for (std::size_t index = 0U; index < archive.files().size(); ++index) {
        const auto& candidate = archive.files()[index];
        if (&candidate == &entry ||
            (candidate.hash == entry.hash &&
             candidate.nameOffset == entry.nameOffset &&
             candidate.flags == entry.flags &&
             candidate.unpackedSize == entry.unpackedSize &&
             candidate.storedSize == entry.storedSize &&
             candidate.dataOffset == entry.dataOffset &&
             candidate.name == entry.name)) {
            return index;
        }
    }
    throw ParseError("UDSP file entry does not belong to the archive");
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

bool isLogicalPathValid(
    const std::string_view logicalPath,
    const std::size_t pathLimit) noexcept {
    return inspectLogicalPath(logicalPath, pathLimit) == LogicalPathError::none;
}

std::string normalizeLogicalPath(
    const std::string_view logicalPath,
    const std::size_t pathLimit) {
    switch (inspectLogicalPath(logicalPath, pathLimit)) {
    case LogicalPathError::size:
        throw ParseError("logical archive path is empty or exceeds its configured limit");
    case LogicalPathError::forbiddenCharacter:
        throw ParseError("logical archive path contains a forbidden character");
    case LogicalPathError::boundarySeparator:
        throw ParseError("logical archive path must be relative and name a file");
    case LogicalPathError::unsafeComponent:
        throw ParseError("logical archive path contains an unsafe component");
    case LogicalPathError::none:
        break;
    }
    std::string normalized(logicalPath);
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return normalized;
}

FileLookupResult Archive::lookup(
    const std::string_view logicalPath,
    const std::size_t pathLimit) const {
    const auto normalized = normalizeLogicalPath(logicalPath, pathLimit);
    const auto separator = normalized.find_last_of('\\');
    const auto directoryPath = separator == std::string::npos
        ? std::string_view{}
        : std::string_view(normalized).substr(0U, separator);
    const auto fileName = separator == std::string::npos
        ? std::string_view(normalized)
        : std::string_view(normalized).substr(separator + 1U);
    const auto directoryHash = nameHash(directoryPath);
    const auto fileHash = nameHash(fileName);

    FileLookupResult result;
    auto directory = std::lower_bound(
        directories_.begin(), directories_.end(), directoryHash,
        [](const DirectoryEntry& entry, const std::uint32_t hash) {
            return entry.hash < hash;
        });
    for (; directory != directories_.end() && directory->hash == directoryHash;
         ++directory) {
        if (!legacyEqual(directory->path, directoryPath)) {
            continue;
        }
        const auto directoryIndex = static_cast<std::size_t>(
            directory - directories_.begin());
        const auto first = static_cast<std::size_t>(directory->firstFileIndex);
        const auto end = first + static_cast<std::size_t>(directory->fileCount);
        auto file = std::lower_bound(
            files_.begin() + static_cast<std::ptrdiff_t>(first),
            files_.begin() + static_cast<std::ptrdiff_t>(end),
            fileHash,
            [](const FileEntry& entry, const std::uint32_t hash) {
                return entry.hash < hash;
            });
        for (; file != files_.begin() + static_cast<std::ptrdiff_t>(end) &&
               file->hash == fileHash;
             ++file) {
            if (!legacyEqual(file->name, fileName)) {
                continue;
            }
            const auto fileIndex = static_cast<std::size_t>(file - files_.begin());
            if (result.status == LookupStatus::unique) {
                if (result.fileIndex == fileIndex) {
                    // Aliased directory ranges can point at the same physical
                    // record; that is one file handle, not two candidates.
                    continue;
                }
                return {.status = LookupStatus::ambiguous};
            }
            result = {
                .status = LookupStatus::unique,
                .directoryIndex = directoryIndex,
                .fileIndex = fileIndex,
            };
        }
    }
    return result;
}

Archive Archive::parse(
    const std::span<const std::uint8_t> bytes,
    const ParseLimits& limits) {
    if (bytes.size() > limits.maxArchiveSize) {
        throw ParseError("UDSP archive exceeds the configured size limit");
    }
    const auto header = readHeader(bytes);
    validateLayout(header, bytes.size());
    validateLimits(header, bytes.size(), limits);
    return parseMetadata(
        header,
        bytes.size(),
        bytes.subspan(header.directoryOffset, header.directoryBytes),
        bytes.subspan(header.fileOffset, header.fileBytes),
        bytes.subspan(header.stringOffset, header.stringBytes),
        limits);
}

Archive Archive::parseMetadata(
    Header header,
    const std::uint64_t archiveSize,
    const std::span<const std::uint8_t> directoryTable,
    const std::span<const std::uint8_t> fileTable,
    const std::span<const std::uint8_t> stringTable,
    const ParseLimits& limits) {
    Archive archive;
    archive.archiveSize_ = archiveSize;
    archive.header_ = header;
    const auto directoryCount = header.directoryBytes / kRecordSize;
    const auto fileCount = header.fileBytes / kRecordSize;
    archive.directories_.reserve(directoryCount);
    archive.files_.reserve(fileCount);

    std::uint64_t totalDecodedNameBytes = 0U;
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
        entry.path = decodeName(
            stringTable, entry.nameOffset, field, limits, totalDecodedNameBytes);
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

    std::uint64_t totalUnpackedSize = 0U;
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
        if (entry.storedSize > limits.maxStoredEntrySize) {
            throw ParseError(field + " stored size exceeds the configured limit");
        }
        if (entry.unpackedSize > limits.maxUnpackedEntrySize) {
            throw ParseError(field + " unpacked size exceeds the configured limit");
        }
        totalUnpackedSize = checkedAdd(
            totalUnpackedSize, entry.unpackedSize, "UDSP total unpacked size");
        if (totalUnpackedSize > limits.maxTotalUnpackedSize) {
            throw ParseError("UDSP total unpacked size exceeds the configured limit");
        }
        entry.name = decodeName(
            stringTable, entry.nameOffset, field, limits, totalDecodedNameBytes);
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

Archive Archive::open(
    const std::filesystem::path& path,
    const ParseLimits& limits) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ParseError("cannot open archive: " + path.string());
    }
    return open(input, path.string(), limits);
}

Archive Archive::open(
    std::istream& input,
    const std::string_view sourceLabel,
    const ParseLimits& limits) {
    const auto size = currentStreamSize(input, sourceLabel);
    return openRegion(input, size, sourceLabel, 0U, size, limits);
}

Archive Archive::openRegion(
    const std::filesystem::path& path,
    const std::uint64_t offset,
    const std::uint64_t size,
    const ParseLimits& limits) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ParseError("cannot open archive: " + path.string());
    }

    const auto physicalSize = currentStreamSize(input, path.string());
    const auto sourceLabel = path.string();
    return openRegion(
        input,
        physicalSize,
        std::string_view(sourceLabel),
        offset,
        size,
        limits);
}

Archive Archive::openRegion(
    std::istream& input,
    const std::uint64_t containingFileSize,
    const std::string_view sourceLabel,
    const std::uint64_t offset,
    const std::uint64_t size,
    const ParseLimits& limits) {
    requireRange(offset, size, containingFileSize, "UDSP containing-file region");
    if (size > limits.maxArchiveSize) {
        throw ParseError("UDSP archive exceeds the configured size limit");
    }
    if (size < kHeaderSize) {
        throw ParseError("archive region is shorter than the UDSP header");
    }
    requireCurrentStreamSize(input, containingFileSize, sourceLabel);

    std::array<std::uint8_t, kHeaderSize> headerBytes{};
    readExact(input, offset, headerBytes, sourceLabel);
    const auto header = readHeader(headerBytes);
    validateLayout(header, size);
    validateLimits(header, size, limits);

    const auto metadataSize = size - header.directoryOffset;
    if (metadataSize > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("UDSP metadata is too large for this process");
    }
    const auto metadataOffset = checkedAdd(
        offset, header.directoryOffset, "UDSP metadata absolute offset");
    requireStreamRangeRepresentable(metadataOffset, metadataSize, "UDSP metadata");
    std::vector<std::uint8_t> metadata(static_cast<std::size_t>(metadataSize));
    readExact(input, metadataOffset, metadata, sourceLabel);

    const auto directoryStart = std::size_t{0};
    const auto fileStart = static_cast<std::size_t>(header.fileOffset - header.directoryOffset);
    const auto stringStart = static_cast<std::size_t>(header.stringOffset - header.directoryOffset);
    const auto metadataView = std::span<const std::uint8_t>(metadata);
    auto archive = parseMetadata(
        header,
        size,
        metadataView.subspan(directoryStart, header.directoryBytes),
        metadataView.subspan(fileStart, header.fileBytes),
        metadataView.subspan(stringStart, header.stringBytes),
        limits);
    archive.backingOffset_ = offset;
    return archive;
}

Archive Archive::openRegion(
    std::istream& input,
    const std::uint64_t containingFileSize,
    const char* const sourceLabel,
    const std::uint64_t offset,
    const std::uint64_t size,
    const ParseLimits& limits) {
    return openRegion(
        input,
        containingFileSize,
        sourceLabel == nullptr ? std::string_view{} : std::string_view(sourceLabel),
        offset,
        size,
        limits);
}

Archive Archive::openRegion(
    std::istream& input,
    const std::uint64_t containingFileSize,
    const std::string& sourceLabel,
    const std::uint64_t offset,
    const std::uint64_t size,
    const ParseLimits& limits) {
    return openRegion(
        input,
        containingFileSize,
        std::string_view(sourceLabel),
        offset,
        size,
        limits);
}

Archive Archive::openRegion(
    std::istream& input,
    const std::uint64_t containingFileSize,
    const std::filesystem::path& sourcePath,
    const std::uint64_t offset,
    const std::uint64_t size,
    const ParseLimits& limits) {
    const auto sourceLabel = sourcePath.string();
    return openRegion(
        input,
        containingFileSize,
        std::string_view(sourceLabel),
        offset,
        size,
        limits);
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

std::vector<std::uint8_t> readFilePrefix(
    const std::filesystem::path& path,
    const Archive& archive,
    const FileEntry& entry,
    const std::size_t maximumBytes) {
    const auto fileIndex = fileIndexForEntry(archive, entry);
    const auto targetSize = std::min<std::uint64_t>(maximumBytes, entry.unpackedSize);
    if (targetSize > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("UDSP file prefix is too large for this process");
    }
    if (!entry.isCompressed()) {
        requireStreamRangeRepresentable(
            checkedAdd(
                archive.backingOffset(),
                entry.dataOffset,
                "UDSP file absolute offset"),
            targetSize,
            "UDSP uncompressed file prefix");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ParseError("cannot open archive: " + path.string());
    }
    const auto containingFileSize = currentStreamSize(input, path.string());
    return readFilePrefix(
        input,
        containingFileSize,
        path.string(),
        archive,
        fileIndex,
        maximumBytes);
}

std::vector<std::uint8_t> readFilePrefix(
    std::istream& input,
    const std::uint64_t containingFileSize,
    const std::string_view sourceLabel,
    const Archive& archive,
    const std::size_t fileIndex,
    const std::size_t maximumBytes) {
    const auto& entry = validateFileRead(containingFileSize, archive, fileIndex);
    const auto targetSize = std::min<std::uint64_t>(maximumBytes, entry.unpackedSize);
    if (targetSize > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("UDSP file prefix is too large for this process");
    }
    const auto payloadOffset = checkedAdd(
        archive.backingOffset(), entry.dataOffset, "UDSP file absolute offset");
    if (!entry.isCompressed()) {
        requireStreamRangeRepresentable(
            payloadOffset, targetSize, "UDSP uncompressed file prefix");
    }
    requireCurrentStreamSize(input, containingFileSize, sourceLabel);
    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(targetSize));
    if (targetSize == 0U) {
        return output;
    }

    auto cursor = payloadOffset;
    auto inputRemaining = static_cast<std::uint64_t>(entry.storedSize);
    auto pull = [&](const std::span<std::uint8_t> destination) {
        if (destination.size() > inputRemaining) {
            throw ParseError("truncated UDSP file payload");
        }
        readExact(input, cursor, destination, sourceLabel);
        cursor = checkedAdd(cursor, destination.size(), "UDSP file cursor");
        inputRemaining -= destination.size();
    };

    if (!entry.isCompressed()) {
        output.resize(static_cast<std::size_t>(targetSize));
        pull(output);
        return output;
    }

    std::uint64_t produced = 0U;
    while (output.size() < targetSize) {
        std::array<std::uint8_t, 1> opcodeBytes{};
        pull(opcodeBytes);
        const auto opcode = opcodeBytes[0];
        if (opcode == 0x65U) {
            std::array<std::uint8_t, 5> block{};
            pull(block);
            const auto blockSize = ((static_cast<std::uint64_t>(block[0]) + 3U) / 4U) * 4U;
            if (blockSize > static_cast<std::uint64_t>(entry.unpackedSize) - produced) {
                throw ParseError("compressed block exceeds the declared output size");
            }
            const auto copied = std::min<std::uint64_t>(
                blockSize, targetSize - output.size());
            for (std::uint64_t index = 0U; index < copied; ++index) {
                output.push_back(block[1U + static_cast<std::size_t>(index % 4U)]);
            }
            produced += blockSize;
        }
        else if (opcode == 0x66U || opcode == 0x67U) {
            std::array<std::uint8_t, 1> countBytes{};
            pull(countBytes);
            const auto blockSize = static_cast<std::uint64_t>(countBytes[0]);
            if (blockSize > inputRemaining ||
                blockSize > static_cast<std::uint64_t>(entry.unpackedSize) - produced) {
                throw ParseError("truncated or oversized UDSP literal block");
            }
            const auto copied = static_cast<std::size_t>(std::min<std::uint64_t>(
                blockSize, targetSize - output.size()));
            const auto oldSize = output.size();
            output.resize(oldSize + copied);
            pull(std::span<std::uint8_t>(output).subspan(oldSize, copied));
            // If the requested prefix ends inside a literal, the unread suffix
            // is intentionally left on disk; no caller-visible cursor escapes.
            produced += blockSize;
        }
        else {
            throw ParseError("unknown UDSP compression opcode");
        }
    }
    return output;
}

std::vector<std::uint8_t> readFile(
    const std::filesystem::path& path,
    const Archive& archive,
    const FileEntry& entry,
    const std::size_t outputLimit) {
    const auto fileIndex = fileIndexForEntry(archive, entry);
    if (entry.unpackedSize > outputLimit) {
        throw ParseError("UDSP file exceeds the configured output limit");
    }
    if (entry.storedSize > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("UDSP stored file is too large for this process");
    }
    requireStreamRangeRepresentable(
        checkedAdd(
            archive.backingOffset(), entry.dataOffset, "UDSP file absolute offset"),
        entry.storedSize,
        "UDSP stored file");
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ParseError("cannot open archive: " + path.string());
    }
    const auto containingFileSize = currentStreamSize(input, path.string());
    return readFile(
        input,
        containingFileSize,
        path.string(),
        archive,
        fileIndex,
        outputLimit);
}

std::vector<std::uint8_t> readFile(
    std::istream& input,
    const std::uint64_t containingFileSize,
    const std::string_view sourceLabel,
    const Archive& archive,
    const std::size_t fileIndex,
    const std::size_t outputLimit) {
    const auto& entry = validateFileRead(containingFileSize, archive, fileIndex);
    if (entry.unpackedSize > outputLimit) {
        throw ParseError("UDSP file exceeds the configured output limit");
    }
    if (entry.storedSize > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("UDSP stored file is too large for this process");
    }
    const auto payloadOffset = checkedAdd(
        archive.backingOffset(), entry.dataOffset, "UDSP file absolute offset");
    requireStreamRangeRepresentable(
        payloadOffset, entry.storedSize, "UDSP stored file");
    requireCurrentStreamSize(input, containingFileSize, sourceLabel);
    std::vector<std::uint8_t> stored(static_cast<std::size_t>(entry.storedSize));
    readExact(input, payloadOffset, stored, sourceLabel);
    if (entry.isCompressed()) {
        return decompress(stored, entry.unpackedSize, outputLimit);
    }
    return stored;
}

} // namespace airfix::udsp
