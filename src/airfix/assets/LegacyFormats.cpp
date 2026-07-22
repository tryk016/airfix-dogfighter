#include "airfix/assets/LegacyFormats.hpp"

#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace airfix::assets {
namespace {

[[nodiscard]] std::uint64_t checkedAdd(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view field) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw ParseError(std::string(field) + " overflows");
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checkedMultiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view field) {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw ParseError(std::string(field) + " overflows");
    }
    return left * right;
}

void requireRange(
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::uint64_t limit,
    const std::string_view field) {
    if (offset > limit || checkedAdd(offset, size, field) > limit) {
        throw ParseError(std::string(field) + " is outside the file (offset=" +
            std::to_string(offset) + ", size=" + std::to_string(size) +
            ", limit=" + std::to_string(limit) + ")");
    }
}

[[nodiscard]] std::uint16_t readU16(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 2U, bytes.size(), field);
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 4U, bytes.size(), field);
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::vector<CcfChunk> parseCcfChunkSequence(
    const std::span<const std::uint8_t> bytes,
    const std::size_t begin,
    const std::size_t end,
    const std::string_view field) {
    if (begin > end || end > bytes.size()) {
        throw ParseError(std::string(field) + " has an invalid containing range");
    }
    std::vector<CcfChunk> chunks;
    auto cursor = begin;
    while (cursor < end) {
        requireRange(cursor, 6U, end, std::string(field) + " chunk header");
        const auto id = readU16(bytes, cursor, std::string(field) + " chunk id");
        const auto totalSize = readU32(
            bytes, cursor + 2U, std::string(field) + " chunk size");
        if (totalSize < 6U) {
            throw ParseError(std::string(field) + " chunk is shorter than its header");
        }
        requireRange(cursor, totalSize, end, std::string(field) + " chunk");
        chunks.push_back({
            .id = id,
            .totalSize = totalSize,
            .offset = cursor,
            .directChildren = {},
        });
        cursor = static_cast<std::size_t>(checkedAdd(
            cursor, totalSize, std::string(field) + " next chunk"));
    }
    return chunks;
}

[[nodiscard]] std::string parseCcfString(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    const std::string_view field) {
    constexpr std::uint16_t kStringChunk = 0xF020U;
    constexpr std::uint32_t kMaximumStringBytes = 4097U;
    if (chunk.id != kStringChunk || chunk.totalSize < 10U) {
        throw ParseError(std::string(field) + " is not a valid CCF string chunk");
    }
    const auto payloadOffset = checkedAdd(chunk.offset, 6U, field);
    const auto byteCount = readU32(
        bytes, static_cast<std::size_t>(payloadOffset), field);
    if (byteCount > kMaximumStringBytes ||
        checkedAdd(10U, byteCount, field) != chunk.totalSize) {
        throw ParseError(std::string(field) + " has an invalid byte count");
    }
    // The shipped writer uses a special zero-byte representation for an
    // empty CcString; non-empty strings include their terminal NUL.
    if (byteCount == 0U) {
        return {};
    }
    const auto textOffset = checkedAdd(payloadOffset, 4U, field);
    requireRange(textOffset, byteCount, bytes.size(), field);
    const auto textBegin = static_cast<std::size_t>(textOffset);
    const auto terminator = checkedAdd(textOffset, byteCount - 1U, field);
    if (bytes[static_cast<std::size_t>(terminator)] != 0U) {
        throw ParseError(std::string(field) + " is not NUL terminated");
    }
    for (std::uint32_t index = 0U; index + 1U < byteCount; ++index) {
        if (bytes[textBegin + index] == 0U) {
            throw ParseError(std::string(field) + " contains an embedded NUL");
        }
    }
    return std::string(
        reinterpret_cast<const char*>(bytes.data() + textBegin),
        byteCount - 1U);
}

struct CcfName {
    std::string name;
    std::string prefix;
};

[[nodiscard]] CcfName parseCcfName(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    const std::string_view field) {
    constexpr std::uint16_t kNameChunk = 0xF010U;
    constexpr std::uint16_t kStringChunk = 0xF020U;
    if (chunk.id != kNameChunk) {
        throw ParseError(std::string(field) + " does not begin with a CCF name");
    }
    const auto begin = checkedAdd(chunk.offset, 6U, field);
    const auto end = checkedAdd(chunk.offset, chunk.totalSize, field);
    const auto children = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(begin),
        static_cast<std::size_t>(end),
        field);
    if (children.size() != 2U || children[0].id != kStringChunk ||
        children[1].id != kStringChunk) {
        throw ParseError(std::string(field) + " must contain name and prefix strings");
    }
    return {
        .name = parseCcfString(bytes, children[0], std::string(field) + " name"),
        .prefix = parseCcfString(bytes, children[1], std::string(field) + " prefix"),
    };
}

