#include "AirfixWindowsRenderSettingsCoordinator.hpp"

namespace airfix::windows {
namespace {

using ApplyOutcome = AirfixWindowsRenderSettingsApplyOutcome;
using ApplyStatus = AirfixWindowsRenderSettingsApplyStatus;
using FailureKind = AirfixWindowsRenderSettingsPersistenceFailureKind;
using PersistenceState = AirfixWindowsRenderSettingsPersistenceState;
using ReloadStatus = AirfixWindowsRenderSettingsReloadStatus;
using StoreStatus = AirfixWindowsRenderSettingsStoreStatus;

[[nodiscard]] constexpr FailureKind
failureFor(const PersistenceState state) noexcept {
  return state == PersistenceState::blocked ? FailureKind::blocked
                                            : FailureKind::unavailable;
}

} // namespace

AirfixWindowsRenderSettingsReloadResult
classifyWindowsRenderSettingsCommitReadback(
    const airfix::settings::RenderSettingsLoadResult &load) noexcept {
  using ReloadStatus = AirfixWindowsRenderSettingsReloadStatus;
  using Source = airfix::settings::RenderSettingsLoadSource;
  using FileStatus = airfix::settings::RenderSettingsFileStatus;

  if (load.persistenceBlocked) {
    return {.status = ReloadStatus::blocked};
  }
  if (load.source != Source::current ||
      load.current.status != FileStatus::valid ||
      airfix::render::validateRenderPresentationSettings(load.settings)
          .has_value()) {
    return {.status = ReloadStatus::failed};
  }
  return {
      .status = ReloadStatus::loaded,
      .settings = load.settings,
  };
}

std::optional<AirfixWindowsRenderSettingsCoordinator>
AirfixWindowsRenderSettingsCoordinator::create(
    const airfix::render::RenderPresentationSettings &persistentBase,
    const airfix::render::RenderPresentationSettingsOverride &sessionOverrides,
    const airfix::render::RenderPresentationSettings &activeEffectiveSettings,
    const AirfixWindowsRenderSettingsPersistenceState persistenceState,
    const AirfixWindowsRenderSettingsCallbacks callbacks) noexcept {
  const auto resolved = airfix::render::resolveRenderPresentationSettings(
      persistentBase, sessionOverrides);
  if (!resolved.accepted() ||
      airfix::render::validateRenderPresentationSettings(
          activeEffectiveSettings)
          .has_value()) {
    return std::nullopt;
  }
  if (persistenceState ==
          AirfixWindowsRenderSettingsPersistenceState::available &&
      (callbacks.applyRenderer == nullptr || callbacks.store == nullptr ||
       callbacks.reload == nullptr)) {
    return std::nullopt;
  }

  return AirfixWindowsRenderSettingsCoordinator{
      persistentBase,   sessionOverrides, activeEffectiveSettings,
      persistenceState, callbacks,
  };
}

AirfixWindowsRenderSettingsCoordinator::AirfixWindowsRenderSettingsCoordinator(
    const airfix::render::RenderPresentationSettings &persistentBase,
    const airfix::render::RenderPresentationSettingsOverride &sessionOverrides,
    const airfix::render::RenderPresentationSettings &effectiveSettings,
    const AirfixWindowsRenderSettingsPersistenceState persistenceState,
    const AirfixWindowsRenderSettingsCallbacks callbacks) noexcept
    : persistentBase_(persistentBase), sessionOverrides_(sessionOverrides),
      effectiveSettings_(effectiveSettings),
      persistenceState_(persistenceState), callbacks_(callbacks) {}

AirfixWindowsRenderSettingsApplyOutcome
AirfixWindowsRenderSettingsCoordinator::currentOutcome(
    const AirfixWindowsRenderSettingsApplyStatus status) const noexcept {
  return {
      .status = status,
      .effectiveSettings = effectiveSettings_,
      .validationIssue = std::nullopt,
      .rendererIssue = std::nullopt,
      .persistenceFailure = std::nullopt,
      .rendererChanged = false,
  };
}

AirfixWindowsRenderSettingsCoordinator::PersistenceAttempt
AirfixWindowsRenderSettingsCoordinator::persist(
    const airfix::render::RenderPresentationSettings &candidate) noexcept {
  if (persistenceState_ != PersistenceState::available) {
    return {
        .failure = failureFor(persistenceState_),
    };
  }
  if (callbacks_.store == nullptr) {
    persistenceState_ = PersistenceState::unavailable;
    return {
        .failure = FailureKind::unavailable,
    };
  }

  const auto stored =
      callbacks_.store(callbacks_.persistenceContext, candidate);
  switch (stored.status) {
  case StoreStatus::committed:
    return {
        .durable = true,
        .commitWasUnknown = false,
        .failure = std::nullopt,
    };
  case StoreStatus::unavailable:
    persistenceState_ = PersistenceState::unavailable;
    return {
        .failure = FailureKind::unavailable,
    };
  case StoreStatus::blocked:
    persistenceState_ = PersistenceState::blocked;
    return {
        .failure = FailureKind::blocked,
    };
  case StoreStatus::failed:
    return {
        .failure = FailureKind::saveFailed,
    };
  case StoreStatus::commitUnknown:
    break;
  }

  if (callbacks_.reload == nullptr) {
    return {
        .commitWasUnknown = true,
        .failure = FailureKind::reloadFailed,
    };
  }
  const auto reloaded = callbacks_.reload(callbacks_.persistenceContext);
  switch (reloaded.status) {
  case ReloadStatus::loaded:
    if (reloaded.settings == candidate) {
      return {
          .durable = true,
          .commitWasUnknown = true,
          .failure = std::nullopt,
      };
    }
    return {
        .commitWasUnknown = true,
        .failure = FailureKind::commitUnknownMismatch,
    };
  case ReloadStatus::unavailable:
    persistenceState_ = PersistenceState::unavailable;
    return {
        .commitWasUnknown = true,
        .failure = FailureKind::unavailable,
    };
  case ReloadStatus::blocked:
    persistenceState_ = PersistenceState::blocked;
    return {
        .commitWasUnknown = true,
        .failure = FailureKind::blocked,
    };
  case ReloadStatus::failed:
    return {
        .commitWasUnknown = true,
        .failure = FailureKind::reloadFailed,
    };
  }

  return {
      .commitWasUnknown = true,
      .failure = FailureKind::reloadFailed,
  };
}

bool AirfixWindowsRenderSettingsCoordinator::publicationGate(
    void *const context, const airfix::render::RenderPresentationSettings
                             &effectiveCandidate) noexcept {
  auto &publication = *static_cast<PublicationContext *>(context);
  if (publication.called || publication.coordinator == nullptr ||
      publication.persistentCandidate == nullptr ||
      publication.effectiveCandidate == nullptr ||
      effectiveCandidate != *publication.effectiveCandidate) {
    return false;
  }

  publication.called = true;
  publication.persistence =
      publication.coordinator->persist(*publication.persistentCandidate);
  return publication.persistence.durable;
}

AirfixWindowsRenderSettingsApplyOutcome
AirfixWindowsRenderSettingsCoordinator::applyPersistentCandidate(
    const airfix::render::RenderPresentationSettings &candidate) noexcept {
  const auto resolved = airfix::render::resolveRenderPresentationSettings(
      candidate, sessionOverrides_);
  if (!resolved.accepted()) {
    auto outcome = currentOutcome(ApplyStatus::invalid);
    outcome.validationIssue = resolved.issue;
    return outcome;
  }

  if (persistenceState_ != PersistenceState::available) {
    auto outcome = currentOutcome(ApplyStatus::persistenceFailure);
    outcome.persistenceFailure = failureFor(persistenceState_);
    return outcome;
  }

  if (resolved.settings == effectiveSettings_) {
    const auto persistence = persist(candidate);
    if (!persistence.durable) {
      auto outcome = currentOutcome(ApplyStatus::persistenceFailure);
      outcome.persistenceFailure = persistence.failure;
      return outcome;
    }

    persistentBase_ = candidate;
    return currentOutcome(persistence.commitWasUnknown
                              ? ApplyStatus::commitUnknownDurable
                              : ApplyStatus::success);
  }

  PublicationContext publication{
      .coordinator = this,
      .persistentCandidate = &candidate,
      .effectiveCandidate = &resolved.settings,
      .called = false,
      .persistence = {},
  };
  const RenderPresentationSettingsPublicationGate gate{
      .callback = &AirfixWindowsRenderSettingsCoordinator::publicationGate,
      .context = &publication,
  };
  const auto renderer = callbacks_.applyRenderer(callbacks_.rendererContext,
                                                 resolved.settings, gate);

  if (!renderer.accepted() || !renderer.changed || !publication.called ||
      !publication.persistence.durable) {
    if (publication.called && publication.persistence.failure.has_value()) {
      auto outcome = currentOutcome(ApplyStatus::persistenceFailure);
      outcome.rendererIssue = renderer.issue;
      outcome.persistenceFailure = publication.persistence.failure;
      return outcome;
    }

    auto outcome = currentOutcome(ApplyStatus::prepareOrRenderFailure);
    outcome.rendererIssue = renderer.issue;
    return outcome;
  }

  persistentBase_ = candidate;
  effectiveSettings_ = resolved.settings;
  auto outcome = currentOutcome(publication.persistence.commitWasUnknown
                                    ? ApplyStatus::commitUnknownDurable
                                    : ApplyStatus::success);
  outcome.rendererChanged = true;
  return outcome;
}

} // namespace airfix::windows
