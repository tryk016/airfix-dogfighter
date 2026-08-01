#include "airfix/content/LegacyAircraftHealthGaugeSubmission.hpp"
#include "airfix/content/LegacyAircraftHealthGaugeTextureSet.hpp"
#include "airfix/render/LegacyAircraftHealthGaugePlan.hpp"
#include "airfix/render/NativeRenderLayout.hpp"
#include "support/SyntheticContent.hpp"

#include <array>
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
using airfix::content::LegacyAircraftHealthGaugeBlendMode;
using airfix::content::LegacyAircraftHealthGaugeDepthMode;
using airfix::content::LegacyAircraftHealthGaugeSamplingMode;
using airfix::content::LegacyAircraftHealthGaugeSubmissionStatus;
using airfix::content::LegacyAircraftHealthGaugeTextureArchive;
using airfix::content::LegacyAircraftHealthGaugeTextureLoadIssueKind;
using airfix::content::LegacyAircraftHealthGaugeTextureLoadLimits;
using airfix::content::LegacyAircraftHealthGaugeTextureLoadPhase;
using airfix::content::LegacyAircraftHealthGaugeTextureLoadResult;
using airfix::content::LegacyAircraftHealthGaugeTextureRole;
using airfix::content::VerifiedContentSession;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::string_view backgroundPath =
    "Graphics\\Ingame\\HUD\\armour_meter.gti";
constexpr std::string_view foregroundPath = "Graphics\\Ingame\\HUD\\armour.gti";

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
    const auto red = static_cast<std::uint8_t>(seed + index % 13U);
    pixels.push_back(static_cast<std::uint8_t>(red + 2U));
    pixels.push_back(static_cast<std::uint8_t>(red + 1U));
    pixels.push_back(red);
    pixels.push_back(static_cast<std::uint8_t>(0xFFU - index % 5U));
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

[[nodiscard]] Bytes makePalettedAlphaGti(const std::uint32_t width,
                                         const std::uint32_t height,
                                         const std::uint32_t checksum) {
  const std::size_t pixelCount =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  Bytes bytes;
  appendU32(bytes, airfix::assets::kGtiMagic);
  appendU32(bytes, airfix::assets::kGtiVersion);
  appendU32(bytes, airfix::assets::kGtiChecksumChunk);
  appendU32(bytes, 4U);
  appendU32(bytes, checksum);
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
    bytes.push_back(static_cast<std::uint8_t>(0xFFU - index % 5U));
  }
  return bytes;
}

[[nodiscard]] std::vector<UdspInputEntry> representativeSourceEntries() {
  return {{
      .logicalPath = std::string(backgroundPath),
      .bytes = makeRgba8Gti(128U, 128U, 0x10U, 0x10203040U),
  }};
}

[[nodiscard]] std::vector<UdspInputEntry> representativeLocalizationEntries() {
  return {{
      .logicalPath = std::string(foregroundPath),
      .bytes = makeRgba8Gti(128U, 128U, 0x40U, 0x50607080U),
  }};
}

class PackFixture final {
public:
  PackFixture(std::vector<UdspInputEntry> sourceEntries,
              std::vector<UdspInputEntry> localizationEntries)
      : pack_(airfix::testing::makeSyntheticAfPack(
            std::span<const UdspInputEntry>(sourceEntries),
            std::span<const UdspInputEntry>(localizationEntries))),
        path_(workspace_.path() / "private-content.afpack") {
    airfix::testing::detail::writeFile(path_, pack_.bytes);
  }

  [[nodiscard]] VerifiedContentSession
  open(const std::uint64_t generation = 201U) const {
    auto input = std::make_unique<std::ifstream>(path_, std::ios::binary |
                                                            std::ios::ate);
    require(static_cast<bool>(*input),
            "failed to open synthetic authenticated content");
    return VerifiedContentSession::open(std::move(input),
                                        "synthetic owner-local content",
                                        ContentRevision{
                                            .generation = generation,
                                            .pack =
                                                {
                                                    .size = pack_.size,
                                                    .sha256 = pack_.sha256,
                                                },
                                        });
  }

private:
  airfix::testing::detail::TempWorkspace workspace_;
  SyntheticAfPack pack_;
  std::filesystem::path path_;
};