[[nodiscard]] std::string parseCcfWrappedString(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk,
    const std::string_view field) {
    constexpr std::uint16_t kStringChunk = 0xF020U;
    const auto begin = checkedAdd(chunk.offset, 6U, field);
    const auto end = checkedAdd(chunk.offset, chunk.totalSize, field);
    const auto children = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(begin),
        static_cast<std::size_t>(end),
        field);
    if (children.size() != 1U || children[0].id != kStringChunk) {
        throw ParseError(std::string(field) + " must contain exactly one CCF string");
    }
    return parseCcfString(bytes, children[0], field);
}

void validateCcfMaterialVectors(
    const std::span<const std::uint8_t> bytes,
    const CcfChunk& chunk) {
    if (chunk.totalSize != 46U) {
        throw ParseError("CCF material vectors have an invalid size");
    }
    const auto first = checkedAdd(chunk.offset, 6U, "CCF material vectors");
    const auto second = checkedAdd(first, 18U, "CCF material vectors");
    if (readU16(bytes, static_cast<std::size_t>(first), "CCF material vector") != 0xF030U ||
        readU32(bytes, static_cast<std::size_t>(first + 2U), "CCF material vector") != 18U ||
        readU16(bytes, static_cast<std::size_t>(second), "CCF material vector") != 0xF030U ||
        readU32(bytes, static_cast<std::size_t>(second + 2U), "CCF material vector") != 18U) {
        throw ParseError("CCF material vectors do not contain two vec3 chunks");
    }
}

[[nodiscard]] CcfMaterialMetadata parseCcfMaterial(
    const std::span<const std::uint8_t> bytes,
    CcfChunk& material) {
    constexpr std::uint16_t kMaterialChunk = 0x2100U;
    if (material.id != kMaterialChunk) {
        throw ParseError("CCF material section contains a non-material chunk");
    }
    const auto payloadBegin = checkedAdd(material.offset, 6U, "CCF material payload");
    const auto materialEnd = checkedAdd(material.offset, material.totalSize, "CCF material end");
    requireRange(payloadBegin, 6U, materialEnd, "CCF material name header");
    CcfChunk nameChunk{
        .id = readU16(bytes, static_cast<std::size_t>(payloadBegin), "CCF material name id"),
        .totalSize = readU32(
            bytes, static_cast<std::size_t>(payloadBegin + 2U), "CCF material name size"),
        .offset = payloadBegin,
        .directChildren = {},
    };
    if (nameChunk.totalSize < 6U) {
        throw ParseError("CCF material name is shorter than its header");
    }
    requireRange(nameChunk.offset, nameChunk.totalSize, materialEnd, "CCF material name");
    const auto name = parseCcfName(bytes, nameChunk, "CCF material name");
    auto cursor = checkedAdd(nameChunk.offset, nameChunk.totalSize, "CCF material reference");
    requireRange(cursor, 4U, materialEnd, "CCF material reference");
    const auto reference = readU32(
        bytes, static_cast<std::size_t>(cursor), "CCF material reference");
    cursor = checkedAdd(cursor, 4U, "CCF material properties");
    material.directChildren = parseCcfChunkSequence(
        bytes,
        static_cast<std::size_t>(cursor),
        static_cast<std::size_t>(materialEnd),
        "CCF material property");

    CcfMaterialMetadata metadata{
        .name = name.name,
        .prefix = name.prefix,
        .reference = reference,
        .primaryTexture = std::nullopt,
        .secondaryTexture = std::nullopt,
        .environmentTexture = std::nullopt,
        .offset = material.offset,
    };
    bool hasVectors = false;
    bool hasFlags = false;
    bool hasMode = false;
    bool hasScalars = false;
    for (const auto& property : material.directChildren) {
        switch (property.id) {
        case 0x2110U:
            if (metadata.primaryTexture.has_value()) {
                throw ParseError("CCF material has duplicate primary textures");
            }
            metadata.primaryTexture = parseCcfWrappedString(
                bytes, property, "CCF material primary texture");
            break;
        case 0x2111U:
            if (metadata.secondaryTexture.has_value()) {
                throw ParseError("CCF material has duplicate secondary textures");
            }
            metadata.secondaryTexture = parseCcfWrappedString(
                bytes, property, "CCF material secondary texture");
            break;
        case 0x2120U:
            if (metadata.environmentTexture.has_value()) {
                throw ParseError("CCF material has duplicate environment textures");
            }
            metadata.environmentTexture = parseCcfWrappedString(
                bytes, property, "CCF material environment texture");
            break;
        case 0x2140U:
            if (hasVectors) {
                throw ParseError("CCF material has duplicate vector properties");
            }
            validateCcfMaterialVectors(bytes, property);
            hasVectors = true;
            break;
        case 0x2150U:
            if (hasFlags || property.totalSize != 12U) {
                throw ParseError("CCF material has invalid flag properties");
            }
            hasFlags = true;
            break;
        case 0x2151U:
            if (hasMode || property.totalSize != 7U) {
                throw ParseError("CCF material has an invalid mode property");
            }
            hasMode = true;
            break;
        case 0x2152U:
            if (hasScalars || property.totalSize != 18U) {
                throw ParseError("CCF material has invalid scalar properties");
            }
            hasScalars = true;
            break;
        default:
            // LoadSceneCcf closes unknown chunks and continues. Retain them in
            // directChildren so later schema versions remain inspectable.
            break;
        }
    }
    return metadata;
}

} // namespace

