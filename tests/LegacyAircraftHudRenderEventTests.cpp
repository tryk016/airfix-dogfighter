#include "airfix/content/LegacyAircraftHudRenderEvent.hpp"
#include "support/SyntheticContent.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace airfix::content;
using namespace airfix::render;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

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
      {.logicalPath = "Graphics\\Ingame\\HUD\\armour_meter.gti",
       .bytes = makeRgba8Gti(128U, 128U, 0x10U, 0x10000001U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\digits.gti",
       .bytes = makeRgba8Gti(16U, 128U, 0x20U, 0x10000002U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\indicator.gti",
       .bytes = makeRgba8Gti(16U, 32U, 0x30U, 0x10000003U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\weapon_alpha.gti",
       .bytes = makeRgba8Gti(64U, 32U, 0x40U, 0x10000004U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\AcAlpha.gti",
       .bytes = makeRgba8Gti(64U, 64U, 0x50U, 0x10000005U)},
  };
}

[[nodiscard]] std::vector<UdspInputEntry> localizationEntries() {
  return {
      {.logicalPath = "Graphics\\Ingame\\HUD\\armour.gti",
       .bytes = makeRgba8Gti(128U, 128U, 0x60U, 0x20000001U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\clock1.gti",
       .bytes = makeRgba8Gti(64U, 64U, 0x70U, 0x20000002U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\clock2.gti",
       .bytes = makeRgba8Gti(64U, 64U, 0x80U, 0x20000003U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\weapon1.gti",
       .bytes = makeRgba8Gti(64U, 64U, 0x90U, 0x20000004U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\weapon2.gti",
       .bytes = makeRgba8Gti(64U, 64U, 0xA0U, 0x20000005U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\techstar.gti",
       .bytes = makeRgba8Gti(64U, 64U, 0xB0U, 0x20000006U)},
      {.logicalPath = "Graphics\\Ingame\\HUD\\techcross.gti",
       .bytes = makeRgba8Gti(64U, 64U, 0xC0U, 0x20000007U)},
  };
}

class PackFixture final {
public:
  PackFixture()
      : pack_(airfix::testing::makeSyntheticAfPack(
            std::span<const UdspInputEntry>(sourceEntries()),
            std::span<const UdspInputEntry>(localizationEntries()))),
        path_(workspace_.path() / "private-content.afpack") {
    airfix::testing::detail::writeFile(path_, pack_.bytes);
  }

  [[nodiscard]] VerifiedContentSession
  open(const std::uint64_t generation = 801U) const {
    auto input = std::make_unique<std::ifstream>(path_, std::ios::binary |
                                                            std::ios::ate);
    require(static_cast<bool>(*input), "synthetic pack did not open");
    return VerifiedContentSession::open(
        std::move(input), "synthetic full HUD content",
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

struct Owners final {
  LoadedLegacyAircraftHudInstrumentTextureSet instruments;
  LoadedLegacyAircraftHudRollingDigitsTextureSet digits;
  LoadedLegacyAircraftHudWeaponPanelTextureSet weapons;
  LoadedLegacyAircraftHealthGaugeTextureSet health;
  LoadedLegacyAircraftHudIdentityStatusTextureSet identity;
};

[[nodiscard]] Owners loadOwners(VerifiedContentSession &session) {
  auto instruments = loadLegacyAircraftHudInstrumentTextures(session);
  auto digits = loadLegacyAircraftHudRollingDigitsTexture(session);
  auto weapons = loadLegacyAircraftHudWeaponPanelTextures(session);
  auto health = loadLegacyAircraftHealthGaugeTextures(session);
  auto identity = loadLegacyAircraftHudIdentityStatusTextures(session);
  require(instruments.success() && digits.success() && weapons.success() &&
              health.success() && identity.success(),
          "complete synthetic HUD owner set did not load");
  return {
      .instruments = std::move(*instruments.textures),
      .digits = std::move(*digits.textures),
      .weapons = std::move(*weapons.textures),
      .health = std::move(*health.textures),
      .identity = std::move(*identity.textures),
  };
}

[[nodiscard]] NativeRenderLayout nativeLayout() {
  const auto result = buildNativeRenderLayout({
      .outputExtent = {1920U, 1080U},
      .uiDesignExtent = {640.0F, 480.0F},
  });
  require(result.complete(), "native HUD test layout did not build");
  return *result.layout;
}

[[nodiscard]] LegacyAircraftHudRenderEventInput
representativeInput(const Owners &owners) {
  const auto *weaponIcon = owners.weapons.findIconForWeaponType("WpAlpha");
  const auto *aircraftIcon = owners.identity.findIconForAircraftType("AcAlpha");
  require(weaponIcon != nullptr && aircraftIcon != nullptr,
          "synthetic HUD catalogs did not resolve");
  const auto initialHundred = makeLegacyAircraftHudRollingDigitsState(100);
  const auto initialZero = makeLegacyAircraftHudRollingDigitsState(0);
  return {
      .gates = {.activeWindowPresent = false,
                .cameraAttachedAtEntry = true,
                .typeHudEnabled = true,
                .cameraAttachedAfterLayout = true},
      .screenWidth = 640U,
      .screenHeight = 480U,
      .instruments = {.rightNormalizedValue = 0.25F,
                      .leftNormalizedValue = 0.75F,
                      .leftTintArgb = 0xFF3366CCU,
                      .rightFaceTextureAvailable = true,
                      .leftFaceTextureAvailable = true,
                      .indicatorTextureAvailable = true},
      .instrumentReadouts = {.rightDigitStateAvailable = true,
                             .rightDigitState = initialZero,
                             .quantizedVectorMagnitudeTimesHundred = 123,
                             .leftDigitStateAvailable = true,
                             .leftDigitState = initialHundred,
                             .quantizedRemainingRatioPercent = 75,
                             .leftTintArgb = 0xFF3366CCU,
                             .rollingDigitAtlasAvailable = true},
      .weaponPanels = {.primaryBackgroundTextureAvailable = true,
                       .secondaryBackgroundTextureAvailable = true,
                       .rollingDigitAtlasAvailable = true,
                       .primary = {.weaponPresent = true,
                                   .digitStateAvailable = true,
                                   .digitState = initialHundred,
                                   .quantizedAmmo = 42,
                                   .iconCatalogMatch = true,
                                   .iconTextureAvailable = true,
                                   .iconTextureIndex = weaponIcon->iconIndex,
                                   .quantizedStatusIndex = 0},
                       .secondary = {.weaponPresent = true,
                                     .digitStateAvailable = true,
                                     .digitState = initialHundred,
                                     .quantizedAmmo = 7,
                                     .iconCatalogMatch = true,
                                     .iconTextureAvailable = true,
                                     .iconTextureIndex = weaponIcon->iconIndex,
                                     .quantizedStatusIndex = 4}},
      .healthGauge = {.displayedHealth = 50.0F,
                      .maximumHealth = 100.0F,
                      .armourMeterTextureAvailable = true,
                      .armourTextureAvailable = true},
      .identityStatus = {.aircraftIconCatalogMatch = true,
                         .aircraftIconTextureAvailable = true,
                         .aircraftIconTextureIndex = aircraftIcon->iconIndex,
                         .healthDigitStateAvailable = true,
                         .healthDigitState = initialHundred,
                         .quantizedHealthPercent = 50,
                         .teamId = 1,
                         .technologyStarTextureAvailable = true,
                         .technologyCrossTextureAvailable = true,
                         .technologyDigitStateAvailable = true,
                         .technologyDigitState = initialZero,
                         .quantizedTechnologyLevel = 3,
                         .rollingDigitAtlasAvailable = true},
  };
}

[[nodiscard]] LegacyAircraftHudRenderEventResult build(
    LegacyAircraftHudElapsedClockState &clock,
    const LegacyAircraftHudRenderEventInput &input, const Owners &owners,
    const LoadedLegacyAircraftHealthGaugeTextureSet *healthOverride = nullptr) {
  return buildLegacyAircraftHudRenderEvent(
      clock, input, owners.instruments, owners.digits, owners.weapons,
      healthOverride == nullptr ? owners.health : *healthOverride,
      owners.identity, nativeLayout(), 100.0F);
}

void testCompleteEventCommitsAtomically() {
  PackFixture fixture;
  auto session = fixture.open();
  const auto owners = loadOwners(session);
  auto input = representativeInput(owners);
  LegacyAircraftHudElapsedClockState clock;
  require(clock.advance(0.25F).advanced(),
          "complete event could not establish elapsed time");

  const auto result = build(clock, input, owners);
  require(result.ready() && result.event->belongsTo(
                                owners.instruments, owners.digits,
                                owners.weapons, owners.health, owners.identity),
          "complete authenticated HUD event did not publish");
  const auto &event = *result.event;
  require(event.stageTransactionToken != 0U &&
              event.accumulatedElapsedSeconds == 0.25F &&
              event.instruments.commandCount != 0U &&
              event.instrumentReadouts.readoutCount == 2U &&
              event.weaponPanels.commandCount == 14U &&
              event.healthGauge.commandCount != 0U &&
              event.identityStatus.commandCount == 10U &&
              event.totalCommandCount() ==
                  event.instruments.commandCount + 8U +
                      event.weaponPanels.commandCount +
                      event.healthGauge.commandCount +
                      event.identityStatus.commandCount,
          "full HUD event lost native subsection order or command accounting");
  const auto &updates = *result.stateUpdates;
  require(updates.rightInstrumentReadoutAvailable &&
              updates.leftInstrumentReadoutAvailable &&
              updates.primaryWeaponAmmoAvailable &&
              updates.secondaryWeaponAmmoAvailable &&
              updates.healthPercentAvailable &&
              updates.technologyLevelAvailable &&
              updates.rightInstrumentReadout.cachedElapsedSeconds == 0.25F &&
              updates.leftInstrumentReadout.cachedElapsedSeconds == 0.25F &&
              updates.primaryWeaponAmmo.cachedElapsedSeconds == 0.25F &&
              updates.secondaryWeaponAmmo.cachedElapsedSeconds == 0.25F &&
              updates.healthPercent.cachedElapsedSeconds == 0.25F &&
              updates.technologyLevel.cachedElapsedSeconds == 0.25F,
          "six rolling displays did not consume one elapsed snapshot");
  require(!clock.stageInProgress() &&
              std::bit_cast<std::uint32_t>(clock.accumulatedElapsedSeconds()) ==
                  0U,
          "complete event did not commit exact positive-zero clock reset");
}

void testGateAndLatePlanFailuresRetainEverything() {
  PackFixture fixture;
  auto session = fixture.open();
  const auto owners = loadOwners(session);
  auto input = representativeInput(owners);
  LegacyAircraftHudElapsedClockState clock;
  require(clock.advance(0.5F).advanced(),
          "failure test could not establish elapsed time");

  input.gates.activeWindowPresent = true;
  auto result = build(clock, input, owners);
  require(
      result.status == LegacyAircraftHudRenderEventStatus::clockBeginRejected &&
          result.clockBeginStatus ==
              LegacyAircraftHudElapsedClockBeginStatus::activeWindowPresent &&
          !result.event.has_value() && !result.stateUpdates.has_value() &&
          clock.accumulatedElapsedSeconds() == 0.5F && !clock.stageInProgress(),
      "rejected native gate consumed time or published partial state");

  input = representativeInput(owners);
  input.identityStatus.healthDigitState.retentionFactorValid = true;
  input.identityStatus.healthDigitState.cachedElapsedSeconds = 0.5F;
  input.identityStatus.healthDigitState.retentionFactor =
      std::numeric_limits<float>::quiet_NaN();
  result = build(clock, input, owners);
  require(
      result.status ==
              LegacyAircraftHudRenderEventStatus::identityStatusPlanRejected &&
          result.clockEndStatus ==
              LegacyAircraftHudElapsedClockEndStatus::aborted &&
          !result.event.has_value() && !result.stateUpdates.has_value() &&
          clock.accumulatedElapsedSeconds() == 0.5F && !clock.stageInProgress(),
      "late plan failure published state or consumed the clock");
}

void testCrossTransactionOwnerFailsOnlyAtEventBoundary() {
  PackFixture fixture;
  auto session = fixture.open();
  const auto owners = loadOwners(session);
  auto otherSession = fixture.open();
  const auto otherOwners = loadOwners(otherSession);
  require(owners.health.revision == otherOwners.health.revision &&
              owners.health.transactionIdentity !=
                  otherOwners.health.transactionIdentity,
          "cross-transaction fixture did not isolate identity");
  LegacyAircraftHudElapsedClockState clock;
  require(clock.advance(0.75F).advanced(),
          "owner mismatch test could not establish elapsed time");

  const auto result =
      build(clock, representativeInput(owners), owners, &otherOwners.health);
  require(result.status ==
                  LegacyAircraftHudRenderEventStatus::eventValidationFailed &&
              result.clockEndStatus ==
                  LegacyAircraftHudElapsedClockEndStatus::aborted &&
              !result.event.has_value() && !result.stateUpdates.has_value() &&
              clock.accumulatedElapsedSeconds() == 0.75F &&
              !clock.stageInProgress(),
          "mixed authenticated transactions crossed the atomic event boundary");
}

void testForgeryAndReplayAreRejected() {
  PackFixture fixture;
  auto session = fixture.open();
  const auto owners = loadOwners(session);
  LegacyAircraftHudElapsedClockState clock;
  require(clock.advance(0.1F).advanced(),
          "forgery test could not establish first elapsed time");
  const auto first = build(clock, representativeInput(owners), owners);
  require(first.ready(), "forgery baseline event did not build");

  auto forged = *first.event;
  forged.stageTransactionToken = 0U;
  require(!forged.belongsTo(owners.instruments, owners.digits, owners.weapons,
                            owners.health, owners.identity),
          "zero transaction token reached a backend");
  forged = *first.event;
  std::swap(forged.weaponPanels.orderedCommands[1U],
            forged.weaponPanels.orderedCommands[2U]);
  require(!forged.belongsTo(owners.instruments, owners.digits, owners.weapons,
                            owners.health, owners.identity),
          "reordered nested HUD commands reached a backend");

  require(clock.advance(0.1F).advanced(),
          "forgery test could not establish second elapsed time");
  const auto second = build(clock, representativeInput(owners), owners);
  require(second.ready() && second.event->stageTransactionToken !=
                                first.event->stageTransactionToken,
          "successive complete HUD events reused a transaction token");
}

} // namespace

int main() {
  try {
    testCompleteEventCommitsAtomically();
    testGateAndLatePlanFailuresRetainEverything();
    testCrossTransactionOwnerFailsOnlyAtEventBoundary();
    testForgeryAndReplayAreRejected();
    std::cout << "Legacy aircraft HUD render-event tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft HUD render-event tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
