#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudWeaponPanelsSubmission.hpp"
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
using airfix::content::LegacyAircraftHudWeaponPanelTextureArchive;
using airfix::content::LegacyAircraftHudWeaponPanelTextureKind;
using airfix::content::LegacyAircraftHudWeaponPanelTextureLoadIssueKind;
using airfix::content::LegacyAircraftHudWeaponPanelTextureLoadLimits;
using airfix::content::LegacyAircraftHudWeaponPanelTextureLoadPhase;
using airfix::content::LegacyAircraftHudWeaponPanelTextureLoadResult;
using airfix::content::VerifiedContentSession;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::string_view background1Path =
    "Graphics\\Ingame\\HUD\\weapon1.gti";
constexpr std::string_view background2Path =
    "Graphics\\Ingame\\HUD\\weapon2.gti";
constexpr std::string_view iconAlphaPath =
    "Graphics\\Ingame\\HUD\\weapon_alpha.gti";
constexpr std::string_view iconBetaPath =
    "Graphics\\Ingame\\HUD\\weapon_beta.gti";
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
          .bytes = makeRgba8Gti(64U, 32U, 0x70U, 0xA0B0C0D0U),
      },
      {
          .logicalPath = std::string(iconBetaPath),
          .bytes = makeRgba8Gti(64U, 32U, 0x90U, 0x10293847U),
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
          .logicalPath = std::string(background1Path),
          .bytes = makeRgba8Gti(64U, 64U, 0x10U, 0x10203040U),
      },
      {
          .logicalPath = std::string(background2Path),
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
  open(const std::uint64_t generation = 701U) const {
    auto input = std::make_unique<std::ifstream>(path_, std::ios::binary |
                                                            std::ios::ate);
    require(static_cast<bool>(*input), "synthetic pack did not open");
    return VerifiedContentSession::open(
        std::move(input), "synthetic weapon-panel content",
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
    const LegacyAircraftHudWeaponPanelTextureLoadResult &result,
    const LegacyAircraftHudWeaponPanelTextureLoadIssueKind expected,
    const std::string_view message) {
  require(!result.textures.has_value() && !result.issues.empty() &&
              result.issues.front().kind == expected,
          message);
}

void testDiscoversAuthenticatedCatalogWithoutHardcodedNames() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto result =
      airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session);
  require(result.success() && result.textures->valid() &&
              result.textures->belongsTo(session) &&
              result.textures->textures.size() == 4U &&
              result.textures->iconCount == 2U,
          "representative dynamic weapon catalog did not load atomically");

  const auto *primary = result.textures->background(
      LegacyAircraftHudWeaponPanelTextureKind::primaryBackground);
  const auto *secondary = result.textures->background(
      LegacyAircraftHudWeaponPanelTextureKind::secondaryBackground);
  const auto *alpha = result.textures->findIconForWeaponType("WpAlPhA");
  const auto *beta = result.textures->findIconForWeaponType("WpBETA");
  require(primary != nullptr && secondary != nullptr && alpha != nullptr &&
              beta != nullptr &&
              result.textures->findIconForWeaponType("WpMissing") == nullptr &&
              result.textures->findIconForWeaponType("W") == nullptr,
          "fixed roles or allocation-free dynamic lookup failed");
  require(primary->archive ==
                  LegacyAircraftHudWeaponPanelTextureArchive::localization &&
              primary->logicalPath == background1Path &&
              primary->textureId.value == 0U &&
              secondary->archive ==
                  LegacyAircraftHudWeaponPanelTextureArchive::localization &&
              secondary->logicalPath == background2Path &&
              secondary->textureId.value == 1U &&
              alpha->archive ==
                  LegacyAircraftHudWeaponPanelTextureArchive::source &&
              alpha->kind == LegacyAircraftHudWeaponPanelTextureKind::icon &&
              alpha->matchKey == "alpha" && alpha->iconIndex < 2U &&
              alpha->textureId.value == alpha->iconIndex + 2U &&
              beta->matchKey == "beta" && beta->iconIndex < 2U &&
              beta->textureId.value == beta->iconIndex + 2U,
          "catalog provenance, dense IDs, or suffix keys changed");
  require(result.textures->icon(alpha->iconIndex) == alpha &&
              result.textures->icon(beta->iconIndex) == beta &&
              result.textures->icon(2U) == nullptr &&
              result.textures->background(
                  LegacyAircraftHudWeaponPanelTextureKind::icon) == nullptr,
          "bounded owner-local accessors failed");
}

