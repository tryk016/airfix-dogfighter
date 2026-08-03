#include "airfix/crypto/Sha256.hpp"
#include "airfix/io/DurableFile.hpp"
#include "airfix/render/RenderPresentationSettings.hpp"
#include "airfix/settings/RenderPresentationSettingsCodec.hpp"
#include "airfix/settings/RenderPresentationSettingsStore.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view currentName = "render-presentation.afrs";
constexpr std::string_view backupName = "render-presentation.afrs.backup";

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TempDirectory final {
public:
  TempDirectory() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0U; attempt < 100U; ++attempt) {
      root_ = std::filesystem::temp_directory_path() /
              ("airfix-settings-store-" + std::to_string(stamp) + "-" +
               std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(root_, error)) {
        settings_ = root_ / "settings";
        return;
      }
      if (error && error != std::errc::file_exists) {
        throw std::runtime_error("cannot create settings test root");
      }
    }
    throw std::runtime_error("cannot allocate settings test root");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  [[nodiscard]] const std::filesystem::path &settings() const {
    return settings_;
  }

  [[nodiscard]] std::filesystem::path current() const {
    return settings_ / currentName;
  }

  [[nodiscard]] std::filesystem::path backup() const {
    return settings_ / backupName;
  }

private:
  std::filesystem::path root_;
  std::filesystem::path settings_;
};

[[nodiscard]] airfix::render::RenderPresentationSettings
settings(const float scale, const bool diagnostics = false) {
  return {
      .renderScalePercent = scale,
      .scenePresentation =
          airfix::render::ScenePresentationMode::widescreenHorPlus,
      .visualProfile = airfix::render::VisualProfile::classic,
      .diagnosticsOverlayEnabled = diagnostics,
  };
}

[[nodiscard]] std::vector<std::uint8_t>
readFile(const std::filesystem::path &path) {
  return airfix::io::readBoundedRegularFile(
      path, airfix::settings::maximumRenderSettingsDocumentBytes);
}

void writeRaw(const std::filesystem::path &path,
              const std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output || (!bytes.empty() &&
                  !output.write(reinterpret_cast<const char *>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size())))) {
    throw std::runtime_error("cannot write settings fixture");
  }
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

void repairFutureEnvelope(std::vector<std::uint8_t> &bytes) {
  putU32(bytes, 8U, 5U);
  repairEnvelope(bytes);
}

void requireStoreError(
    const std::function<void()> &action,
    const airfix::settings::RenderSettingsStoreErrorKind kind,
    const std::filesystem::path &privatePath) {
  try {
    action();
  } catch (const airfix::settings::RenderSettingsStoreError &error) {
    require(error.kind() == kind, "store error kind mismatch");
    require(std::string(error.what()).find(privatePath.string()) ==
                std::string::npos,
            "store error leaked a host path");
    return;
  }
  throw std::runtime_error("expected settings store error");
}

void testMissingSaveAndRotation() {
  TempDirectory temporary;
  const auto missing =
      airfix::settings::loadRenderPresentationSettings(temporary.settings());
  require(
      missing.source == airfix::settings::RenderSettingsLoadSource::defaults &&
          missing.settings == settings(100.0F) && !missing.persistenceBlocked,
      "missing store did not select canonical defaults");

  const auto first = settings(75.0F);
  const auto firstSave = airfix::settings::saveRenderPresentationSettings(
      temporary.settings(), first);
  require(firstSave.status ==
                  airfix::settings::RenderSettingsSaveStatus::committed &&
              !firstSave.backupRotated,
          "first settings save had the wrong result");
  require(airfix::settings::loadRenderPresentationSettings(temporary.settings())
                  .settings == first,
          "saved settings did not survive a restart");

  const auto unchanged = airfix::settings::saveRenderPresentationSettings(
      temporary.settings(), first);
  require(unchanged.status ==
                  airfix::settings::RenderSettingsSaveStatus::unchanged &&
              !unchanged.backupRotated,
          "idempotent save rewrote settings");

  const auto second = settings(150.0F, true);
  const auto secondSave = airfix::settings::saveRenderPresentationSettings(
      temporary.settings(), second);
  require(secondSave.backupRotated,
          "changed settings did not rotate the valid current");
  require(airfix::settings::loadRenderPresentationSettings(temporary.settings())
                  .settings == second,
          "rotated settings did not publish the candidate");

  auto corrupt = readFile(temporary.current());
  corrupt.back() ^= 1U;
  writeRaw(temporary.current(), corrupt);
  const auto recovered =
      airfix::settings::loadRenderPresentationSettings(temporary.settings());
  require(recovered.source ==
                  airfix::settings::RenderSettingsLoadSource::backup &&
              recovered.settings == first && recovered.recoveredFromBackup(),
          "corrupt current did not recover its validated backup");
}

