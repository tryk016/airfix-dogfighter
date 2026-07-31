#include "airfix/content/LegacyWeaponCrosshairBinding.hpp"
#include "airfix/content/LegacyWeaponCrosshairSpriteSubmission.hpp"
#include "airfix/content/LegacyWeaponCrosshairTextureSet.hpp"
#include "support/SyntheticContent.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using airfix::content::ContentRevision;
using airfix::content::LegacyWeaponCrosshairBindingStatus;
using airfix::content::LegacyWeaponCrosshairBlendMode;
using airfix::content::LegacyWeaponCrosshairCompositionStatus;
using airfix::content::LegacyWeaponCrosshairDepthMode;
using airfix::content::LegacyWeaponCrosshairRenderEventState;
using airfix::content::LegacyWeaponCrosshairRenderSlot;
using airfix::content::LegacyWeaponCrosshairSpriteSubmissionStatus;
using airfix::content::LegacyWeaponCrosshairTextureLoadIssueKind;
using airfix::content::LegacyWeaponCrosshairTextureLoadLimits;
using airfix::content::LegacyWeaponCrosshairTextureLoadPhase;
using airfix::content::LegacyWeaponCrosshairTextureLoadResult;
using airfix::content::LegacyWeaponCrosshairTextureRole;
using airfix::content::LegacyWeaponCrosshairVisibilityDecision;
using airfix::content::VerifiedContentSession;
using airfix::simulation::LegacyWeaponSightRole;
using airfix::simulation::LegacyWeaponTypeId;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::array<std::string_view, 3U> logicalPaths{{
    "Graphics\\Ingame\\HUD\\MGsight.gti",
    "Graphics\\Ingame\\HUD\\ROsight.gti",
    "Graphics\\Ingame\\HUD\\BOsight.gti",
}};

constexpr std::array<LegacyWeaponCrosshairTextureRole, 3U> roles{{
    LegacyWeaponCrosshairTextureRole::machineGun,
    LegacyWeaponCrosshairTextureRole::rocket,
    LegacyWeaponCrosshairTextureRole::bomb,
}};

struct ExpectedWeaponSight final {
  LegacyWeaponTypeId type;
  std::string_view registeredName;
  std::optional<LegacyWeaponSightRole> role;
};

constexpr std::array<ExpectedWeaponSight,
                     airfix::simulation::legacyWeaponTypeCount>
    expectedWeaponSights{{
        {LegacyWeaponTypeId::machineGun, "WpMGun",
         LegacyWeaponSightRole::machineGun},
        {LegacyWeaponTypeId::rocket, "WpRocket", LegacyWeaponSightRole::rocket},
        {LegacyWeaponTypeId::bomb, "WpBomb", LegacyWeaponSightRole::bomb},
        {LegacyWeaponTypeId::cannon, "WpCannon", LegacyWeaponSightRole::rocket},
        {LegacyWeaponTypeId::teslaCoil, "WpTeslacoil",
         LegacyWeaponSightRole::rocket},
        {LegacyWeaponTypeId::particleBeam, "WpParticleBeam",
         LegacyWeaponSightRole::rocket},
        {LegacyWeaponTypeId::missile, "WpMissile",
         LegacyWeaponSightRole::rocket},
        {LegacyWeaponTypeId::paraMine, "WpParaMine", std::nullopt},
        {LegacyWeaponTypeId::atomicBomb, "WpABomb",
         LegacyWeaponSightRole::bomb},
    }};

static_assert(noexcept(airfix::content::bindLegacyWeaponCrosshairTexture(
    std::declval<
        const airfix::content::LoadedLegacyWeaponCrosshairTextureSet &>(),
    std::declval<const VerifiedContentSession &>(),
    LegacyWeaponTypeId::machineGun)));

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
    pixels.push_back(0xFFU);
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