void requireAtomicFailure(
    const LegacyAircraftHealthGaugeTextureLoadResult &result,
    const LegacyAircraftHealthGaugeTextureLoadIssueKind kind,
    const std::string_view message) {
  require(!result.textures.has_value() && !result.issues.empty() &&
              result.issues.front().kind == kind,
          message);
}

void testLoadsExactCrossArchiveTextureSet() {
  PackFixture fixture(representativeSourceEntries(),
                      representativeLocalizationEntries());
  auto session = fixture.open();
  const auto result =
      airfix::content::loadLegacyAircraftHealthGaugeTextures(session);
  require(result.success(), "representative health textures did not load");
  require(result.textures->valid() && result.textures->belongsTo(session),
          "loaded health texture set lost authenticated identity");

  const auto *background = result.textures->texture(
      LegacyAircraftHealthGaugeTextureRole::background);
  const auto *foreground = result.textures->texture(
      LegacyAircraftHealthGaugeTextureRole::foreground);
  require(background != nullptr && foreground != nullptr,
          "loaded health texture roles were not addressable");
  require(background->archive ==
                  LegacyAircraftHealthGaugeTextureArchive::source &&
              background->logicalPath == backgroundPath &&
              background->upload.uploadLevels.front().width == 128U &&
              background->upload.uploadLevels.front().height == 128U,
          "health background did not preserve its source contract");
  require(foreground->archive ==
                  LegacyAircraftHealthGaugeTextureArchive::localization &&
              foreground->logicalPath == foregroundPath &&
              foreground->upload.uploadLevels.front().width == 128U &&
              foreground->upload.uploadLevels.front().height == 128U,
          "health foreground did not preserve its localization contract");
  require(background->sourceFileIndex == foreground->sourceFileIndex,
          "fixture no longer exercises equal file indices across archives");
  require(background->textureId.value == 0U &&
              foreground->textureId.value == 1U,
          "dedicated health texture IDs are not dense and stable");
  require(result.textures->texture(
              static_cast<LegacyAircraftHealthGaugeTextureRole>(0xFFU)) ==
              nullptr,
          "invalid health texture role unexpectedly resolved");
}

void testRequiresCorrectAuthenticatedArchiveForEachRole() {
  {
    PackFixture fixture(representativeSourceEntries(), {});
    auto session = fixture.open();
    const auto result =
        airfix::content::loadLegacyAircraftHealthGaugeTextures(session);
    requireAtomicFailure(
        result, LegacyAircraftHealthGaugeTextureLoadIssueKind::textureNotFound,
        "missing localization texture published a partial set");
    require(result.issues.front().role ==
                    LegacyAircraftHealthGaugeTextureRole::foreground &&
                result.issues.front().archive ==
                    LegacyAircraftHealthGaugeTextureArchive::localization,
            "missing localization texture reported the wrong provenance");
  }

  {
    auto sourceEntries = representativeSourceEntries();
    sourceEntries.push_back({
        .logicalPath = std::string(foregroundPath),
        .bytes = makeRgba8Gti(128U, 128U, 0x40U, 0x50607080U),
    });
    PackFixture fixture(std::move(sourceEntries), {});
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHealthGaugeTextures(session),
        LegacyAircraftHealthGaugeTextureLoadIssueKind::textureNotFound,
        "foreground in Resource.up bypassed the localization contract");
  }
}

void testRejectsMalformedTextureAtomically() {
  {
    auto localizationEntries = representativeLocalizationEntries();
    localizationEntries.front().bytes =
        makeRgba8Gti(64U, 128U, 0x40U, 0x50607080U);
    PackFixture fixture(representativeSourceEntries(),
                        std::move(localizationEntries));
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHealthGaugeTextures(session),
        LegacyAircraftHealthGaugeTextureLoadIssueKind::unexpectedDimensions,
        "wrong foreground dimensions published a partial set");
  }

  {
    auto sourceEntries = representativeSourceEntries();
    sourceEntries.front().bytes = makePalettedAlphaGti(128U, 128U, 0x10203040U);
    PackFixture fixture(std::move(sourceEntries),
                        representativeLocalizationEntries());
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHealthGaugeTextures(session),
        LegacyAircraftHealthGaugeTextureLoadIssueKind::unexpectedFormat,
        "wrong background format published a partial set");
  }
}