std::uint32_t gtiBitsPerPixel(const std::uint32_t format) noexcept {
    switch (format) {
    case 1U: return 4U;
    case 2U:
    case 3U:
    case 9U:
    case 0x16U: return 8U;
    case 4U:
    case 5U:
    case 6U:
    case 0x0AU:
    case 0x0BU:
    case 0x0CU:
    case 0x12U:
    case 0x13U:
    case 0x17U:
    case 0x18U:
    case 0x19U:
    case 0x1AU:
    case 0x1CU:
    case 0x1DU:
    case 0x1EU:
    case 0x20U:
    case 0x21U: return 16U;
    case 7U:
    case 0x14U: return 24U;
    case 8U:
    case 0x15U:
    case 0x1BU:
    case 0x1FU: return 32U;
    default: return 0U;
    }
}

std::uint64_t expectedGtiPixelBytes(
    const std::uint32_t format,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t mipmapLevels) {
    const auto bits = gtiBitsPerPixel(format);
    if (bits == 0U || mipmapLevels == 0U || mipmapLevels > 16U) {
        throw ParseError("GTI uses an unsupported pixel format or mipmap count");
    }
    const auto pixels = checkedMultiply(width, height, "GTI pixel count");
    const auto baseBytes = checkedMultiply(pixels, bits, "GTI base bits") / 8U;
    std::uint64_t total = 0U;
    for (std::uint32_t level = 0U; level < mipmapLevels; ++level) {
        total = checkedAdd(total, baseBytes >> (level * 2U), "GTI mipmap bytes");
    }
    return total;
}

