#include "airfix/archive/UdspArchive.hpp"
#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/LegacyFormats.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::uint8_t> readArchive(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open archive: " + path);
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("invalid archive size: " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() && !input.read(
            reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("cannot read archive: " + path);
    }
    return bytes;
}

[[nodiscard]] bool isAfChunkRoot(const std::uint32_t id) noexcept {
    return id == airfix::assets::kAfObjectRoot ||
        id == airfix::assets::kAfModelRoot ||
        id == airfix::assets::kAfHouseRoot ||
        id == airfix::assets::kAfFullHouseRoot ||
        id == airfix::assets::kAfBriefingRoot ||
        id == airfix::assets::kAfPathRoot;
}

[[nodiscard]] std::string fourCcText(const std::uint32_t id) {
    std::string text(4U, '?');
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const auto value = static_cast<std::uint8_t>(id >> (index * 8U));
        if (value >= 0x20U && value <= 0x7EU) {
            text[index] = static_cast<char>(value);
        }
    }
    return text;
}

} // namespace

int main(const int argc, const char* const* argv) {
    const bool summaryOnly = argc == 3 && std::string(argv[1]) == "--summary";
    const bool verifyPayloads = argc == 3 && std::string(argv[1]) == "--verify";
    const bool inventory = argc == 3 && std::string(argv[1]) == "--inventory";
    if (argc != 2 && !summaryOnly && !verifyPayloads && !inventory) {
        std::cerr << "usage: udsp-list [--summary|--verify|--inventory] <archive.up>\n";
        return 2;
    }

    try {
        const auto pathIndex = summaryOnly || verifyPayloads || inventory ? 2 : 1;
        const std::string archivePath = argv[pathIndex];
        const auto bytes = verifyPayloads ? readArchive(archivePath) : std::vector<std::uint8_t>{};
        const auto archive = verifyPayloads
            ? airfix::udsp::Archive::parse(bytes)
            : airfix::udsp::Archive::open(archivePath);
        std::uint64_t storedBytes = 0U;
        std::uint64_t unpackedBytes = 0U;
        std::size_t compressedFiles = 0U;
        for (const auto& file : archive.files()) {
            storedBytes += file.storedSize;
            unpackedBytes += file.unpackedSize;
            compressedFiles += file.isCompressed() ? 1U : 0U;
        }

        std::cout << "UDSP version=0x" << std::hex << archive.header().version << std::dec
                  << " archiveBytes=" << archive.archiveSize()
                  << " directories=" << archive.directories().size()
                  << " files=" << archive.files().size()
                  << " compressedFiles=" << compressedFiles
                  << " storedBytes=" << storedBytes
                  << " unpackedBytes=" << unpackedBytes << '\n';

        if (summaryOnly) {
            return 0;
        }

        if (inventory) {
            std::cout << "path\textension\tstored\tunpacked\tcompressed\tprefix16\tdetail\n";
            for (const auto& directory : archive.directories()) {
                const auto first = static_cast<std::size_t>(directory.firstFileIndex);
                const auto end = first + static_cast<std::size_t>(directory.fileCount);
                for (auto index = first; index < end; ++index) {
                    const auto& file = archive.files().at(index);
                    auto extension = std::string{"[none]"};
                    const auto dot = file.name.find_last_of('.');
                    if (dot != std::string::npos && dot + 1U < file.name.size()) {
                        extension = file.name.substr(dot + 1U);
                        std::transform(extension.begin(), extension.end(), extension.begin(),
                            [](const char value) {
                                return value >= 'A' && value <= 'Z'
                                    ? static_cast<char>(value + ('a' - 'A'))
                                    : value;
                            });
                    }
                    auto logicalPath = directory.path;
                    if (!logicalPath.empty() && logicalPath.back() != '\\') {
                        logicalPath.push_back('\\');
                    }
                    logicalPath += file.name;
                    for (char& value : logicalPath) {
                        if (value == '\t' || value == '\r' || value == '\n') {
                            value = '?';
                        }
                    }
                    const auto prefix = airfix::udsp::readFilePrefix(
                        archivePath, archive, file, 16U);
                    std::ostringstream prefixHex;
                    prefixHex << std::hex << std::setfill('0');
                    for (const auto value : prefix) {
                        prefixHex << std::setw(2) << static_cast<unsigned>(value);
                    }
                    std::ostringstream detail;
                    constexpr std::size_t kAssetReadLimit = 64U * 1024U * 1024U;
                    try {
                        if (prefix.size() >= 4U) {
                            const auto magic = static_cast<std::uint32_t>(prefix[0]) |
                                (static_cast<std::uint32_t>(prefix[1]) << 8U) |
                                (static_cast<std::uint32_t>(prefix[2]) << 16U) |
                                (static_cast<std::uint32_t>(prefix[3]) << 24U);
                            if (magic == airfix::assets::kGtiMagic) {
                                const auto data = airfix::udsp::readFile(
                                    archivePath, archive, file, kAssetReadLimit);
                                const auto metadata = airfix::assets::parseGti(data);
                                detail << "GTI";
                                if (metadata.checksum.has_value()) {
                                    detail << ":crc=" << std::hex << *metadata.checksum << std::dec;
                                }
                                detail << ":crcChunks=" << metadata.checksumChunkCount
                                       << ":terminalOverrun="
                                       << (metadata.terminalDeclaredOverrun ? 1 : 0);
                                for (const auto& variant : metadata.variants) {
                                    const auto rgba = airfix::assets::decodeGtiBaseRgba(
                                        data, variant, 4U * 1024U * 1024U);
                                    detail << ":fmt=" << variant.format
                                           << ',' << variant.width << 'x' << variant.height
                                           << ",pal=" << variant.paletteEntries
                                           << ",mips=" << variant.mipmapLevels
                                           << ",pixels=" << variant.pixelDataSize
                                           << ",expected=" << variant.expectedPixelDataSize
                                           << ",trailing=" << variant.trailingBytes
                                           << ",rgba=" << rgba.pixels.size();
                                }
                            }
                            else if (magic == airfix::assets::kCcfMagic) {
                                const auto data = airfix::udsp::readFile(
                                    archivePath, archive, file, kAssetReadLimit);
                                const auto metadata = airfix::assets::parseCcf(data);
                                detail << "CCF:top=";
                                for (std::size_t child = 0U;
                                     child < metadata.topLevelChunks.size();
                                     ++child) {
                                    if (child != 0U) {
                                        detail << ',';
                                    }
                                    const auto& section = metadata.topLevelChunks[child];
                                    std::set<std::uint16_t> directIds;
                                    for (const auto& directChild : section.directChildren) {
                                        directIds.insert(directChild.id);
                                    }
                                    detail << "0x" << std::hex
                                           << metadata.topLevelChunks[child].id << std::dec
                                           << '['
                                           << section.directChildren.size() << ";ids=";
                                    auto firstId = true;
                                    for (const auto directId : directIds) {
                                        if (!firstId) {
                                            detail << '+';
                                        }
                                        detail << "0x" << std::hex << directId << std::dec;
                                        firstId = false;
                                    }
                                    detail << ']';
                                }
                            }
                            else if (isAfChunkRoot(magic)) {
                                const auto data = airfix::udsp::readFile(
                                    archivePath, archive, file, kAssetReadLimit);
                                constexpr std::size_t kAfChunkLimit = 100'000U;
                                const auto container = airfix::assets::parseAfChunkContainer(
                                    data, kAfChunkLimit);
                                detail << "AFCHUNK:root=" << fourCcText(container.rootId)
                                       << ":chunks=" << container.chunks.size();
                                if (magic == airfix::assets::kAfObjectRoot ||
                                    magic == airfix::assets::kAfModelRoot) {
                                    const auto definition =
                                        airfix::assets::parseObjectDefinition(data);
                                    detail << ":object:type=" << definition.type.has_value()
                                           << ",category="
                                           << definition.category.has_value()
                                           << ",name=" << definition.name.has_value()
                                           << ",nationality="
                                           << definition.nationality.has_value()
                                           << ",texture="
                                           << definition.textureRoot.has_value()
                                           << ",ccf=" << definition.ccfPath.has_value()
                                           << ",mesh=" << definition.meshName.has_value()
                                           << ",gravity=" << definition.gravity.has_value()
                                           << ",hidden=" << definition.hidden
                                           << ",unknown="
                                           << definition.unknownChunks.size();
                                }
                            }
                        }
                    }
                    catch (const std::exception& error) {
                        throw std::runtime_error(
                            "asset metadata " + logicalPath + ": " + error.what());
                    }
                    std::cout << logicalPath << '\t'
                              << extension << '\t'
                              << file.storedSize << '\t'
                              << file.unpackedSize << '\t'
                              << (file.isCompressed() ? 1 : 0) << '\t'
                              << prefixHex.str() << '\t'
                              << detail.str() << '\n';
                }
            }
            return 0;
        }

        if (verifyPayloads) {
            constexpr std::size_t kPerFileOutputLimit = 512U * 1024U * 1024U;
            std::size_t verified = 0U;
            for (const auto& file : archive.files()) {
                if (!file.isCompressed()) {
                    continue;
                }
                const auto encoded = std::span<const std::uint8_t>(bytes).subspan(
                    file.dataOffset, file.storedSize);
                (void)airfix::udsp::decompress(
                    encoded, file.unpackedSize, kPerFileOutputLimit);
                ++verified;
            }
            std::cout << "verifiedCompressedFiles=" << verified << '\n';
            return 0;
        }

        for (const auto& directory : archive.directories()) {
            const auto first = static_cast<std::size_t>(directory.firstFileIndex);
            const auto end = first + static_cast<std::size_t>(directory.fileCount);
            for (auto index = first; index < end; ++index) {
                const auto& file = archive.files().at(index);
                std::cout << directory.path;
                if (!directory.path.empty() && directory.path.back() != '\\') {
                    std::cout << '\\';
                }
                std::cout << file.name
                          << "\tstored=" << file.storedSize
                          << "\tunpacked=" << file.unpackedSize
                          << "\tflags=0x" << std::hex << file.flags << std::dec
                          << '\n';
            }
        }
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "udsp-list: " << error.what() << '\n';
        return 1;
    }
}
