#include "airfix/archive/UdspArchive.hpp"
#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/AssetResolver.hpp"
#include "airfix/assets/LegacyFormats.hpp"
#include "airfix/render/DiagnosticRasterizer.hpp"
#include "airfix/render/DrawModel.hpp"
#include "airfix/render/DrawMesh.hpp"
#include "airfix/render/LegacyGeometry.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr std::size_t kObjectReadLimit = 1024U * 1024U;
constexpr std::size_t kAssetReadLimit = 64U * 1024U * 1024U;
constexpr std::size_t kDecodedTextureLimit = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumPrimaryTextures = 4'096U;
constexpr std::size_t kMaximumAggregateTexturePixels = 64U * 1024U * 1024U;

struct LoadedTexture {
    airfix::render::TextureAssetId id{};
    airfix::assets::RgbaImage image;
};

[[nodiscard]] const airfix::udsp::FileEntry& requireUniqueEntry(
    const airfix::udsp::Archive& archive,
    const std::string& logicalPath) {
    const auto lookup = archive.lookup(logicalPath);
    if (lookup.status != airfix::udsp::LookupStatus::unique) {
        throw std::runtime_error("expected one UDSP entry: " + logicalPath);
    }
    return archive.files().at(lookup.fileIndex);
}

[[nodiscard]] const airfix::assets::GtiVariant& selectVariant(
    const airfix::assets::GtiMetadata& metadata) {
    constexpr std::array<std::uint32_t, 5> preference{8U, 7U, 4U, 3U, 6U};
    for (const auto format : preference) {
        const auto found = std::find_if(
            metadata.variants.begin(), metadata.variants.end(),
            [format](const auto& variant) { return variant.format == format; });
        if (found != metadata.variants.end()) {
            return *found;
        }
    }
    throw std::runtime_error("GTI has no diagnostic RGBA format");
}

[[nodiscard]] airfix::assets::RgbaImage decodeTexture(
    const std::filesystem::path& archivePath,
    const airfix::udsp::Archive& archive,
    const std::size_t fileIndex) {
    const auto bytes = airfix::udsp::readFile(
        archivePath, archive, archive.files().at(fileIndex), kAssetReadLimit);
    const auto metadata = airfix::assets::parseGti(bytes);
    const auto& variant = selectVariant(metadata);
    const auto layouts = airfix::assets::describeGtiMipLevels(variant);
    const bool exactChain = std::ranges::all_of(
        layouts, [](const auto& layout) { return layout.exactTexelLayout; });
    if (exactChain) {
        auto chain = airfix::assets::decodeGtiMipChainRgba(
            bytes, variant, kDecodedTextureLimit);
        if (chain.levels.empty()) {
            throw std::runtime_error("decoded GTI mip chain is empty");
        }
        return std::move(chain.levels.front());
    }
    return airfix::assets::decodeGtiBaseRgba(
        bytes, variant, kDecodedTextureLimit);
}

void assignTexture(
    std::optional<airfix::render::TextureAssetId>& destination,
    const airfix::render::TextureAssetId id) {
    if (destination.has_value()) {
        throw std::runtime_error("material texture role is duplicated");
    }
    destination = id;
}

void writePpm(
    const std::filesystem::path& path,
    const airfix::assets::RgbaImage& image) {
    if (path.extension() != ".ppm") {
        throw std::runtime_error("model preview output must use .ppm");
    }
    const auto expectedBytes = static_cast<std::uint64_t>(image.width) *
        image.height * 4U;
    if (expectedBytes != image.pixels.size()) {
        throw std::runtime_error("model preview image has an invalid RGBA size");
    }
    const auto pixelCount = image.pixels.size() / 4U;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 3U) {
        throw std::runtime_error("model preview RGB size overflows");
    }

    std::vector<std::uint8_t> rgb;
    rgb.reserve(pixelCount * 3U);
    for (std::size_t pixel = 0U; pixel < image.pixels.size(); pixel += 4U) {
        rgb.insert(rgb.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(pixel),
                   image.pixels.begin() + static_cast<std::ptrdiff_t>(pixel + 3U));
    }
    const std::string header = "P6\n" + std::to_string(image.width) + ' ' +
        std::to_string(image.height) + "\n255\n";

    int descriptor = -1;
#if defined(_WIN32)
    const auto openError = _wsopen_s(
        &descriptor, path.c_str(),
        _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
        _SH_DENYRW, _S_IREAD | _S_IWRITE);
    if (openError != 0) {
        throw std::runtime_error(
            "cannot exclusively create model preview: " + path.string());
    }