void testMalformedAndFuturePreservation() {
  TempDirectory temporary;
  (void)airfix::settings::saveRenderPresentationSettings(temporary.settings(),
                                                         settings(80.0F));
  writeRaw(temporary.current(),
           std::array<std::uint8_t, 4U>{'B', 'A', 'D', '!'});
  const auto replacement = settings(125.0F);
  const auto saved = airfix::settings::saveRenderPresentationSettings(
      temporary.settings(), replacement);
  require(
      !saved.backupRotated &&
          airfix::settings::loadRenderPresentationSettings(temporary.settings())
                  .settings == replacement,
      "malformed current was promoted or not replaced");

  const auto futureBase = settings(130.0F);
  const auto futureBaseSaved = airfix::settings::saveRenderPresentationSettings(
      temporary.settings(), futureBase);
  require(futureBaseSaved.backupRotated,
          "future-schema fixture did not retain an older valid backup");
  auto future = readFile(temporary.current());
  repairFutureEnvelope(future);
  writeRaw(temporary.current(), future);
  const auto loaded =
      airfix::settings::loadRenderPresentationSettings(temporary.settings());
  require(loaded.source ==
                  airfix::settings::RenderSettingsLoadSource::defaults &&
              loaded.persistenceBlocked &&
              loaded.current.status ==
                  airfix::settings::RenderSettingsFileStatus::futureSchema,
          "future current was interpreted, fell back to the old backup, "
          "or did not block writes");
  const auto before = readFile(temporary.current());
  requireStoreError(
      [&] {
        (void)airfix::settings::saveRenderPresentationSettings(
            temporary.settings(), settings(90.0F));
      },
      airfix::settings::RenderSettingsStoreErrorKind::persistenceBlocked,
      temporary.settings());
  require(readFile(temporary.current()) == before,
          "downgrade save rewrote a future schema");
}

void testLegacySchemasMigrateToCurrentDefaults() {
  TempDirectory temporary;
  const auto legacySettings = settings(135.0F, true);
  (void)airfix::settings::saveRenderPresentationSettings(temporary.settings(),
                                                         legacySettings);

  auto legacy = readFile(temporary.current());
  legacy.resize(71U);
  putU32(legacy, 8U, 1U);
  repairEnvelope(legacy);
  writeRaw(temporary.current(), legacy);

  const auto loaded =
      airfix::settings::loadRenderPresentationSettings(temporary.settings());
  require(loaded.source ==
                  airfix::settings::RenderSettingsLoadSource::current &&
              loaded.settings == legacySettings &&
              loaded.settings.verticalFovAdjustmentDegrees == 0.0F &&
              !loaded.persistenceBlocked,
          "legacy schema did not migrate to a safe default FOV");

  const auto saved = airfix::settings::saveRenderPresentationSettings(
      temporary.settings(), loaded.settings);
  require(saved.backupRotated && readFile(temporary.current()).size() == 92U &&
              readFile(temporary.backup()) == legacy,
          "saving migrated settings did not preserve and replace schema 1");

  TempDirectory safeFovTemporary;
  auto safeFovSettings = settings(145.0F, true);
  safeFovSettings.verticalFovAdjustmentDegrees = 12.0F;
  safeFovSettings.uiScalePercent = 125.0F;
  (void)airfix::settings::saveRenderPresentationSettings(
      safeFovTemporary.settings(), safeFovSettings);

  auto safeFovSchema = readFile(safeFovTemporary.current());
  safeFovSchema.resize(79U);
  putU32(safeFovSchema, 8U, 2U);
  repairEnvelope(safeFovSchema);
  writeRaw(safeFovTemporary.current(), safeFovSchema);

  const auto safeFovLoaded = airfix::settings::loadRenderPresentationSettings(
      safeFovTemporary.settings());
  require(safeFovLoaded.source ==
                  airfix::settings::RenderSettingsLoadSource::current &&
              safeFovLoaded.settings.verticalFovAdjustmentDegrees == 12.0F &&
              safeFovLoaded.settings.uiScalePercent == 100.0F &&
              safeFovLoaded.settings.textureMode ==
                  airfix::texture::TextureMode::classic &&
              !safeFovLoaded.persistenceBlocked,
          "schema 2 did not retain FOV and default UI scale");

  TempDirectory uiScaleTemporary;
  auto uiScaleSettings = settings(155.0F, true);
  uiScaleSettings.verticalFovAdjustmentDegrees = 8.0F;
  uiScaleSettings.uiScalePercent = 140.0F;
  uiScaleSettings.textureMode = airfix::texture::TextureMode::enhanced;
  (void)airfix::settings::saveRenderPresentationSettings(
      uiScaleTemporary.settings(), uiScaleSettings);

  auto uiScaleSchema = readFile(uiScaleTemporary.current());
  uiScaleSchema.resize(87U);
  putU32(uiScaleSchema, 8U, 3U);
  repairEnvelope(uiScaleSchema);
  writeRaw(uiScaleTemporary.current(), uiScaleSchema);

  const auto uiScaleLoaded = airfix::settings::loadRenderPresentationSettings(
      uiScaleTemporary.settings());
  require(uiScaleLoaded.source ==
                  airfix::settings::RenderSettingsLoadSource::current &&
              uiScaleLoaded.settings.verticalFovAdjustmentDegrees == 8.0F &&
              uiScaleLoaded.settings.uiScalePercent == 140.0F &&
              uiScaleLoaded.settings.textureMode ==
                  airfix::texture::TextureMode::classic &&
              !uiScaleLoaded.persistenceBlocked,
          "schema 3 did not retain UI scale and default TextureMode");
}

