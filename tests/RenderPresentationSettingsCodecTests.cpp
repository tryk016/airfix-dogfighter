#include "airfix/crypto/Sha256.hpp"
#include "airfix/render/RenderPresentationSettings.hpp"
#include "airfix/settings/RenderPresentationSettingsCodec.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void putU16(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void putU32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void repairEnvelope(std::vector<std::uint8_t>& bytes) {
    putU32(bytes, 12U, static_cast<std::uint32_t>(bytes.size()));
    airfix::crypto::Sha256 hash;
    hash.update(std::span<const std::uint8_t>(bytes).first(16U));
    hash.update(std::span<const std::uint8_t>(bytes).subspan(48U));
    const auto digest = hash.finish();
    std::copy(digest.begin(), digest.end(), bytes.begin() + 16);
}

void requireCodecError(const std::function<void()>& action,
                       const airfix::settings::RenderSettingsCodecErrorKind expected) {
    try {
        action();
    } catch (const airfix::settings::RenderSettingsCodecError& error) {
        require(error.kind() == expected, "AFRS error kind mismatch");
        return;
    }
    throw std::runtime_error("expected AFRS codec error");
}

[[nodiscard]] airfix::render::RenderPresentationSettingsRecord testRecord() {
    const auto built = airfix::render::makeRenderPresentationSettingsRecord({
        .renderScalePercent = 137.5F,
        .scenePresentation = airfix::render::ScenePresentationMode::originalFourByThree,
        .visualProfile = airfix::render::VisualProfile::enhanced,
        .diagnosticsOverlayEnabled = true,
    });
    require(built.complete(), "test semantic record is invalid");
    return *built.record;
}

[[nodiscard]] std::string hex(const std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

void testCanonicalRoundTrip() {
    const auto record = testRecord();
    const auto bytes = airfix::settings::encodeRenderSettingsDocument(record);
    require(bytes.size() == 71U, "AFRS canonical size changed");
    require(hex(bytes) == "41465253010000000100000047000000"
                          "C24A5674CC5CDC2EA296608C2C727940"
                          "10EECFD5CC5A3EFA0474B22700F4B38F"
                          "0100040000800943020001000103000100010400010001",
            "AFRS golden byte vector changed");
    require(std::string(bytes.begin(), bytes.begin() + 4) == "AFRS",
            "AFRS canonical magic changed");
    const auto decoded = airfix::settings::decodeRenderSettingsDocument(bytes);
    require(std::holds_alternative<airfix::render::RenderPresentationSettingsRecord>(decoded),
            "current AFRS decoded as opaque");
    require(std::get<airfix::render::RenderPresentationSettingsRecord>(decoded) == record,
            "AFRS semantic round-trip changed fields");
    require(airfix::settings::encodeRenderSettingsDocument(
                std::get<airfix::render::RenderPresentationSettingsRecord>(decoded)) == bytes,
            "AFRS byte round-trip is not deterministic");

    for (std::size_t size = 0U; size < bytes.size(); ++size) {
        const std::vector<std::uint8_t> truncated(
            bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
        try {
            (void)airfix::settings::decodeRenderSettingsDocument(truncated);
        } catch (const airfix::settings::RenderSettingsCodecError&) {
            continue;
        }
        throw std::runtime_error("AFRS accepted truncation at byte " + std::to_string(size));
    }
}

void testEnvelopeFailures() {
    const auto canonical = airfix::settings::encodeRenderSettingsDocument(testRecord());
    auto mutated = canonical;
    mutated[0] = 'X';
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(mutated); },
                      airfix::settings::RenderSettingsCodecErrorKind::badMagic);

    mutated = canonical;
    putU16(mutated, 4U, 2U);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(mutated); },
                      airfix::settings::RenderSettingsCodecErrorKind::unsupportedEnvelopeVersion);

    mutated = canonical;
    putU16(mutated, 6U, 1U);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(mutated); },
                      airfix::settings::RenderSettingsCodecErrorKind::unsupportedFlags);

    mutated = canonical;
    putU32(mutated, 12U, 72U);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(mutated); },
                      airfix::settings::RenderSettingsCodecErrorKind::declaredSizeMismatch);

    mutated = canonical;
    mutated.back() ^= 1U;
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(mutated); },
                      airfix::settings::RenderSettingsCodecErrorKind::integrityMismatch);

    std::vector<std::uint8_t> oversized(airfix::settings::maximumRenderSettingsDocumentBytes + 1U);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(oversized); },
                      airfix::settings::RenderSettingsCodecErrorKind::tooLarge);
}

