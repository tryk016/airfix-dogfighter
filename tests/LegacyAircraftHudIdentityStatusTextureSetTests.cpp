#include "airfix/content/LegacyAircraftHudIdentityStatusSubmission.hpp"
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
using airfix::content::LegacyAircraftHudIdentityStatusTextureArchive;
using airfix::content::LegacyAircraftHudIdentityStatusTextureKind;
using airfix::content::LegacyAircraftHudIdentityStatusTextureLoadIssueKind;
using airfix::content::LegacyAircraftHudIdentityStatusTextureLoadLimits;
using airfix::content::LegacyAircraftHudIdentityStatusTextureLoadPhase;
using airfix::content::LegacyAircraftHudIdentityStatusTextureLoadResult;
using airfix::content::VerifiedContentSession;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::string_view technologyStarPath =
    "Graphics\\Ingame\\HUD\\techstar.gti";
constexpr std::string_view technologyCrossPath =
    "Graphics\\Ingame\\HUD\\techcross.gti";
constexpr std::string_view iconAlphaPath = "Graphics\\Ingame\\HUD\\AcAlpha.gti";
constexpr std::string_view iconBetaPath = "Graphics\\Ingame\\HUD\\AcBeta.gti";
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
  return {
      {
          .logicalPath = std::string(iconAlphaPath),
          .bytes = makeRgba8Gti(64U, 64U, 0x70U, 0xA0B0C0D0U),
      },
      {
          .logicalPath = std::string(iconBetaPath),
          .bytes = makeRgba8Gti(64U, 64U, 0x90U, 0x10293847U),
      },
      {
          .logicalPath = "Graphics\\Ingame\\HUD\\unrelated.gti",
          .bytes = makeRgba8Gti(1U, 1U, 0x20U, 0x55667788U),
      },
      {
          .logicalPath = std::string(digitsPath),
          .bytes = makeRgba8Gti(16U, 128U, 0x30U, 0x88776655U),
      },
  };
}

