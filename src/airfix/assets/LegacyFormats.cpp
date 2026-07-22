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
    }
    return metadata;
}

} // namespace airfix::assets