RgbaImage decodeGtiBaseRgba(
    const std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    const std::size_t outputLimit) {
    const auto bits = gtiBitsPerPixel(variant.format);
    if (bits == 0U || variant.width == 0U || variant.height == 0U) {
        throw ParseError("cannot decode unsupported GTI base image");
    }
    const auto pixelCount = checkedMultiply(
        variant.width, variant.height, "GTI base pixel count");
    const auto outputBytes = checkedMultiply(pixelCount, 4U, "GTI RGBA output");
    if (outputBytes > outputLimit ||
        outputBytes > std::numeric_limits<std::size_t>::max()) {
        throw ParseError("GTI RGBA output exceeds the configured limit");
    }
    const auto sourceBits = checkedMultiply(pixelCount, bits, "GTI base source bits");
    const auto sourceBytes = checkedAdd(sourceBits, 7U, "GTI rounded source bits") / 8U;
    requireRange(variant.pixelDataOffset, sourceBytes, bytes.size(), "GTI base pixels");
    if (variant.paletteEntries != 0U) {
        const auto paletteBytes = static_cast<std::uint64_t>(variant.paletteEntries) * 4U;
        if (variant.pixelDataOffset < paletteBytes) {
            throw ParseError("GTI palette offset underflows");
        }
        requireRange(
            variant.pixelDataOffset - paletteBytes,
            paletteBytes,
            bytes.size(),
            "GTI palette");
    }

    RgbaImage image{
        .width = variant.width,
        .height = variant.height,
        .pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(outputBytes)),
    };
    const auto source = bytes.subspan(static_cast<std::size_t>(variant.pixelDataOffset));
    const auto paletteOffset = static_cast<std::size_t>(
        variant.pixelDataOffset - static_cast<std::uint64_t>(variant.paletteEntries) * 4U);

    const auto writePixel = [&](
        const std::size_t index,
        const std::uint8_t red,
        const std::uint8_t green,
        const std::uint8_t blue,
        const std::uint8_t alpha) {
        const auto output = index * 4U;
        image.pixels[output] = red;
        image.pixels[output + 1U] = green;
        image.pixels[output + 2U] = blue;
        image.pixels[output + 3U] = alpha;
    };
    const auto palettePixel = [&](const std::size_t outputIndex, const std::uint8_t index,
                                  const std::uint8_t alpha) {
        if (index >= variant.paletteEntries) {
            throw ParseError("GTI palette index is outside the palette");
        }
        const auto palette = paletteOffset + static_cast<std::size_t>(index) * 4U;
        // CcColorInt/ARGB8888 is little-endian B,G,R,A in memory.
        writePixel(
            outputIndex,
            bytes[palette + 2U],
            bytes[palette + 1U],
            bytes[palette],
            alpha);
    };

    for (std::size_t index = 0U; index < pixelCount; ++index) {
        switch (variant.format) {
        case 3U:
            palettePixel(index, source[index], 0xFFU);
            break;
        case 4U:
            palettePixel(index, source[index * 2U], source[index * 2U + 1U]);
            break;
        case 6U: {
            const auto packed = static_cast<std::uint16_t>(source[index * 2U]) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(source[index * 2U + 1U]) << 8U);
            // Match the original conversion: nibbles occupy the high four
            // output bits (0xF becomes 0xF0 rather than replicated 0xFF).
            writePixel(
                index,
                static_cast<std::uint8_t>((packed & 0x0F00U) >> 4U),
                static_cast<std::uint8_t>(packed & 0x00F0U),
                static_cast<std::uint8_t>((packed & 0x000FU) << 4U),
                static_cast<std::uint8_t>((packed & 0xF000U) >> 8U));
            break;
        }
        case 7U:
            writePixel(
                index,
                source[index * 3U],
                source[index * 3U + 1U],
                source[index * 3U + 2U],
                0xFFU);
            break;
        case 8U:
            writePixel(
                index,
                source[index * 4U + 2U],
                source[index * 4U + 1U],
                source[index * 4U],
                source[index * 4U + 3U]);
            break;
        default:
            throw ParseError("GTI base decoder does not implement this format");
        }
    }
    return image;
}