void testLimitsCancellationAndCallbackFailureAreAtomic() {
  PackFixture fixture(representativeSourceEntries(),
                      representativeLocalizationEntries());
  {
    auto session = fixture.open();
    LegacyAircraftHealthGaugeTextureLoadLimits limits;
    limits.maximumSourceBytes = 7U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHealthGaugeTextures(session, limits),
        LegacyAircraftHealthGaugeTextureLoadIssueKind::invalidLimits,
        "invalid health texture limits were accepted");
  }

  {
    auto session = fixture.open();
    std::stop_source stop;
    stop.request_stop();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHealthGaugeTextures(
            session, {}, stop.get_token()),
        LegacyAircraftHealthGaugeTextureLoadIssueKind::cancelled,
        "cancelled health texture load published a set");
  }

  {
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHealthGaugeTextures(
            session, {}, {},
            [](const auto &progress) {
              if (progress.phase == LegacyAircraftHealthGaugeTextureLoadPhase::
                                        lookingUpTextures) {
                throw std::runtime_error("synthetic callback failure");
              }
            }),
        LegacyAircraftHealthGaugeTextureLoadIssueKind::progressCallbackFailure,
        "throwing callback published a health texture set");
  }
}

void testMovedFromSessionIsRejected() {
  PackFixture fixture(representativeSourceEntries(),
                      representativeLocalizationEntries());
  auto session = fixture.open();
  auto moved = std::move(session);
  requireAtomicFailure(
      airfix::content::loadLegacyAircraftHealthGaugeTextures(session),
      LegacyAircraftHealthGaugeTextureLoadIssueKind::sessionIdentityChanged,
      "moved-from session authenticated a health texture set");
  require(
      airfix::content::loadLegacyAircraftHealthGaugeTextures(moved).success(),
      "moved-to session could not load health textures");
}

[[nodiscard]] airfix::render::LegacyAircraftHealthGaugePlan
halfHealthPlan() noexcept {
  return airfix::render::buildLegacyAircraftHealthGaugePlan({
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .displayedHealth = 50.0F,
      .maximumHealth = 100.0F,
      .armourMeterTextureAvailable = true,
      .armourTextureAvailable = true,
  });
}

[[nodiscard]] airfix::render::NativeRenderLayout nativeLayout(
    const airfix::render::UiLogicalExtent uiDesignExtent = {640.0F, 480.0F}) {
  const auto built = airfix::render::buildNativeRenderLayout({
      .outputExtent = {1920U, 1080U},
      .uiDesignExtent = uiDesignExtent,
  });
  require(built.complete(), "native health-gauge layout did not build");
  return *built.layout;
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 1.0e-3F) noexcept {
  return std::abs(actual - expected) <= tolerance;
}