[[nodiscard]] Bytes makeIndexedGti() {
  Bytes bytes;
  appendU32(bytes, airfix::assets::kGtiMagic);
  appendU32(bytes, airfix::assets::kGtiVersion);
  appendU32(bytes, airfix::assets::kGtiChecksumChunk);
  appendU32(bytes, 4U);
  appendU32(bytes, 0x33445566U);
  appendU32(bytes, airfix::assets::kGtiImageChunk);
  appendU32(bytes, 20U + 4U + 32U * 32U);
  appendU32(bytes, 3U);
  appendU32(bytes, 32U);
  appendU32(bytes, 32U);
  appendU32(bytes, 1U);
  appendU32(bytes, 1U);
  bytes.insert(bytes.end(), {0x30U, 0x20U, 0x10U, 0xFFU});
  bytes.resize(bytes.size() + 32U * 32U, 0U);
  return bytes;
}

[[nodiscard]] std::vector<UdspInputEntry> representativeEntries() {
  std::vector<UdspInputEntry> entries;
  entries.reserve(logicalPaths.size());
  for (std::size_t index = 0U; index < logicalPaths.size(); ++index) {
    entries.push_back({
        .logicalPath = std::string(logicalPaths[index]),
        .bytes = makeRgba8Gti(32U, 32U,
                              static_cast<std::uint8_t>(0x10U + index * 0x10U),
                              static_cast<std::uint32_t>(0x10203040U + index)),
    });
  }
  return entries;
}

class PackFixture final {
public:
  explicit PackFixture(std::vector<UdspInputEntry> entries)
      : pack_(airfix::testing::makeSyntheticAfPack(entries)),
        path_(workspace_.path() / "private-content.afpack") {
    airfix::testing::detail::writeFile(path_, pack_.bytes);
  }

