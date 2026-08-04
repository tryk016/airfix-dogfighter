#include "airfix/texture/TexturePackLocator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using airfix::texture::TexturePackLocatorCodecError;
using airfix::texture::TexturePackLocatorCodecIssue;
using airfix::texture::TexturePackLocatorRecord;

constexpr std::string_view packageDirectory =
    "pack-12345678-90ab-cdef-1234-567890abcdef";

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void requireCodecError(const std::function<void()>& action,
                       const TexturePackLocatorCodecIssue expected) {
    try {
        action();
    } catch (const TexturePackLocatorCodecError& error) {
        require(error.issue() == expected,
                "texture locator failed with an unexpected category");
        return;
    }
    throw std::runtime_error("expected texture locator codec failure");
}

[[nodiscard]] TexturePackLocatorRecord record() {
    return {
        .packageDirectoryName = std::string(packageDirectory),
        .manifestRelativePath = "manifests/reviewed.jsonl",
    };
}

void writeU16(std::vector<std::uint8_t>& bytes,
              const std::size_t offset,
              const std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
}

void testCanonicalRoundTrip() {
    const auto expected = record();
    const auto encoded = airfix::texture::encodeTexturePackLocator(expected);
    const auto decoded = airfix::texture::decodeTexturePackLocator(encoded);
    require(std::holds_alternative<TexturePackLocatorRecord>(decoded) &&
                std::get<TexturePackLocatorRecord>(decoded) == expected,
            "AFTL round trip changed the locator");
    require(encoded.size() ==
                52U + expected.packageDirectoryName.size() +
                    expected.manifestRelativePath.size() &&
                std::equal(encoded.begin(), encoded.begin() + 4, "AFTL") &&
                encoded[4] == 1U && encoded[5] == 0U,
            "AFTL canonical header is incorrect");
    require(airfix::texture::encodeTexturePackLocator(expected) == encoded,
            "AFTL encoding is not deterministic");
}

void testDirectoryIdentityValidation() {
    require(airfix::texture::validTexturePackDirectoryName(packageDirectory),
            "canonical importer directory identity was rejected");
    for (const auto invalid : {
             "pack-12345678-90AB-cdef-1234-567890abcdef",
             "pack-12345678-90ab-cdef-1234-567890abcdeg",
             "pack-1234567890ab-cdef-1234-567890abcdef",
             "other-12345678-90ab-cdef-1234-567890abcdef",
             "pack-../../private",
         }) {
        require(!airfix::texture::validTexturePackDirectoryName(invalid),
                "unsafe package directory identity was accepted");
        auto invalidRecord = record();
        invalidRecord.packageDirectoryName = invalid;
        requireCodecError(
            [&invalidRecord] {
                static_cast<void>(
                    airfix::texture::encodeTexturePackLocator(invalidRecord));
            },
            TexturePackLocatorCodecIssue::invalidDirectoryName);
    }
}

void testManifestPathValidation() {
    for (const auto invalid : {
             "",
             "/absolute.jsonl",
             "../escape.jsonl",
             "manifests/../escape.jsonl",
             "manifests//reviewed.jsonl",
             "manifests/con/reviewed.jsonl",
             "manifests/reviewed.jsonl/",
         }) {
        auto invalidRecord = record();
        invalidRecord.manifestRelativePath = invalid;
        requireCodecError(
            [&invalidRecord] {
                static_cast<void>(
                    airfix::texture::encodeTexturePackLocator(invalidRecord));
            },
            TexturePackLocatorCodecIssue::invalidManifestRelativePath);
    }
}

void testMalformedAndChecksumFailures() {
    const auto canonical =
        airfix::texture::encodeTexturePackLocator(record());
    for (std::size_t size = 0U; size < canonical.size(); ++size) {
        requireCodecError(
            [&canonical, size] {
                static_cast<void>(airfix::texture::decodeTexturePackLocator(
                    std::span<const std::uint8_t>(canonical).first(size)));
            },
            size < 52U ? TexturePackLocatorCodecIssue::malformed
                       : TexturePackLocatorCodecIssue::malformed);
    }

    auto badFlags = canonical;
    badFlags[6] = 1U;
    requireCodecError(
        [&badFlags] {
            static_cast<void>(
                airfix::texture::decodeTexturePackLocator(badFlags));
        },
        TexturePackLocatorCodecIssue::malformed);

    auto badReserved = canonical;
    badReserved[16] = 1U;
    requireCodecError(
        [&badReserved] {
            static_cast<void>(
                airfix::texture::decodeTexturePackLocator(badReserved));
        },
        TexturePackLocatorCodecIssue::malformed);

    auto badChecksum = canonical;
    badChecksum[20] ^= 0x01U;
    requireCodecError(
        [&badChecksum] {
            static_cast<void>(
                airfix::texture::decodeTexturePackLocator(badChecksum));
        },
        TexturePackLocatorCodecIssue::checksumMismatch);
}

void testPastAndFutureSchemas() {
    auto past = airfix::texture::encodeTexturePackLocator(record());
    writeU16(past, 4U, 0U);
    requireCodecError(
        [&past] {
            static_cast<void>(airfix::texture::decodeTexturePackLocator(past));
        },
        TexturePackLocatorCodecIssue::unsupportedPastSchema);

    auto future = airfix::texture::encodeTexturePackLocator(record());
    writeU16(future, 4U, 2U);
    const auto decoded = airfix::texture::decodeTexturePackLocator(future);
    const auto* opaque = std::get_if<
        airfix::texture::OpaqueFutureTexturePackLocatorRecord>(&decoded);
    require(opaque != nullptr && opaque->schemaVersion == 2U &&
                opaque->exactBytes == future,
            "future AFTL record was not retained byte-for-byte");
}

void testInputLimit() {
    std::vector<std::uint8_t> oversized(
        airfix::texture::maximumTexturePackLocatorBytes + 1U, 0U);
    requireCodecError(
        [&oversized] {
            static_cast<void>(
                airfix::texture::decodeTexturePackLocator(oversized));
        },
        TexturePackLocatorCodecIssue::tooLarge);
}

} // namespace

int main() {
    try {
        testCanonicalRoundTrip();
        testDirectoryIdentityValidation();
        testManifestPathValidation();
        testMalformedAndChecksumFailures();
        testPastAndFutureSchemas();
        testInputLimit();
        std::cout << "Texture pack locator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
