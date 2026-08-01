#include "airfix/content/LegacyAircraftHudRollingDigitsSubmission.hpp"
#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/render/NativeRenderLayout.hpp"
#include "support/SyntheticContent.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using airfix::content::ContentRevision;
using airfix::content::LegacyAircraftHudRollingDigitsBlendMode;
using airfix::content::LegacyAircraftHudRollingDigitsDepthMode;
using airfix::content::LegacyAircraftHudRollingDigitsSamplingMode;
using airfix::content::LegacyAircraftHudRollingDigitsSubmissionStatus;
using airfix::content::LegacyAircraftHudRollingDigitsTextureLoadIssueKind;
using airfix::content::LegacyAircraftHudRollingDigitsTextureLoadLimits;
using airfix::content::LegacyAircraftHudRollingDigitsTextureLoadPhase;
using airfix::content::LegacyAircraftHudRollingDigitsTextureLoadResult;
using airfix::content::LegacyAircraftHudRollingDigitsTextureRole;
using airfix::content::VerifiedContentSession;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::string_view digitsPath = "Graphics\\Ingame\\HUD\\digits.gti";

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void appendU32(Bytes &bytes, const std::uint32_t value) {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
  }
}

[[nodiscard]] Bytes makeRgba8Gti(const std::uint32_t width,
                                 const std::uint32_t height,
                                 const std::uint32_t checksum = 0x10203040U) {
  const std::size_t pixelCount =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  Bytes pixels;
  pixels.reserve(pixelCount * 4U);
  for (std::size_t index = 0U; index < pixelCount; ++index) {
    const auto red = static_cast<std::uint8_t>(index % 251U);
    pixels.insert(pixels.end(),
                  {static_cast<std::uint8_t>(red + 2U),
                   static_cast<std::uint8_t>(red + 1U), red,
                   static_cast<std::uint8_t>(0xFFU - index % 7U)});
  }
  Bytes bytes;
  appendU32(bytes, airfix::assets::kGtiMagic);
  appendU32(bytes, airfix::assets::kGtiVersion);
  appendU32(bytes, airfix::assets::kGtiChecksumChunk);
  appendU32(bytes, 4U);
  appendU32(bytes, checksum);
  appendU32(bytes, airfix::assets::kGtiImageChunk);
  appendU32(bytes, static_cast<std::uint32_t>(20U + pixels.size()));
  appendU32(bytes, 8U);
  appendU32(bytes, width);
  appendU32(bytes, height);
  appendU32(bytes, 0U);
  appendU32(bytes, 1U);
  bytes.insert(bytes.end(), pixels.begin(), pixels.end());
  return bytes;
}

[[nodiscard]] Bytes makeFormat4Gti() {
  constexpr std::uint32_t width = 16U;
  constexpr std::uint32_t height = 128U;
  constexpr std::size_t pixelCount = static_cast<std::size_t>(width) * height;
  Bytes bytes;
  appendU32(bytes, airfix::assets::kGtiMagic);
  appendU32(bytes, airfix::assets::kGtiVersion);
  appendU32(bytes, airfix::assets::kGtiImageChunk);
  appendU32(bytes, static_cast<std::uint32_t>(20U + 4U + pixelCount * 2U));
  appendU32(bytes, 4U);
  appendU32(bytes, width);
  appendU32(bytes, height);
  appendU32(bytes, 1U);
  appendU32(bytes, 1U);
  bytes.insert(bytes.end(), {0x30U, 0x20U, 0x10U, 0xFFU});
  for (std::size_t index = 0U; index < pixelCount; ++index) {
    bytes.push_back(0U);
    bytes.push_back(static_cast<std::uint8_t>(index % 0xFFU));
  }
  return bytes;
}

[[nodiscard]] std::vector<UdspInputEntry>
sourceEntries(Bytes digits = makeRgba8Gti(16U, 128U)) {
  return {{.logicalPath = std::string(digitsPath), .bytes = std::move(digits)}};
}

[[nodiscard]] std::vector<UdspInputEntry> localizationEntries() {
  return {{.logicalPath = "Synthetic\\Locale.bin", .bytes = {0x41U}}};
}

class PackFixture final {
public:
  explicit PackFixture(std::vector<UdspInputEntry> source = sourceEntries())
      : pack_(airfix::testing::makeSyntheticAfPack(
            std::span<const UdspInputEntry>(source),
            std::span<const UdspInputEntry>(localization_))),
        path_(workspace_.path() / "private-content.afpack") {
    airfix::testing::detail::writeFile(path_, pack_.bytes);
  }

  [[nodiscard]] VerifiedContentSession
  open(const std::uint64_t generation = 301U) const {
    auto input = std::make_unique<std::ifstream>(path_, std::ios::binary |
                                                            std::ios::ate);
    require(static_cast<bool>(*input), "synthetic pack did not open");
    return VerifiedContentSession::open(
        std::move(input), "synthetic owner-local content",
        ContentRevision{
            .generation = generation,
            .pack = {.size = pack_.size, .sha256 = pack_.sha256},
        });
  }

private:
  std::vector<UdspInputEntry> localization_{localizationEntries()};
  airfix::testing::detail::TempWorkspace workspace_;
  SyntheticAfPack pack_;
  std::filesystem::path path_;
};

