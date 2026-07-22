#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace airfix::udsp {

inline constexpr std::uint32_t kVersion = 0x00000101U;
inline constexpr std::size_t kHeaderSize = 32U;
inline constexpr std::size_t kRecordSize = 24U;
inline constexpr std::uint32_t kCompressedFlag = 0x00000001U;

class ParseError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Header {
    std::uint32_t version{};
    std::uint32_t directoryBytes{};
    std::uint32_t directoryOffset{};
    std::uint32_t stringBytes{};
    std::uint32_t stringOffset{};
    std::uint32_t fileBytes{};
    std::uint32_t fileOffset{};
};

struct DirectoryEntry {
    std::uint32_t hash{};
    std::uint32_t nameOffset{};
    std::uint32_t unknown08{};
    std::uint32_t unknown0C{};
    std::uint32_t fileCount{};
    std::uint32_t fileTableByteOffset{};
    std::uint32_t firstFileIndex{};
    std::string path;
};

struct FileEntry {
    std::uint32_t hash{};
    std::uint32_t nameOffset{};
    std::uint32_t flags{};
    std::uint32_t unpackedSize{};
    std::uint32_t storedSize{};
    std::uint32_t dataOffset{};
    std::string name;

    [[nodiscard]] bool isCompressed() const noexcept {
        return (flags & kCompressedFlag) != 0U;
    }
};

enum class LookupStatus : std::uint8_t {
    notFound,
    unique,
    ambiguous,
};

struct FileLookupResult {
    LookupStatus status{LookupStatus::notFound};
    std::size_t directoryIndex{};
    std::size_t fileIndex{};
};

class Archive final {
public:
    [[nodiscard]] static Archive parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static Archive open(const std::filesystem::path& path);
    [[nodiscard]] static Archive openRegion(
        const std::filesystem::path& path,
        std::uint64_t offset,
        std::uint64_t size);

    [[nodiscard]] const Header& header() const noexcept { return header_; }
    [[nodiscard]] const std::vector<DirectoryEntry>& directories() const noexcept {
        return directories_;
    }
    [[nodiscard]] const std::vector<FileEntry>& files() const noexcept { return files_; }
    [[nodiscard]] std::uint64_t archiveSize() const noexcept { return archiveSize_; }
    [[nodiscard]] std::uint64_t backingOffset() const noexcept { return backingOffset_; }
    [[nodiscard]] FileLookupResult lookup(
        std::string_view logicalPath,
        std::size_t pathLimit = 4096U) const;

private:
    [[nodiscard]] static Archive parseMetadata(
        Header header,
        std::uint64_t archiveSize,
        std::span<const std::uint8_t> directoryTable,
        std::span<const std::uint8_t> fileTable,
        std::span<const std::uint8_t> stringTable);

    Header header_;
    std::vector<DirectoryEntry> directories_;
    std::vector<FileEntry> files_;
    std::uint64_t archiveSize_{};
    std::uint64_t backingOffset_{};
};

[[nodiscard]] std::uint32_t nameHash(std::string_view name) noexcept;
[[nodiscard]] std::string normalizeLogicalPath(
    std::string_view logicalPath,
    std::size_t pathLimit = 4096U);

[[nodiscard]] std::vector<std::uint8_t> decompress(
    std::span<const std::uint8_t> encoded,
    std::size_t expectedSize,
    std::size_t outputLimit);

[[nodiscard]] std::vector<std::uint8_t> readFilePrefix(
    const std::filesystem::path& path,
    const Archive& archive,
    const FileEntry& entry,
    std::size_t maximumBytes);

[[nodiscard]] std::vector<std::uint8_t> readFile(
    const std::filesystem::path& path,
    const Archive& archive,
    const FileEntry& entry,
    std::size_t outputLimit);

} // namespace airfix::udsp