[[nodiscard]] std::vector<UdspInputEntry> localizationEntries() {
  return {
      {
          .logicalPath = std::string(technologyStarPath),
          .bytes = makeRgba8Gti(64U, 64U, 0x10U, 0x10203040U),
      },
      {
          .logicalPath = std::string(technologyCrossPath),
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
  open(const std::uint64_t generation = 711U) const {
    auto input = std::make_unique<std::ifstream>(path_, std::ios::binary |
                                                            std::ios::ate);
    require(static_cast<bool>(*input), "synthetic pack did not open");
    return VerifiedContentSession::open(
        std::move(input), "synthetic identity-status content",
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
    const LegacyAircraftHudIdentityStatusTextureLoadResult &result,
    const LegacyAircraftHudIdentityStatusTextureLoadIssueKind expected,
    const std::string_view message) {
  require(!result.textures.has_value() && !result.issues.empty() &&
              result.issues.front().kind == expected,
          message);
}

void testDiscoversAuthenticatedCatalogWithoutHardcodedNames() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto result =
      airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session);
  require(result.success() && result.textures->valid() &&
              result.textures->belongsTo(session) &&
              result.textures->textures.size() == 4U &&
              result.textures->iconCount == 2U,
          "representative dynamic aircraft catalog did not load atomically");

  const auto *star = result.textures->teamBadge(
      LegacyAircraftHudIdentityStatusTextureKind::technologyStar);
  const auto *cross = result.textures->teamBadge(
      LegacyAircraftHudIdentityStatusTextureKind::technologyCross);
  const auto *alpha = result.textures->findIconForAircraftType("aCaLpHa");
  const auto *beta = result.textures->findIconForAircraftType("ACBETA");
  require(star != nullptr && cross != nullptr && alpha != nullptr &&
              beta != nullptr &&
              result.textures->findIconForAircraftType("Alpha") == nullptr &&
              result.textures->findIconForAircraftType("AcMissing") == nullptr,
          "fixed roles or complete type-name lookup failed");
  require(star->archive ==
                  LegacyAircraftHudIdentityStatusTextureArchive::localization &&
              star->logicalPath == technologyStarPath &&
              star->textureId.value == 0U &&
              cross->logicalPath == technologyCrossPath &&
              cross->textureId.value == 1U &&
              alpha->archive ==
                  LegacyAircraftHudIdentityStatusTextureArchive::source &&
              alpha->kind ==
                  LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon &&
              alpha->matchKey == "AcAlpha" && alpha->iconIndex < 2U &&
              alpha->textureId.value == alpha->iconIndex + 2U &&
              beta->matchKey == "AcBeta" && beta->iconIndex < 2U &&
              beta->textureId.value == beta->iconIndex + 2U,
          "catalog provenance, dense IDs, or full-stem keys changed");
  require(result.textures->icon(alpha->iconIndex) == alpha &&
              result.textures->icon(beta->iconIndex) == beta &&
              result.textures->icon(2U) == nullptr &&
              result.textures->teamBadge(
                  LegacyAircraftHudIdentityStatusTextureKind::aircraftIcon) ==
                  nullptr,
          "bounded owner-local accessors failed");
}

void testArchiveDimensionsAndLimitsFailClosed() {
  {
    auto localization = localizationEntries();
    localization.pop_back();
    auto source = sourceEntries();
    source.push_back({
        .logicalPath = std::string(technologyCrossPath),
        .bytes = makeRgba8Gti(64U, 64U, 0x40U, 0x50607080U),
    });
    PackFixture fixture(std::move(source), std::move(localization));
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session),
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::textureNotFound,
        "team badge in Resource.up bypassed localization ownership");
  }
  {
    auto source = sourceEntries();
    source.front().bytes = makeRgba8Gti(64U, 32U, 0x70U, 0xA0B0C0D0U);
    PackFixture fixture(std::move(source), localizationEntries());
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session),
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
            unexpectedDimensions,
        "wrong aircraft-icon dimensions published a partial catalog");
  }
  {
    PackFixture fixture(sourceEntries(), localizationEntries());
    auto session = fixture.open();
    LegacyAircraftHudIdentityStatusTextureLoadLimits limits;
    limits.maximumIconCount = 1U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session,
                                                                     limits),
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
            catalogLimitExceeded,
        "dynamic aircraft catalog bypassed its configured limit");
  }
  {
    std::vector<UdspInputEntry> source{{
        .logicalPath = "Elsewhere\\AcAlpha.gti",
        .bytes = makeRgba8Gti(64U, 64U, 0x70U, 0xA0B0C0D0U),
    }};
    PackFixture fixture(std::move(source), localizationEntries());
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session),
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
            catalogDirectoryNotFound,
        "wrong source directory was accepted as the native catalog");
  }
}

void testCancellationCallbacksAndMovedSessionAreAtomic() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  {
    auto session = fixture.open();
    LegacyAircraftHudIdentityStatusTextureLoadLimits limits;
    limits.maximumSourceBytes = 7U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session,
                                                                     limits),
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::invalidLimits,
        "invalid loader limits were accepted");
  }
  {
    auto session = fixture.open();
    std::stop_source stop;
    stop.request_stop();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(
            session, {}, stop.get_token()),
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::cancelled,
        "cancelled loader published textures");
  }
  {
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(
            session, {}, {},
            [](const auto &progress) {
              if (progress.phase ==
                  LegacyAircraftHudIdentityStatusTextureLoadPhase::
                      discoveringCatalog) {
                throw std::runtime_error("synthetic callback failure");
              }
            }),
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
            progressCallbackFailure,
        "throwing progress callback published textures");
  }
  {
    auto session = fixture.open();
    auto moved = std::move(session);
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session),
        LegacyAircraftHudIdentityStatusTextureLoadIssueKind::
            sessionIdentityChanged,
        "moved-from session authenticated textures");
    require(airfix::content::loadLegacyAircraftHudIdentityStatusTextures(moved)
                .success(),
            "moved-to session could not load the aircraft catalog");
  }
}

