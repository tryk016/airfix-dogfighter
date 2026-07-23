#pragma once

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/crypto/Sha256.hpp"
#include "airfix/package/AfPackWriter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace airfix::testing {

using Bytes = std::vector<std::uint8_t>;

struct UdspInputEntry {
    std::string logicalPath;
    Bytes bytes;
    std::uint32_t flags{};
    std::optional<std::uint32_t> unpackedSize;
};

namespace detail {

struct PreparedFile {
    std::string name;
    Bytes bytes;
    std::uint32_t hash{};
    std::uint32_t nameOffset{};
    std::uint32_t dataOffset{};
    std::uint32_t flags{};
    std::uint32_t unpackedSize{};
};

struct PreparedDirectory {
    std::string path;
    std::uint32_t hash{};
    std::uint32_t nameOffset{};
    std::uint32_t firstFileIndex{};
    std::vector<PreparedFile> files;
};

[[nodiscard]] inline std::uint32_t checkedU32(
    const std::uint64_t value,
    const std::string_view field) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(
            "synthetic UDSP " + std::string(field) + " exceeds the 32-bit format limit");
    }
    return static_cast<std::uint32_t>(value);
}

inline void appendU32(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

inline void writeU32(
    Bytes& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) {
        throw std::runtime_error("synthetic UDSP header write is out of range");
    }
    for (std::size_t byte = 0U; byte < sizeof(value); ++byte) {
        bytes[offset + byte] =
            static_cast<std::uint8_t>(value >> static_cast<unsigned>(byte * 8U));
    }
}

inline void appendRecord(
    Bytes& bytes,
    const std::uint32_t field00,
    const std::uint32_t field04,
    const std::uint32_t field08,
    const std::uint32_t field0C,
    const std::uint32_t field10,
    const std::uint32_t field14) {
    appendU32(bytes, field00);
    appendU32(bytes, field04);
    appendU32(bytes, field08);
    appendU32(bytes, field0C);
    appendU32(bytes, field10);
    appendU32(bytes, field14);
}

[[nodiscard]] inline std::uint32_t appendString(
    Bytes& strings,
    const std::string_view text) {
    const auto offset = checkedU32(strings.size(), "string offset");
    strings.insert(strings.end(), text.begin(), text.end());
    strings.push_back(0U);
    return offset;
}

[[nodiscard]] inline std::vector<PreparedDirectory> prepareDirectories(
    const std::span<const UdspInputEntry> inputEntries) {
    std::vector<PreparedDirectory> directories;
    directories.reserve(inputEntries.size());

    for (const auto& input : inputEntries) {
        const auto normalized = udsp::normalizeLogicalPath(input.logicalPath);
        const auto separator = normalized.find_last_of('\\');
        const auto directoryPath = separator == std::string::npos
            ? std::string{}
            : normalized.substr(0U, separator);
        const auto fileName = separator == std::string::npos
            ? normalized
            : normalized.substr(separator + 1U);

        if (directoryPath.size() > udsp::ParseLimits{}.maxNameSize ||
            fileName.size() > udsp::ParseLimits{}.maxNameSize) {
            throw std::runtime_error(
                "synthetic UDSP path component exceeds the default parser name limit");
        }
        if (input.bytes.size() > udsp::ParseLimits{}.maxStoredEntrySize) {
            throw std::runtime_error(
                "synthetic UDSP entry exceeds the default parser payload limit");
        }
        if (input.flags != 0U && input.flags != udsp::kCompressedFlag) {
            throw std::runtime_error(
                "synthetic UDSP entry uses unsupported flags");
        }
        const auto unpackedSize = input.unpackedSize.value_or(
            checkedU32(input.bytes.size(), "unpacked file size"));

        auto directory = std::find_if(
            directories.begin(),
            directories.end(),
            [&](const PreparedDirectory& candidate) {
                return candidate.path == directoryPath;
            });
        if (directory == directories.end()) {
            directories.push_back(PreparedDirectory{
                .path = directoryPath,
                .hash = udsp::nameHash(directoryPath),
                .nameOffset = 0U,
                .firstFileIndex = 0U,
                .files = {},
            });
            directory = std::prev(directories.end());
        }
        directory->files.push_back(PreparedFile{
            .name = fileName,
            .bytes = input.bytes,
            .hash = udsp::nameHash(fileName),
            .flags = input.flags,
            .unpackedSize = unpackedSize,
        });
    }

    std::sort(
        directories.begin(),
        directories.end(),
        [](const PreparedDirectory& left, const PreparedDirectory& right) {
            if (left.hash != right.hash) {
                return left.hash < right.hash;
            }
            return left.path < right.path;
        });
    for (auto& directory : directories) {
        std::stable_sort(
            directory.files.begin(),
            directory.files.end(),
            [](const PreparedFile& left, const PreparedFile& right) {
                if (left.hash != right.hash) {
                    return left.hash < right.hash;
                }
                return left.name < right.name;
            });
    }
    return directories;
}

