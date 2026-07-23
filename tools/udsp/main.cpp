#include "airfix/archive/UdspArchive.hpp"
#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/AssetResolver.hpp"
#include "airfix/assets/CcfPlacedScene.hpp"
#include "airfix/assets/CcfRoomDrawPlan.hpp"
#include "airfix/assets/CcfRoomScene.hpp"
#include "airfix/assets/LegacyFormats.hpp"
#include "airfix/assets/MissionEntryResolver.hpp"
#include "airfix/assets/WorldCcfTextureResolver.hpp"
#include "airfix/render/LegacyGeometry.hpp"
#include "airfix/render/CcfRoomDrawAssembly.hpp"
#include "airfix/render/TextureRuntimePlan.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
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

template <typename Value>
void checkedAccumulate(
    std::uint64_t& total,
    const Value value,
    const char* const label) {
    static_assert(std::is_integral_v<Value> && std::is_unsigned_v<Value>);
    if constexpr (
        std::numeric_limits<Value>::max() >
        std::numeric_limits<std::uint64_t>::max()) {
        if (value > std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error(
                std::string{"inventory aggregate overflow: "} + label);
        }
    }
    const auto converted = static_cast<std::uint64_t>(value);
    if (total > std::numeric_limits<std::uint64_t>::max() - converted) {
        throw std::runtime_error(
            std::string{"inventory aggregate overflow: "} + label);
    }
    total += converted;
}

} // namespace

