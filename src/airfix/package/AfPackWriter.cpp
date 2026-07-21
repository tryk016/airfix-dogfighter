#include "airfix/package/AfPackWriter.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace airfix::afpack {
namespace {

struct PreparedEntry {
    std::string logicalPath;
    EntryKind kind{};
    std::filesystem::path sourcePath;
    std::vector<std::uint8_t> inlineBytes;
    std::uint64_t size{};
    crypto::Sha256Digest digest{};
    std::uint64_t pathOffset{};
    std::uint64_t dataOffset{};
};

[[nodiscard]] std::uint64_t checkedAdd(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view field) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::runtime_error(std::string(field) + " overflows");
    }
    return left + right;
}

[[nodiscard]] std::uint64_t alignUp(const std::uint64_t value) {
    const auto added = checkedAdd(value, kDataAlignment - 1U, "AFPACK alignment");
    return added & ~static_cast<std::uint64_t>(kDataAlignment - 1U);
}

[[nodiscard]] std::string kindName(const EntryKind kind) {
    switch (kind) {
    case EntryKind::sourceArchive: return "source-archive";
    case EntryKind::localization: return "localization";
    case EntryKind::video: return "video";
    case EntryKind::convertedAsset: return "converted-asset";
    case EntryKind::manifest: break;
    }
    throw std::runtime_error("unsupported AFPACK writer entry kind");
}

[[nodiscard]] std::string jsonString(const std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output{"\""};
    for (const char character : value) {
        const auto byte = static_cast<std::uint8_t>(character);
        if (character == '"' || character == '\\') {
            output.push_back('\\');
            output.push_back(character);
        }
        else if (byte < 0x20U) {
            output += "\\u00";
            output.push_back(digits[byte >> 4U]);
            output.push_back(digits[byte & 0x0FU]);
        }
        else {
            output.push_back(character);
        }
    }
    output.push_back('"');
    return output;
}

[[nodiscard]] std::string makeManifest(
    const ManifestMetadata& metadata,
    const std::vector<PreparedEntry>& entries) {
    if (metadata.locale.empty()) {
        throw std::runtime_error("AFPACK locale must not be empty");
    }
    std::ostringstream json;
    json << "{\n"
         << "  \"schema\": \"airfix.afpack.manifest\",\n"
         << "  \"version\": 1,\n"
         << "  \"game\": {\n"
         << "    \"id\": \"airfix-dogfighter\",\n"
         << "    \"sourceVersion\": " << jsonString(metadata.sourceVersion) << "\n"
         << "  },\n"
         << "  \"converter\": {\n"
         << "    \"version\": " << jsonString(metadata.converterVersion) << ",\n"
         << "    \"commit\": " << jsonString(metadata.converterCommit) << "\n"
         << "  },\n"
         << "  \"locale\": " << jsonString(metadata.locale) << ",\n"
         << "  \"capabilities\": {\n"
         << "    \"music\": false,\n"
         << "    \"multiplayer\": false,\n"
         << "    \"editors\": false\n"
         << "  },\n"
         << "  \"entries\": [\n";
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        json << "    {\n"
             << "      \"path\": " << jsonString(entry.logicalPath) << ",\n"
             << "      \"kind\": " << jsonString(kindName(entry.kind)) << ",\n"
             << "      \"size\": " << entry.size << ",\n"
             << "      \"sha256\": " << jsonString(crypto::toHex(entry.digest)) << "\n"
             << "    }" << (index + 1U == entries.size() ? "\n" : ",\n");
    }
    json << "  ]\n}\n";
    return json.str();
}