class TempWorkspace final {
public:
    TempWorkspace() {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto root = std::filesystem::temp_directory_path();
        for (std::uint32_t attempt = 0U; attempt < 128U; ++attempt) {
            const auto timestamp = std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
            auto candidate = root /
                ("airfix-synthetic-content-" + std::to_string(timestamp) + "-" +
                 std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed)));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
            if (error) {
                throw std::runtime_error(
                    "failed to create synthetic content workspace: " + error.message());
            }
        }
        throw std::runtime_error("failed to allocate a unique synthetic content workspace");
    }

    TempWorkspace(const TempWorkspace&) = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;

    ~TempWorkspace() {
        if (path_.empty()) {
            return;
        }
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

inline void writeFile(
    const std::filesystem::path& path,
    const std::span<const std::uint8_t> bytes) {
    if (bytes.size() >
        static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("synthetic content input exceeds stream limits");
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to create synthetic content input");
    }
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw std::runtime_error("failed to write synthetic content input");
    }
}

[[nodiscard]] inline Bytes readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("failed to open generated synthetic AFPACK");
    }
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("failed to determine generated synthetic AFPACK size");
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > std::numeric_limits<std::size_t>::max() ||
        size > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("generated synthetic AFPACK exceeds process stream limits");
    }

    Bytes bytes(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("failed to read generated synthetic AFPACK");
    }
    return bytes;
}

} // namespace detail

// Builds a structurally valid UDSP archive. Compressed entries supply their
// already encoded bytes, compression flag, and declared unpacked size.
// Duplicate logical paths are retained so tests can exercise ambiguous lookup.
[[nodiscard]] inline Bytes makeUdspArchive(
    const std::span<const UdspInputEntry> inputEntries) {
    auto directories = detail::prepareDirectories(inputEntries);
    Bytes archive(udsp::kHeaderSize, 0U);

    std::uint64_t firstFileIndex = 0U;
    for (auto& directory : directories) {
        directory.firstFileIndex = detail::checkedU32(
            firstFileIndex, "first file index");
        firstFileIndex += directory.files.size();
        for (auto& file : directory.files) {
            file.dataOffset = detail::checkedU32(archive.size(), "file data offset");
            archive.insert(archive.end(), file.bytes.begin(), file.bytes.end());
        }
    }

    Bytes strings;
    for (auto& directory : directories) {
        directory.nameOffset = detail::appendString(strings, directory.path);
        for (auto& file : directory.files) {
            file.nameOffset = detail::appendString(strings, file.name);
        }
    }

    const auto directoryOffset =
        detail::checkedU32(archive.size(), "directory table offset");
    for (const auto& directory : directories) {
        detail::appendRecord(
            archive,
            directory.hash,
            directory.nameOffset,
            0U,
            0U,
            detail::checkedU32(directory.files.size(), "directory file count"),
            detail::checkedU32(
                static_cast<std::uint64_t>(directory.firstFileIndex) *
                    udsp::kRecordSize,
                "directory file-table offset"));
    }

    const auto fileOffset = detail::checkedU32(archive.size(), "file table offset");
    for (const auto& directory : directories) {
        for (const auto& file : directory.files) {
            const auto payloadSize =
                detail::checkedU32(file.bytes.size(), "file payload size");
            detail::appendRecord(
                archive,
                file.hash,
                file.nameOffset,
                file.flags,
                file.unpackedSize,
                payloadSize,
                file.dataOffset);
        }
    }

    const auto stringOffset =
        detail::checkedU32(archive.size(), "string table offset");
    archive.insert(archive.end(), strings.begin(), strings.end());

    archive[0] = 'U';
    archive[1] = 'D';
    archive[2] = 'S';
    archive[3] = 'P';
    detail::writeU32(archive, 4U, udsp::kVersion);
    detail::writeU32(
        archive,
        8U,
        detail::checkedU32(
            static_cast<std::uint64_t>(directories.size()) * udsp::kRecordSize,
            "directory table size"));
    detail::writeU32(archive, 12U, directoryOffset);
    detail::writeU32(
        archive, 16U, detail::checkedU32(strings.size(), "string table size"));
    detail::writeU32(archive, 20U, stringOffset);
    detail::writeU32(
        archive,
        24U,
        detail::checkedU32(firstFileIndex * udsp::kRecordSize, "file table size"));
    detail::writeU32(archive, 28U, fileOffset);

    // Keep the helper fail-fast: callers get a usable fixture or an exception,
    // never bytes that only appear valid until a later test stage.
    (void)udsp::Archive::parse(archive);
    return archive;
}

