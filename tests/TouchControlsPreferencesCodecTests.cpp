#include "airfix/crypto/Sha256.hpp"
#include "airfix/settings/TouchControlsPreferencesCodec.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void putU16(std::vector<std::uint8_t> &bytes, const std::size_t offset,
            const std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void putU32(std::vector<std::uint8_t> &bytes, const std::size_t offset,
            const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void repairEnvelope(std::vector<std::uint8_t> &bytes) {
  putU32(bytes, 12U, static_cast<std::uint32_t>(bytes.size()));
  airfix::crypto::Sha256 hash;
  hash.update(std::span<const std::uint8_t>(bytes).first(16U));
  hash.update(std::span<const std::uint8_t>(bytes).subspan(48U));
  const auto digest = hash.finish();
  std::copy(digest.begin(), digest.end(), bytes.begin() + 16);
}

void requireCodecError(
    const std::function<void()> &action,
    const airfix::settings::TouchControlsPreferencesCodecErrorKind expected) {
  try {
    action();
  } catch (const airfix::settings::TouchControlsPreferencesCodecError &error) {
    require(error.kind() == expected, "AFTC error kind mismatch");
    return;
  }
  throw std::runtime_error("expected AFTC codec error");
}

[[nodiscard]] airfix::input::TouchControlsPreferencesRecord testRecord() {
  const auto built = airfix::input::makeTouchControlsPreferencesRecord({
      .layout =
          {
              .handedness = airfix::input::TouchControlsHandedness::leftHanded,
              .density = airfix::input::TouchControlsDensity::compact,
          },
      .restingOpacityPercent = 70U,
  });
  require(built.complete(), "test AFTC record is invalid");
  return *built.record;
}

void testCanonicalRoundTripAndTruncation() {
  const auto record = testRecord();
  const auto bytes =
      airfix::settings::encodeTouchControlsPreferencesDocument(record);
  require(bytes.size() == 63U &&
              std::string(bytes.begin(), bytes.begin() + 4) == "AFTC",
          "AFTC canonical envelope changed");
  const auto decoded =
      airfix::settings::decodeTouchControlsPreferencesDocument(bytes);
  require(std::holds_alternative<airfix::input::TouchControlsPreferencesRecord>(
              decoded) &&
              std::get<airfix::input::TouchControlsPreferencesRecord>(
                  decoded) == record,
          "AFTC semantic round-trip changed fields");
  require(airfix::settings::encodeTouchControlsPreferencesDocument(
              std::get<airfix::input::TouchControlsPreferencesRecord>(
                  decoded)) == bytes,
          "AFTC byte round-trip is not deterministic");

  for (std::size_t size = 0U; size < bytes.size(); ++size) {
    const std::vector<std::uint8_t> truncated(
        bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
    try {
      (void)airfix::settings::decodeTouchControlsPreferencesDocument(truncated);
    } catch (const airfix::settings::TouchControlsPreferencesCodecError &) {
      continue;
    }
    throw std::runtime_error("AFTC accepted truncation at byte " +
                             std::to_string(size));
  }
}

void testEnvelopeAndFieldFailures() {
  const auto canonical =
      airfix::settings::encodeTouchControlsPreferencesDocument(testRecord());

  auto mutated = canonical;
  mutated[0] = 'X';
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(mutated);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::badMagic);
  mutated = canonical;
  putU16(mutated, 4U, 2U);
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(mutated);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::
          unsupportedEnvelopeVersion);
  mutated = canonical;
  putU16(mutated, 6U, 1U);
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(mutated);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::
          unsupportedFlags);
  mutated = canonical;
  mutated.back() ^= 1U;
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(mutated);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::
          integrityMismatch);

  auto duplicate = canonical;
  putU16(duplicate, 53U, 1U);
  repairEnvelope(duplicate);
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(
            duplicate);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::duplicateField);
  auto unknown = canonical;
  putU16(unknown, 53U, 4U);
  repairEnvelope(unknown);
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(unknown);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::unknownField);
  auto missing = canonical;
  missing.resize(58U);
  repairEnvelope(missing);
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(missing);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::missingField);
  auto invalidSize = canonical;
  putU16(invalidSize, 60U, 2U);
  invalidSize.push_back(0U);
  repairEnvelope(invalidSize);
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(
            invalidSize);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::
          invalidFieldSize);
}

void testSemanticAndFutureSchemas() {
  const auto canonical =
      airfix::settings::encodeTouchControlsPreferencesDocument(testRecord());
  for (const auto offset : {52U, 57U}) {
    auto invalid = canonical;
    invalid[offset] = 0xFFU;
    repairEnvelope(invalid);
    requireCodecError(
        [&] {
          (void)airfix::settings::decodeTouchControlsPreferencesDocument(
              invalid);
        },
        airfix::settings::TouchControlsPreferencesCodecErrorKind::
            invalidSemanticRecord);
  }
  auto invalidOpacity = canonical;
  invalidOpacity[62U] = 49U;
  repairEnvelope(invalidOpacity);
  requireCodecError(
      [&] {
        (void)airfix::settings::decodeTouchControlsPreferencesDocument(
            invalidOpacity);
      },
      airfix::settings::TouchControlsPreferencesCodecErrorKind::
          invalidSemanticRecord);

  auto future = canonical;
  putU32(future, 8U, 2U);
  future.push_back(0xA5U);
  repairEnvelope(future);
  const auto decoded =
      airfix::settings::decodeTouchControlsPreferencesDocument(future);
  require(std::holds_alternative<
              airfix::settings::OpaqueFutureTouchControlsPreferencesRecord>(
              decoded),
          "future AFTC schema was interpreted");
  const auto &opaque =
      std::get<airfix::settings::OpaqueFutureTouchControlsPreferencesRecord>(
          decoded);
  require(opaque.schemaVersion == 2U && opaque.exactBytes == future,
          "future AFTC bytes were not preserved exactly");
}

} // namespace

int main() {
  try {
    testCanonicalRoundTripAndTruncation();
    testEnvelopeAndFieldFailures();
    testSemanticAndFutureSchemas();
    std::cout << "Touch controls preferences codec tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Touch controls preferences codec tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