struct ReplaceHookContext final {
  enum class Mode {
    throwBefore,
    replaceThenThrow,
  };

  Mode mode{Mode::throwBefore};
  bool throwOnCurrentOnly{};
};

void replaceHook(const std::filesystem::path &prepared,
                 const std::filesystem::path &target, void *const opaque) {
  auto &context = *static_cast<ReplaceHookContext *>(opaque);
  const bool current = target.filename() == currentName;
  if (context.throwOnCurrentOnly && !current) {
    airfix::io::replaceFileDurable(prepared, target);
    return;
  }
  if (context.mode == ReplaceHookContext::Mode::replaceThenThrow) {
    airfix::io::replaceFileDurable(prepared, target);
  }
  throw std::runtime_error("synthetic replacement failure");
}

void throwRetry(const std::filesystem::path &, const std::filesystem::path &,
                void *) {
  throw std::runtime_error("synthetic durability retry failure");
}

void testFailureAndAmbiguousReadback() {
  TempDirectory temporary;
  const auto first = settings(100.0F);
  (void)airfix::settings::saveRenderPresentationSettings(temporary.settings(),
                                                         first);

  ReplaceHookContext before{
      .mode = ReplaceHookContext::Mode::throwBefore,
      .throwOnCurrentOnly = true,
  };
  const airfix::settings::testing::RenderSettingsStoreHooks beforeHooks{
      .replace = replaceHook,
      .context = &before,
  };
  requireStoreError(
      [&] {
        (void)
            airfix::settings::testing::saveRenderPresentationSettingsWithHooks(
                temporary.settings(), settings(120.0F), beforeHooks);
      },
      airfix::settings::RenderSettingsStoreErrorKind::saveFailed,
      temporary.settings());
  require(airfix::settings::loadRenderPresentationSettings(temporary.settings())
                  .settings == first,
          "pre-rename failure changed active settings");

  ReplaceHookContext after{
      .mode = ReplaceHookContext::Mode::replaceThenThrow,
      .throwOnCurrentOnly = true,
  };
  const airfix::settings::testing::RenderSettingsStoreHooks afterHooks{
      .replace = replaceHook,
      .context = &after,
  };
  const auto confirmed =
      airfix::settings::testing::saveRenderPresentationSettingsWithHooks(
          temporary.settings(), settings(140.0F), afterHooks);
  require(
      confirmed.status ==
          airfix::settings::RenderSettingsSaveStatus::committedAfterReadback,
      "post-rename error was not confirmed by exact readback");

  const airfix::settings::testing::RenderSettingsStoreHooks unknownHooks{
      .replace = replaceHook,
      .retryDurability = throwRetry,
      .context = &after,
  };
  requireStoreError(
      [&] {
        (void)
            airfix::settings::testing::saveRenderPresentationSettingsWithHooks(
                temporary.settings(), settings(160.0F), unknownHooks);
      },
      airfix::settings::RenderSettingsStoreErrorKind::commitUnknown,
      temporary.settings());
}