GtiMetadata parseGti(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 8U || readU32(bytes, 0U, "GTI magic") != kGtiMagic) {
        throw ParseError("invalid GTI magic");
    }
    if (readU32(bytes, 4U, "GTI version") != kGtiVersion) {
        throw ParseError("unsupported GTI version");
    }

    GtiMetadata metadata;
    std::set<std::uint32_t> formats;
    std::size_t cursor = 8U;
    while (cursor < bytes.size()) {
        requireRange(cursor, 8U, bytes.size(), "GTI chunk header");
        const auto id = readU32(bytes, cursor, "GTI chunk id");
        const auto payloadSize = readU32(bytes, cursor + 4U, "GTI chunk size");
        const auto payloadOffset = checkedAdd(cursor, 8U, "GTI chunk payload");
        const auto declaredEnd = checkedAdd(payloadOffset, payloadSize, "GTI chunk payload");
        auto availablePayloadSize = static_cast<std::uint64_t>(payloadSize);
        if (declaredEnd > bytes.size()) {
            // Some shipped GTI files end inside the declared extent of their
            // final Imag variant. The original loader seeks to the declared
            // end (which terminates its scan) but reads the selected variant
            // using format-derived byte counts. Preserve this observed quirk
            // without allowing a following chunk or an undersized metadata
            // header.
            if (id != kGtiImageChunk || payloadOffset > bytes.size()) {
                throw ParseError("GTI non-image chunk payload exceeds the file");
            }
            availablePayloadSize = bytes.size() - payloadOffset;
            metadata.terminalDeclaredOverrun = true;
        }
        metadata.chunks.push_back({
            .id = id,
            .payloadSize = payloadSize,
            .payloadOffset = payloadOffset,
        });

        if (id == kGtiChecksumChunk) {
            if (payloadSize != 4U) {
                throw ParseError("invalid GTI checksum chunk");
            }
            metadata.checksum = readU32(
                bytes, static_cast<std::size_t>(payloadOffset), "GTI checksum");
            ++metadata.checksumChunkCount;
        }
        else if (id == kGtiImageChunk) {
            if (availablePayloadSize < 20U) {
                throw ParseError("GTI image chunk is shorter than its metadata");
            }
            const auto payload = static_cast<std::size_t>(payloadOffset);
            const auto format = readU32(bytes, payload, "GTI format");
            const auto width = readU32(bytes, payload + 4U, "GTI width");
            const auto height = readU32(bytes, payload + 8U, "GTI height");
            const auto paletteEntries = readU32(bytes, payload + 12U, "GTI palette count");
            const auto mipmapLevels = readU32(bytes, payload + 16U, "GTI mipmap count");
            if (width == 0U || height == 0U || width > 32768U || height > 32768U ||
                paletteEntries > 65536U || mipmapLevels == 0U || mipmapLevels > 16U) {
                throw ParseError("GTI image metadata is outside supported bounds");
            }
            const auto paletteBytes = static_cast<std::uint64_t>(paletteEntries) * 4U;
            if (paletteBytes > availablePayloadSize - 20U) {
                throw ParseError("GTI palette exceeds image chunk");
            }
            if (!formats.insert(format).second) {
                throw ParseError("GTI contains duplicate image format");
            }
            const auto pixelDataOffset = checkedAdd(
                payloadOffset, checkedAdd(20U, paletteBytes, "GTI image metadata"),
                "GTI pixel data");
            const auto actualPixelBytes = checkedAdd(
                payloadOffset, availablePayloadSize, "GTI image end") - pixelDataOffset;
            const auto expectedPixelBytes = expectedGtiPixelBytes(
                format, width, height, mipmapLevels);
            if (actualPixelBytes < expectedPixelBytes) {
                throw ParseError("GTI image chunk is shorter than its computed mipmap data");
            }
            metadata.variants.push_back({
                .format = format,
                .width = width,
                .height = height,
                .paletteEntries = paletteEntries,
                .mipmapLevels = mipmapLevels,
                .pixelDataOffset = pixelDataOffset,
                .pixelDataSize = static_cast<std::uint32_t>(actualPixelBytes),
                .expectedPixelDataSize = expectedPixelBytes,
                .trailingBytes = actualPixelBytes - expectedPixelBytes,
            });
        }
        cursor = declaredEnd > bytes.size()
            ? bytes.size()
            : static_cast<std::size_t>(declaredEnd);
    }
    if (metadata.variants.empty()) {
        throw ParseError("GTI contains no image variant");
    }
    return metadata;
}

CcfMetadata parseCcf(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 14U || readU32(bytes, 0U, "CCF magic") != kCcfMagic) {
        throw ParseError("invalid CCF magic");
    }
    if (readU32(bytes, 4U, "CCF version") != kCcfVersion) {
        throw ParseError("unsupported CCF version");
    }
    const auto rootId = readU16(bytes, 8U, "CCF root id");
    const auto rootSize = readU32(bytes, 10U, "CCF root size");
    if (rootId != 1U || rootSize < 6U ||
        checkedAdd(8U, rootSize, "CCF root") != bytes.size()) {
        throw ParseError("CCF has an invalid root chunk");
    }

    CcfMetadata metadata{
        .rootId = rootId,
        .rootSize = rootSize,
        .topLevelChunks = {},
        .materials = {},
    };
    metadata.topLevelChunks = parseCcfChunkSequence(bytes, 14U, bytes.size(), "CCF root");
    for (auto& section : metadata.topLevelChunks) {
        const auto childBegin = checkedAdd(section.offset, 6U, "CCF section payload");
        const auto childEnd = checkedAdd(section.offset, section.totalSize, "CCF section end");
        section.directChildren = parseCcfChunkSequence(
            bytes,
            static_cast<std::size_t>(childBegin),
            static_cast<std::size_t>(childEnd),
            "CCF section");
        if (section.id == 0x2000U) {
            for (auto& material : section.directChildren) {
                if (material.id == 0x2100U) {
                    metadata.materials.push_back(parseCcfMaterial(bytes, material));
                }
            }
        }
    }
    return metadata;
}

} // namespace airfix::assets
