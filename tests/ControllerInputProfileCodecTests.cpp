#include "airfix/crypto/Sha256.hpp"
#include "airfix/input/ControllerInputProfile.hpp"
#include "airfix/settings/ControllerInputProfileCodec.hpp"

#include <algorithm>
#include <cstddef>
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
    const airfix::settings::ControllerInputProfileCodecErrorKind expected) {
  try {
    action();
  } catch (const airfix::settings::ControllerInputProfileCodecError &error) {
    require(error.kind() == expected,
            "AFIP error kind mismatch: expected " +
                std::to_string(static_cast<unsigned>(expected)) +
                ", received " +
                std::to_string(static_cast<unsigned>(error.kind())));
    return;
  }
  throw std::runtime_error("expected AFIP codec error");
}

[[nodiscard]] airfix::input::ControllerInputProfileRecord testRecord() {
  auto record = airfix::input::makeDefaultControllerInputProfileRecord();
  record.axes[0].innerDeadzoneQ15 = 2048U;
  record.axes[0].outerSaturationQ15 = 30000U;
  record.axes[0].sensitivityPermille = 1250U;
  record.axes[0].responseCurve =
      airfix::input::ControllerResponseCurve::squared;
  record.axes[0].inverted = 1U;
  require(airfix::input::resolveControllerInputProfile(record).complete(),
          "test AFIP record is invalid");
  return record;
}

void testCanonicalRoundTrip() {
  const auto record = testRecord();
  const auto bytes =
      airfix::settings::encodeControllerInputProfileDocument(record);
  require(bytes.size() ==
              89U + static_cast<std::size_t>(record.bindingCount) * 12U,
          "AFIP canonical packed size changed");
  require(bytes.size() <=
              airfix::settings::maximumControllerInputProfileDocumentBytes,
          "AFIP canonical document exceeds its public bound");
  require(std::string(bytes.begin(), bytes.begin() + 4) == "AFIP",
          "AFIP canonical magic changed");

  const auto decoded =
      airfix::settings::decodeControllerInputProfileDocument(bytes);
  require(std::holds_alternative<airfix::input::ControllerInputProfileRecord>(
              decoded),
          "current AFIP decoded as opaque");
  const auto &decodedRecord =
      std::get<airfix::input::ControllerInputProfileRecord>(decoded);
  require(decodedRecord.schemaVersion == record.schemaVersion &&
              decodedRecord.axes == record.axes &&
              decodedRecord.bindingCount == record.bindingCount,
          "AFIP semantic round-trip changed fixed fields");
  for (std::size_t index = 0U; index < record.bindingCount; ++index) {
    require(decodedRecord.bindings[index] == record.bindings[index],
            "AFIP semantic round-trip changed a binding");
  }
  require(airfix::settings::encodeControllerInputProfileDocument(
              decodedRecord) == bytes,
          "AFIP byte round-trip is not deterministic");

  for (std::size_t size = 0U; size < bytes.size(); ++size) {
    const std::vector<std::uint8_t> truncated(
        bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
    try {
      static_cast<void>(
          airfix::settings::decodeControllerInputProfileDocument(truncated));
    } catch (const airfix::settings::ControllerInputProfileCodecError &) {
      continue;
    }
    throw std::runtime_error("AFIP accepted truncation at byte " +
                             std::to_string(size));
  }
}

void testEnvelopeFailures() {
  const auto canonical =
      airfix::settings::encodeControllerInputProfileDocument(testRecord());

  auto mutated = canonical;
  mutated[0] = 'X';
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(mutated));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::badMagic);

  mutated = canonical;
  putU16(mutated, 4U, 2U);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(mutated));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::
          unsupportedEnvelopeVersion);

  mutated = canonical;
  putU16(mutated, 6U, 1U);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(mutated));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::unsupportedFlags);

  mutated = canonical;
  putU32(mutated, 12U, static_cast<std::uint32_t>(mutated.size() + 1U));
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(mutated));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::
          declaredSizeMismatch);

  mutated = canonical;
  mutated.back() ^= 1U;
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(mutated));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::
          integrityMismatch);

  std::vector<std::uint8_t> oversized(
      airfix::settings::maximumControllerInputProfileDocumentBytes + 1U);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(oversized));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::tooLarge);
}

