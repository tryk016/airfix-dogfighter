#include "airfix/texture/TextureModeMissionReload.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using airfix::texture::ActiveMissionTextureState;
using airfix::texture::TextureMode;
using airfix::texture::TextureModeMissionReloadCallback;
using airfix::texture::TextureModeMissionReloadCoordinator;
using airfix::texture::TextureModeMissionReloadStatus;
using airfix::texture::TexturePackageAvailability;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

struct ReloadProbe final {
  bool succeed{true};
  std::uint32_t calls{};
  TextureMode lastMode{TextureMode::classic};
};

bool reload(void *const context, const TextureMode mode) noexcept {
  auto &probe = *static_cast<ReloadProbe *>(context);
  ++probe.calls;
  probe.lastMode = mode;
  return probe.succeed;
}

[[nodiscard]] TextureModeMissionReloadCoordinator
coordinator(ReloadProbe &probe,
            const ActiveMissionTextureState active = {},
            const TexturePackageAvailability availability =
                TexturePackageAvailability::ready) {
  auto result = TextureModeMissionReloadCoordinator::create(
      active, availability,
      TextureModeMissionReloadCallback{.function = &reload, .context = &probe});
  require(result.has_value(), "valid mission reload coordinator was rejected");
  return *result;
}

void testInvalidConstructionFailsClosed() {
  ReloadProbe probe;
  require(!TextureModeMissionReloadCoordinator::create(
               ActiveMissionTextureState{
                   .requestedMode = TextureMode::classic,
                   .effectiveMode = TextureMode::enhanced,
               },
               TexturePackageAvailability::ready,
               {.function = &reload, .context = &probe})
               .has_value(),
          "impossible active mission created a coordinator");
  require(!TextureModeMissionReloadCoordinator::create(
               {}, TexturePackageAvailability::ready, {})
               .has_value(),
          "missing reload callback created a coordinator");
  require(!TextureModeMissionReloadCoordinator::create(
               {}, static_cast<TexturePackageAvailability>(0xFFU),
               {.function = &reload, .context = &probe})
               .has_value(),
          "forged package availability created a coordinator");
}

void testUnchangedRequestDoesNotReload() {
  ReloadProbe probe;
  auto owner = coordinator(probe);
  const auto outcome = owner.request(TextureMode::classic);
  require(outcome.accepted() &&
              outcome.status == TextureModeMissionReloadStatus::unchanged &&
              probe.calls == 0U &&
              outcome.activeMission == ActiveMissionTextureState{} &&
              !outcome.requestedState.missionReloadRequired,
          "unchanged Classic request invoked a reload");
}

void testSuccessfulPublicationAdvancesOnlyAfterCallback() {
  ReloadProbe probe;
  auto owner = coordinator(probe);
  const auto outcome = owner.request(TextureMode::enhanced);
  const ActiveMissionTextureState expected{
      .requestedMode = TextureMode::enhanced,
      .effectiveMode = TextureMode::enhanced,
  };
  require(outcome.accepted() &&
              outcome.status == TextureModeMissionReloadStatus::reloaded &&
              probe.calls == 1U && probe.lastMode == TextureMode::enhanced &&
              outcome.activeMission == expected &&
              owner.activeMission() == expected &&
              !outcome.requestedState.missionReloadRequired,
          "successful Enhanced publication did not advance exact state");

  const auto classic = owner.request(TextureMode::classic);
  require(classic.accepted() && probe.calls == 2U &&
              probe.lastMode == TextureMode::classic &&
              owner.activeMission() == ActiveMissionTextureState{},
          "Enhanced-to-Classic publication did not replace the mission");
}

void testFailurePreservesActiveMissionAndCanRetry() {
  ReloadProbe probe{.succeed = false};
  auto owner = coordinator(probe);
  const auto failed = owner.request(TextureMode::enhanced);
  require(!failed.accepted() &&
              failed.status == TextureModeMissionReloadStatus::reloadFailed &&
              failed.requestedState.missionReloadRequired &&
              failed.activeMission == ActiveMissionTextureState{} &&
              owner.activeMission() == ActiveMissionTextureState{} &&
              probe.calls == 1U && probe.lastMode == TextureMode::enhanced,
          "failed reload partially advanced active mission state");

  probe.succeed = true;
  const auto retried = owner.request(TextureMode::enhanced);
  require(retried.accepted() &&
              retried.status == TextureModeMissionReloadStatus::reloaded &&
              probe.calls == 2U &&
              owner.activeMission().effectiveMode == TextureMode::enhanced,
          "failed reload could not be retried transactionally");
}

void testFallbackPublicationUsesClassic() {
  ReloadProbe probe;
  auto owner = coordinator(probe, {},
                           TexturePackageAvailability::notConfigured);
  const auto outcome = owner.request(TextureMode::enhanced);
  require(outcome.accepted() &&
              outcome.status == TextureModeMissionReloadStatus::reloaded &&
              probe.calls == 1U && probe.lastMode == TextureMode::classic &&
              owner.activeMission() ==
                  ActiveMissionTextureState{
                      .requestedMode = TextureMode::enhanced,
                      .effectiveMode = TextureMode::classic,
                  } &&
              outcome.requestedState.fallbackToClassic(),
          "unavailable package did not publish a complete Classic fallback");
}

void testPackageLossReplacesActiveEnhancedMissionWithClassic() {
  ReloadProbe probe;
  auto owner = coordinator(
      probe,
      ActiveMissionTextureState{
          .requestedMode = TextureMode::enhanced,
          .effectiveMode = TextureMode::enhanced,
      },
      TexturePackageAvailability::unavailable);
  const auto outcome = owner.request(TextureMode::enhanced);
  require(outcome.accepted() && probe.calls == 1U &&
              probe.lastMode == TextureMode::classic &&
              owner.activeMission().requestedMode == TextureMode::enhanced &&
              owner.activeMission().effectiveMode == TextureMode::classic,
          "package loss left an active Enhanced mission published");
}

void testForgedRequestDoesNotCallProduct() {
  ReloadProbe probe;
  auto owner = coordinator(probe);
  const auto outcome = owner.request(static_cast<TextureMode>(0xFFU));
  require(!outcome.accepted() &&
              outcome.status == TextureModeMissionReloadStatus::invalidState &&
              probe.calls == 0U && owner.activeMission() ==
                                       ActiveMissionTextureState{},
          "forged request reached the product callback");
}

} // namespace

int main() {
  try {
    testInvalidConstructionFailsClosed();
    testUnchangedRequestDoesNotReload();
    testSuccessfulPublicationAdvancesOnlyAfterCallback();
    testFailurePreservesActiveMissionAndCanRetry();
    testFallbackPublicationUsesClassic();
    testPackageLossReplacesActiveEnhancedMissionWithClassic();
    testForgedRequestDoesNotCallProduct();
    std::cout << "Texture mode mission reload tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Texture mode mission reload tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