[[nodiscard]] airfix::render::NativeRenderLayout
nativeLayout(const airfix::render::UiLogicalExtent design = {640.0F, 480.0F}) {
  const auto built = airfix::render::buildNativeRenderLayout({
      .outputExtent = {1920U, 1080U},
      .uiDesignExtent = design,
  });
  require(built.complete(), "native identity/status layout did not build");
  return *built.layout;
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 1.0e-3F) noexcept {
  return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] airfix::render::LegacyAircraftHudIdentityStatusPlan
identityPlan(const std::uint32_t iconIndex) noexcept {
  return airfix::render::buildLegacyAircraftHudIdentityStatusPlan({
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .accumulatedElapsedSeconds = 0.0F,
      .aircraftIconCatalogMatch = true,
      .aircraftIconTextureAvailable = true,
      .aircraftIconTextureIndex = iconIndex,
      .healthDigitStateAvailable = true,
      .healthDigitState =
          airfix::render::makeLegacyAircraftHudRollingDigitsState(100),
      .quantizedHealthPercent = 100,
      .teamId = 1,
      .technologyStarTextureAvailable = true,
      .technologyCrossTextureAvailable = true,
      .technologyDigitStateAvailable = true,
      .technologyDigitState =
          airfix::render::makeLegacyAircraftHudRollingDigitsState(0),
      .quantizedTechnologyLevel = 0,
      .rollingDigitAtlasAvailable = true,
  });
}

void testSubmissionBindsBothAuthenticatedOwners() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto identityTextures =
      airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session);
  const auto digitTextures =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session);
  require(identityTextures.success() && digitTextures.success(),
          "submission texture owners did not load");
  const auto *alpha =
      identityTextures.textures->findIconForAircraftType("AcAlpha");
  require(alpha != nullptr, "submission catalog fixture did not resolve");
  const auto plan = identityPlan(alpha->iconIndex);
  require(plan.ready() && plan.commandCount == 10U,
          "submission plan fixture did not build");

  const auto result =
      airfix::content::buildLegacyAircraftHudIdentityStatusSubmission(
          plan, *identityTextures.textures, *digitTextures.textures,
          nativeLayout(), 100.0F);
  require(result.ready() &&
              result.submission->belongsTo(*identityTextures.textures,
                                           *digitTextures.textures) &&
              result.submission->commandCount == 10U &&
              result.submission->command(10U) == nullptr,
          "identity/status submission was not published atomically");

  const auto *icon = result.submission->command(0U);
  const auto *health = result.submission->command(1U);
  const auto *team = result.submission->command(5U);
  const auto *technology = result.submission->command(6U);
  require(
      icon != nullptr && health != nullptr && team != nullptr &&
          technology != nullptr &&
          icon->textureNamespace ==
              airfix::content::LegacyAircraftHudIdentityStatusTextureNamespace::
                  identityStatus &&
          icon->textureId == alpha->textureId &&
          icon->sourceTextureIndex == alpha->iconIndex &&
          health->textureNamespace ==
              airfix::content::LegacyAircraftHudIdentityStatusTextureNamespace::
                  rollingDigits &&
          health->textureId.value == 0U && team->textureId.value == 0U &&
          team->teamBadge ==
              airfix::render::LegacyAircraftHudTeamBadge::technologyStar &&
          technology->textureNamespace ==
              airfix::content::LegacyAircraftHudIdentityStatusTextureNamespace::
                  rollingDigits,
      "submission lost texture namespace, ID, or team role");
  require(icon->uv ==
                  airfix::content::LegacyAircraftHudIdentityStatusUvRect{
                      0.0F, 0.0F, 1.0F, 1.0F} &&
              close(health->uv.maximumU, 7.0F / 16.0F) &&
              close(health->uv.minimumV, 1.0F / 128.0F) &&
              close(health->uv.maximumV, 10.0F / 128.0F) &&
              close(icon->outputRect.x, 352.5F) &&
              close(icon->outputRect.y, 798.75F) &&
              close(team->outputRect.x, 888.0F) &&
              close(team->outputRect.y, 918.0F) &&
              close(technology->outputRect.x, 924.0F) &&
              close(technology->outputRect.y, 1010.25F),
          "native UV or 1920x1080 UI mapping changed");

  auto forged = *result.submission;
  forged.orderedCommands[0U].textureId.value = 0U;
  require(
      !forged.belongsTo(*identityTextures.textures, *digitTextures.textures),
      "forged aircraft-icon ID reached a backend");
  forged = *result.submission;
  std::swap(forged.orderedCommands[4U], forged.orderedCommands[5U]);
  require(
      !forged.belongsTo(*identityTextures.textures, *digitTextures.textures),
      "reordered identity/status commands reached a backend");
}