void testWrongTypesBlockPersistence() {
  TempDirectory temporary;
  std::filesystem::create_directory(temporary.settings());
  std::filesystem::create_directory(temporary.current());
  const auto loaded =
      airfix::settings::loadRenderPresentationSettings(temporary.settings());
  require(
      loaded.current.status ==
              airfix::settings::RenderSettingsFileStatus::wrongTypeOrLinked &&
          loaded.persistenceBlocked,
      "wrong-type current did not fail closed");
  requireStoreError(
      [&] {
        (void)airfix::settings::saveRenderPresentationSettings(
            temporary.settings(), settings(110.0F));
      },
      airfix::settings::RenderSettingsStoreErrorKind::persistenceBlocked,
      temporary.settings());
}

void testOversizedAndLinkedRecords() {
  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.settings());
    const std::vector<std::uint8_t> oversized(
        airfix::settings::maximumRenderSettingsDocumentBytes + 1U, 0xA5U);
    writeRaw(temporary.current(), oversized);
    const auto loaded =
        airfix::settings::loadRenderPresentationSettings(temporary.settings());
    require(loaded.current.status ==
                    airfix::settings::RenderSettingsFileStatus::oversized &&
                loaded.source ==
                    airfix::settings::RenderSettingsLoadSource::defaults &&
                !loaded.persistenceBlocked,
            "oversized regular current did not select safe defaults");
    (void)airfix::settings::saveRenderPresentationSettings(temporary.settings(),
                                                           settings(115.0F));
    require(
        airfix::settings::loadRenderPresentationSettings(temporary.settings())
                .settings == settings(115.0F),
        "oversized current could not be replaced safely");
  }

  {
    TempDirectory temporary;
    (void)airfix::settings::saveRenderPresentationSettings(temporary.settings(),
                                                           settings(90.0F));
    std::error_code error;
    std::filesystem::create_hard_link(
        temporary.current(), temporary.settings() / "linked-alias.afrs", error);
    require(!error, "cannot create settings hard-link fixture");
    const auto loaded =
        airfix::settings::loadRenderPresentationSettings(temporary.settings());
    require(
        loaded.current.status ==
                airfix::settings::RenderSettingsFileStatus::wrongTypeOrLinked &&
            loaded.persistenceBlocked,
        "linked current did not block persistence");
    requireStoreError(
        [&] {
          (void)airfix::settings::saveRenderPresentationSettings(
              temporary.settings(), settings(130.0F));
        },
        airfix::settings::RenderSettingsStoreErrorKind::persistenceBlocked,
        temporary.settings());
  }
}

void testLinkedSettingsDirectory() {
  TempDirectory temporary;
  const auto target = temporary.settings().parent_path() / "settings-target";
  require(std::filesystem::create_directory(target),
          "cannot create linked settings target");
  const auto record =
      airfix::render::makeRenderPresentationSettingsRecord(settings(135.0F));
  require(record.complete(), "linked settings fixture is invalid");
  const auto bytes =
      airfix::settings::encodeRenderSettingsDocument(*record.record);
  writeRaw(target / currentName, bytes);

  std::error_code error;
  std::filesystem::create_directory_symlink(target, temporary.settings(),
                                            error);
  if (error) {
    // Windows may require Developer Mode for this fixture. POSIX CI always
    // exercises the rejection; DurableFile separately covers final-file
    // reparse points on Windows when the host permits their creation.
    return;
  }
  const auto loaded =
      airfix::settings::loadRenderPresentationSettings(temporary.settings());
  require(
      loaded.source == airfix::settings::RenderSettingsLoadSource::defaults &&
          loaded.current.status ==
              airfix::settings::RenderSettingsFileStatus::wrongTypeOrLinked &&
          loaded.persistenceBlocked,
      "linked settings directory was followed during load");
  requireStoreError(
      [&] {
        (void)airfix::settings::saveRenderPresentationSettings(
            temporary.settings(), settings(145.0F));
      },
      airfix::settings::RenderSettingsStoreErrorKind::invalidDirectory,
      temporary.settings());
}

} // namespace

int main() {
  try {
    testMissingSaveAndRotation();
    testMalformedAndFuturePreservation();
    testLegacySchemasMigrateToCurrentDefaults();
    testFailureAndAmbiguousReadback();
    testWrongTypesBlockPersistence();
    testOversizedAndLinkedRecords();
    testLinkedSettingsDirectory();
    std::cout << "Render presentation settings store tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Render presentation settings store tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