  [[nodiscard]] VerifiedContentSession
  open(const std::uint64_t generation = 101U) const {
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

void requireAtomicFailure(const LegacyWeaponCrosshairTextureLoadResult &result,
                          const LegacyWeaponCrosshairTextureLoadIssueKind kind,
                          const std::string_view message) {
  require(!result.textures.has_value() && !result.issues.empty() &&
              result.issues.front().kind == kind,
          message);
}

void testLoadsAuthenticatedExactTextureSet() {
  PackFixture fixture(representativeEntries());
  auto session = fixture.open();
  const auto result =
      airfix::content::loadLegacyWeaponCrosshairTextures(session);
  require(result.success(), "representative crosshair textures did not load");
  const auto &loaded = *result.textures;
  require(loaded.valid() && loaded.belongsTo(session) &&
              loaded.decodedRgbaBytes == 3U * 32U * 32U * 4U &&
              loaded.uploadRgbaBytes == loaded.decodedRgbaBytes &&
              loaded.residentRgbaBytes == loaded.decodedRgbaBytes,
          "crosshair set provenance or byte accounting changed");

  auto equalRevisionDifferentHandle = fixture.open();
  require(!loaded.belongsTo(equalRevisionDifferentHandle),
          "crosshair set detached from authenticated stream identity");

  for (std::size_t index = 0U; index < loaded.textures.size(); ++index) {
    const auto &texture = loaded.textures[index];
    const auto *selected = loaded.texture(roles[index]);
    const auto expectedSeed = static_cast<std::uint8_t>(0x10U + index * 0x10U);
    require(selected == &texture && texture.role == roles[index] &&
                texture.textureId.value == index &&
                texture.logicalPath == logicalPaths[index] &&
                texture.upload.format == 8U &&
                texture.upload.checksum == 0x10203040U + index &&
                texture.uploadLevels.size() == 1U &&
                texture.uploadLevels[0].width == 32U &&
                texture.uploadLevels[0].height == 32U &&
                texture.uploadLevels[0].pixels.size() == 4096U &&
                texture.uploadLevels[0].pixels[0] == expectedSeed &&
                texture.uploadLevels[0].pixels[1] == expectedSeed + 1U &&
                texture.uploadLevels[0].pixels[2] == expectedSeed + 2U &&
                texture.uploadLevels[0].pixels[3] == 0xFFU,
            "loaded crosshair role, identity, format or pixels changed");
  }
  require(loaded.texture(
              static_cast<LegacyWeaponCrosshairTextureRole>(0xFFU)) == nullptr,
          "invalid crosshair role selected a texture");

  auto forged = loaded;
  forged.textures[1].logicalPath = logicalPaths[0];
  require(!forged.valid() &&
              forged.texture(LegacyWeaponCrosshairTextureRole::rocket) ==
                  nullptr,
          "forged texture-set metadata remained selectable");
}

void testRecoveredWeaponTypeCatalog() {
  const auto catalog = airfix::simulation::legacyWeaponTypeCatalog();
  require(catalog.size() == expectedWeaponSights.size(),
          "weapon type catalog count changed");
  for (std::size_t index = 0U; index < catalog.size(); ++index) {
    const auto &actual = catalog[index];
    const auto &expected = expectedWeaponSights[index];
    require(actual.id == expected.type &&
                actual.registeredName == expected.registeredName &&
                actual.sightRole == expected.role &&
                airfix::simulation::findLegacyWeaponType(expected.type) ==
                    &actual &&
                airfix::simulation::findLegacyWeaponType(
                    expected.registeredName) == &actual,
            "recovered weapon type or sight mapping changed");
  }
  require(airfix::simulation::findLegacyWeaponType(
              static_cast<LegacyWeaponTypeId>(0xFFU)) == nullptr &&
              airfix::simulation::findLegacyWeaponType("wpmgun") == nullptr &&
              airfix::simulation::findLegacyWeaponType("") == nullptr,
          "invalid or normalized weapon identifier resolved");
}

void testAuthenticatedWeaponTypeBindings() {
  PackFixture fixture(representativeEntries());
  auto session = fixture.open();
  const auto loadedResult =
      airfix::content::loadLegacyWeaponCrosshairTextures(session);
  require(loadedResult.success(), "binding fixture did not load");
  const auto &loaded = *loadedResult.textures;

  for (const auto &expected : expectedWeaponSights) {
    const auto result = airfix::content::bindLegacyWeaponCrosshairTexture(
        loaded, session, expected.type);
    if (!expected.role.has_value()) {
      require(result.status == LegacyWeaponCrosshairBindingStatus::noSight &&
                  !result.binding.has_value(),
              "sightless weapon published a crosshair binding");
      continue;
    }

    const auto *texture = loaded.texture(*expected.role);
    require(texture != nullptr && result.success() &&
                result.binding->weaponType == expected.type &&
                result.binding->role == *expected.role &&
                result.binding->textureId == texture->textureId &&
                result.binding->revision == loaded.revision &&
                result.binding->transactionIdentity ==
                    loaded.transactionIdentity &&
                result.binding->belongsTo(session),
            "weapon type did not bind its authenticated sight");
  }

  const auto invalid = airfix::content::bindLegacyWeaponCrosshairTexture(
      loaded, session, static_cast<LegacyWeaponTypeId>(0xFFU));
  require(invalid.status ==
                  LegacyWeaponCrosshairBindingStatus::invalidWeaponType &&
              !invalid.binding.has_value(),
          "invalid weapon type did not fail closed");

  auto equalRevisionDifferentHandle = fixture.open();
  const auto mismatched = airfix::content::bindLegacyWeaponCrosshairTexture(
      loaded, equalRevisionDifferentHandle, LegacyWeaponTypeId::machineGun);
  require(mismatched.status ==
                  LegacyWeaponCrosshairBindingStatus::sessionIdentityMismatch &&
              !mismatched.binding.has_value(),
          "equal revision with another stream handle was accepted");

  auto forged = loaded;
  forged.textures[0].logicalPath = logicalPaths[1];
  const auto rejected = airfix::content::bindLegacyWeaponCrosshairTexture(
      forged, session, LegacyWeaponTypeId::machineGun);
  require(rejected.status ==
                  LegacyWeaponCrosshairBindingStatus::invalidTextureSet &&
              !rejected.binding.has_value(),
          "forged texture set published a weapon binding");

  const auto noSight = airfix::content::bindLegacyWeaponCrosshairTexture(
      forged, equalRevisionDifferentHandle, LegacyWeaponTypeId::paraMine);
  require(noSight.status == LegacyWeaponCrosshairBindingStatus::noSight &&
              !noSight.binding.has_value(),
          "sightless weapon outcome depended on private texture provenance");
}

void testLookupFailuresAreAtomicAndAttributed() {
  {
    auto entries = representativeEntries();
    entries.erase(entries.begin() + 1);
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result =
        airfix::content::loadLegacyWeaponCrosshairTextures(session);
    requireAtomicFailure(
        result, LegacyWeaponCrosshairTextureLoadIssueKind::textureNotFound,
        "missing sight did not fail atomically");
    require(result.issues.front().role ==
                LegacyWeaponCrosshairTextureRole::rocket,
            "missing sight issue lost its role");
  }
  {
    auto entries = representativeEntries();
    entries.push_back(entries.front());
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result =
        airfix::content::loadLegacyWeaponCrosshairTextures(session);
    requireAtomicFailure(
        result, LegacyWeaponCrosshairTextureLoadIssueKind::textureAmbiguous,
        "ambiguous sight did not fail atomically");
    require(result.issues.front().role ==
                LegacyWeaponCrosshairTextureRole::machineGun,
            "ambiguous sight issue lost its role");
  }
}

void testGtiContractFailuresAreTypedAndAtomic() {
  {
    auto entries = representativeEntries();
    entries[0].bytes = {0x00U, 0x01U, 0x02U};
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result =
        airfix::content::loadLegacyWeaponCrosshairTextures(session);
    requireAtomicFailure(
        result,
        LegacyWeaponCrosshairTextureLoadIssueKind::texturePreparationFailure,
        "malformed GTI did not fail atomically");
    require(result.issues.front().preparationIssue ==
                airfix::render::GtiUploadDataIssueKind::parseFailure,
            "GTI preparation issue was not preserved");
  }
  {
    auto entries = representativeEntries();
    entries[1].bytes = makeIndexedGti();
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result =
        airfix::content::loadLegacyWeaponCrosshairTextures(session);
    requireAtomicFailure(
        result, LegacyWeaponCrosshairTextureLoadIssueKind::unexpectedFormat,
        "non-format-8 sight was accepted");
    require(result.issues.front().role ==
                LegacyWeaponCrosshairTextureRole::rocket,
            "format issue lost its role");
  }
  {
    auto entries = representativeEntries();
    entries[2].bytes = makeRgba8Gti(16U, 32U, 0x44U, 0x99887766U);
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result =
        airfix::content::loadLegacyWeaponCrosshairTextures(session);
    requireAtomicFailure(
        result, LegacyWeaponCrosshairTextureLoadIssueKind::unexpectedDimensions,
        "non-32x32 sight was accepted");
    require(result.issues.front().role ==
                LegacyWeaponCrosshairTextureRole::bomb,
            "dimension issue lost its role");
  }
}

void testBudgetsFailBeforePublication() {
  PackFixture fixture(representativeEntries());
  const auto requireLimit = [&](LegacyWeaponCrosshairTextureLoadLimits limits,
                                const auto expected,
                                const std::string_view message) {
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyWeaponCrosshairTextures(session, limits),
        expected, message);
  };

  {
    LegacyWeaponCrosshairTextureLoadLimits limits;
    limits.maximumSourceBytes = 8U;
    requireLimit(limits,
                 LegacyWeaponCrosshairTextureLoadIssueKind::sourceLimitExceeded,
                 "source output budget was ignored");
  }
  {
    LegacyWeaponCrosshairTextureLoadLimits limits;
    limits.maximumSingleSourceFootprintBytes = 1U;
    requireLimit(
        limits,
        LegacyWeaponCrosshairTextureLoadIssueKind::sourceFootprintLimitExceeded,
        "single-source footprint budget was ignored");
  }
  {
    LegacyWeaponCrosshairTextureLoadLimits limits;
    limits.maximumTotalSourceFootprintBytes = 1U;
    requireLimit(limits,
                 LegacyWeaponCrosshairTextureLoadIssueKind::
                     aggregateSourceFootprintLimitExceeded,
                 "aggregate source budget was ignored");
  }
  {
    LegacyWeaponCrosshairTextureLoadLimits limits;
    limits.maximumDecodedRgbaBytes = 4095U;
    requireLimit(
        limits,
        LegacyWeaponCrosshairTextureLoadIssueKind::decodedRgbaLimitExceeded,
        "decoded RGBA budget was ignored");
  }
  {
    LegacyWeaponCrosshairTextureLoadLimits limits;
    limits.maximumUploadRgbaBytes = 4095U;
    requireLimit(
        limits,
        LegacyWeaponCrosshairTextureLoadIssueKind::uploadRgbaLimitExceeded,
        "upload RGBA budget was ignored");
  }
  {
    LegacyWeaponCrosshairTextureLoadLimits limits;
    limits.maximumResidentRgbaBytes = 4095U;
    requireLimit(
        limits,
        LegacyWeaponCrosshairTextureLoadIssueKind::residentRgbaLimitExceeded,
        "resident RGBA budget was ignored");
  }
  {
    LegacyWeaponCrosshairTextureLoadLimits limits;
    limits.gti.upload.maximumDimension = 31U;
    requireLimit(limits,
                 LegacyWeaponCrosshairTextureLoadIssueKind::invalidLimits,
                 "limits incapable of admitting authentic sights were used");
  }
}

void testCancellationCallbackAndIdentityGuards() {
  PackFixture fixture(representativeEntries());
  {
    auto session = fixture.open();
    std::stop_source source;
    source.request_stop();
    requireAtomicFailure(airfix::content::loadLegacyWeaponCrosshairTextures(
                             session, {}, source.get_token()),
                         LegacyWeaponCrosshairTextureLoadIssueKind::cancelled,
                         "pre-requested cancellation was ignored");
  }
  {
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyWeaponCrosshairTextures(
            session, {}, {},
            [](const auto &) {
              throw std::runtime_error("synthetic callback failure");
            }),
        LegacyWeaponCrosshairTextureLoadIssueKind::progressCallbackFailure,
        "callback failure was not contained");
  }
  {
    auto session = fixture.open(123U);
    auto replacement = fixture.open(123U);
    bool replaced = false;
    const auto result = airfix::content::loadLegacyWeaponCrosshairTextures(
        session, {}, {}, [&](const auto &progress) {
          if (!replaced &&
              progress.phase ==
                  LegacyWeaponCrosshairTextureLoadPhase::lookingUpTextures) {
            session = std::move(replacement);
            replaced = true;
          }
        });
    require(replaced, "identity replacement callback did not run");
    requireAtomicFailure(
        result,
        LegacyWeaponCrosshairTextureLoadIssueKind::sessionIdentityChanged,
        "same-revision authenticated handle replacement escaped detection");
  }
  {
    auto session = fixture.open();
    auto moved = std::move(session);
    requireAtomicFailure(
        airfix::content::loadLegacyWeaponCrosshairTextures(session),
        LegacyWeaponCrosshairTextureLoadIssueKind::sessionIdentityChanged,
        "moved-from authenticated session was accepted");
    require(airfix::content::loadLegacyWeaponCrosshairTextures(moved).success(),
            "moved-to authenticated session became unusable");
  }
}

