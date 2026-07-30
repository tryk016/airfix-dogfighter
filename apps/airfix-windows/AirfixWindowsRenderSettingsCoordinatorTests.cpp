#include "AirfixWindowsRenderSettingsCoordinator.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::render::RenderPresentationSettings;
using airfix::render::RenderPresentationSettingsOverride;
using airfix::windows::AirfixWindowsRenderSettingsApplyStatus;
using airfix::windows::AirfixWindowsRenderSettingsCallbacks;
using airfix::windows::AirfixWindowsRenderSettingsCoordinator;
using airfix::windows::AirfixWindowsRenderSettingsPersistenceFailureKind;
using airfix::windows::AirfixWindowsRenderSettingsPersistenceState;
using airfix::windows::AirfixWindowsRenderSettingsReloadResult;
using airfix::windows::AirfixWindowsRenderSettingsReloadStatus;
using airfix::windows::AirfixWindowsRenderSettingsStoreResult;
using airfix::windows::AirfixWindowsRenderSettingsStoreStatus;
using airfix::windows::classifyWindowsRenderSettingsCommitReadback;
using airfix::windows::RenderPresentationSettingsApplyIssueKind;
using airfix::windows::RenderPresentationSettingsApplyResult;
using airfix::windows::RenderPresentationSettingsPublicationGate;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] RenderPresentationSettings
settings(const float scale = 100.0F, const bool diagnostics = false) noexcept {
  RenderPresentationSettings result;
  result.renderScalePercent = scale;
  result.diagnosticsOverlayEnabled = diagnostics;
  return result;
}

enum class Event : std::uint8_t {
  prepare,
  save,
  reload,
  publish,
};

struct FakeDependencies final {
  std::array<Event, 16U> events{};
  std::size_t eventCount{};
  std::size_t rendererCalls{};
  std::size_t saveCalls{};
  std::size_t reloadCalls{};
  std::size_t publishCalls{};
  bool preparationFails{};
  AirfixWindowsRenderSettingsStoreStatus storeStatus{
      AirfixWindowsRenderSettingsStoreStatus::committed};
  AirfixWindowsRenderSettingsReloadStatus reloadStatus{
      AirfixWindowsRenderSettingsReloadStatus::loaded};
  RenderPresentationSettings rendererCandidate;
  RenderPresentationSettings savedCandidate;
  RenderPresentationSettings reloadedCandidate;

  void event(const Event value) noexcept { events[eventCount++] = value; }
};

[[nodiscard]] RenderPresentationSettingsApplyResult
applyRenderer(void *const context, const RenderPresentationSettings &candidate,
              const RenderPresentationSettingsPublicationGate gate) noexcept {
  auto &fake = *static_cast<FakeDependencies *>(context);
  ++fake.rendererCalls;
  fake.rendererCandidate = candidate;
  fake.event(Event::prepare);
  if (fake.preparationFails) {
    return {
        .changed = false,
        .issue =
            RenderPresentationSettingsApplyIssueKind::targetPreparationFailed,
    };
  }
  if (!gate.accepts(candidate)) {
    return {
        .changed = false,
        .issue =
            RenderPresentationSettingsApplyIssueKind::publicationGateRejected,
    };
  }
  fake.event(Event::publish);
  ++fake.publishCalls;
  return {
      .changed = true,
      .issue = std::nullopt,
  };
}

[[nodiscard]] AirfixWindowsRenderSettingsStoreResult
store(void *const context,
      const RenderPresentationSettings &candidate) noexcept {
  auto &fake = *static_cast<FakeDependencies *>(context);
  fake.event(Event::save);
  ++fake.saveCalls;
  fake.savedCandidate = candidate;
  return {
      .status = fake.storeStatus,
  };
}

[[nodiscard]] AirfixWindowsRenderSettingsReloadResult
reload(void *const context) noexcept {
  auto &fake = *static_cast<FakeDependencies *>(context);
  fake.event(Event::reload);
  ++fake.reloadCalls;
  return {
      .status = fake.reloadStatus,
      .settings = fake.reloadedCandidate,
  };
}

[[nodiscard]] AirfixWindowsRenderSettingsCallbacks
callbacks(FakeDependencies &fake) noexcept {
  return {
      .applyRenderer = applyRenderer,
      .rendererContext = &fake,
      .store = store,
      .reload = reload,
      .persistenceContext = &fake,
  };
}