void testExactArchiveDimensionsAndCatalogLimits() {
  {
    auto localization = localizationEntries();
    localization.pop_back();
    auto source = sourceEntries();
    source.push_back({
        .logicalPath = std::string(background2Path),
        .bytes = makeRgba8Gti(64U, 64U, 0x40U, 0x50607080U),
    });
    PackFixture fixture(std::move(source), std::move(localization));
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session),
        LegacyAircraftHudWeaponPanelTextureLoadIssueKind::textureNotFound,
        "weapon2 in Resource.up bypassed localization ownership");
  }
  {
    auto source = sourceEntries();
    source.front().bytes = makeRgba8Gti(63U, 32U, 0x70U, 0xA0B0C0D0U);
    PackFixture fixture(std::move(source), localizationEntries());
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session),
        LegacyAircraftHudWeaponPanelTextureLoadIssueKind::unexpectedDimensions,
        "wrong icon dimensions published a partial catalog");
  }
  {
    PackFixture fixture(sourceEntries(), localizationEntries());
    auto session = fixture.open();
    LegacyAircraftHudWeaponPanelTextureLoadLimits limits;
    limits.maximumIconCount = 1U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session,
                                                                  limits),
        LegacyAircraftHudWeaponPanelTextureLoadIssueKind::catalogLimitExceeded,
        "dynamic catalog count bypassed its configured limit");
  }
  {
    std::vector<UdspInputEntry> source{{
        .logicalPath = "Elsewhere\\weapon_alpha.gti",
        .bytes = makeRgba8Gti(64U, 32U, 0x70U, 0xA0B0C0D0U),
    }};
    PackFixture fixture(std::move(source), localizationEntries());
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session),
        LegacyAircraftHudWeaponPanelTextureLoadIssueKind::
            catalogDirectoryNotFound,
        "wrong source directory was accepted as the native catalog");
  }
}

void testCancellationCallbacksAndMovedSessionAreAtomic() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  {
    auto session = fixture.open();
    LegacyAircraftHudWeaponPanelTextureLoadLimits limits;
    limits.maximumSourceBytes = 7U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session,
                                                                  limits),
        LegacyAircraftHudWeaponPanelTextureLoadIssueKind::invalidLimits,
        "invalid loader limits were accepted");
  }
  {
    auto session = fixture.open();
    std::stop_source stop;
    stop.request_stop();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(
            session, {}, stop.get_token()),
        LegacyAircraftHudWeaponPanelTextureLoadIssueKind::cancelled,
        "cancelled loader published textures");
  }
  {
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(
            session, {}, {},
            [](const auto &progress) {
              if (progress.phase ==
                  LegacyAircraftHudWeaponPanelTextureLoadPhase::
                      discoveringCatalog) {
                throw std::runtime_error("synthetic callback failure");
              }
            }),
        LegacyAircraftHudWeaponPanelTextureLoadIssueKind::
            progressCallbackFailure,
        "throwing progress callback published textures");
  }
  {
    auto session = fixture.open();
    auto moved = std::move(session);
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session),
        LegacyAircraftHudWeaponPanelTextureLoadIssueKind::
            sessionIdentityChanged,
        "moved-from session authenticated textures");
    require(airfix::content::loadLegacyAircraftHudWeaponPanelTextures(moved)
                .success(),
            "moved-to session could not load the dynamic catalog");
  }
}

[[nodiscard]] airfix::render::NativeRenderLayout
nativeLayout(const airfix::render::UiLogicalExtent design = {640.0F, 480.0F}) {
  const auto built = airfix::render::buildNativeRenderLayout({
      .outputExtent = {1920U, 1080U},
      .uiDesignExtent = design,
  });
  require(built.complete(), "native weapon-panel layout did not build");
  return *built.layout;
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 1.0e-3F) noexcept {
  return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] airfix::render::LegacyAircraftHudWeaponPanelsPlan
panelPlan(const std::uint32_t primaryIcon,
          const std::uint32_t secondaryIcon) noexcept {
  const auto digits =
      airfix::render::makeLegacyAircraftHudRollingDigitsState(123);
  return airfix::render::buildLegacyAircraftHudWeaponPanelsPlan({
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .accumulatedElapsedSeconds = 0.0F,
      .primaryBackgroundTextureAvailable = true,
      .secondaryBackgroundTextureAvailable = true,
      .rollingDigitAtlasAvailable = true,
      .primary =
          {
              .weaponPresent = true,
              .digitStateAvailable = true,
              .digitState = digits,
              .quantizedAmmo = 123,
              .iconCatalogMatch = true,
              .iconTextureAvailable = true,
              .iconTextureIndex = primaryIcon,
              .quantizedStatusIndex = 0,
          },
      .secondary =
          {
              .weaponPresent = true,
              .digitStateAvailable = true,
              .digitState = digits,
              .quantizedAmmo = 123,
              .iconCatalogMatch = true,
              .iconTextureAvailable = true,
              .iconTextureIndex = secondaryIcon,
              .quantizedStatusIndex = 4,
          },
  });
}

