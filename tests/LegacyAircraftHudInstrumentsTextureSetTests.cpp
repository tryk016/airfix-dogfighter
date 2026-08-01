#include "airfix/content/LegacyAircraftHudInstrumentsSubmission.hpp"
#include "airfix/content/LegacyAircraftHudInstrumentsTextureSet.hpp"
#include "airfix/render/LegacyAircraftHudInstruments.hpp"
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
using airfix::content::LegacyAircraftHudInstrumentsSubmissionStatus;
using airfix::content::LegacyAircraftHudInstrumentTextureArchive;
using airfix::content::LegacyAircraftHudInstrumentTextureLoadIssueKind;
using airfix::content::LegacyAircraftHudInstrumentTextureLoadLimits;
using airfix::content::LegacyAircraftHudInstrumentTextureLoadPhase;
using airfix::content::LegacyAircraftHudInstrumentTextureLoadResult;
using airfix::content::LegacyAircraftHudInstrumentTextureRole;
using airfix::content::VerifiedContentSession;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::string_view clock1Path = "Graphics\\Ingame\\HUD\\clock1.gti";
constexpr std::string_view clock2Path = "Graphics\\Ingame\\HUD\\clock2.gti";
constexpr std::string_view indicatorPath =
    "Graphics\\Ingame\\HUD\\indicator.gti";

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
                                 const std::uint8_t seed,
                                 const std::uint32_t checksum) {
  const std::size_t pixelCount =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  Bytes pixels;
  pixels.reserve(pixelCount * 4U);
  for (std::size_t index = 0U; index < pixelCount; ++index) {
    const auto red = static_cast<std::uint8_t>(seed + index % 17U);
    pixels.insert(pixels.end(),
                  {static_cast<std::uint8_t>(red + 2U),
                   static_cast<std::uint8_t>(red + 1U), red,
                   static_cast<std::uint8_t>(0xFFU - index % 9U)});
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

[[nodiscard]] std::vector<UdspInputEntry> sourceEntries() {
  return {{
      .logicalPath = std::string(indicatorPath),
      .bytes = makeRgba8Gti(16U, 32U, 0x70U, 0xA0B0C0D0U),
  }};
}

[[nodiscard]] std::vector<UdspInputEntry> localizationEntries() {
  return {
      {
          .logicalPath = std::string(clock1Path),
          .bytes = makeRgba8Gti(64U, 64U, 0x10U, 0x10203040U),
      },
      {
          .logicalPath = std::string(clock2Path),
          .bytes = makeRgba8Gti(64U, 64U, 0x40U, 0x50607080U),
      },
  };
}

class PackFixture final {
public:
  PackFixture(std::vector<UdspInputEntry> source,
              std::vector<UdspInputEntry> localization)
      : pack_(airfix::testing::makeSyntheticAfPack(
            std::span<const UdspInputEntry>(source),
            std::span<const UdspInputEntry>(localization))),
        path_(workspace_.path() / "private-content.afpack") {
    airfix::testing::detail::writeFile(path_, pack_.bytes);
  }

  [[nodiscard]] VerifiedContentSession
  open(const std::uint64_t generation = 401U) const {
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
  airfix::testing::detail::TempWorkspace workspace_;
  SyntheticAfPack pack_;
  std::filesystem::path path_;
};

void requireAtomicFailure(
    const LegacyAircraftHudInstrumentTextureLoadResult &result,
    const LegacyAircraftHudInstrumentTextureLoadIssueKind expected,
    const std::string_view message) {
  require(!result.textures.has_value() && !result.issues.empty() &&
              result.issues.front().kind == expected,
          message);
}

void testLoadsExactCrossArchiveSet() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto result =
      airfix::content::loadLegacyAircraftHudInstrumentTextures(session);
  require(result.success() && result.textures->valid() &&
              result.textures->belongsTo(session),
          "representative instrument textures did not load atomically");

  const auto *left = result.textures->texture(
      LegacyAircraftHudInstrumentTextureRole::leftFace);
  const auto *right = result.textures->texture(
      LegacyAircraftHudInstrumentTextureRole::rightFace);
  const auto *indicator = result.textures->texture(
      LegacyAircraftHudInstrumentTextureRole::indicator);
  require(left != nullptr && right != nullptr && indicator != nullptr,
          "instrument roles were not addressable");
  require(left->archive ==
                  LegacyAircraftHudInstrumentTextureArchive::localization &&
              left->logicalPath == clock1Path && left->textureId.value == 0U &&
              left->upload.uploadLevels.front().width == 64U &&
              left->upload.uploadLevels.front().height == 64U &&
              right->archive ==
                  LegacyAircraftHudInstrumentTextureArchive::localization &&
              right->logicalPath == clock2Path &&
              right->textureId.value == 1U &&
              indicator->archive ==
                  LegacyAircraftHudInstrumentTextureArchive::source &&
              indicator->logicalPath == indicatorPath &&
              indicator->textureId.value == 2U &&
              indicator->upload.uploadLevels.front().width == 16U &&
              indicator->upload.uploadLevels.front().height == 32U,
          "instrument archive, role, ID or dimensions changed");
  require(result.textures->texture(
              static_cast<LegacyAircraftHudInstrumentTextureRole>(0xFFU)) ==
              nullptr,
          "invalid instrument texture role resolved");
}

void testRequiresExactArchiveAndDimensions() {
  {
    auto localization = localizationEntries();
    localization.pop_back();
    auto source = sourceEntries();
    source.push_back({
        .logicalPath = std::string(clock2Path),
        .bytes = makeRgba8Gti(64U, 64U, 0x40U, 0x50607080U),
    });
    PackFixture fixture(std::move(source), std::move(localization));
    auto session = fixture.open();
    const auto result =
        airfix::content::loadLegacyAircraftHudInstrumentTextures(session);
    requireAtomicFailure(
        result,
        LegacyAircraftHudInstrumentTextureLoadIssueKind::textureNotFound,
        "clock2 in Resource.up bypassed localization ownership");
    require(result.issues.front().role ==
                    LegacyAircraftHudInstrumentTextureRole::rightFace &&
                result.issues.front().archive ==
                    LegacyAircraftHudInstrumentTextureArchive::localization,
            "missing clock2 reported the wrong provenance");
  }

  {
    auto source = sourceEntries();
    source.front().bytes = makeRgba8Gti(16U, 31U, 0x70U, 0xA0B0C0D0U);
    PackFixture fixture(std::move(source), localizationEntries());
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudInstrumentTextures(session),
        LegacyAircraftHudInstrumentTextureLoadIssueKind::unexpectedDimensions,
        "wrong indicator dimensions published a partial set");
  }
}

void testLimitsCancellationCallbacksAndMoveAreAtomic() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  {
    auto session = fixture.open();
    LegacyAircraftHudInstrumentTextureLoadLimits limits;
    limits.maximumSourceBytes = 7U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudInstrumentTextures(session,
                                                                 limits),
        LegacyAircraftHudInstrumentTextureLoadIssueKind::invalidLimits,
        "invalid loader limits were accepted");
  }
  {
    auto session = fixture.open();
    std::stop_source stop;
    stop.request_stop();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudInstrumentTextures(
            session, {}, stop.get_token()),
        LegacyAircraftHudInstrumentTextureLoadIssueKind::cancelled,
        "cancelled loader published textures");
  }
  {
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudInstrumentTextures(
            session, {}, {},
            [](const auto &progress) {
              if (progress.phase ==
                  LegacyAircraftHudInstrumentTextureLoadPhase::
                      lookingUpTextures) {
                throw std::runtime_error("synthetic callback failure");
              }
            }),
        LegacyAircraftHudInstrumentTextureLoadIssueKind::
            progressCallbackFailure,
        "throwing progress callback published textures");
  }
  {
    auto session = fixture.open();
    auto moved = std::move(session);
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudInstrumentTextures(session),
        LegacyAircraftHudInstrumentTextureLoadIssueKind::sessionIdentityChanged,
        "moved-from session authenticated textures");
    require(airfix::content::loadLegacyAircraftHudInstrumentTextures(moved)
                .success(),
            "moved-to session could not load textures");
  }
}

