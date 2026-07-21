#include "airfix/archive/UdspArchive.hpp"

#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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

} // namespace

int main(const int argc, const char* const* argv) {
    const bool summaryOnly = argc == 3 && std::string(argv[1]) == "--summary";
    const bool verifyPayloads = argc == 3 && std::string(argv[1]) == "--verify";
    if (argc != 2 && !summaryOnly && !verifyPayloads) {
        std::cerr << "usage: udsp-list [--summary|--verify] <archive.up>\n";
        return 2;
    }

    try {
        const auto pathIndex = summaryOnly || verifyPayloads ? 2 : 1;
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