#else
    descriptor = ::open(
        path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
        S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        throw std::runtime_error(
            "cannot exclusively create model preview: " + path.string());
    }
#endif

#if defined(_WIN32)
    std::FILE* output = _fdopen(descriptor, "wb");
#else
    std::FILE* output = fdopen(descriptor, "wb");
#endif
    if (output == nullptr) {
#if defined(_WIN32)
        _close(descriptor);
#else
        ::close(descriptor);
#endif
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        throw std::runtime_error("cannot open model preview stream: " + path.string());
    }

    const bool wroteHeader =
        std::fwrite(header.data(), 1U, header.size(), output) == header.size();
    const bool wrotePixels = wroteHeader &&
        std::fwrite(rgb.data(), 1U, rgb.size(), output) == rgb.size();
    const bool closed = std::fclose(output) == 0;
    if (!wrotePixels || !closed) {
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        throw std::runtime_error("cannot write model preview: " + path.string());
    }
}

[[nodiscard]] std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

int main(const int argc, const char* const* argv) {
    if (argc != 4 && argc != 5) {
        std::cerr << "usage: model-preview <archive.up> <logical\\path.object> "
                     "<output.ppm> [--flip-v]\n";
        return 2;
    }
    try {
        const std::filesystem::path archivePath = argv[1];
        const std::string objectPath = argv[2];
        const std::filesystem::path outputPath = argv[3];
        const bool flipV = argc == 5 && std::string(argv[4]) == "--flip-v";
        if (argc == 5 && !flipV) {
            throw std::runtime_error("unknown model-preview option");
        }

        const auto archive = airfix::udsp::Archive::open(archivePath);
        const auto& objectEntry = requireUniqueEntry(archive, objectPath);
        const auto objectBytes = airfix::udsp::readFile(
            archivePath, archive, objectEntry, kObjectReadLimit);
        const auto object = airfix::assets::parseObjectDefinition(objectBytes);
        if (!object.ccfPath.has_value()) {
            throw std::runtime_error("object has no CCF dependency");
        }

        const auto& ccfEntry = requireUniqueEntry(archive, *object.ccfPath);
        const auto ccfBytes = airfix::udsp::readFile(
            archivePath, archive, ccfEntry, kAssetReadLimit);
        const auto ccf = airfix::assets::parseCcf(ccfBytes);
        const auto dependencies =
            airfix::assets::resolveObjectSceneDependencies(object, ccf);
        if (!dependencies.issues.empty() || !dependencies.graphIssues.empty() ||
            !dependencies.rootBlueprintIndex.has_value() || dependencies.meshes.empty()) {
            throw std::runtime_error("object did not resolve to a renderable model subtree");
        }
        const auto textureEntries = airfix::assets::resolveObjectTextureEntries(
            object, dependencies, archive);
        if (!textureEntries.issues.empty() ||
            textureEntries.entries.size() != dependencies.textures.size()) {
            throw std::runtime_error("object texture dependencies are not unique");
        }

        std::vector<airfix::render::DrawMaterial> materials;
        materials.reserve(dependencies.materialIndices.size());
        std::unordered_map<std::uint32_t, std::size_t> materialSlotByReference;
        materialSlotByReference.reserve(dependencies.materialIndices.size());
        for (const auto materialIndex : dependencies.materialIndices) {
            const auto reference = ccf.materials.at(materialIndex).reference;
            const auto [iterator, inserted] = materialSlotByReference.emplace(
                reference, materials.size());
            (void)iterator;
            if (!inserted) {
                throw std::runtime_error("resolved material reference is duplicated");
            }
            materials.push_back({.sourceReference = reference});
        }

        std::vector<LoadedTexture> loadedTextures;
        loadedTextures.reserve(textureEntries.entries.size());
        std::unordered_map<std::size_t, std::size_t> loadedTextureByFile;
        loadedTextureByFile.reserve(textureEntries.entries.size());
        std::size_t aggregateTexturePixels = 0U;
        for (const auto& entry : textureEntries.entries) {
            if (entry.status != airfix::assets::TextureEntryStatus::unique ||
                !entry.archiveFileIndex.has_value()) {
                throw std::runtime_error("texture dependency is unresolved");
            }
            if (*entry.archiveFileIndex > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("texture archive index exceeds runtime ID range");
            }
            const auto id = airfix::render::TextureAssetId{
                static_cast<std::uint32_t>(*entry.archiveFileIndex)};
            const auto materialFound = materialSlotByReference.find(
                entry.materialReference);
            if (materialFound == materialSlotByReference.end()) {
                throw std::runtime_error("texture references an unknown material");
            }
            auto& material = materials[materialFound->second];
            switch (entry.role) {
            case airfix::assets::TextureDependencyRole::primary:
                assignTexture(material.primary, id);
                if (!loadedTextureByFile.contains(*entry.archiveFileIndex)) {
                    if (loadedTextures.size() >= kMaximumPrimaryTextures) {
                        throw std::runtime_error(
                            "primary texture count exceeds diagnostic limit");
                    }
                    auto image = decodeTexture(
                        archivePath, archive, *entry.archiveFileIndex);
                    const auto texturePixels = static_cast<std::uint64_t>(image.width) *
                        image.height;
                    if (texturePixels > kMaximumAggregateTexturePixels ||
                        aggregateTexturePixels >
                            kMaximumAggregateTexturePixels - texturePixels) {
                        throw std::runtime_error(
                            "aggregate primary texture pixels exceed diagnostic limit");
                    }
                    aggregateTexturePixels += static_cast<std::size_t>(texturePixels);
                    loadedTextureByFile.emplace(
                        *entry.archiveFileIndex, loadedTextures.size());
                    loadedTextures.push_back({
                        .id = id,
                        .image = std::move(image),
                    });
                }
                break;
            case airfix::assets::TextureDependencyRole::secondary:
                assignTexture(material.secondary, id);
                break;
            case airfix::assets::TextureDependencyRole::environment:
                assignTexture(material.environment, id);
                break;
            }
        }

        airfix::render::DrawModelPayload model;
        model.meshes.reserve(dependencies.meshes.size());
        model.instances.reserve(dependencies.meshes.size());
        std::size_t triangleCount = 0U;
        std::size_t drawVertexCount = 0U;
        std::size_t rangeCount = 0U;
        for (const auto& meshDependency : dependencies.meshes) {
            const auto converted = airfix::render::convertLegacyGeometry(
                ccf.meshes.at(meshDependency.meshIndex));
            const auto meshSlot = model.meshes.size();
            if (meshSlot > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("model mesh slot exceeds runtime ID range");
            }
            model.meshes.push_back(airfix::render::buildDrawMesh(converted, materials));
            const auto& drawMesh = model.meshes.back();
            const auto meshTriangles = drawMesh.indices.size() / 3U;
            if (meshTriangles > std::numeric_limits<std::size_t>::max() - triangleCount ||
                drawMesh.vertices.size() >
                    std::numeric_limits<std::size_t>::max() - drawVertexCount ||
                drawMesh.ranges.size() >
                    std::numeric_limits<std::size_t>::max() - rangeCount) {
                throw std::runtime_error("aggregate model diagnostic count overflows");
            }
            triangleCount += meshTriangles;
            drawVertexCount += drawMesh.vertices.size();
            rangeCount += drawMesh.ranges.size();
            model.instances.push_back({
                .meshSlot = static_cast<std::uint32_t>(meshSlot),
                .sourceNodeReference =
                    ccf.blueprints.at(meshDependency.blueprintIndex).reference,
                .modelLinear = converted.orientation,
                .modelTranslation = converted.translation,
            });
        }
        std::vector<airfix::render::DiagnosticTextureView> textureViews;
        textureViews.reserve(loadedTextures.size());
        for (const auto& texture : loadedTextures) {
            textureViews.push_back({.id = texture.id, .image = &texture.image});
        }

        airfix::render::DiagnosticRasterizerOptions options;
        options.width = 1024U;
        options.height = 1024U;
        options.flipV = flipV;
        const auto image = airfix::render::rasterizeDiagnosticModel(
            model, textureViews, options);
        writePpm(outputPath, image);

        std::cout << "created=" << outputPath.string()
                  << " nodes=" << dependencies.blueprintIndices.size()
                  << " meshInstances=" << model.instances.size()
                  << " triangles=" << triangleCount
                  << " drawVertices=" << drawVertexCount
                  << " ranges=" << rangeCount
                  << " materials=" << dependencies.materialIndices.size()
                  << " primaryTextures=" << loadedTextures.size()
                  << " flipV=" << flipV
                  << " rgbaFnv64=" << fnv1a(image.pixels) << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "model-preview: " << error.what() << '\n';
        return 1;
    }
}