[[nodiscard]] airfix::render::LegacyAircraftHudInstrumentsPlan plan() noexcept {
  return airfix::render::buildLegacyAircraftHudInstrumentsPlan({
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .rightNormalizedValue = 0.5F,
      .leftNormalizedValue = 0.5F,
      .leftTintArgb = 0xFF804020U,
      .rightFaceTextureAvailable = true,
      .leftFaceTextureAvailable = true,
      .indicatorTextureAvailable = true,
  });
}

[[nodiscard]] airfix::render::NativeRenderLayout
nativeLayout(const airfix::render::UiLogicalExtent design = {640.0F, 480.0F}) {
  const auto built = airfix::render::buildNativeRenderLayout({
      .outputExtent = {1920U, 1080U},
      .uiDesignExtent = design,
  });
  require(built.complete(), "native instrument layout did not build");
  return *built.layout;
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 1.0e-3F) noexcept {
  return std::abs(actual - expected) <= tolerance;
}

void testSubmissionPreservesNativeContract() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto loaded =
      airfix::content::loadLegacyAircraftHudInstrumentTextures(session);
  require(loaded.success(), "submission texture fixture did not load");
  const auto sourcePlan = plan();
  require(sourcePlan.ready(), "submission plan fixture did not build");

  const auto result =
      airfix::content::buildLegacyAircraftHudInstrumentsSubmission(
          sourcePlan, *loaded.textures, nativeLayout(), 100.0F);
  require(result.ready() && result.submission->belongsTo(*loaded.textures) &&
              result.submission->commandCount == 4U &&
              result.submission->command(4U) == nullptr,
          "instrument submission was not published atomically");
  const auto *rightFace = result.submission->command(0U);
  const auto *rightIndicator = result.submission->command(1U);
  const auto *leftFace = result.submission->command(2U);
  const auto *leftIndicator = result.submission->command(3U);
  require(rightFace != nullptr && rightIndicator != nullptr &&
              leftFace != nullptr && leftIndicator != nullptr &&
              rightFace->textureRole ==
                  LegacyAircraftHudInstrumentTextureRole::rightFace &&
              rightFace->textureId.value == 1U &&
              rightIndicator->textureRole ==
                  LegacyAircraftHudInstrumentTextureRole::indicator &&
              rightIndicator->textureId.value == 2U &&
              leftFace->textureRole ==
                  LegacyAircraftHudInstrumentTextureRole::leftFace &&
              leftFace->textureId.value == 0U &&
              leftIndicator->textureRole ==
                  LegacyAircraftHudInstrumentTextureRole::indicator &&
              rightFace->uv ==
                  airfix::content::LegacyAircraftHudInstrumentUvRect{
                      0.0F, 0.0F, 1.0F, 1.0F} &&
              rightIndicator->uv ==
                  airfix::content::LegacyAircraftHudInstrumentUvRect{
                      0.0F, 0.0F, 7.0F / 16.0F, 30.0F / 32.0F} &&
              rightFace->tintArgb == 0xFFFFFFFFU &&
              leftFace->tintArgb == 0xFF804020U,
          "submission lost recovered order, binding, UV or tint");
  require(close(rightFace->outputQuad[0U].x, 1198.5F) &&
              close(rightFace->outputQuad[0U].y, 918.0F) &&
              close(rightFace->outputQuad[2U].x, 1342.5F) &&
              close(rightFace->outputQuad[2U].y, 1062.0F) &&
              close(rightIndicator->outputQuad[0U].x, 1263.75F) &&
              close(rightIndicator->outputQuad[0U].y, 938.25F),
          "instrument geometry did not map through the safe UI viewport");

  const auto enlarged =
      airfix::content::buildLegacyAircraftHudInstrumentsSubmission(
          sourcePlan, *loaded.textures, nativeLayout(), 150.0F);
  require(
      enlarged.ready() &&
          close(enlarged.submission->command(0U)->outputQuad[0U].x, 1317.75F) &&
          close(enlarged.submission->command(0U)->outputQuad[0U].y, 837.0F),
      "paired UI scale did not retain the lower-centre anchor");

  auto otherSession = fixture.open();
  const auto other =
      airfix::content::loadLegacyAircraftHudInstrumentTextures(otherSession);
  require(other.success() && !result.submission->belongsTo(*other.textures),
          "submission crossed authenticated stream identity");
  auto forged = *result.submission;
  forged.orderedCommands[0U].textureId.value = 2U;
  require(!forged.belongsTo(*loaded.textures),
          "forged instrument texture ID reached a backend");
  forged = *result.submission;
  std::swap(forged.orderedCommands[0U], forged.orderedCommands[1U]);
  require(!forged.belongsTo(*loaded.textures),
          "reordered instrument commands reached a backend");
  forged = *result.submission;
  forged.orderedCommands[0U].tintArgb = 0xFF000000U;
  require(!forged.belongsTo(*loaded.textures),
          "forged right instrument tint reached a backend");
  forged = *result.submission;
  forged.orderedCommands[3U].tintArgb = 0xFFFFFFFFU;
  require(!forged.belongsTo(*loaded.textures),
          "mismatched left instrument tints reached a backend");
}