[[nodiscard]] AirfixWindowsRenderSettingsCoordinator
coordinator(FakeDependencies &fake,
            const RenderPresentationSettings &base = settings(),
            const RenderPresentationSettingsOverride &overrides = {},
            const std::optional<RenderPresentationSettings> &activeEffective =
                std::nullopt) {
  const auto desired =
      airfix::render::resolveRenderPresentationSettings(base, overrides);
  require(desired.accepted(), "invalid desired-settings fixture");
  auto result = AirfixWindowsRenderSettingsCoordinator::create(
      base, overrides, activeEffective.value_or(desired.settings),
      AirfixWindowsRenderSettingsPersistenceState::available, callbacks(fake));
  require(result.has_value(), "valid coordinator fixture was rejected");
  return *result;
}

void prepareFailureDoesNotSaveOrPublish() {
  FakeDependencies fake;
  fake.preparationFails = true;
  auto subject = coordinator(fake);
  const auto outcome = subject.applyPersistentCandidate(settings(125.0F));

  require(outcome.status ==
              AirfixWindowsRenderSettingsApplyStatus::prepareOrRenderFailure,
          "preparation failure reported the wrong outcome");
  require(fake.rendererCalls == 1U && fake.saveCalls == 0U &&
              fake.publishCalls == 0U,
          "preparation failure saved or published");
  require(subject.persistentBase() == settings(),
          "preparation failure changed the persistent base");
}

void saveFailurePreventsPublish() {
  FakeDependencies fake;
  fake.storeStatus = AirfixWindowsRenderSettingsStoreStatus::failed;
  auto subject = coordinator(fake);
  const auto outcome = subject.applyPersistentCandidate(settings(125.0F));

  require(outcome.status ==
                  AirfixWindowsRenderSettingsApplyStatus::persistenceFailure &&
              outcome.persistenceFailure ==
                  AirfixWindowsRenderSettingsPersistenceFailureKind::saveFailed,
          "save failure reported the wrong outcome");
  require(fake.saveCalls == 1U && fake.publishCalls == 0U,
          "failed save allowed renderer publication");
  require(subject.persistentBase() == settings(),
          "failed save changed the persistent base");
}

void durabilityHappensBeforePublish() {
  FakeDependencies fake;
  auto subject = coordinator(fake);
  const auto candidate = settings(125.0F, true);
  const auto outcome = subject.applyPersistentCandidate(candidate);

  require(outcome.status == AirfixWindowsRenderSettingsApplyStatus::success &&
              outcome.rendererChanged,
          "successful transaction was not accepted");
  require(fake.eventCount == 3U && fake.events[0] == Event::prepare &&
              fake.events[1] == Event::save && fake.events[2] == Event::publish,
          "transaction did not save after prepare and before publish");
  require(subject.persistentBase() == candidate &&
              subject.effectiveSettings() == candidate,
          "successful transaction did not promote its snapshots");
}

void maskedChangeSavesWithoutRendererGate() {
  FakeDependencies fake;
  RenderPresentationSettingsOverride overrides;
  overrides.renderScalePercent = 150.0F;
  auto subject = coordinator(fake, settings(100.0F), overrides);
  const auto candidate = settings(125.0F);
  const auto outcome = subject.applyPersistentCandidate(candidate);

  require(outcome.status == AirfixWindowsRenderSettingsApplyStatus::success,
          "fully masked base change failed");
  require(fake.rendererCalls == 0U && fake.saveCalls == 1U &&
              fake.publishCalls == 0U,
          "fully masked base change entered the renderer gate");
  require(fake.savedCandidate == candidate &&
              subject.persistentBase() == candidate &&
              subject.effectiveSettings() == settings(150.0F),
          "fully masked change did not preserve base/effective separation");
}