void writeU16(
    std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(
    std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        bytes.at(offset + byte) = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

void writeU64(
    std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::uint64_t value) {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        bytes.at(offset + byte) = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

void writeBytes(std::ofstream& output, const std::span<const std::uint8_t> bytes) {
    if (!bytes.empty() && !output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("cannot write AFPACK output");
    }
}

void writeSource(
    std::ofstream& output,
    const PreparedEntry& entry) {
    if (!entry.inlineBytes.empty() || entry.size == 0U) {
        writeBytes(output, entry.inlineBytes);
        if (crypto::sha256(entry.inlineBytes) != entry.digest) {
            throw std::runtime_error("inline AFPACK payload digest changed");
        }
        return;
    }

    std::ifstream input(entry.sourcePath, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot reopen AFPACK source: " + entry.sourcePath.string());
    }
    // MSVC executables default to a 1 MiB thread stack; leave ample headroom
    // for the caller and hashing state while retaining bounded streaming I/O.
    constexpr std::size_t kBufferSize = 64U * 1024U;
    std::array<std::uint8_t, kBufferSize> buffer{};
    crypto::Sha256 hash;
    auto remaining = entry.size;
    while (remaining != 0U) {
        const auto chunkSize = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        if (!input.read(reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(chunkSize))) {
            throw std::runtime_error("source changed while writing AFPACK: " +
                entry.sourcePath.string());
        }
        const auto chunk = std::span<const std::uint8_t>(buffer).first(chunkSize);
        hash.update(chunk);
        writeBytes(output, chunk);
        remaining -= chunkSize;
    }
    if (input.peek() != std::char_traits<char>::eof() || hash.finish() != entry.digest) {
        throw std::runtime_error("source changed while writing AFPACK: " +
            entry.sourcePath.string());
    }
}

} // namespace

WriteResult writePack(const WriteRequest& request) {
    if (request.outputPath.empty()) {
        throw std::runtime_error("AFPACK output path must not be empty");
    }
    if (request.outputPath.extension() != ".afpack") {
        throw std::runtime_error("AFPACK output must use the .afpack extension");
    }
    if (request.entries.empty()) {
        throw std::runtime_error("AFPACK requires at least one content entry");
    }
    if (std::filesystem::exists(request.outputPath)) {
        throw std::runtime_error("refusing to replace existing AFPACK: " +
            request.outputPath.string());
    }
    const auto parent = request.outputPath.parent_path().empty()
        ? std::filesystem::current_path()
        : request.outputPath.parent_path();
    if (!std::filesystem::is_directory(parent)) {
        throw std::runtime_error("AFPACK output directory does not exist: " + parent.string());
    }

    std::vector<PreparedEntry> prepared;
    prepared.reserve(request.entries.size() + 1U);
    for (const auto& source : request.entries) {
        validateLogicalPath(source.logicalPath);
        if (source.logicalPath == "manifest.json" || source.kind == EntryKind::manifest) {
            throw std::runtime_error("manifest.json is generated by the AFPACK writer");
        }
        if (source.kind != EntryKind::sourceArchive &&
            source.kind != EntryKind::localization &&
            source.kind != EntryKind::video &&
            source.kind != EntryKind::convertedAsset) {
            throw std::runtime_error("unsupported AFPACK source kind");
        }
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(source.sourcePath, sizeError);
        if (sizeError || !std::filesystem::is_regular_file(source.sourcePath)) {
            throw std::runtime_error("AFPACK source is not a regular file: " +
                source.sourcePath.string());
        }
        prepared.push_back({
            .logicalPath = source.logicalPath,
            .kind = source.kind,
            .sourcePath = source.sourcePath,
            .inlineBytes = {},
            .size = size,
            .digest = crypto::sha256FileRegion(source.sourcePath, 0U, size),
            .pathOffset = 0U,
            .dataOffset = 0U,
        });
    }
    std::sort(prepared.begin(), prepared.end(), [](const auto& left, const auto& right) {
        return left.logicalPath < right.logicalPath;
    });
    for (std::size_t index = 1U; index < prepared.size(); ++index) {
        if (prepared[index - 1U].logicalPath == prepared[index].logicalPath) {
            throw std::runtime_error("duplicate AFPACK logical path: " +
                prepared[index].logicalPath);
        }
    }

    const auto manifestText = makeManifest(request.manifest, prepared);
    std::vector<std::uint8_t> manifestBytes(manifestText.begin(), manifestText.end());
    prepared.push_back({
        .logicalPath = "manifest.json",
        .kind = EntryKind::manifest,
        .sourcePath = {},
        .inlineBytes = std::move(manifestBytes),
        .size = static_cast<std::uint64_t>(manifestText.size()),
        .digest = crypto::sha256(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(manifestText.data()), manifestText.size())),
        .pathOffset = 0U,
        .dataOffset = 0U,
    });
    std::sort(prepared.begin(), prepared.end(), [](const auto& left, const auto& right) {
        return left.logicalPath < right.logicalPath;
    });

    if (prepared.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("too many AFPACK entries");
    }
    const auto entryTableSize = static_cast<std::uint64_t>(prepared.size()) *
        kEntryRecordSize;
    const auto stringTableOffset = checkedAdd(kHeaderSize, entryTableSize, "string table");
    std::uint64_t stringTableSize = 0U;
    for (auto& entry : prepared) {
        if (entry.logicalPath.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("AFPACK path is too long");
        }
        entry.pathOffset = stringTableSize;
        stringTableSize = checkedAdd(
            stringTableSize, entry.logicalPath.size(), "string table");
    }
    const auto dataOffset = alignUp(checkedAdd(
        stringTableOffset, stringTableSize, "AFPACK metadata"));
    auto archiveSize = dataOffset;
    for (auto& entry : prepared) {
        entry.dataOffset = alignUp(archiveSize);
        archiveSize = checkedAdd(entry.dataOffset, entry.size, "AFPACK payload");
    }
    if (dataOffset > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("AFPACK metadata is too large for this process");
    }

    std::vector<std::uint8_t> metadata(static_cast<std::size_t>(dataOffset), 0U);
    metadata[0] = 'A';
    metadata[1] = 'F';
    metadata[2] = 'P';
    metadata[3] = 'K';
    writeU16(metadata, 4U, kVersionMajor);
    writeU16(metadata, 6U, kVersionMinor);
    writeU32(metadata, 8U, static_cast<std::uint32_t>(kHeaderSize));
    writeU32(metadata, 16U, static_cast<std::uint32_t>(prepared.size()));
    writeU64(metadata, 24U, kHeaderSize);
    writeU64(metadata, 32U, entryTableSize);
    writeU64(metadata, 40U, stringTableOffset);
    writeU64(metadata, 48U, stringTableSize);
    writeU64(metadata, 56U, dataOffset);
    writeU64(metadata, 64U, archiveSize);

    auto stringCursor = static_cast<std::size_t>(stringTableOffset);
    for (std::size_t index = 0U; index < prepared.size(); ++index) {
        const auto& entry = prepared[index];
        const auto record = kHeaderSize + index * kEntryRecordSize;
        writeU64(metadata, record, entry.pathOffset);
        writeU32(metadata, record + 8U, static_cast<std::uint32_t>(entry.logicalPath.size()));
        writeU16(metadata, record + 12U, static_cast<std::uint16_t>(entry.kind));
        writeU64(metadata, record + 16U, entry.dataOffset);
        writeU64(metadata, record + 24U, entry.size);
        writeU64(metadata, record + 32U, entry.size);
        std::copy(entry.digest.begin(), entry.digest.end(),
            metadata.begin() + static_cast<std::ptrdiff_t>(record + 40U));
        std::copy(entry.logicalPath.begin(), entry.logicalPath.end(),
            metadata.begin() + static_cast<std::ptrdiff_t>(stringCursor));
        stringCursor += entry.logicalPath.size();
    }

    auto temporaryPath = request.outputPath;
    temporaryPath += ".partial";
    if (std::filesystem::exists(temporaryPath)) {
        throw std::runtime_error("stale AFPACK partial file exists: " +
            temporaryPath.string());
    }

    bool temporaryCreated = false;
    try {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot create AFPACK: " + temporaryPath.string());
        }
        temporaryCreated = true;
        writeBytes(output, metadata);
        auto currentOffset = dataOffset;
        const std::array<std::uint8_t, kDataAlignment> padding{};
        for (const auto& entry : prepared) {
            const auto paddingSize = static_cast<std::size_t>(entry.dataOffset - currentOffset);
            writeBytes(output, std::span<const std::uint8_t>(padding).first(paddingSize));
            writeSource(output, entry);
            currentOffset = entry.dataOffset + entry.size;
        }
        output.flush();
        if (!output) {
            throw std::runtime_error("cannot flush AFPACK: " + temporaryPath.string());
        }
        output.close();

        const auto verified = Pack::open(temporaryPath);
        verified.verifyPayloads(temporaryPath);
        std::filesystem::rename(temporaryPath, request.outputPath);
        temporaryCreated = false;
        return {
            .archiveSize = verified.archiveSize(),
            .entries = verified.entries(),
        };
    }
    catch (...) {
        if (temporaryCreated) {
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
        }
        throw;
    }
}

} // namespace airfix::afpack