void testStrictFields() {
    const auto canonical = airfix::settings::encodeRenderSettingsDocument(testRecord());

    auto duplicate = canonical;
    putU16(duplicate, 66U, 3U);
    repairEnvelope(duplicate);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(duplicate); },
                      airfix::settings::RenderSettingsCodecErrorKind::duplicateField);

    auto unknown = canonical;
    putU16(unknown, 66U, 5U);
    repairEnvelope(unknown);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(unknown); },
                      airfix::settings::RenderSettingsCodecErrorKind::unknownField);

    auto outOfOrder = canonical;
    putU16(outOfOrder, 61U, 1U);
    repairEnvelope(outOfOrder);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(outOfOrder); },
                      airfix::settings::RenderSettingsCodecErrorKind::nonCanonicalFieldOrder);

    auto missing = canonical;
    missing.resize(66U);
    repairEnvelope(missing);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(missing); },
                      airfix::settings::RenderSettingsCodecErrorKind::missingField);

    auto invalidSize = canonical;
    putU16(invalidSize, 68U, 2U);
    invalidSize.push_back(0U);
    repairEnvelope(invalidSize);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(invalidSize); },
                      airfix::settings::RenderSettingsCodecErrorKind::invalidFieldSize);

    auto trailing = canonical;
    trailing.push_back(0U);
    repairEnvelope(trailing);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(trailing); },
                      airfix::settings::RenderSettingsCodecErrorKind::trailingData);
}

void testSemanticAndFutureSchemas() {
    const auto canonical = airfix::settings::encodeRenderSettingsDocument(testRecord());

    auto invalidScale = canonical;
    putU32(invalidScale, 52U, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()));
    repairEnvelope(invalidScale);
    requireCodecError([&] { (void)airfix::settings::decodeRenderSettingsDocument(invalidScale); },
                      airfix::settings::RenderSettingsCodecErrorKind::invalidSemanticRecord);

    for (const auto offset : {60U, 65U, 70U}) {
        auto invalidValue = canonical;
        invalidValue[offset] = 2U;
        repairEnvelope(invalidValue);
        requireCodecError(
            [&] { (void)airfix::settings::decodeRenderSettingsDocument(invalidValue); },
            airfix::settings::RenderSettingsCodecErrorKind::invalidSemanticRecord);
    }

    auto future = canonical;
    putU32(future, 8U, 2U);
    future.push_back(0xA5U);
    repairEnvelope(future);
    const auto decoded = airfix::settings::decodeRenderSettingsDocument(future);
    require(std::holds_alternative<airfix::settings::OpaqueFutureRenderSettingsRecord>(decoded),
            "future AFRS schema was interpreted by a downgrade");
    const auto& opaque = std::get<airfix::settings::OpaqueFutureRenderSettingsRecord>(decoded);
    require(opaque.schemaVersion == 2U && opaque.exactBytes == future,
            "future AFRS bytes were not preserved exactly");
}

} // namespace

int main() {
    try {
        testCanonicalRoundTrip();
        testEnvelopeFailures();
        testStrictFields();
        testSemanticAndFutureSchemas();
        std::cout << "Render presentation settings codec tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Render presentation settings codec tests failed: " << error.what() << '\n';
        return 1;
    }
}