void commandLineOverridesNeverContaminatePersistence() {
  FakeDependencies fake;
  RenderPresentationSettingsOverride overrides;
  overrides.renderScalePercent = 150.0F;
  auto subject = coordinator(fake, settings(100.0F), overrides);
  const auto persistentCandidate = settings(100.0F, true);
  const auto outcome = subject.applyPersistentCandidate(persistentCandidate);

  require(outcome.accepted(), "override transaction failed");
  require(fake.rendererCandidate == settings(150.0F, true),
          "renderer did not receive the resolved effective snapshot");
  require(fake.savedCandidate == persistentCandidate &&
              fake.savedCandidate.renderScalePercent == 100.0F,
          "launch-only override contaminated the durable base");
  require(subject.sessionOverrides().renderScalePercent == 150.0F,
          "coordinator mutated the session override");
}

void rejectedStartupSnapshotStillEntersRenderer() {
  FakeDependencies fake;
  RenderPresentationSettingsOverride overrides;
  overrides.renderScalePercent = 150.0F;
  auto subject =
      coordinator(fake, settings(100.0F), overrides, settings(100.0F));
  const auto persistentCandidate = settings(100.0F, true);
  const auto outcome = subject.applyPersistentCandidate(persistentCandidate);

  require(outcome.accepted() && fake.rendererCalls == 1U &&
              fake.saveCalls == 1U && fake.publishCalls == 1U,
          "startup rejection incorrectly selected the direct-save path");
  require(fake.rendererCandidate == settings(150.0F, true),
          "startup recovery did not apply the desired effective snapshot");
  require(fake.savedCandidate == persistentCandidate,
          "startup recovery persisted the session override");
  require(subject.effectiveSettings() == settings(150.0F, true),
          "startup recovery did not promote the renderer snapshot");
}

void commitUnknownConfirmedByReloadIsDurable() {
  FakeDependencies fake;
  fake.storeStatus = AirfixWindowsRenderSettingsStoreStatus::commitUnknown;
  fake.reloadedCandidate = settings(130.0F, true);
  auto subject = coordinator(fake);
  const auto outcome = subject.applyPersistentCandidate(fake.reloadedCandidate);

  require(
      outcome.status ==
              AirfixWindowsRenderSettingsApplyStatus::commitUnknownDurable &&
          outcome.accepted(),
      "matching commit-unknown readback was not accepted");
  require(fake.eventCount == 4U && fake.events[0] == Event::prepare &&
              fake.events[1] == Event::save &&
              fake.events[2] == Event::reload &&
              fake.events[3] == Event::publish,
          "commit-unknown readback did not precede publication");
  require(subject.persistentBase() == fake.reloadedCandidate,
          "confirmed commit-unknown did not promote persistent base");
}

void commitUnknownMismatchIsRejected() {
  FakeDependencies fake;
  fake.storeStatus = AirfixWindowsRenderSettingsStoreStatus::commitUnknown;
  fake.reloadedCandidate = settings(90.0F);
  auto subject = coordinator(fake);
  const auto outcome = subject.applyPersistentCandidate(settings(130.0F));

  require(outcome.status ==
                  AirfixWindowsRenderSettingsApplyStatus::persistenceFailure &&
              outcome.persistenceFailure ==
                  AirfixWindowsRenderSettingsPersistenceFailureKind::
                      commitUnknownMismatch,
          "mismatched commit-unknown readback reported the wrong outcome");
  require(fake.reloadCalls == 1U && fake.publishCalls == 0U,
          "mismatched commit-unknown readback allowed publication");
  require(subject.persistentBase() == settings(),
          "mismatched commit-unknown changed persistent base");
}

void commitUnknownReadbackRequiresTheCurrentRecord() {
  using FileStatus = airfix::settings::RenderSettingsFileStatus;
  using LoadResult = airfix::settings::RenderSettingsLoadResult;
  using Source = airfix::settings::RenderSettingsLoadSource;
  using ReloadStatus = AirfixWindowsRenderSettingsReloadStatus;

  LoadResult missing;
  missing.settings = settings(130.0F);
  missing.source = Source::defaults;
  missing.current.status = FileStatus::missing;
  require(classifyWindowsRenderSettingsCommitReadback(missing).status ==
              ReloadStatus::failed,
          "default fallback falsely confirmed a commit-unknown save");

  LoadResult recovered;
  recovered.settings = settings(130.0F);
  recovered.source = Source::backup;
  recovered.current.status = FileStatus::malformed;
  recovered.backup.status = FileStatus::valid;
  require(classifyWindowsRenderSettingsCommitReadback(recovered).status ==
              ReloadStatus::failed,
          "backup recovery falsely confirmed a commit-unknown current save");

  LoadResult current;
  current.settings = settings(130.0F);
  current.source = Source::current;
  current.current.status = FileStatus::valid;
  const auto confirmed = classifyWindowsRenderSettingsCommitReadback(current);
  require(confirmed.status == ReloadStatus::loaded &&
              confirmed.settings == current.settings,
          "valid current record did not confirm commit-unknown durability");

  current.persistenceBlocked = true;
  require(classifyWindowsRenderSettingsCommitReadback(current).status ==
              ReloadStatus::blocked,
          "persistence block did not override a current readback");
}

