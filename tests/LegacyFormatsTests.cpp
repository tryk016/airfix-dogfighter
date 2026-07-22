#include "airfix/assets/LegacyFormats.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void appendU16(Bytes& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(Bytes& bytes, const std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8U)));
    }
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireParseError(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const airfix::assets::ParseError&) {
        return;
    }
    throw std::runtime_error("expected asset ParseError");
}

[[nodiscard]] Bytes makeGti() {
    Bytes bytes;
    appendU32(bytes, airfix::assets::kGtiMagic);
    appendU32(bytes, airfix::assets::kGtiVersion);
    appendU32(bytes, airfix::assets::kGtiChecksumChunk);
    appendU32(bytes, 4U);
    appendU32(bytes, 0x12345678U);
    appendU32(bytes, airfix::assets::kGtiImageChunk);
    appendU32(bytes, 36U);
    appendU32(bytes, 8U);
    appendU32(bytes, 2U);
    appendU32(bytes, 2U);
    appendU32(bytes, 0U);
    appendU32(bytes, 1U);
    bytes.insert(bytes.end(), 16U, 0xAAU);
    return bytes;
}

[[nodiscard]] Bytes makeCcf() {
    Bytes bytes;
    appendU32(bytes, airfix::assets::kCcfMagic);
    appendU32(bytes, airfix::assets::kCcfVersion);
    appendU16(bytes, 1U);
    appendU32(bytes, 18U);
    appendU16(bytes, 0x3000U);
    appendU32(bytes, 12U);
    appendU16(bytes, 0x3100U);
    appendU32(bytes, 6U);
    return bytes;
}

void testGti() {
    const auto bytes = makeGti();
    const auto metadata = airfix::assets::parseGti(bytes);
    require(metadata.checksum == 0x12345678U, "GTI checksum mismatch");
    require(metadata.chunks.size() == 2U, "GTI chunk count mismatch");
    require(metadata.variants.size() == 1U, "GTI variant count mismatch");
    require(metadata.variants[0].format == 8U, "GTI format mismatch");
    require(metadata.variants[0].width == 2U && metadata.variants[0].height == 2U,
        "GTI dimensions mismatch");
    require(metadata.variants[0].pixelDataSize == 16U, "GTI pixel bytes mismatch");
    require(metadata.variants[0].expectedPixelDataSize == 16U,
        "GTI computed pixel bytes mismatch");

    auto invalid = bytes;
    invalid[0] = 'X';
    requireParseError([&] { (void)airfix::assets::parseGti(invalid); });
    invalid = bytes;
    invalid[12] = 0x05U;
    requireParseError([&] { (void)airfix::assets::parseGti(invalid); });

    auto repeatedChecksum = bytes;
    Bytes checksumChunk;
    appendU32(checksumChunk, airfix::assets::kGtiChecksumChunk);
    appendU32(checksumChunk, 4U);
    appendU32(checksumChunk, 0xAABBCCDDU);
    repeatedChecksum.insert(
        repeatedChecksum.begin() + 20, checksumChunk.begin(), checksumChunk.end());
    const auto repeatedMetadata = airfix::assets::parseGti(repeatedChecksum);
    require(repeatedMetadata.checksumChunkCount == 2U,
        "GTI repeated checksum count mismatch");
    require(repeatedMetadata.checksum == 0xAABBCCDDU,
        "GTI must keep the final checksum");

    auto terminalOverrun = bytes;
    terminalOverrun[24] = 40U;
    const auto overrunMetadata = airfix::assets::parseGti(terminalOverrun);
    require(overrunMetadata.terminalDeclaredOverrun,
        "GTI terminal declared-size quirk was not recorded");
    require(overrunMetadata.variants[0].expectedPixelDataSize == 16U,
        "GTI terminal overrun changed the computed pixel size");

    requireParseError([] {
        (void)airfix::assets::expectedGtiPixelBytes(
            8U,
            std::numeric_limits<std::uint32_t>::max(),
            std::numeric_limits<std::uint32_t>::max(),
            1U);
    });
}

void testCcf() {
    const auto bytes = makeCcf();
    const auto metadata = airfix::assets::parseCcf(bytes);
    require(metadata.rootId == 1U, "CCF root id mismatch");
    require(metadata.topLevelChunks.size() == 1U, "CCF top-level count mismatch");
    require(metadata.topLevelChunks[0].id == 0x3000U, "CCF child id mismatch");
    require(metadata.topLevelChunks[0].directChildren.size() == 1U,
        "CCF direct child count mismatch");
    require(metadata.topLevelChunks[0].directChildren[0].id == 0x3100U,
        "CCF direct child id mismatch");

    auto invalid = bytes;
    invalid[10] = 17U;
    requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });
    invalid = bytes;
    invalid[16] = 5U;
    requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });
}

[[nodiscard]] airfix::assets::GtiVariant onePixelVariant(
    const std::uint32_t format,
    const std::uint32_t paletteEntries,
    const std::uint64_t pixelOffset,
    const std::uint32_t pixelBytes) {
    return {
        .format = format,
        .width = 1U,
        .height = 1U,
        .paletteEntries = paletteEntries,
        .mipmapLevels = 1U,
        .pixelDataOffset = pixelOffset,
        .pixelDataSize = pixelBytes,
        .expectedPixelDataSize = pixelBytes,
        .trailingBytes = 0U,
    };
}

void testGtiBaseDecoding() {
    {
        const Bytes data{0x03U, 0x02U, 0x01U, 0x99U, 0x00U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(3U, 1U, 4U, 1U), 4U);
        require(image.pixels == Bytes{0x01U, 0x02U, 0x03U, 0xFFU},
            "GTI P8 conversion mismatch");
    }
    {
        const Bytes data{0x30U, 0x20U, 0x10U, 0x99U, 0x00U, 0x80U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(4U, 1U, 4U, 2U), 4U);
        require(image.pixels == Bytes{0x10U, 0x20U, 0x30U, 0x80U},
            "GTI P8A8 conversion mismatch");
    }
    {
        const Bytes data{0x23U, 0xF1U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(6U, 0U, 0U, 2U), 4U);
        require(image.pixels == Bytes{0x10U, 0x20U, 0x30U, 0xF0U},
            "GTI ARGB4444 conversion mismatch");
    }
    {
        const Bytes data{0x11U, 0x22U, 0x33U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(7U, 0U, 0U, 3U), 4U);
        require(image.pixels == Bytes{0x11U, 0x22U, 0x33U, 0xFFU},
            "GTI RGB888 conversion mismatch");
    }
    {
        const Bytes data{0x33U, 0x22U, 0x11U, 0x44U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(8U, 0U, 0U, 4U), 4U);
        require(image.pixels == Bytes{0x11U, 0x22U, 0x33U, 0x44U},
            "GTI ARGB8888 conversion mismatch");
    }
    {
        const Bytes data{0x33U, 0x22U, 0x11U, 0x44U};
        requireParseError([&] {
            (void)airfix::assets::decodeGtiBaseRgba(
                data, onePixelVariant(8U, 0U, 0U, 4U), 3U);
        });
    }
}

} // namespace

int main() {
    try {
        testGti();
        testCcf();
        testGtiBaseDecoding();
        std::cout << "all legacy asset tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "legacy asset test failure: " << error.what() << '\n';
        return 1;
    }
}