[[nodiscard]] inline Bytes makeUdspArchive(
    const std::initializer_list<UdspInputEntry> inputEntries) {
    return makeUdspArchive(
        std::span<const UdspInputEntry>(inputEntries.begin(), inputEntries.size()));
}

struct SyntheticAfPack {
    Bytes sourceArchive;
    Bytes localizationArchive;
    Bytes bytes;
    std::uint64_t size{};
    crypto::Sha256Digest sha256{};

    // std::istringstream owns a string. This helper deliberately contains all
    // embedded NUL bytes and lets a test pass any diagnostic/poison source label.
    [[nodiscard]] std::string streamContents() const {
        return {
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size(),
        };
    }
};

// Accepts prebuilt nested archives so rejection tests can put a deliberately
// malformed UDSP behind a valid AFPACK table, payload digest and manifest.
[[nodiscard]] inline SyntheticAfPack makeSyntheticAfPackFromArchives(
    Bytes sourceArchive,
    Bytes localizationArchive =
        makeUdspArchive(std::span<const UdspInputEntry>{})) {
    detail::TempWorkspace workspace;
    const auto sourcePath = workspace.path() / "Resource.up";
    const auto localizationPath = workspace.path() / "English.up";
    const auto outputPath = workspace.path() / "fixture.afpack";
    detail::writeFile(sourcePath, sourceArchive);
    detail::writeFile(localizationPath, localizationArchive);

    const auto written = afpack::writePack({
        .outputPath = outputPath,
        .manifest = {
            .sourceVersion = "1.01",
            .converterVersion = "synthetic-test",
            .converterCommit = "0000000000000000000000000000000000000000",
            .locale = "en",
        },
        .entries = {
            {
                .logicalPath = "source/Resource.up",
                .kind = afpack::EntryKind::sourceArchive,
                .sourcePath = sourcePath,
            },
            {
                .logicalPath = "localization/English.up",
                .kind = afpack::EntryKind::localization,
                .sourcePath = localizationPath,
            },
        },
    });

    auto bytes = detail::readFile(outputPath);
    if (written.archiveSize != bytes.size()) {
        throw std::runtime_error("synthetic AFPACK writer reported an inconsistent size");
    }
    const auto size = static_cast<std::uint64_t>(bytes.size());
    const auto digest = crypto::sha256(bytes);
    return {
        .sourceArchive = std::move(sourceArchive),
        .localizationArchive = std::move(localizationArchive),
        .bytes = std::move(bytes),
        .size = size,
        .sha256 = digest,
    };
}

// Uses the production AFPACK writer so the fixture's table, payload hashes and
// strict manifest stay representative of installable content.
[[nodiscard]] inline SyntheticAfPack makeSyntheticAfPack(
    const std::span<const UdspInputEntry> sourceEntries,
    const std::span<const UdspInputEntry> localizationEntries = {}) {
    return makeSyntheticAfPackFromArchives(
        makeUdspArchive(sourceEntries),
        makeUdspArchive(localizationEntries));
}

[[nodiscard]] inline SyntheticAfPack makeSyntheticAfPack(
    const std::initializer_list<UdspInputEntry> sourceEntries) {
    return makeSyntheticAfPack(
        std::span<const UdspInputEntry>(sourceEntries.begin(), sourceEntries.size()));
}

[[nodiscard]] inline SyntheticAfPack makeSyntheticAfPack(
    const std::initializer_list<UdspInputEntry> sourceEntries,
    const std::initializer_list<UdspInputEntry> localizationEntries) {
    return makeSyntheticAfPack(
        std::span<const UdspInputEntry>(sourceEntries.begin(), sourceEntries.size()),
        std::span<const UdspInputEntry>(
            localizationEntries.begin(), localizationEntries.size()));
}

} // namespace airfix::testing