void blockedPersistenceRejectsPanelApply() {
  const auto initial = settings();
  auto subject = AirfixWindowsRenderSettingsCoordinator::create(
      initial, {}, initial,
      AirfixWindowsRenderSettingsPersistenceState::blocked, {});
  require(subject.has_value(), "blocked coordinator could not be represented");

  const auto outcome = subject->applyPersistentCandidate(settings(120.0F));
  require(!subject->persistenceAvailable() &&
              outcome.status ==
                  AirfixWindowsRenderSettingsApplyStatus::persistenceFailure &&
              outcome.persistenceFailure ==
                  AirfixWindowsRenderSettingsPersistenceFailureKind::blocked,
          "blocked persistence did not reject Apply explicitly");
  require(subject->persistentBase() == initial,
          "blocked persistence changed the base");
}

void unavailablePersistenceAndInvalidCandidatesAreTyped() {
  auto unavailable = AirfixWindowsRenderSettingsCoordinator::create(
      settings(), {}, settings(),
      AirfixWindowsRenderSettingsPersistenceState::unavailable, {});
  require(unavailable.has_value(),
          "unavailable coordinator could not be represented");
  const auto unavailableOutcome =
      unavailable->applyPersistentCandidate(settings(120.0F));
  require(unavailableOutcome.persistenceFailure ==
              AirfixWindowsRenderSettingsPersistenceFailureKind::unavailable,
          "unavailable persistence was not distinguished");

  FakeDependencies fake;
  auto subject = coordinator(fake);
  auto invalid = settings();
  invalid.renderScalePercent = 250.0F;
  const auto invalidOutcome = subject.applyPersistentCandidate(invalid);
  require(invalidOutcome.status ==
                  AirfixWindowsRenderSettingsApplyStatus::invalid &&
              invalidOutcome.validationIssue.has_value() &&
              fake.rendererCalls == 0U && fake.saveCalls == 0U,
          "invalid candidate did not fail before side effects");
}

void startupSnapshotsAreValidatedIndependently() {
  auto invalidActive = settings();
  invalidActive.renderScalePercent = 250.0F;
  require(!AirfixWindowsRenderSettingsCoordinator::create(
               settings(), {}, invalidActive,
               AirfixWindowsRenderSettingsPersistenceState::unavailable, {})
               .has_value(),
          "invalid active renderer snapshot was accepted");

  RenderPresentationSettingsOverride invalidOverrides;
  invalidOverrides.renderScalePercent = 250.0F;
  require(!AirfixWindowsRenderSettingsCoordinator::create(
               settings(), invalidOverrides, settings(),
               AirfixWindowsRenderSettingsPersistenceState::unavailable, {})
               .has_value(),
          "invalid desired startup snapshot was accepted");
}

} // namespace

int main() {
  try {
    prepareFailureDoesNotSaveOrPublish();
    saveFailurePreventsPublish();
    durabilityHappensBeforePublish();
    maskedChangeSavesWithoutRendererGate();
    commandLineOverridesNeverContaminatePersistence();
    rejectedStartupSnapshotStillEntersRenderer();
    commitUnknownConfirmedByReloadIsDurable();
    commitUnknownMismatchIsRejected();
    commitUnknownReadbackRequiresTheCurrentRecord();
    blockedPersistenceRejectsPanelApply();
    unavailablePersistenceAndInvalidCandidatesAreTyped();
    startupSnapshotsAreValidatedIndependently();
    std::cout << "Airfix Windows render settings coordinator tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Airfix Windows render settings coordinator tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