void testSubmissionBindsBothAuthenticatedOwners() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto weaponTextures =
      airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session);
  const auto digitTextures =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session);
  require(weaponTextures.success() && digitTextures.success(),
          "submission texture owners did not load");
  const auto *alpha = weaponTextures.textures->findIconForWeaponType("WpAlpha");
  const auto *beta = weaponTextures.textures->findIconForWeaponType("WpBeta");
  require(alpha != nullptr && beta != nullptr,
          "submission catalog fixtures did not resolve");
  const auto plan = panelPlan(alpha->iconIndex, beta->iconIndex);
  require(plan.ready() && plan.commandCount == 14U,
          "submission plan fixture did not build");

  const auto result =
      airfix::content::buildLegacyAircraftHudWeaponPanelsSubmission(
          plan, *weaponTextures.textures, *digitTextures.textures,
          nativeLayout(), 100.0F);
  require(result.ready() &&
              result.submission->belongsTo(*weaponTextures.textures,
                                           *digitTextures.textures) &&
              result.submission->commandCount == 14U &&
              result.submission->command(14U) == nullptr,
          "weapon-panel submission was not published atomically");

  const auto *primaryBackground = result.submission->command(0U);
  const auto *secondaryBackground = result.submission->command(1U);
  const auto *primaryDigit = result.submission->command(2U);
  const auto *primaryIcon = result.submission->command(6U);
  const auto *primaryStatus = result.submission->command(7U);
  require(primaryBackground != nullptr && secondaryBackground != nullptr &&
              primaryDigit != nullptr && primaryIcon != nullptr &&
              primaryStatus != nullptr,
          "submission commands were not addressable");
  require(
      primaryBackground->textureNamespace ==
              airfix::content::LegacyAircraftHudWeaponPanelTextureNamespace::
                  weaponPanels &&
          primaryBackground->textureId.value == 0U &&
          secondaryBackground->textureId.value == 1U &&
          primaryDigit->textureNamespace ==
              airfix::content::LegacyAircraftHudWeaponPanelTextureNamespace::
                  rollingDigits &&
          primaryDigit->textureId.value == 0U &&
          primaryIcon->textureId == alpha->textureId &&
          primaryIcon->sourceTextureIndex == alpha->iconIndex &&
          primaryStatus->textureNamespace ==
              airfix::content::LegacyAircraftHudWeaponPanelTextureNamespace::
                  none &&
          primaryStatus->blendMode ==
              airfix::content::LegacyAircraftHudWeaponPanelBlendMode::
                  destinationMultiplySourceColour &&
          primaryStatus->colourArgb == 0xBF269A1AU,
      "submission lost texture namespace, ID, blend, or palette");
  require(primaryBackground->uv ==
                  airfix::content::LegacyAircraftHudWeaponPanelUvRect{
                      0.0F, 0.0F, 1.0F, 1.0F} &&
              close(primaryDigit->uv.maximumU, 7.0F / 16.0F) &&
              close(primaryDigit->uv.minimumV, 1.0F / 128.0F) &&
              close(primaryDigit->uv.maximumV, 10.0F / 128.0F) &&
              close(primaryBackground->outputRect.x, 741.75F) &&
              close(primaryBackground->outputRect.y, 918.0F) &&
              close(primaryBackground->outputRect.width, 144.0F) &&
              close(primaryStatus->outputRect.x, 755.25F) &&
              close(primaryStatus->outputRect.width, 117.0F),
          "native UV or 1920x1080 UI mapping changed");

  auto forged = *result.submission;
  forged.orderedCommands[6U].textureId.value = 0U;
  require(!forged.belongsTo(*weaponTextures.textures, *digitTextures.textures),
          "forged icon ID reached a backend");
  forged = *result.submission;
  forged.orderedCommands[7U].blendMode = airfix::content::
      LegacyAircraftHudWeaponPanelBlendMode::sourceAlphaOneMinusSourceAlpha;
  require(!forged.belongsTo(*weaponTextures.textures, *digitTextures.textures),
          "forged status blend reached a backend");
  forged = *result.submission;
  std::swap(forged.orderedCommands[1U], forged.orderedCommands[2U]);
  require(!forged.belongsTo(*weaponTextures.textures, *digitTextures.textures),
          "reordered panel commands reached a backend");
}