void testSubmissionPreservesAuthenticatedNativeContract() {
  PackFixture fixture(representativeSourceEntries(),
                      representativeLocalizationEntries());
  auto session = fixture.open();
  const auto loadedResult =
      airfix::content::loadLegacyAircraftHealthGaugeTextures(session);
  require(loadedResult.success(), "submission fixture did not load");
  const auto &textures = *loadedResult.textures;
  const auto plan = halfHealthPlan();
  require(plan.ready(), "submission fixture plan did not build");

  const auto submission =
      airfix::content::buildLegacyAircraftHealthGaugeSubmission(
          plan, textures, nativeLayout(), 100.0F);
  require(submission.ready() && submission.submission->belongsTo(textures) &&
              submission.submission->commandCount == plan.commandCount &&
              submission.submission->uiScalePercent == 100.0F &&
              submission.submission->command(plan.commandCount) == nullptr,
          "authenticated health-gauge packet was not published atomically");

  const auto *background = submission.submission->command(0U);
  const auto *firstMask = submission.submission->command(1U);
  const auto *foreground =
      submission.submission->command(plan.commandCount - 1U);
  require(background != nullptr && firstMask != nullptr &&
              foreground != nullptr && background->textured() &&
              background->textureRole ==
                  LegacyAircraftHealthGaugeTextureRole::background &&
              background->textureId.value == 0U &&
              background->colourArgb == 0xFFFFFFFFU &&
              background->blendMode == LegacyAircraftHealthGaugeBlendMode::
                                           sourceAlphaOneMinusSourceAlpha &&
              background->depthMode ==
                  LegacyAircraftHealthGaugeDepthMode::alwaysWrite &&
              background->samplingMode ==
                  LegacyAircraftHealthGaugeSamplingMode::linearClamp &&
              firstMask->kind ==
                  airfix::render::LegacyAircraftHealthGaugeCommandKind::
                      damageMaskQuad &&
              !firstMask->textured() && firstMask->colourArgb == 0xFF000000U &&
              firstMask->blendMode ==
                  LegacyAircraftHealthGaugeBlendMode::opaque &&
              foreground->textureRole ==
                  LegacyAircraftHealthGaugeTextureRole::foreground &&
              foreground->textureId.value == 1U,
          "submission lost native order, texture identity or GPU state");
  require(close(background->outputQuad[0].x, 258.0F) &&
              close(background->outputQuad[0].y, 774.0F) &&
              close(background->outputQuad[2].x, 546.0F) &&
              close(background->outputQuad[2].y, 1062.0F),
          "100% widget did not map through the 16:9 safe UI viewport");

  const auto enlarged =
      airfix::content::buildLegacyAircraftHealthGaugeSubmission(
          plan, textures, nativeLayout(), 150.0F);
  require(enlarged.ready(), "150% health-gauge widget was rejected");
  const auto *enlargedBackground = enlarged.submission->command(0U);
  require(enlargedBackground != nullptr &&
              close(enlargedBackground->outputQuad[0].x, 267.0F) &&
              close(enlargedBackground->outputQuad[0].y, 621.0F) &&
              close(enlargedBackground->outputQuad[2].x, 699.0F) &&
              close(enlargedBackground->outputQuad[2].y, 1053.0F),
          "independent UI scale did not retain the bottom-left anchor");

  auto otherSession = fixture.open();
  const auto otherTextures =
      airfix::content::loadLegacyAircraftHealthGaugeTextures(otherSession);
  require(otherTextures.success() &&
              !submission.submission->belongsTo(*otherTextures.textures),
          "equal revision on another authenticated handle reused a packet");
  auto forged = *submission.submission;
  forged.orderedCommands.front().textureId.value = 1U;
  require(!forged.belongsTo(textures),
          "forged role-local texture identity reached a backend");
  forged = *submission.submission;
  std::swap(forged.orderedCommands[0U], forged.orderedCommands[1U]);
  require(!forged.belongsTo(textures),
          "reordered gauge commands reached a backend");
  forged = *submission.submission;
  forged.orderedCommands.front().kind =
      static_cast<airfix::render::LegacyAircraftHealthGaugeCommandKind>(0xFFU);
  require(!forged.belongsTo(textures),
          "unknown packet command reached a backend");
}