void testSubmissionFailuresAreAtomic() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto identityTextures =
      airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session);
  const auto digitTextures =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session);
  require(identityTextures.success() && digitTextures.success(),
          "failure texture owners did not load");
  const auto *alpha =
      identityTextures.textures->findIconForAircraftType("AcAlpha");
  require(alpha != nullptr, "failure catalog fixture did not resolve");
  const auto validPlan = identityPlan(alpha->iconIndex);
  const auto layout = nativeLayout();

  auto invalidPlan = validPlan;
  invalidPlan.status = airfix::render::
      LegacyAircraftHudIdentityStatusPlanStatus::typeHudDisabled;
  auto result = airfix::content::buildLegacyAircraftHudIdentityStatusSubmission(
      invalidPlan, *identityTextures.textures, *digitTextures.textures, layout,
      100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudIdentityStatusSubmissionStatus::
                  planNotReady &&
          !result.submission.has_value(),
      "non-ready plan published a packet");

  auto invalidIdentityTextures = *identityTextures.textures;
  invalidIdentityTextures.textures[0U].textureId.value = 9U;
  result = airfix::content::buildLegacyAircraftHudIdentityStatusSubmission(
      validPlan, invalidIdentityTextures, *digitTextures.textures, layout,
      100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudIdentityStatusSubmissionStatus::
                  invalidIdentityTextureSet &&
          !result.submission.has_value(),
      "invalid identity owner published a packet");

  auto otherSession = fixture.open();
  const auto otherDigits =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(otherSession);
  require(otherDigits.success(), "mismatched digit owner did not load");
  result = airfix::content::buildLegacyAircraftHudIdentityStatusSubmission(
      validPlan, *identityTextures.textures, *otherDigits.textures, layout,
      100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudIdentityStatusSubmissionStatus::
                  textureOwnersMismatch &&
          !result.submission.has_value(),
      "cross-session texture owners published a packet");

  auto wrongExtent = validPlan;
  wrongExtent.sourceScreenWidth = 639U;
  result = airfix::content::buildLegacyAircraftHudIdentityStatusSubmission(
      wrongExtent, *identityTextures.textures, *digitTextures.textures, layout,
      100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudIdentityStatusSubmissionStatus::
                  incompatibleLegacyScreenExtent &&
          !result.submission.has_value(),
      "non-reference source extent was reinterpreted");

  result = airfix::content::buildLegacyAircraftHudIdentityStatusSubmission(
      validPlan, *identityTextures.textures, *digitTextures.textures,
      nativeLayout({800.0F, 600.0F}), 100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudIdentityStatusSubmissionStatus::
                  incompatibleUiDesignExtent &&
          !result.submission.has_value(),
      "non-reference UI design extent was reinterpreted");

  result = airfix::content::buildLegacyAircraftHudIdentityStatusSubmission(
      validPlan, *identityTextures.textures, *digitTextures.textures, layout,
      std::numeric_limits<float>::quiet_NaN());
  require(
      result.status ==
              airfix::content::LegacyAircraftHudIdentityStatusSubmissionStatus::
                  uiScaleOutOfRange &&
          !result.submission.has_value(),
      "NaN UI scale was accepted");
}

} // namespace

int main() {
  try {
    testDiscoversAuthenticatedCatalogWithoutHardcodedNames();
    testArchiveDimensionsAndLimitsFailClosed();
    testCancellationCallbacksAndMovedSessionAreAtomic();
    testSubmissionBindsBothAuthenticatedOwners();
    testSubmissionFailuresAreAtomic();
    std::cout << "Legacy aircraft HUD identity/status texture tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft HUD identity/status texture test failure: "
              << error.what() << '\n';
    return 1;
  }
}
