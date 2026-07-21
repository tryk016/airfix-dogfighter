#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace airfix::afpack {

inline constexpr std::uint16_t kVersionMajor = 1U;
inline constexpr std::uint16_t kVersionMinor = 0U;
inline constexpr std::size_t kHeaderSize = 80U;
inline constexpr std::size_t kEntryRecordSize = 80U;
inline constexpr std::size_t kDataAlignment = 16U;

class ParseError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class EntryKind : std::uint16_t {
    manifest = 1U,
    sourceArchive = 2U,
    localization = 3U,
    video = 4U,
    convertedAsset = 5U,
};

struct Header {
    std::uint16_t versionMajor{};
    std::uint16_t versionMinor{};
    std::uint32_t headerSize{};
    std::uint32_t flags{};
    std::uint32_t entryCount{};
    std::uint32_t reserved20{};
    std::uint64_t entryTableOffset{};
    std::uint64_t entryTableSize{};
    std::uint64_t stringTableOffset{};
    std::uint64_t stringTableSize{};
    std::uint64_t dataOffset{};
    std::uint64_t archiveSize{};
    std::uint64_t reserved72{};
};

struct Entry {
    std::uint64_t pathOffset{};
    std::uint32_t pathSize{};
    EntryKind kind{};
    std::uint16_t flags{};
    std::uint64_t dataOffset{};
    std::uint64_t storedSize{};
    std::uint64_t contentSize{};
    std::array<std::uint8_t, 32> sha256{};
    std::uint64_t reserved72{};
    std::string path;
};

class Pack final {
public:
    [[nodiscard]] static Pack parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static Pack open(const std::filesystem::path& path);

    [[nodiscard]] const Header& header() const noexcept { return header_; }
    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::uint64_t archiveSize() const noexcept { return archiveSize_; }
    void verifyPayloads(const std::filesystem::path& path) const;

private:
    [[nodiscard]] static Pack parseMetadata(
        Header header,
        std::uint64_t archiveSize,
        std::span<const std::uint8_t> entryTable,
        std::span<const std::uint8_t> stringTable);

    Header header_;
    std::vector<Entry> entries_;
    std::uint64_t archiveSize_{};
};

void validateLogicalPath(std::string_view path);

} // namespace airfix::afpack