void testSubmissionFailuresAreAtomic() {
  PackFixture fixture(sourceEntries(), localizationEntries());
  auto session = fixture.open();
  const auto weaponTextures =
      airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session);
  const auto digitTextures =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session);
  require(weaponTextures.success() && digitTextures.success(),
          "failure texture owners did not load");
  const auto *alpha = weaponTextures.textures->findIconForWeaponType("WpAlpha");
  const auto *beta = weaponTextures.textures->findIconForWeaponType("WpBeta");
  require(alpha != nullptr && beta != nullptr,
          "failure catalog fixtures did not resolve");
  const auto validPlan = panelPlan(alpha->iconIndex, beta->iconIndex);
  const auto layout = nativeLayout();

  auto invalidPlan = validPlan;
  invalidPlan.status =
      airfix::render::LegacyAircraftHudWeaponPanelsPlanStatus::typeHudDisabled;
  auto result = airfix::content::buildLegacyAircraftHudWeaponPanelsSubmission(
      invalidPlan, *weaponTextures.textures, *digitTextures.textures, layout,
      100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudWeaponPanelsSubmissionStatus::
                  planNotReady &&
          !result.submission.has_value(),
      "non-ready plan published a packet");

  auto invalidWeaponTextures = *weaponTextures.textures;
  invalidWeaponTextures.textures[0U].textureId.value = 9U;
  result = airfix::content::buildLegacyAircraftHudWeaponPanelsSubmission(
      validPlan, invalidWeaponTextures, *digitTextures.textures, layout,
      100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudWeaponPanelsSubmissionStatus::
                  invalidWeaponTextureSet &&
          !result.submission.has_value(),
      "invalid weapon owner published a packet");

  auto otherSession = fixture.open();
  const auto otherDigits =
      airfix::content::loadLegacyAircraftHudRollingDigitsTexture(otherSession);
  require(otherDigits.success(), "mismatched digit owner did not load");
  result = airfix::content::buildLegacyAircraftHudWeaponPanelsSubmission(
      validPlan, *weaponTextures.textures, *otherDigits.textures, layout,
      100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudWeaponPanelsSubmissionStatus::
                  textureOwnersMismatch &&
          !result.submission.has_value(),
      "cross-session texture owners published a packet");

  auto wrongExtent = validPlan;
  wrongExtent.sourceScreenWidth = 639U;
  result = airfix::content::buildLegacyAircraftHudWeaponPanelsSubmission(
      wrongExtent, *weaponTextures.textures, *digitTextures.textures, layout,
      100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudWeaponPanelsSubmissionStatus::
                  incompatibleLegacyScreenExtent &&
          !result.submission.has_value(),
      "non-reference source extent was reinterpreted");

  result = airfix::content::buildLegacyAircraftHudWeaponPanelsSubmission(
      validPlan, *weaponTextures.textures, *digitTextures.textures,
      nativeLayout({800.0F, 600.0F}), 100.0F);
  require(
      result.status ==
              airfix::content::LegacyAircraftHudWeaponPanelsSubmissionStatus::
                  incompatibleUiDesignExtent &&
          !result.submission.has_value(),
      "non-reference UI design extent was reinterpreted");

  result = airfix::content::buildLegacyAircraftHudWeaponPanelsSubmission(
      validPlan, *weaponTextures.textures, *digitTextures.textures, layout,
      std::numeric_limits<float>::quiet_NaN());
  require(
      result.status ==
              airfix::content::LegacyAircraftHudWeaponPanelsSubmissionStatus::
                  uiScaleOutOfRange &&
          !result.submission.has_value(),
      "NaN UI scale was accepted");
}

} // namespace

int main() {
  try {
    testDiscoversAuthenticatedCatalogWithoutHardcodedNames();
    testExactArchiveDimensionsAndCatalogLimits();
    testCancellationCallbacksAndMovedSessionAreAtomic();
    testSubmissionBindsBothAuthenticatedOwners();
    testSubmissionFailuresAreAtomic();
    std::cout << "Legacy aircraft HUD weapon-panel texture tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft HUD weapon-panel texture test failure: "
              << error.what() << '\n';
    return 1;
  }
}
