#include "airfix/archive/UdspArchive.hpp"
#include "airfix/assets/LegacyFormats.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

struct LocatedEntry final {
    const airfix::udsp::FileEntry* file{};
    std::string logicalPath;
};

[[nodiscard]] LocatedEntry findEntry(
    const airfix::udsp::Archive& archive,
    const std::string& requestedPath) {
    for (const auto& directory : archive.directories()) {
        const auto first = static_cast<std::size_t>(directory.firstFileIndex);
        const auto end = first + static_cast<std::size_t>(directory.fileCount);
        for (auto index = first; index < end; ++index) {
            const auto& file = archive.files().at(index);
            auto path = directory.path;
            if (!path.empty() && path.back() != '\\') {
                path.push_back('\\');
            }
            path += file.name;
            if (path == requestedPath) {
                return {.file = &file, .logicalPath = std::move(path)};
            }
        }
    }
    throw std::runtime_error("UDSP path not found: " + requestedPath);
}

[[nodiscard]] const airfix::assets::GtiVariant& selectVariant(
    const airfix::assets::GtiMetadata& metadata,
    const std::optional<std::uint32_t> requestedFormat) {
    const auto findFormat = [&](const std::uint32_t format) {
        return std::find_if(metadata.variants.begin(), metadata.variants.end(),
            [&](const auto& variant) { return variant.format == format; });
    };
    if (requestedFormat.has_value()) {
        const auto found = findFormat(*requestedFormat);
        if (found == metadata.variants.end()) {
            throw std::runtime_error("requested GTI format is not present");
        }
        return *found;
    }
    constexpr std::array<std::uint32_t, 5> preference{8U, 7U, 4U, 3U, 6U};
    for (const auto format : preference) {
        const auto found = findFormat(format);
        if (found != metadata.variants.end()) {
            return *found;
        }
    }
    throw std::runtime_error("GTI has no previewable format");
}

void writePpm(
    const std::filesystem::path& path,
    const airfix::assets::RgbaImage& image) {
    if (path.extension() != ".ppm") {
        throw std::runtime_error("preview output must use the .ppm extension");
    }
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("refusing to replace existing preview: " + path.string());
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create preview: " + path.string());
    }
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t pixel = 0U; pixel < image.pixels.size(); pixel += 4U) {
        output.write(reinterpret_cast<const char*>(image.pixels.data() + pixel), 3);
    }
    if (!output) {
        throw std::runtime_error("cannot write preview: " + path.string());
    }
}

} // namespace

int main(const int argc, const char* const* argv) {
    if (argc != 4 && argc != 5) {
        std::cerr << "usage: gti-preview <archive.up> <logical\\path.gti> "
                     "<output.ppm> [format]\n";
        return 2;
    }
    try {
        const std::filesystem::path archivePath = argv[1];
        const std::string logicalPath = argv[2];
        const std::filesystem::path outputPath = argv[3];
        const auto requestedFormat = argc == 5
            ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(std::stoul(argv[4])))
            : std::nullopt;

        const auto archive = airfix::udsp::Archive::open(archivePath);
        const auto located = findEntry(archive, logicalPath);
        constexpr std::size_t kReadLimit = 64U * 1024U * 1024U;
        const auto bytes = airfix::udsp::readFile(
            archivePath, archive, *located.file, kReadLimit);
        const auto metadata = airfix::assets::parseGti(bytes);
        const auto& variant = selectVariant(metadata, requestedFormat);
        constexpr std::size_t kPreviewOutputLimit = 64U * 1024U * 1024U;
        const auto image = airfix::assets::decodeGtiBaseRgba(
            bytes, variant, kPreviewOutputLimit);
        writePpm(outputPath, image);

        std::cout << "created=" << outputPath.string()
                  << " source=" << located.logicalPath
                  << " format=" << variant.format
                  << " dimensions=" << image.width << 'x' << image.height << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "gti-preview: " << error.what() << '\n';
        return 1;
    }
}