void testSpriteSubmissionPreservesRecoveredContractAndCallerPolicy() {
  PackFixture fixture(representativeEntries());
  auto session = fixture.open();
  const auto loadedResult =
      airfix::content::loadLegacyWeaponCrosshairTextures(session);
  require(loadedResult.success(), "sprite fixture did not load");
  const auto &loaded = *loadedResult.textures;
  const auto binding = airfix::content::bindLegacyWeaponCrosshairTexture(
      loaded, session, LegacyWeaponTypeId::machineGun);
  require(binding.success(), "sprite fixture did not bind the machine gun");

  const airfix::render::LegacyWeaponCrosshairSpritePlan plan{
      .projectedTarget = {},
      .outputRect = {-7.5F, 102.25F, 64.0F, 48.0F},
      .logicalDistanceScale = 1.5F,
      .insideSceneViewport = false,
      .withinRecoveredDepthRange = true,
  };
  const auto drawn =
      airfix::content::buildLegacyWeaponCrosshairSpriteSubmission(
          plan, *binding.binding,
          LegacyWeaponCrosshairVisibilityDecision::draw);
  require(
      drawn.ready() && drawn.submission->belongsTo(loaded) &&
          drawn.submission->weaponType == LegacyWeaponTypeId::machineGun &&
          drawn.submission->role ==
              LegacyWeaponCrosshairTextureRole::machineGun &&
          drawn.submission->textureId.value == 0U &&
          drawn.submission->outputRect == plan.outputRect &&
          drawn.submission->uv ==
              airfix::content::LegacyWeaponCrosshairUvRect{} &&
          drawn.submission->tintArgb ==
              airfix::content::legacyWeaponCrosshairTintArgb &&
          drawn.submission->blendMode ==
              LegacyWeaponCrosshairBlendMode::sourceAlphaOneMinusSourceAlpha &&
          drawn.submission->depthMode ==
              LegacyWeaponCrosshairDepthMode::alwaysWrite &&
          !drawn.submission->insideSceneViewport &&
          drawn.submission->withinRecoveredDepthRange,
      "ready sprite did not preserve geometry, provenance or recovered state");

  const auto suppressed =
      airfix::content::buildLegacyWeaponCrosshairSpriteSubmission(
          plan, *binding.binding,
          LegacyWeaponCrosshairVisibilityDecision::suppress);
  require(suppressed.status ==
                  LegacyWeaponCrosshairSpriteSubmissionStatus::suppressed &&
              !suppressed.submission.has_value(),
          "explicit caller suppression published a sprite");

  auto forgedBinding = *binding.binding;
  forgedBinding.textureId.value = 2U;
  const auto forged =
      airfix::content::buildLegacyWeaponCrosshairSpriteSubmission(
          plan, forgedBinding, LegacyWeaponCrosshairVisibilityDecision::draw);
  require(forged.status ==
                  LegacyWeaponCrosshairSpriteSubmissionStatus::invalidBinding &&
              !forged.submission.has_value(),
          "role/texture namespace mismatch was accepted");

  auto invalidPlan = plan;
  invalidPlan.outputRect.width = std::numeric_limits<float>::quiet_NaN();
  const auto invalidRectangle =
      airfix::content::buildLegacyWeaponCrosshairSpriteSubmission(
          invalidPlan, *binding.binding,
          LegacyWeaponCrosshairVisibilityDecision::draw);
  require(invalidRectangle.status ==
                  LegacyWeaponCrosshairSpriteSubmissionStatus::
                      invalidOutputRectangle &&
              !invalidRectangle.submission.has_value(),
          "non-finite output rectangle reached a native backend");

  auto otherSession = fixture.open();
  const auto otherLoaded =
      airfix::content::loadLegacyWeaponCrosshairTextures(otherSession);
  require(otherLoaded.success() &&
              !drawn.submission->belongsTo(*otherLoaded.textures),
          "equal revision on another authenticated handle reused a sprite");

  const auto secondaryBinding =
      airfix::content::bindLegacyWeaponCrosshairTexture(
          loaded, session, LegacyWeaponTypeId::rocket);
  require(secondaryBinding.success(),
          "composition fixture did not bind the secondary rocket");
  auto secondaryPlan = plan;
  secondaryPlan.outputRect.x = 300.0F;
  const auto secondary =
      airfix::content::buildLegacyWeaponCrosshairSpriteSubmission(
          secondaryPlan, *secondaryBinding.binding,
          LegacyWeaponCrosshairVisibilityDecision::draw);
  require(secondary.ready(),
          "composition fixture did not build the secondary sprite");

  const LegacyWeaponCrosshairRenderEventState activeRenderEvent{
      .typeRenderEligible = true,
      .vehicleInactive = false,
      .cameraAttached = true,
  };
  const auto composition =
      airfix::content::composeLegacyWeaponCrosshairRenderEvent(
          activeRenderEvent, loaded, secondary.submission, drawn.submission);
  require(composition.ready() && composition.count == 2U &&
              composition.entry(0U) != nullptr &&
              composition.entry(0U)->slot ==
                  LegacyWeaponCrosshairRenderSlot::selectedSecondary &&
              composition.entry(0U)->submission.weaponType ==
                  LegacyWeaponTypeId::rocket &&
              composition.entry(1U) != nullptr &&
              composition.entry(1U)->slot ==
                  LegacyWeaponCrosshairRenderSlot::primary &&
              composition.entry(1U)->submission.weaponType ==
                  LegacyWeaponTypeId::machineGun &&
              composition.entry(2U) == nullptr,
          "event-0x06 composition lost secondary-before-primary order");

  const auto primaryOnly =
      airfix::content::composeLegacyWeaponCrosshairRenderEvent(
          activeRenderEvent, loaded, std::nullopt, drawn.submission);
  require(primaryOnly.ready() && primaryOnly.count == 1U &&
              primaryOnly.entry(0U) != nullptr &&
              primaryOnly.entry(0U)->slot ==
                  LegacyWeaponCrosshairRenderSlot::primary,
          "missing secondary substituted or suppressed the primary slot");

  const auto empty = airfix::content::composeLegacyWeaponCrosshairRenderEvent(
      activeRenderEvent, loaded, std::nullopt, std::nullopt);
  require(empty.ready() && empty.count == 0U && empty.entry(0U) == nullptr,
          "empty native weapon slots did not produce an empty ready plan");

  const auto requireGate = [&](LegacyWeaponCrosshairRenderEventState state,
                               const auto expected,
                               const std::string_view message) {
    const auto gated = airfix::content::composeLegacyWeaponCrosshairRenderEvent(
        state, loaded, secondary.submission, drawn.submission);
    require(gated.status == expected && gated.count == 0U &&
                gated.entry(0U) == nullptr,
            message);
  };
  requireGate({.typeRenderEligible = false,
               .vehicleInactive = true,
               .cameraAttached = false},
              LegacyWeaponCrosshairCompositionStatus::typeNotRenderEligible,
              "type gate did not retain native first-gate precedence");
  requireGate({.typeRenderEligible = true,
               .vehicleInactive = true,
               .cameraAttached = false},
              LegacyWeaponCrosshairCompositionStatus::vehicleInactive,
              "inactive gate did not precede the camera gate");
  requireGate({.typeRenderEligible = true,
               .vehicleInactive = false,
               .cameraAttached = false},
              LegacyWeaponCrosshairCompositionStatus::cameraNotAttached,
              "camera gate published crosshairs");

  auto forgedSubmission = *drawn.submission;
  forgedSubmission.textureId.value = 2U;
  const auto invalid = airfix::content::composeLegacyWeaponCrosshairRenderEvent(
      activeRenderEvent, loaded, secondary.submission, forgedSubmission);
  require(invalid.status ==
                  LegacyWeaponCrosshairCompositionStatus::invalidSubmission &&
              invalid.count == 0U && invalid.entry(0U) == nullptr,
          "forged primary packet caused a partial crosshair publication");
}

} // namespace

int main() {
  try {
    testLoadsAuthenticatedExactTextureSet();
    testRecoveredWeaponTypeCatalog();
    testAuthenticatedWeaponTypeBindings();
    testLookupFailuresAreAtomicAndAttributed();
    testGtiContractFailuresAreTypedAndAtomic();
    testBudgetsFailBeforePublication();
    testCancellationCallbackAndIdentityGuards();
    testSpriteSubmissionPreservesRecoveredContractAndCallerPolicy();
    std::cout << "Legacy weapon crosshair texture-set tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy weapon crosshair texture-set tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