void testSubmissionFailuresAreAtomic() {
  PackFixture fixture(representativeSourceEntries(),
                      representativeLocalizationEntries());
  auto session = fixture.open();
  const auto loaded =
      airfix::content::loadLegacyAircraftHealthGaugeTextures(session);
  require(loaded.success(), "failure fixture did not load");
  const auto layout = nativeLayout();

  const auto requireFailure =
      [&](const auto &plan, const auto &textures, const auto &candidateLayout,
          const float uiScalePercent, const auto expected,
          const std::string_view message) {
        const auto result =
            airfix::content::buildLegacyAircraftHealthGaugeSubmission(
                plan, textures, candidateLayout, uiScalePercent);
        require(result.status == expected && !result.submission.has_value(),
                message);
      };

  auto invalidPlan = halfHealthPlan();
  invalidPlan.status =
      airfix::render::LegacyAircraftHealthGaugePlanStatus::typeHudDisabled;
  requireFailure(invalidPlan, *loaded.textures, layout, 100.0F,
                 LegacyAircraftHealthGaugeSubmissionStatus::planNotReady,
                 "non-ready gauge plan published a packet");

  auto invalidTextures = *loaded.textures;
  invalidTextures.textures.front().textureId.value = 7U;
  requireFailure(halfHealthPlan(), invalidTextures, layout, 100.0F,
                 LegacyAircraftHealthGaugeSubmissionStatus::invalidTextureSet,
                 "invalid authenticated texture set published a packet");

  auto narrow = halfHealthPlan();
  narrow.sourceScreenWidth = 639U;
  requireFailure(
      narrow, *loaded.textures, layout, 100.0F,
      LegacyAircraftHealthGaugeSubmissionStatus::incompatibleLegacyScreenExtent,
      "non-reference source screen was silently reinterpreted");

  requireFailure(
      halfHealthPlan(), *loaded.textures, nativeLayout({800.0F, 600.0F}),
      100.0F,
      LegacyAircraftHealthGaugeSubmissionStatus::incompatibleUiDesignExtent,
      "non-reference UI design domain was silently reinterpreted");
  requireFailure(halfHealthPlan(), *loaded.textures, layout, 74.0F,
                 LegacyAircraftHealthGaugeSubmissionStatus::uiScaleOutOfRange,
                 "out-of-policy UI scale was accepted");
  requireFailure(halfHealthPlan(), *loaded.textures, layout,
                 std::numeric_limits<float>::quiet_NaN(),
                 LegacyAircraftHealthGaugeSubmissionStatus::uiScaleOutOfRange,
                 "NaN UI scale was accepted");

  auto unknownCommand = halfHealthPlan();
  unknownCommand.orderedCommands.front().kind =
      static_cast<airfix::render::LegacyAircraftHealthGaugeCommandKind>(0xFFU);
  requireFailure(unknownCommand, *loaded.textures, layout, 100.0F,
                 LegacyAircraftHealthGaugeSubmissionStatus::invalidPlanCommand,
                 "unknown plan command reached a backend");

  auto emptyPlan = halfHealthPlan();
  emptyPlan.commandCount = 0U;
  requireFailure(emptyPlan, *loaded.textures, layout, 100.0F,
                 LegacyAircraftHealthGaugeSubmissionStatus::invalidPlanCommand,
                 "empty ready plan published a packet");

  auto reorderedPlan = halfHealthPlan();
  std::swap(reorderedPlan.orderedCommands[0U],
            reorderedPlan.orderedCommands[1U]);
  requireFailure(reorderedPlan, *loaded.textures, layout, 100.0F,
                 LegacyAircraftHealthGaugeSubmissionStatus::invalidPlanCommand,
                 "reordered plan published a packet");

  auto nonFinite = halfHealthPlan();
  nonFinite.orderedCommands[1U].quad[0U].x =
      std::numeric_limits<float>::quiet_NaN();
  requireFailure(nonFinite, *loaded.textures, layout, 100.0F,
                 LegacyAircraftHealthGaugeSubmissionStatus::outputMappingFailed,
                 "non-finite quad reached a backend");

  auto outsideOutput = halfHealthPlan();
  outsideOutput.orderedCommands[1U].quad[0U].x = -1000.0F;
  requireFailure(outsideOutput, *loaded.textures, layout, 100.0F,
                 LegacyAircraftHealthGaugeSubmissionStatus::outputMappingFailed,
                 "off-output quad reached a backend");
}

} // namespace

int main() {
  try {
    testLoadsExactCrossArchiveTextureSet();
    testRequiresCorrectAuthenticatedArchiveForEachRole();
    testRejectsMalformedTextureAtomically();
    testLimitsCancellationAndCallbackFailureAreAtomic();
    testMovedFromSessionIsRejected();
    testSubmissionPreservesAuthenticatedNativeContract();
    testSubmissionFailuresAreAtomic();
    std::cout << "Legacy aircraft health gauge texture set tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft health gauge texture set test failure: "
              << error.what() << '\n';
    return 1;
  }
}