void requireAtomicFailure(
    const LegacyAircraftHudRollingDigitsTextureLoadResult &result,
    const LegacyAircraftHudRollingDigitsTextureLoadIssueKind expected,
    const std::string_view message) {
  require(!result.textures.has_value() && !result.issues.empty() &&
              result.issues.front().kind == expected,
          message);
}

void testLoadsExactAuthenticatedAtlas() {
  PackFixture fixture;
  auto session = fixture.open();
  const auto result =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session);
  require(result.success() && result.textures->valid() &&
              result.textures->belongsTo(session),
          "representative digit atlas did not authenticate");
  const auto *texture = result.textures->texture(
      LegacyAircraftHudRollingDigitsTextureRole::digits);
  require(texture != nullptr && texture->textureId.value == 0U &&
              texture->logicalPath == digitsPath &&
              texture->upload.format == 8U &&
              texture->upload.uploadLevels.front().width == 16U &&
              texture->upload.uploadLevels.front().height == 128U &&
              texture->upload.checksum == 0x10203040U &&
              texture->uploadLevels.size() == 1U,
          "digit atlas lost its exact source/upload contract");
  require(result.textures->texture(
              static_cast<LegacyAircraftHudRollingDigitsTextureRole>(0xFFU)) ==
              nullptr,
          "unknown digit role was accepted");
  auto forged = *result.textures;
  forged.textures.front().textureId.value = 7U;
  require(!forged.valid(), "forged digit texture identity stayed valid");
}

void testLoadFailuresAreAtomic() {
  {
    PackFixture fixture(
        {{.logicalPath = "Synthetic\\Only.bin", .bytes = {0x01U}}});
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session),
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::textureNotFound,
        "missing digit atlas published a set");
  }
  {
    PackFixture fixture(sourceEntries(makeFormat4Gti()));
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session),
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::unexpectedFormat,
        "non-selected GTI format published a digit atlas");
  }
  {
    PackFixture fixture(sourceEntries(makeRgba8Gti(15U, 128U)));
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session),
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
            unexpectedDimensions,
        "wrong digit dimensions published a set");
  }
  {
    PackFixture fixture;
    auto session = fixture.open();
    LegacyAircraftHudRollingDigitsTextureLoadLimits limits;
    limits.maximumSourceBytes = 8U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session,
                                                                   limits),
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::sourceLimitExceeded,
        "source budget violation published a set");
  }
  {
    PackFixture fixture;
    auto session = fixture.open();
    std::stop_source stop;
    stop.request_stop();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudRollingDigitsTexture(
            session, {}, stop.get_token()),
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::cancelled,
        "cancelled digit load published a set");
  }
  {
    PackFixture fixture;
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudRollingDigitsTexture(
            session, {}, {},
            [](const auto &progress) {
              if (progress.phase ==
                  LegacyAircraftHudRollingDigitsTextureLoadPhase::
                      lookingUpTexture) {
                throw std::runtime_error("synthetic callback failure");
              }
            }),
        LegacyAircraftHudRollingDigitsTextureLoadIssueKind::
            progressCallbackFailure,
        "throwing digit callback published a set");
  }
}

[[nodiscard]] airfix::render::NativeRenderLayout
nativeLayout(const airfix::render::UiLogicalExtent extent = {640.0F, 480.0F}) {
  const auto built = airfix::render::buildNativeRenderLayout({
      .outputExtent = {1920U, 1080U},
      .uiDesignExtent = extent,
  });
  require(built.complete(), "native digit layout did not build");
  return *built.layout;
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 1.0e-4F) noexcept {
  return std::fabs(actual - expected) <= tolerance;
}