void testSubmissionFailuresAreAtomic() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto loaded =
      airfix::content::loadLegacyAircraftHudInstrumentTextures(session);
  require(loaded.success(), "failure texture fixture did not load");
  const auto layout = nativeLayout();

  auto invalidPlan = plan();
  invalidPlan.status =
      airfix::render::LegacyAircraftHudInstrumentsPlanStatus::typeHudDisabled;
  auto result = airfix::content::buildLegacyAircraftHudInstrumentsSubmission(
      invalidPlan, *loaded.textures, layout, 100.0F);
  require(result.status ==
                  LegacyAircraftHudInstrumentsSubmissionStatus::planNotReady &&
              !result.submission.has_value(),
          "non-ready instrument plan published a packet");

  auto invalidTextures = *loaded.textures;
  invalidTextures.textures[0U].textureId.value = 9U;
  result = airfix::content::buildLegacyAircraftHudInstrumentsSubmission(
      plan(), invalidTextures, layout, 100.0F);
  require(
      result.status ==
              LegacyAircraftHudInstrumentsSubmissionStatus::invalidTextureSet &&
          !result.submission.has_value(),
      "invalid texture owner published a packet");

  auto wrongExtent = plan();
  wrongExtent.sourceScreenWidth = 639U;
  result = airfix::content::buildLegacyAircraftHudInstrumentsSubmission(
      wrongExtent, *loaded.textures, layout, 100.0F);
  require(result.status == LegacyAircraftHudInstrumentsSubmissionStatus::
                               incompatibleLegacyScreenExtent &&
              !result.submission.has_value(),
          "non-reference source extent was reinterpreted");

  result = airfix::content::buildLegacyAircraftHudInstrumentsSubmission(
      plan(), *loaded.textures, nativeLayout({800.0F, 600.0F}), 100.0F);
  require(result.status == LegacyAircraftHudInstrumentsSubmissionStatus::
                               incompatibleUiDesignExtent &&
              !result.submission.has_value(),
          "non-reference UI design extent was reinterpreted");

  result = airfix::content::buildLegacyAircraftHudInstrumentsSubmission(
      plan(), *loaded.textures, layout,
      std::numeric_limits<float>::quiet_NaN());
  require(
      result.status ==
              LegacyAircraftHudInstrumentsSubmissionStatus::uiScaleOutOfRange &&
          !result.submission.has_value(),
      "NaN UI scale was accepted");

  auto nonFinite = plan();
  nonFinite.orderedCommands[0U].destinationQuad[0U].x =
      std::numeric_limits<float>::quiet_NaN();
  result = airfix::content::buildLegacyAircraftHudInstrumentsSubmission(
      nonFinite, *loaded.textures, layout, 100.0F);
  require(result.status == LegacyAircraftHudInstrumentsSubmissionStatus::
                               outputMappingFailed &&
              !result.submission.has_value(),
          "non-finite plan geometry reached a backend");
}

} // namespace

int main() {
  try {
    testLoadsExactCrossArchiveSet();
    testRequiresExactArchiveAndDimensions();
    testLimitsCancellationCallbacksAndMoveAreAtomic();
    testSubmissionPreservesNativeContract();
    testSubmissionFailuresAreAtomic();
    std::cout << "Legacy aircraft HUD instrument texture set tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft HUD instrument texture set test failure: "
              << error.what() << '\n';
    return 1;
  }
}
