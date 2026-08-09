#include "airfix/content/MissionLaunchSelectionCodec.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
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
    const airfix::content::MissionSelectionCodecErrorKind expected) {
  try {
    action();
  } catch (const airfix::content::MissionSelectionCodecError &error) {
    require(error.kind() == expected, "AFMS error kind mismatch");
    return;
  }
  throw std::runtime_error("expected AFMS codec error");
}

[[nodiscard]] airfix::content::MissionLaunchSelection selection() {
  return {
      .setupLogicalPath = "Setup\\Synthetic.setup",
      .levelLogicalPath = "Levels\\Synthetic.level",
      .playerObjectLogicalPath = "Objects\\Synthetic.object",
      .requestedStartIndex = 3U,
  };
}

void testCanonicalRoundTrip() {
  const auto expected = selection();
  const auto bytes = airfix::content::encodeMissionLaunchSelection(expected);
  require(bytes.size() < airfix::content::maximumMissionSelectionDocumentBytes,
          "AFMS canonical document exceeds its bound");
  require(std::string(bytes.begin(), bytes.begin() + 4) == "AFMS",
          "AFMS canonical magic changed");
  require(airfix::content::decodeMissionLaunchSelection(bytes) == expected,
          "AFMS semantic round-trip changed fields");
  require(airfix::content::encodeMissionLaunchSelection(
              airfix::content::decodeMissionLaunchSelection(bytes)) == bytes,
          "AFMS byte round-trip is not deterministic");

  auto withoutPlayer = expected;
  withoutPlayer.playerObjectLogicalPath.reset();
  const auto noPlayerBytes =
      airfix::content::encodeMissionLaunchSelection(withoutPlayer);
  require(airfix::content::decodeMissionLaunchSelection(noPlayerBytes) ==
              withoutPlayer,
          "AFMS optional player path did not round-trip");

  auto normalized = expected;
  normalized.setupLogicalPath = "Setup/Synthetic.setup";
  require(airfix::content::decodeMissionLaunchSelection(
              airfix::content::encodeMissionLaunchSelection(normalized)) ==
              expected,
          "AFMS encoder did not canonicalize separators");

  for (std::size_t size = 0U; size < bytes.size(); ++size) {
    const std::vector<std::uint8_t> truncated(
        bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
    try {
      (void)airfix::content::decodeMissionLaunchSelection(truncated);
    } catch (const airfix::content::MissionSelectionCodecError &) {
      continue;
    }
    throw std::runtime_error("AFMS accepted truncation");
  }
}

void testEnvelopeFailures() {
  const auto canonical =
      airfix::content::encodeMissionLaunchSelection(selection());

  auto mutated = canonical;
  mutated[0] = 'X';
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(mutated); },
      airfix::content::MissionSelectionCodecErrorKind::badMagic);

  mutated = canonical;
  putU16(mutated, 4U, 2U);
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(mutated); },
      airfix::content::MissionSelectionCodecErrorKind::
          unsupportedEnvelopeVersion);

  mutated = canonical;
  putU16(mutated, 6U, 1U);
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(mutated); },
      airfix::content::MissionSelectionCodecErrorKind::unsupportedFlags);

  mutated = canonical;
  putU32(mutated, 8U, 2U);
  repairEnvelope(mutated);
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(mutated); },
      airfix::content::MissionSelectionCodecErrorKind::
          unsupportedSchemaVersion);

  mutated = canonical;
  putU32(mutated, 12U, 1U);
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(mutated); },
      airfix::content::MissionSelectionCodecErrorKind::declaredSizeMismatch);

  mutated = canonical;
  mutated.back() ^= 1U;
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(mutated); },
      airfix::content::MissionSelectionCodecErrorKind::integrityMismatch);

  std::vector<std::uint8_t> oversized(
      airfix::content::maximumMissionSelectionDocumentBytes + 1U);
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(oversized); },
      airfix::content::MissionSelectionCodecErrorKind::tooLarge);
}

void testStrictFieldsAndPaths() {
  const auto canonical =
      airfix::content::encodeMissionLaunchSelection(selection());
  auto duplicate = canonical;
  // The second field begins after header + setup TLV.
  const std::size_t secondField =
      48U + 4U + selection().setupLogicalPath.size();
  putU16(duplicate, secondField, 1U);
  repairEnvelope(duplicate);
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(duplicate); },
      airfix::content::MissionSelectionCodecErrorKind::duplicateField);

  auto unknown = canonical;
  putU16(unknown, secondField, 5U);
  repairEnvelope(unknown);
  requireCodecError(
      [&] { (void)airfix::content::decodeMissionLaunchSelection(unknown); },
      airfix::content::MissionSelectionCodecErrorKind::unknownField);

  auto forwardSlash = canonical;
  const auto slash =
      std::find(forwardSlash.begin() + 52, forwardSlash.end(), '\\');
  require(slash != forwardSlash.end(), "AFMS test path has no separator");
  *slash = '/';
  repairEnvelope(forwardSlash);
  requireCodecError(
      [&] {
        (void)airfix::content::decodeMissionLaunchSelection(forwardSlash);
      },
      airfix::content::MissionSelectionCodecErrorKind::nonCanonicalLogicalPath);

  auto unsafe = selection();
  unsafe.levelLogicalPath = "Levels\\..\\Synthetic.level";
  requireCodecError(
      [&] { (void)airfix::content::encodeMissionLaunchSelection(unsafe); },
      airfix::content::MissionSelectionCodecErrorKind::invalidLogicalPath);

  auto nonAscii = selection();
  nonAscii.setupLogicalPath.push_back(static_cast<char>(0xC3));
  requireCodecError(
      [&] { (void)airfix::content::encodeMissionLaunchSelection(nonAscii); },
      airfix::content::MissionSelectionCodecErrorKind::invalidLogicalPath);
}

} // namespace

int main() {
  try {
    testCanonicalRoundTrip();
    testEnvelopeFailures();
    testStrictFieldsAndPaths();
    std::cout << "Mission launch selection codec tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Mission launch selection codec tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