void testSubmissionBindsIdentityAndNativeOutput() {
  PackFixture fixture;
  auto session = fixture.open();
  const auto loaded =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session);
  require(loaded.success(), "digit submission fixture did not load");
  const airfix::render::LegacyAircraftHudRollingDigitsState state{
      .digitPositions = {0.0F, 1.0F, 2.5F, 9.0F}};
  const auto plan = airfix::render::buildLegacyAircraftHudRollingDigitsPlan(
      state, 100.0F, 200.0F, 0x7FFFFFFFU, true);
  const auto result =
      airfix::content::buildLegacyAircraftHudRollingDigitsSubmission(
          plan, *loaded.textures, nativeLayout(), 100.0F);
  require(result.ready() && result.submission->belongsTo(*loaded.textures) &&
              result.submission->commandCount == 4U &&
              result.submission->command(4U) == nullptr,
          "authenticated digit packet was not published atomically");
  const auto *zero = result.submission->command(0U);
  const auto *fractional = result.submission->command(2U);
  require(zero != nullptr && fractional != nullptr && zero->slotIndex == 0U &&
              zero->textureId.value == 0U &&
              zero->textureRole ==
                  LegacyAircraftHudRollingDigitsTextureRole::digits &&
              close(zero->outputRect.x, 465.0F) &&
              close(zero->outputRect.y, 452.25F) &&
              close(zero->outputRect.width, 15.75F) &&
              close(zero->outputRect.height, 20.25F) &&
              close(zero->uv.minimumV, 1.0F / 128.0F) &&
              close(zero->uv.maximumU, 7.0F / 16.0F) &&
              close(fractional->uv.minimumV, 28.5F / 128.0F) &&
              zero->tintArgb == 0x7FFFFFFFU &&
              zero->blendMode == LegacyAircraftHudRollingDigitsBlendMode::
                                     sourceAlphaOneMinusSourceAlpha &&
              zero->depthMode ==
                  LegacyAircraftHudRollingDigitsDepthMode::alwaysWrite &&
              zero->samplingMode ==
                  LegacyAircraftHudRollingDigitsSamplingMode::linearClamp,
          "digit packet lost output, UV or recovered GPU state");

  const auto enlarged =
      airfix::content::buildLegacyAircraftHudRollingDigitsSubmission(
          plan, *loaded.textures, nativeLayout(), 150.0F);
  require(
      enlarged.ready() &&
          close(enlarged.submission->command(0U)->outputRect.x, 465.0F) &&
          close(enlarged.submission->command(0U)->outputRect.y, 453.375F) &&
          close(enlarged.submission->command(0U)->outputRect.width, 23.625F) &&
          close(enlarged.submission->command(1U)->outputRect.x, 492.0F),
      "independent digit scale did not retain the widget origin");

  auto otherSession = fixture.open();
  const auto other =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(otherSession);
  require(other.success() && !result.submission->belongsTo(*other.textures),
          "equal revision on another transaction reused a digit packet");
  auto forged = *result.submission;
  forged.orderedCommands[0U].textureId.value = 3U;
  require(!forged.belongsTo(*loaded.textures),
          "forged digit texture ID reached a backend");
  forged = *result.submission;
  std::swap(forged.orderedCommands[0U], forged.orderedCommands[1U]);
  require(!forged.belongsTo(*loaded.textures),
          "reordered digit slots reached a backend");
}

void testSubmissionFailuresAreAtomic() {
  PackFixture fixture;
  auto session = fixture.open();
  const auto loaded =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session);
  require(loaded.success(), "digit failure fixture did not load");
  const airfix::render::LegacyAircraftHudRollingDigitsState state{
      .digitPositions = {0.0F, 1.0F, 2.0F, 3.0F}};
  auto plan = airfix::render::buildLegacyAircraftHudRollingDigitsPlan(
      state, 100.0F, 200.0F, 0xFFFFFFFFU, true);
  const auto requireFailure = [&](const auto &candidatePlan,
                                  const auto &textures, const auto &layout,
                                  const float scale, const auto expected,
                                  const std::string_view message) {
    const auto result =
        airfix::content::buildLegacyAircraftHudRollingDigitsSubmission(
            candidatePlan, textures, layout, scale);
    require(result.status == expected && !result.submission.has_value(),
            message);
  };

  auto unavailable = plan;
  unavailable.status = airfix::render::
      LegacyAircraftHudRollingDigitsPlanStatus::atlasUnavailable;
  requireFailure(unavailable, *loaded.textures, nativeLayout(), 100.0F,
                 LegacyAircraftHudRollingDigitsSubmissionStatus::planNotReady,
                 "non-ready digit plan published a packet");
  auto forgedTextures = *loaded.textures;
  forgedTextures.textures.front().textureId.value = 9U;
  requireFailure(
      plan, forgedTextures, nativeLayout(), 100.0F,
      LegacyAircraftHudRollingDigitsSubmissionStatus::invalidTextureSet,
      "invalid digit texture owner published a packet");
  requireFailure(plan, *loaded.textures, nativeLayout({800.0F, 600.0F}), 100.0F,
                 LegacyAircraftHudRollingDigitsSubmissionStatus::
                     incompatibleUiDesignExtent,
                 "foreign UI design domain was reinterpreted");
  requireFailure(
      plan, *loaded.textures, nativeLayout(), 151.0F,
      LegacyAircraftHudRollingDigitsSubmissionStatus::uiScaleOutOfRange,
      "out-of-policy digit scale was accepted");
  plan.orderedCommands[0U].sourceRect.bottom = 129.0F;
  requireFailure(
      plan, *loaded.textures, nativeLayout(), 100.0F,
      LegacyAircraftHudRollingDigitsSubmissionStatus::invalidPlanCommand,
      "out-of-atlas digit UV published a packet");
}

} // namespace

int main() {
  try {
    testLoadsExactAuthenticatedAtlas();
    testLoadFailuresAreAtomic();
    testSubmissionBindsIdentityAndNativeOutput();
    testSubmissionFailuresAreAtomic();
    std::cout << "LegacyAircraftHudRollingDigits texture tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "LegacyAircraftHudRollingDigits texture tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