void testStrictFields() {
  const auto canonical =
      airfix::settings::encodeControllerInputProfileDocument(testRecord());
  constexpr std::size_t firstFieldHeader = 48U;
  constexpr std::size_t firstFieldPayload = 52U;
  constexpr std::size_t secondFieldHeader = 84U;
  constexpr std::size_t secondFieldPayload = 88U;

  auto duplicate = canonical;
  putU16(duplicate, secondFieldHeader, 1U);
  repairEnvelope(duplicate);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(duplicate));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::duplicateField);

  auto unknown = canonical;
  putU16(unknown, secondFieldHeader, 3U);
  repairEnvelope(unknown);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(unknown));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::unknownField);

  std::vector<std::uint8_t> outOfOrder(
      canonical.begin(),
      canonical.begin() + static_cast<std::ptrdiff_t>(firstFieldHeader));
  outOfOrder.insert(outOfOrder.end(),
                    canonical.begin() +
                        static_cast<std::ptrdiff_t>(secondFieldHeader),
                    canonical.end());
  outOfOrder.insert(
      outOfOrder.end(),
      canonical.begin() + static_cast<std::ptrdiff_t>(firstFieldHeader),
      canonical.begin() + static_cast<std::ptrdiff_t>(secondFieldHeader));
  repairEnvelope(outOfOrder);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(outOfOrder));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::
          nonCanonicalFieldOrder);

  auto missing = canonical;
  missing.resize(secondFieldHeader);
  repairEnvelope(missing);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(missing));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::missingField);

  auto invalidAxisSize = canonical;
  putU16(invalidAxisSize, firstFieldHeader + 2U, 31U);
  repairEnvelope(invalidAxisSize);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(
                invalidAxisSize));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::invalidFieldSize);

  auto inconsistentCount = canonical;
  ++inconsistentCount[secondFieldPayload];
  repairEnvelope(inconsistentCount);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(
                inconsistentCount));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::invalidFieldSize);

  auto trailing = canonical;
  trailing.push_back(0U);
  repairEnvelope(trailing);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(trailing));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::trailingData);

  auto invalidSemantic = canonical;
  putU16(invalidSemantic, firstFieldPayload + 2U, 0U);
  repairEnvelope(invalidSemantic);
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::decodeControllerInputProfileDocument(
                invalidSemantic));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::
          invalidSemanticRecord);
}

void testEncoderAndFutureSchema() {
  auto invalid = testRecord();
  invalid.schemaVersion = 0U;
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::encodeControllerInputProfileDocument(invalid));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::
          invalidSchemaVersion);

  invalid = testRecord();
  invalid.axes[0].outerSaturationQ15 = invalid.axes[0].innerDeadzoneQ15;
  requireCodecError(
      [&] {
        static_cast<void>(
            airfix::settings::encodeControllerInputProfileDocument(invalid));
      },
      airfix::settings::ControllerInputProfileCodecErrorKind::
          invalidSemanticRecord);

  auto future =
      airfix::settings::encodeControllerInputProfileDocument(testRecord());
  putU32(future, 8U, 2U);
  future.push_back(0xA5U);
  repairEnvelope(future);
  const auto decoded =
      airfix::settings::decodeControllerInputProfileDocument(future);
  require(
      std::holds_alternative<
          airfix::settings::OpaqueFutureControllerInputProfileRecord>(decoded),
      "future AFIP schema was interpreted by a downgrade");
  const auto &opaque =
      std::get<airfix::settings::OpaqueFutureControllerInputProfileRecord>(
          decoded);
  require(opaque.schemaVersion == 2U && opaque.exactBytes == future,
          "future AFIP bytes were not preserved exactly");
}

} // namespace

int main() {
  try {
    testCanonicalRoundTrip();
    testEnvelopeFailures();
    testStrictFields();
    testEncoderAndFutureSchema();
    std::cout << "Controller input profile codec tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Controller input profile codec tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