int main(const int argc, const char* const* argv) {
    const bool summaryOnly = argc == 3 && std::string(argv[1]) == "--summary";
    const bool verifyPayloads = argc == 3 && std::string(argv[1]) == "--verify";
    const bool inventory = argc == 3 && std::string(argv[1]) == "--inventory";
    const bool resolveObjects = argc == 3 && std::string(argv[1]) == "--resolve-objects";
    if (argc != 2 && !summaryOnly && !verifyPayloads && !inventory && !resolveObjects) {
        std::cerr << "usage: udsp-list "
                  << "[--summary|--verify|--inventory|--resolve-objects] <archive.up>\n";
        return 2;
    }

    try {
        const auto pathIndex = summaryOnly || verifyPayloads || inventory || resolveObjects
            ? 2
            : 1;
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

        if (resolveObjects) {
            constexpr std::size_t kObjectReadLimit = 1024U * 1024U;
            constexpr std::size_t kCcfReadLimit = 64U * 1024U * 1024U;
            std::size_t objectCount = 0U;
            std::size_t meshCount = 0U;
            std::size_t nullCount = 0U;
            std::size_t lightCount = 0U;
            std::size_t noSelectorCount = 0U;
            std::size_t caseFoldMatchCount = 0U;
            std::size_t selectedBlueprintNodeCount = 0U;
            std::size_t selectedMeshInstanceCount = 0U;
            std::size_t materialCount = 0U;
            std::size_t textureEdgeCount = 0U;
            std::size_t resolvedTextureEntryCount = 0U;
            std::size_t missingTextureRootCount = 0U;
            std::size_t invalidTexturePathCount = 0U;
            std::size_t missingTextureEntryCount = 0U;
            std::size_t ambiguousTextureEntryCount = 0U;
            std::size_t runtimeTextureImportCount = 0U;
            std::size_t authoredTextureCount = 0U;
            std::size_t generatedTextureCount = 0U;
            std::uint64_t uploadedMipCount = 0U;
            std::uint64_t allocatedMipCount = 0U;
            std::uint64_t uploadRgbaBytes = 0U;
            std::uint64_t residentRgbaBytes = 0U;
            for (const auto& file : archive.files()) {
                const auto prefix = airfix::udsp::readFilePrefix(
                    archivePath, archive, file, 4U);
                if (prefix.size() != 4U) {
                    continue;
                }
                const auto magic = static_cast<std::uint32_t>(prefix[0]) |
                    (static_cast<std::uint32_t>(prefix[1]) << 8U) |
                    (static_cast<std::uint32_t>(prefix[2]) << 16U) |
                    (static_cast<std::uint32_t>(prefix[3]) << 24U);
                if (magic != airfix::assets::kAfObjectRoot &&
                    magic != airfix::assets::kAfModelRoot) {
                    continue;
                }
                const auto objectBytes = airfix::udsp::readFile(
                    archivePath, archive, file, kObjectReadLimit);
                const auto object = airfix::assets::parseObjectDefinition(objectBytes);
                if (!object.ccfPath.has_value()) {
                    throw std::runtime_error("object resolver found a missing CCF path");
                }
                const auto ccfLookup = archive.lookup(*object.ccfPath);
                if (ccfLookup.status != airfix::udsp::LookupStatus::unique) {
                    throw std::runtime_error("object resolver did not find one CCF entry");
                }
                const auto ccfBytes = airfix::udsp::readFile(
                    archivePath, archive, archive.files().at(ccfLookup.fileIndex),
                    kCcfReadLimit);
                const auto ccf = airfix::assets::parseCcf(ccfBytes);
                const auto resolution = airfix::assets::resolveObjectDependencies(
                    object, ccf);
                if (!resolution.issues.empty()) {
                    throw std::runtime_error("object resolver found a semantic dependency issue");
                }
                const auto sceneResolution =
                    airfix::assets::resolveObjectSceneDependencies(object, ccf);
                if (!sceneResolution.issues.empty() ||
                    !sceneResolution.graphIssues.empty()) {
                    throw std::runtime_error(
                        "object scene resolver found a semantic graph issue");
                }
                const auto textureResolution =
                    airfix::assets::resolveObjectTextureEntries(
                        object, sceneResolution, archive);
                for (const auto& texture : textureResolution.entries) {
                    switch (texture.status) {
                    case airfix::assets::TextureEntryStatus::unique:
                        if (!texture.archiveFileIndex.has_value()) {
                            throw std::runtime_error(
                                "unique texture resolution has no archive entry");
                        }
                        ++resolvedTextureEntryCount;
                        break;
                    case airfix::assets::TextureEntryStatus::missingTextureRoot:
                        ++missingTextureRootCount;
                        break;
                    case airfix::assets::TextureEntryStatus::invalidLogicalPath:
                        ++invalidTexturePathCount;
                        break;
                    case airfix::assets::TextureEntryStatus::notFound:
                        ++missingTextureEntryCount;
                        break;
                    case airfix::assets::TextureEntryStatus::ambiguous:
                        ++ambiguousTextureEntryCount;
                        break;
                    }
                }
                if (textureResolution.issues.empty()) {
                    std::vector<std::uint32_t> materialReferences;
                    materialReferences.reserve(
                        sceneResolution.materialIndices.size());
                    for (const auto materialIndex :
                         sceneResolution.materialIndices) {
                        if (materialIndex >= ccf.materials.size()) {
                            throw std::runtime_error(
                                "object runtime material index is invalid");
                        }
                        materialReferences.push_back(
                            ccf.materials[materialIndex].reference);
                    }
                    const auto textureBindings =
                        airfix::render::buildTextureBindingPlan(
                            materialReferences,
                            sceneResolution.textures,
                            textureResolution);
                    if (!textureBindings.issues.empty()) {
                        throw std::runtime_error(
                            "object runtime texture binding failed");
                    }
                    runtimeTextureImportCount +=
                        textureBindings.imports.size();
                    for (const auto& request : textureBindings.imports) {
                        if (request.archiveFileIndex >=
                            archive.files().size()) {
                            throw std::runtime_error(
                                "object texture import index is invalid");
                        }
                        const auto textureBytes = airfix::udsp::readFile(
                            archivePath,
                            archive,
                            archive.files().at(request.archiveFileIndex),
                            kCcfReadLimit);
                        const auto textureMetadata =
                            airfix::assets::parseGti(textureBytes);
                        const auto upload =
                            airfix::render::describeGtiUpload(
                                request, textureMetadata);
                        if (!upload.issues.empty() ||
                            !upload.plan.has_value()) {
                            throw std::runtime_error(
                                "object texture upload planning failed");
                        }
                        authoredTextureCount +=
                            upload.plan->mipPolicy ==
                                airfix::render::
                                    GtiMipPolicy::authoredChain
                            ? 1U
                            : 0U;
                        generatedTextureCount +=
                            upload.plan->mipPolicy ==
                                airfix::render::
                                    GtiMipPolicy::generateFromBase
                            ? 1U
                            : 0U;
                        uploadedMipCount +=
                            upload.plan->uploadedMipCount;
                        allocatedMipCount +=
                            upload.plan->allocatedMipCount;
                        uploadRgbaBytes +=
                            upload.plan->uploadRgbaBytes;
                        residentRgbaBytes +=
                            upload.plan->residentRgbaBytes;
                    }
                }
                ++objectCount;
                selectedBlueprintNodeCount += sceneResolution.blueprintIndices.size();
                selectedMeshInstanceCount += sceneResolution.meshes.size();
                materialCount += sceneResolution.materialIndices.size();
                textureEdgeCount += sceneResolution.textures.size();
                if (resolution.selectorStatus ==
                    airfix::assets::BlueprintSelectorStatus::noSelector) {
                    ++noSelectorCount;
                    continue;
                }
                if (resolution.selectorStatus !=
                        airfix::assets::BlueprintSelectorStatus::unique ||
                    !resolution.blueprintIndex.has_value()) {
                    throw std::runtime_error("object resolver did not select one blueprint");
                }
                const auto& blueprint = ccf.blueprints.at(*resolution.blueprintIndex);
                if (*object.meshName != blueprint.name) {
                    ++caseFoldMatchCount;
                }
                switch (blueprint.kind) {
                case airfix::assets::CcfBlueprintKind::mesh:
                    ++meshCount;
                    break;
                case airfix::assets::CcfBlueprintKind::nullNode: ++nullCount; break;
                case airfix::assets::CcfBlueprintKind::light: ++lightCount; break;
                }
            }
            std::cout << "resolvedObjects=" << objectCount
                      << " mesh=" << meshCount
                      << " null=" << nullCount
                      << " light=" << lightCount
                      << " noSelector=" << noSelectorCount
                      << " caseFoldMatches=" << caseFoldMatchCount
                      << " selectedBlueprintNodes=" << selectedBlueprintNodeCount
                      << " selectedMeshInstances=" << selectedMeshInstanceCount
                      << " materials=" << materialCount
                      << " textureEdges=" << textureEdgeCount
                      << " resolvedTextures=" << resolvedTextureEntryCount
                      << " missingTextureRoots=" << missingTextureRootCount
                      << " invalidTexturePaths=" << invalidTexturePathCount
                      << " missingTextureEntries=" << missingTextureEntryCount
                      << " ambiguousTextureEntries=" << ambiguousTextureEntryCount
                      << " runtimeTextureImports="
                      << runtimeTextureImportCount
                      << " authoredTextureImports="
                      << authoredTextureCount
                      << " generatedTextureImports="
                      << generatedTextureCount
                      << " uploadedMips=" << uploadedMipCount
                      << " allocatedMips=" << allocatedMipCount
                      << " uploadRgba=" << uploadRgbaBytes
                      << " residentRgba=" << residentRgbaBytes
                      << '\n';
            const auto unresolvedTextureCount = missingTextureRootCount +
                invalidTexturePathCount + missingTextureEntryCount +
                ambiguousTextureEntryCount;
            return unresolvedTextureCount == 0U ? 0 : 1;
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
                                    const auto layouts =
                                        airfix::assets::describeGtiMipLevels(variant);
                                    const auto exactMipChain = std::all_of(
                                        layouts.begin(), layouts.end(),
                                        [](const auto& layout) {
                                            return layout.exactTexelLayout;
                                        });
                                    std::size_t rgbaBytes = 0U;
                                    std::size_t decodedMipLevels = 0U;
                                    if (exactMipChain) {
                                        const auto chain =
                                            airfix::assets::decodeGtiMipChainRgba(
                                                data, variant, 4U * 1024U * 1024U);
                                        decodedMipLevels = chain.levels.size();
                                        for (const auto& level : chain.levels) {
                                            rgbaBytes += level.pixels.size();
                                        }
                                    }
                                    else {
                                        const auto base =
                                            airfix::assets::decodeGtiBaseRgba(
                                                data, variant, 4U * 1024U * 1024U);
                                        decodedMipLevels = 1U;
                                        rgbaBytes = base.pixels.size();
                                    }
                                    detail << ":fmt=" << variant.format
                                           << ',' << variant.width << 'x' << variant.height
                                           << ",pal=" << variant.paletteEntries
                                           << ",mips=" << variant.mipmapLevels
                                           << ",pixels=" << variant.pixelDataSize
                                           << ",expected=" << variant.expectedPixelDataSize
                                           << ",trailing=" << variant.trailingBytes
                                           << ",mipUpload="
                                           << (exactMipChain ? "authored" : "generate")
                                           << ",decodedMips=" << decodedMipLevels
                                           << ",rgba=" << rgbaBytes;
                                }
                            }
                            else if (magic == airfix::assets::kCcfMagic) {
                                const auto data = airfix::udsp::readFile(
                                    archivePath, archive, file, kAssetReadLimit);
                                const auto metadata = airfix::assets::parseCcf(data);
                                if (metadata.rooms.empty()) {
                                    throw std::runtime_error(
                                        "CCF has no physical room");
                                }
                                const auto placedScene =
                                    airfix::assets::resolvePlacedScene(metadata);
                                if (!placedScene.issues.empty()) {
                                    throw std::runtime_error(
                                        "CCF placed-scene reference resolution failed");
                                }
                                if (placedScene.nodes.size() !=
                                    metadata.placedNodes.size()) {
                                    throw std::runtime_error(
                                        "CCF placed-scene node count mismatch");
                                }
                                const auto roomScene =
                                    airfix::assets::resolveCcfRoomScene(
                                        metadata);
                                if (!roomScene.issues.empty()) {
                                    throw std::runtime_error(
                                        "CCF room-scene reference resolution failed");
                                }
                                std::uint64_t roomsValidated = 0U;
                                std::uint64_t nonEmptyRooms = 0U;
                                std::uint64_t roomInstanceCount = 0U;
                                std::uint64_t roomMeshSlotCount = 0U;
                                std::uint64_t roomMaterialBindingCount = 0U;
                                std::uint64_t roomTextureEdgeCount = 0U;
                                std::uint64_t roomDrawVertexCount = 0U;
                                std::uint64_t roomDrawIndexCount = 0U;
                                std::uint64_t roomDrawRangeCount = 0U;
                                std::uint64_t receiverInstanceCount = 0U;
                                std::uint64_t coveredObjectCount = 0U;
                                std::vector<std::uint8_t> roomCoverage(
                                    metadata.placedNodes.size(), 0U);
                                for (std::size_t roomIndex = 0U;
                                     roomIndex < metadata.rooms.size();
                                     ++roomIndex) {
                                    const auto roomPlan =
                                        airfix::assets::resolveRoomDrawPlan(
                                            metadata, roomIndex);
                                    if (!roomPlan.issues.empty()) {
                                        throw std::runtime_error(
                                            "CCF room draw planning failed");
                                    }
                                    for (const auto placedNodeIndex :
                                         roomPlan.placedNodeIndices) {
                                        if (placedNodeIndex >=
                                                roomCoverage.size() ||
                                            metadata.placedNodes[
                                                placedNodeIndex].kind !=
                                                airfix::assets::
                                                    CcfPlacedNodeKind::object ||
                                            roomCoverage[placedNodeIndex] !=
                                                0U) {
                                            throw std::runtime_error(
                                                "CCF room draw coverage is "
                                                "invalid");
                                        }
                                        roomCoverage[placedNodeIndex] = 1U;
                                    }
                                    std::vector<airfix::render::DrawMaterial>
                                        roomMaterials;
                                    roomMaterials.reserve(
                                        roomPlan.materialIndices.size());
                                    for (const auto materialIndex :
                                         roomPlan.materialIndices) {
                                        if (materialIndex >=
                                            metadata.materials.size()) {
                                            throw std::runtime_error(
                                                "CCF room material index is invalid");
                                        }
                                        roomMaterials.push_back({
                                            .sourceReference =
                                                metadata.materials[materialIndex].
                                                    reference,
                                        });
                                    }
                                    const auto roomDraw =
                                        airfix::render::buildRoomDrawAssembly(
                                            metadata,
                                            roomIndex,
                                            roomMaterials);
                                    if (!roomDraw.issues.empty()) {
                                        throw std::runtime_error(
                                            "CCF room draw assembly failed");
                                    }
                                    checkedAccumulate(
                                        roomsValidated, 1U, "CCF rooms");
                                    checkedAccumulate(
                                        nonEmptyRooms,
                                        roomDraw.model.instances.empty()
                                            ? 0U
                                            : 1U,
                                        "CCF non-empty rooms");
                                    checkedAccumulate(
                                        roomInstanceCount,
                                        roomDraw.model.instances.size(),
                                        "CCF room instances");
                                    checkedAccumulate(
                                        receiverInstanceCount,
                                        roomIndex == 0U
                                            ? roomDraw.model.instances.size()
                                            : 0U,
                                        "CCF receiver instances");
                                    checkedAccumulate(
                                        roomMeshSlotCount,
                                        roomDraw.model.meshes.size(),
                                        "CCF room mesh slots");
                                    checkedAccumulate(
                                        roomMaterialBindingCount,
                                        roomMaterials.size(),
                                        "CCF room material bindings");
                                    checkedAccumulate(
                                        roomTextureEdgeCount,
                                        roomPlan.textures.size(),
                                        "CCF room texture edges");
                                    for (const auto& drawMesh :
                                         roomDraw.model.meshes) {
                                        checkedAccumulate(
                                            roomDrawVertexCount,
                                            drawMesh.vertices.size(),
                                            "CCF room draw vertices");
                                        checkedAccumulate(
                                            roomDrawIndexCount,
                                            drawMesh.indices.size(),
                                            "CCF room draw indices");
                                        checkedAccumulate(
                                            roomDrawRangeCount,
                                            drawMesh.ranges.size(),
                                            "CCF room draw ranges");
                                    }
                                }
                                for (std::size_t placedNodeIndex = 0U;
                                     placedNodeIndex <
                                         metadata.placedNodes.size();
                                     ++placedNodeIndex) {
                                    const auto expected =
                                        metadata.placedNodes[
                                            placedNodeIndex].kind ==
                                                airfix::assets::
                                                    CcfPlacedNodeKind::object &&
                                        placedScene.nodes[
                                            placedNodeIndex].instantiated;
                                    if (roomCoverage[placedNodeIndex] !=
                                        (expected ? 1U : 0U)) {
                                        throw std::runtime_error(
                                            "CCF room draw coverage is "
                                            "incomplete");
                                    }
                                    checkedAccumulate(
                                        coveredObjectCount,
                                        expected ? 1U : 0U,
                                        "CCF covered objects");
                                }
                                const auto primaryTextures = std::count_if(
                                    metadata.materials.begin(), metadata.materials.end(),
                                    [](const auto& material) {
                                        return material.primaryTexture.has_value();
                                    });
                                const auto secondaryTextures = std::count_if(
                                    metadata.materials.begin(), metadata.materials.end(),
                                    [](const auto& material) {
                                        return material.secondaryTexture.has_value();
                                    });
                                const auto environmentTextures = std::count_if(
                                    metadata.materials.begin(), metadata.materials.end(),
                                    [](const auto& material) {
                                        return material.environmentTexture.has_value();
                                    });
                                const auto emptyNames = std::count_if(
                                    metadata.materials.begin(), metadata.materials.end(),
                                    [](const auto& material) {
                                        return material.name.empty();
                                    });
                                const auto emptyPrefixes = std::count_if(
                                    metadata.materials.begin(), metadata.materials.end(),
                                    [](const auto& material) {
                                        return material.prefix.empty();
                                    });
                                std::size_t vertexCount = 0U;
                                std::size_t triangleCount = 0U;
                                std::size_t textureCoordinateCount = 0U;
                                std::size_t paintCount = 0U;
                                std::size_t optionalVertexVectorCount = 0U;
                                std::size_t rangeCount = 0U;
                                std::size_t convertedMeshCount = 0U;
                                for (const auto& mesh : metadata.meshes) {
                                    const auto converted =
                                        airfix::render::convertLegacyGeometry(mesh);
                                    if (converted.vertices.size() != mesh.vertices.size() ||
                                        converted.triangles.size() != mesh.triangles.size()) {
                                        throw std::runtime_error(
                                            "CCF geometry conversion changed record counts");
                                    }
                                    ++convertedMeshCount;
                                    vertexCount += mesh.vertices.size();
                                    triangleCount += mesh.triangles.size();
                                    rangeCount += mesh.range.has_value() ? 1U : 0U;
                                    optionalVertexVectorCount += std::count_if(
                                        mesh.vertices.begin(), mesh.vertices.end(),
                                        [](const auto& vertex) {
                                            return vertex.optionalVector.has_value();
                                        });
                                    textureCoordinateCount += std::count_if(
                                        mesh.triangles.begin(), mesh.triangles.end(),
                                        [](const auto& triangle) {
                                            return triangle.textureCoordinates.has_value();
                                        });
                                    paintCount += std::count_if(
                                        mesh.triangles.begin(), mesh.triangles.end(),
                                        [](const auto& triangle) {
                                            return triangle.paint.has_value();
                                        });
                                }
                                const auto nullBlueprintCount = std::count_if(
                                    metadata.blueprints.begin(), metadata.blueprints.end(),
                                    [](const auto& blueprint) {
                                        return blueprint.kind ==
                                            airfix::assets::CcfBlueprintKind::nullNode;
                                    });
                                const auto lightBlueprintCount = std::count_if(
                                    metadata.blueprints.begin(), metadata.blueprints.end(),
                                    [](const auto& blueprint) {
                                        return blueprint.kind ==
                                            airfix::assets::CcfBlueprintKind::light;
                                    });
                                std::size_t placedObjectCount = 0U;
                                std::size_t placedNullCount = 0U;
                                std::size_t placedLightCount = 0U;
                                std::size_t placedMatrixCount = 0U;
                                std::size_t placedAlternateCount = 0U;
                                std::size_t placedParentEdgeCount = 0U;
                                std::size_t meshParentEdgeCount = 0U;
                                std::size_t roomFallbackCount = 0U;
                                std::size_t resolvedPortalRoomCount = 0U;
                                std::size_t fogCount = 0U;
                                std::size_t enabledFogCount = 0U;
                                std::size_t staticBspTreeCount = 0U;
                                std::size_t portalBspTreeCount = 0U;
                                std::size_t staticBspNodeCount = 0U;
                                std::size_t portalBspNodeCount = 0U;
                                std::size_t staticBspPolygonCount = 0U;
                                std::size_t portalBspPolygonCount = 0U;
                                for (const auto& room : metadata.rooms) {
                                    fogCount += room.fog.has_value() ? 1U : 0U;
                                    enabledFogCount +=
                                        room.fog.has_value() &&
                                            room.fog->enabledRaw != 0U
                                        ? 1U
                                        : 0U;
                                    staticBspTreeCount +=
                                        room.staticBspTrees.size();
                                    portalBspTreeCount +=
                                        room.portalBspTrees.size();
                                    for (const auto* trees : {
                                             &room.staticBspTrees,
                                             &room.portalBspTrees}) {
                                        for (const auto& tree : *trees) {
                                            if (tree.kind ==
                                                airfix::assets::
                                                    CcfBspTreeKind::staticTree) {
                                                staticBspNodeCount +=
                                                    tree.nodes.size();
                                                staticBspPolygonCount +=
                                                    tree.polygons.size();
                                            }
                                            else {
                                                portalBspNodeCount +=
                                                    tree.nodes.size();
                                                portalBspPolygonCount +=
                                                    tree.polygons.size();
                                            }
                                        }
                                    }
                                }
                                for (const auto& placed : metadata.placedNodes) {
                                    switch (placed.kind) {
                                    case airfix::assets::CcfPlacedNodeKind::object:
                                        ++placedObjectCount;
                                        break;
                                    case airfix::assets::CcfPlacedNodeKind::nullNode:
                                        ++placedNullCount;
                                        break;
                                    case airfix::assets::CcfPlacedNodeKind::light:
                                        ++placedLightCount;
                                        break;
                                    }
                                    if (std::holds_alternative<
                                            std::array<airfix::assets::CcfVector3, 3>>(
                                            placed.transform.orientation)) {
                                        ++placedMatrixCount;
                                    }
                                    else {
                                        ++placedAlternateCount;
                                    }
                                }
                                for (const auto& resolved : placedScene.nodes) {
                                    roomFallbackCount += resolved.roomTarget.kind ==
                                            airfix::assets::PlacedRoomTargetKind::
                                                externalReceiverFallback
                                        ? 1U
                                        : 0U;
                                    resolvedPortalRoomCount +=
                                        resolved.portalRoomTarget.kind ==
                                            airfix::assets::
                                                PlacedPortalRoomTargetKind::parsedRoom
                                        ? 1U
                                        : 0U;
                                    if (!resolved.parentTarget.has_value()) {
                                        continue;
                                    }
                                    if (resolved.parentTarget->kind ==
                                        airfix::assets::PlacedParentTargetKind::placedNode) {
                                        ++placedParentEdgeCount;
                                    }
                                    else {
                                        ++meshParentEdgeCount;
                                    }
                                }
                                detail << "CCF:rooms=" << metadata.rooms.size()
                                       << ":materials=" << metadata.materials.size()
                                       << ",primary=" << primaryTextures
                                       << ",secondary=" << secondaryTextures
                                       << ",environment=" << environmentTextures
                                       << ",emptyNames=" << emptyNames
                                       << ",emptyPrefixes=" << emptyPrefixes
                                       << ":meshes=" << metadata.meshes.size()
                                       << ",converted=" << convertedMeshCount
                                       << ",vertices=" << vertexCount
                                       << ",optionalVertexVectors="
                                       << optionalVertexVectorCount
                                       << ",triangles=" << triangleCount
                                       << ",uv=" << textureCoordinateCount
                                       << ",paint=" << paintCount
                                       << ",ranges=" << rangeCount
                                       << ":blueprints=" << metadata.blueprints.size()
                                       << ",nulls=" << nullBlueprintCount
                                       << ",lights=" << lightBlueprintCount
                                       << ":placed=" << metadata.placedNodes.size()
                                       << ",objects=" << placedObjectCount
                                       << ",nulls=" << placedNullCount
                                       << ",lights=" << placedLightCount
                                       << ",matrices=" << placedMatrixCount
                                       << ",alternate=" << placedAlternateCount
                                       << ",roots=" << placedScene.rootIndices.size()
                                       << ",placedParents=" << placedParentEdgeCount
                                       << ",meshParents=" << meshParentEdgeCount
                                       << ",roomFallbacks=" << roomFallbackCount
                                       << ",portalRooms=" << resolvedPortalRoomCount
                                       << ":spatial=fog=" << fogCount
                                       << ",enabledFog=" << enabledFogCount
                                       << ",staticTrees=" << staticBspTreeCount
                                       << ",portalTrees=" << portalBspTreeCount
                                       << ",staticNodes=" << staticBspNodeCount
                                       << ",portalNodes=" << portalBspNodeCount
                                       << ",staticPolygons="
                                       << staticBspPolygonCount
                                       << ",portalPolygons="
                                       << portalBspPolygonCount
                                       << ",bindings=" << roomScene.bindings.size()
                                       << ":roomDraw=roomsValidated="
                                       << roomsValidated
                                       << ",nonEmptyRooms="
                                       << nonEmptyRooms
                                       << ",instances="
                                       << roomInstanceCount
                                       << ",coveredObjects="
                                       << coveredObjectCount
                                       << ",receiverInstances="
                                       << receiverInstanceCount
                                       << ",meshSlots="
                                       << roomMeshSlotCount
                                       << ",materialBindings="
                                       << roomMaterialBindingCount
                                       << ",textureEdges="
                                       << roomTextureEdgeCount
                                       << ",drawVertices="
                                       << roomDrawVertexCount
                                       << ",drawIndices="
                                       << roomDrawIndexCount
                                       << ",drawRanges="
                                       << roomDrawRangeCount
                                       << ":top=";
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
                                else if (magic == airfix::assets::kAfHouseRoot) {
                                    const auto definition =
                                        airfix::assets::parseWorldDefinition(data);
                                    std::size_t lineCount = 0U;
                                    for (const auto& list : definition.lineLists) {
                                        lineCount += list.lines.size();
                                    }
                                    if (!definition.ccfPath.has_value()) {
                                        throw std::runtime_error(
                                            "world has no CCF path");
                                    }
                                    const auto ccfLookup =
                                        archive.lookup(*definition.ccfPath);
                                    if (ccfLookup.status !=
                                        airfix::udsp::LookupStatus::unique) {
                                        throw std::runtime_error(
                                            "world CCF entry is not unique");
                                    }
                                    const auto ccfData =
                                        airfix::udsp::readFile(
                                            archivePath,
                                            archive,
                                            archive.files().at(
                                                ccfLookup.fileIndex),
                                            kAssetReadLimit);
                                    const auto ccf =
                                        airfix::assets::parseCcf(ccfData);
                                    if (ccf.rooms.empty()) {
                                        throw std::runtime_error(
                                            "world CCF has no physical room");
                                    }
                                    std::uint64_t roomsValidated = 0U;
                                    std::uint64_t nonEmptyRooms = 0U;
                                    std::uint64_t roomInstanceCount = 0U;
                                    std::uint64_t roomMeshSlotCount = 0U;
                                    std::uint64_t receiverInstanceCount = 0U;
                                    std::uint64_t materialBindingCount = 0U;
                                    std::uint64_t textureEdgeCount = 0U;
                                    std::uint64_t textureImportCount = 0U;
                                    std::uint64_t authoredTextureCount = 0U;
                                    std::uint64_t generatedTextureCount = 0U;
                                    std::uint64_t uploadedMipCount = 0U;
                                    std::uint64_t allocatedMipCount = 0U;
                                    std::uint64_t uploadRgbaBytes = 0U;
                                    std::uint64_t residentRgbaBytes = 0U;
                                    for (std::size_t roomIndex = 0U;
                                         roomIndex < ccf.rooms.size();
                                         ++roomIndex) {
                                        const auto worldTextures =
                                            airfix::assets::
                                                resolveWorldRoomTextures(
                                                    definition,
                                                    ccf,
                                                    ccfLookup.fileIndex,
                                                    archive,
                                                    roomIndex);
                                        if (!worldTextures.issues.empty() ||
                                            !worldTextures.plan.issues.empty() ||
                                            !worldTextures.textures.issues.empty()) {
                                            throw std::runtime_error(
                                                "world room texture "
                                                "resolution failed");
                                        }
                                        std::vector<std::uint32_t>
                                            roomMaterialReferences;
                                        roomMaterialReferences.reserve(
                                            worldTextures.plan.materialIndices.
                                                size());
                                        for (const auto materialIndex :
                                             worldTextures.plan.
                                                 materialIndices) {
                                            if (materialIndex >=
                                                ccf.materials.size()) {
                                                throw std::runtime_error(
                                                    "world room material "
                                                    "index is invalid");
                                            }
                                            roomMaterialReferences.push_back(
                                                ccf.materials[materialIndex].
                                                    reference);
                                        }
                                        const auto textureBindings =
                                            airfix::render::
                                                buildTextureBindingPlan(
                                                    roomMaterialReferences,
                                                    worldTextures.plan.
                                                        textures,
                                                    worldTextures.textures);
                                        if (!textureBindings.issues.empty()) {
                                            throw std::runtime_error(
                                                "world room runtime texture "
                                                "binding failed");
                                        }
                                        for (const auto& request :
                                             textureBindings.imports) {
                                            if (request.archiveFileIndex >=
                                                archive.files().size()) {
                                                throw std::runtime_error(
                                                    "world texture import "
                                                    "index is invalid");
                                            }
                                            const auto textureData =
                                                airfix::udsp::readFile(
                                                    archivePath,
                                                    archive,
                                                    archive.files().at(
                                                        request.
                                                            archiveFileIndex),
                                                    kAssetReadLimit);
                                            const auto textureMetadata =
                                                airfix::assets::parseGti(
                                                    textureData);
                                            const auto upload =
                                                airfix::render::
                                                    describeGtiUpload(
                                                        request,
                                                        textureMetadata);
                                            if (!upload.issues.empty() ||
                                                !upload.plan.has_value()) {
                                                throw std::runtime_error(
                                                    "world texture upload "
                                                    "planning failed");
                                            }
                                            checkedAccumulate(
                                                authoredTextureCount,
                                                upload.plan->mipPolicy ==
                                                        airfix::render::
                                                            GtiMipPolicy::
                                                                authoredChain
                                                    ? 1U
                                                    : 0U,
                                                "world authored textures");
                                            checkedAccumulate(
                                                generatedTextureCount,
                                                upload.plan->mipPolicy ==
                                                        airfix::render::
                                                            GtiMipPolicy::
                                                                generateFromBase
                                                    ? 1U
                                                    : 0U,
                                                "world generated textures");
                                            checkedAccumulate(
                                                uploadedMipCount,
                                                upload.plan->
                                                    uploadedMipCount,
                                                "world uploaded mips");
                                            checkedAccumulate(
                                                allocatedMipCount,
                                                upload.plan->
                                                    allocatedMipCount,
                                                "world allocated mips");
                                            checkedAccumulate(
                                                uploadRgbaBytes,
                                                upload.plan->
                                                    uploadRgbaBytes,
                                                "world upload RGBA bytes");
                                            checkedAccumulate(
                                                residentRgbaBytes,
                                                upload.plan->
                                                    residentRgbaBytes,
                                                "world resident RGBA bytes");
                                        }
                                        checkedAccumulate(
                                            roomsValidated,
                                            1U,
                                            "world CCF rooms");
                                        checkedAccumulate(
                                            nonEmptyRooms,
                                            worldTextures.plan.
                                                    placedNodeIndices.empty()
                                                ? 0U
                                                : 1U,
                                            "world non-empty CCF rooms");
                                        checkedAccumulate(
                                            roomInstanceCount,
                                            worldTextures.plan.
                                                placedNodeIndices.size(),
                                            "world room instances");
                                        checkedAccumulate(
                                            receiverInstanceCount,
                                            roomIndex == 0U
                                                ? worldTextures.plan.
                                                      placedNodeIndices.size()
                                                : 0U,
                                            "world receiver instances");
                                        checkedAccumulate(
                                            roomMeshSlotCount,
                                            worldTextures.plan.
                                                meshIndices.size(),
                                            "world room mesh slots");
                                        checkedAccumulate(
                                            materialBindingCount,
                                            textureBindings.materials.size(),
                                            "world material bindings");
                                        checkedAccumulate(
                                            textureEdgeCount,
                                            worldTextures.textures.entries.
                                                size(),
                                            "world texture edges");
                                        checkedAccumulate(
                                            textureImportCount,
                                            textureBindings.imports.size(),
                                            "world texture imports");
                                    }
                                    detail << ":world:texture="
                                           << definition.textureRoot.has_value()
                                           << ",ccf=" << definition.ccfPath.has_value()
                                           << ",backdrop="
                                           << definition.backdrop.has_value()
                                           << ",floorY="
                                           << definition.floorYLevels.has_value()
                                           << ",rooms=" << definition.rooms.size()
                                           << ",lineLists="
                                           << definition.lineLists.size()
                                           << ",lines=" << lineCount
                                           << ",ignoredDuplicates="
                                           << definition.ignoredDuplicateChunks.size()
                                           << ",unknown="
                                           << definition.unknownChunks.size()
                                           << ":ccf=rooms="
                                           << ccf.rooms.size()
                                           << ",meshes="
                                           << ccf.meshes.size()
                                           << ",blueprints="
                                           << ccf.blueprints.size()
                                           << ",placed="
                                           << ccf.placedNodes.size()
                                           << ":roomDraw=roomsValidated="
                                           << roomsValidated
                                           << ",nonEmptyRooms="
                                           << nonEmptyRooms
                                           << ",instances="
                                           << roomInstanceCount
                                           << ",receiverInstances="
                                           << receiverInstanceCount
                                           << ",meshSlots="
                                           << roomMeshSlotCount
                                           << ",materialBindings="
                                           << materialBindingCount
                                           << ",edges="
                                           << textureEdgeCount
                                           << ",imports="
                                           << textureImportCount
                                           << ",authored="
                                           << authoredTextureCount
                                           << ",generated="
                                           << generatedTextureCount
                                           << ",uploadedMips="
                                           << uploadedMipCount
                                           << ",allocatedMips="
                                           << allocatedMipCount
                                           << ",uploadRgba="
                                           << uploadRgbaBytes
                                           << ",residentRgba="
                                           << residentRgbaBytes;
                                }
                                else if (magic == airfix::assets::kAfFullHouseRoot) {
                                    const auto definition =
                                        airfix::assets::parseLevelDefinition(data);
                                    const auto entries =
                                        airfix::assets::resolveMissionEntries(
                                            definition, archive);
                                    std::size_t instanceStateWords = 0U;
                                    std::map<std::size_t, std::size_t>
                                        instanceStateWordWidths;
                                    for (const auto& state : definition.instanceStates) {
                                        instanceStateWords += state.stateWords.size();
                                        ++instanceStateWordWidths[state.stateWords.size()];
                                    }
                                    const auto compatibilityValues = std::count_if(
                                        definition.models.begin(), definition.models.end(),
                                        [](const auto& model) {
                                            return model.compatibilityValue.has_value();
                                        });
                                    const auto resolvedObjects = std::count_if(
                                        entries.placements.begin(),
                                        entries.placements.end(),
                                        [](const auto& placement) {
                                            return placement.objectEntry.status ==
                                                airfix::assets::MissionEntryStatus::unique;
                                        });
                                    const auto resolvedModels = std::count_if(
                                        entries.models.begin(), entries.models.end(),
                                        [](const auto& model) {
                                            return model.objectEntry.status ==
                                                airfix::assets::MissionEntryStatus::unique;
                                        });
                                    detail << ":level:checksum="
                                           << definition.geometryChecksum.has_value()
                                           << ",world="
                                           << definition.worldPath.has_value()
                                           << ",objects=" << definition.objects.size()
                                           << ",models=" << definition.models.size()
                                           << ",modelCompatibility="
                                           << compatibilityValues
                                           << ",instanceStates="
                                           << definition.instanceStates.size()
                                           << ",instanceStateWords="
                                           << instanceStateWords
                                           << ",instanceStateWordWidths=";
                                    auto firstWidth = true;
                                    for (const auto [width, count] :
                                         instanceStateWordWidths) {
                                        if (!firstWidth) {
                                            detail << '+';
                                        }
                                        detail << width << 'x' << count;
                                        firstWidth = false;
                                    }
                                    detail
                                           << ",unknown="
                                           << definition.unknownChunks.size()
                                           << ",worldResolved="
                                           << (entries.worldEntry.status ==
                                               airfix::assets::MissionEntryStatus::unique)
                                           << ",objectsResolved=" << resolvedObjects
                                           << ",modelsResolved=" << resolvedModels
                                           << ",dependencyIssues="
                                           << entries.issues.size();
                                }
                                else if (magic == airfix::assets::kAfBriefingRoot) {
                                    const auto definition =
                                        airfix::assets::parseBriefing(data);
                                    const auto knownStrings =
                                        static_cast<std::size_t>(definition.name.has_value()) +
                                        static_cast<std::size_t>(definition.outline.has_value()) +
                                        static_cast<std::size_t>(definition.outline2.has_value()) +
                                        static_cast<std::size_t>(definition.text.has_value()) +
                                        static_cast<std::size_t>(definition.text2.has_value()) +
                                        static_cast<std::size_t>(definition.primary.has_value()) +
                                        static_cast<std::size_t>(definition.secondary.has_value()) +
                                        static_cast<std::size_t>(definition.aircraft.has_value()) +
                                        static_cast<std::size_t>(
                                            definition.selectedAircraft.has_value());
                                    detail << ":briefing:known=" << knownStrings
                                           << ",unknown="
                                           << definition.unknownChunks.size();
                                }
                                else if (magic == airfix::assets::kAfPathRoot) {
                                    const auto definition =
                                        airfix::assets::parsePathDefinition(data);
                                    detail << ":path:pose="
                                           << definition.pose.has_value()
                                           << ",records=" << definition.records.size()
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
