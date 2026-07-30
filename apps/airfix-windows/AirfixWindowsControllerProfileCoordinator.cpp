#include "AirfixWindowsControllerProfileCoordinator.hpp"

namespace airfix::windows {
namespace {

using ApplyStatus = AirfixWindowsControllerProfileApplyStatus;
using FailureKind = AirfixWindowsControllerProfileFailureKind;
using PersistenceState = AirfixWindowsControllerProfilePersistenceState;
using ReloadStatus = AirfixWindowsControllerProfileReloadStatus;
using StoreStatus = AirfixWindowsControllerProfileStoreStatus;

} // namespace

AirfixWindowsControllerProfileReloadResult
classifyWindowsControllerProfileCommitReadback(
    const airfix::settings::ControllerInputProfileLoadResult &load) noexcept {
  using FileStatus = airfix::settings::ControllerInputProfileFileStatus;
  using Source = airfix::settings::ControllerInputProfileLoadSource;

  if (load.persistenceBlocked) {
    return {
        .status = ReloadStatus::blocked,
        .currentRecord = std::nullopt,
    };
  }
  if (load.source != Source::current ||
      load.current.status != FileStatus::valid) {
    return {
        .status = ReloadStatus::failed,
        .currentRecord = std::nullopt,
    };
  }

  const auto &record = load.profile.record();
  if (!airfix::input::resolveControllerInputProfile(record).complete()) {
    return {
        .status = ReloadStatus::failed,
        .currentRecord = std::nullopt,
    };
  }
  return {
      .status = ReloadStatus::loadedCurrent,
      .currentRecord = record,
  };
}

std::optional<AirfixWindowsControllerProfileCoordinator>
AirfixWindowsControllerProfileCoordinator::create(
    const airfix::input::ControllerInputProfileRecord &persistentBase,
    const AirfixWindowsControllerProfilePersistenceState persistenceState,
    const AirfixWindowsControllerProfileCallbacks callbacks) noexcept {
  if (!airfix::input::resolveControllerInputProfile(persistentBase)
           .complete()) {
    return std::nullopt;
  }
  return AirfixWindowsControllerProfileCoordinator{
      persistentBase,
      persistenceState,
      callbacks,
  };
}

AirfixWindowsControllerProfileCoordinator::
    AirfixWindowsControllerProfileCoordinator(
        const airfix::input::ControllerInputProfileRecord &persistentBase,
        const AirfixWindowsControllerProfilePersistenceState persistenceState,
        const AirfixWindowsControllerProfileCallbacks callbacks) noexcept
    : persistentBase_(persistentBase), persistenceState_(persistenceState),
      callbacks_(callbacks) {}

AirfixWindowsControllerProfileApplyOutcome
AirfixWindowsControllerProfileCoordinator::currentOutcome(
    const AirfixWindowsControllerProfileApplyStatus status) const noexcept {
  return {
      .status = status,
      .persistentBase = persistentBase_,
      .validationIssue = std::nullopt,
      .failure = std::nullopt,
  };
}

AirfixWindowsControllerProfileApplyOutcome
AirfixWindowsControllerProfileCoordinator::applyPersistentCandidate(
    const airfix::input::ControllerInputProfileRecord &candidate) noexcept {
  const auto resolved = airfix::input::resolveControllerInputProfile(candidate);
  if (!resolved.complete()) {
    auto outcome = currentOutcome(ApplyStatus::invalid);
    outcome.validationIssue = resolved.issue;
    return outcome;
  }

  if (persistenceState_ == PersistenceState::unavailable) {
    return currentOutcome(ApplyStatus::unavailable);
  }
  if (persistenceState_ == PersistenceState::blocked) {
    return currentOutcome(ApplyStatus::blocked);
  }
  if (callbacks_.store == nullptr) {
    persistenceState_ = PersistenceState::unavailable;
    return currentOutcome(ApplyStatus::unavailable);
  }

  const auto stored = callbacks_.store(callbacks_.context, candidate);
  switch (stored.status) {
  case StoreStatus::unchanged:
    persistentBase_ = candidate;
    return currentOutcome(ApplyStatus::unchanged);
  case StoreStatus::committed:
    persistentBase_ = candidate;
    return currentOutcome(ApplyStatus::committed);
  case StoreStatus::unavailable:
    persistenceState_ = PersistenceState::unavailable;
    return currentOutcome(ApplyStatus::unavailable);
  case StoreStatus::blocked:
    persistenceState_ = PersistenceState::blocked;
    return currentOutcome(ApplyStatus::blocked);
  case StoreStatus::failed: {
    auto outcome = currentOutcome(ApplyStatus::failed);
    outcome.failure = FailureKind::saveFailed;
    return outcome;
  }
  case StoreStatus::commitUnknown:
    break;
  }

  if (callbacks_.reload == nullptr) {
    persistenceState_ = PersistenceState::unavailable;
    return currentOutcome(ApplyStatus::unavailable);
  }

  const auto reloaded = callbacks_.reload(callbacks_.context);
  switch (reloaded.status) {
  case ReloadStatus::loadedCurrent:
    break;
  case ReloadStatus::unavailable:
    persistenceState_ = PersistenceState::unavailable;
    return currentOutcome(ApplyStatus::unavailable);
  case ReloadStatus::blocked:
    persistenceState_ = PersistenceState::blocked;
    return currentOutcome(ApplyStatus::blocked);
  case ReloadStatus::failed: {
    auto outcome = currentOutcome(ApplyStatus::failed);
    outcome.failure = FailureKind::reloadFailed;
    return outcome;
  }
  }

  if (!reloaded.currentRecord.has_value() ||
      !airfix::input::resolveControllerInputProfile(*reloaded.currentRecord)
           .complete()) {
    auto outcome = currentOutcome(ApplyStatus::failed);
    outcome.failure = FailureKind::reloadFailed;
    return outcome;
  }
  if (*reloaded.currentRecord != candidate) {
    auto outcome = currentOutcome(ApplyStatus::failed);
    outcome.failure = FailureKind::commitUnknownMismatch;
    return outcome;
  }

  persistentBase_ = candidate;
  return currentOutcome(ApplyStatus::commitUnknown);
}

} // namespace airfix::windows
