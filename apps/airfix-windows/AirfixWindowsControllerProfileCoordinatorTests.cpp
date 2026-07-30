#include "AirfixWindowsControllerProfileCoordinator.hpp"

#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::input::ControllerInputProfileRecord;
using airfix::settings::ControllerInputProfileFileStatus;
using airfix::settings::ControllerInputProfileLoadResult;
using airfix::settings::ControllerInputProfileLoadSource;
using airfix::windows::AirfixWindowsControllerProfileApplyStatus;
using airfix::windows::AirfixWindowsControllerProfileCallbacks;
using airfix::windows::AirfixWindowsControllerProfileCoordinator;
using airfix::windows::AirfixWindowsControllerProfileFailureKind;
using airfix::windows::AirfixWindowsControllerProfilePersistenceState;
using airfix::windows::AirfixWindowsControllerProfileReloadResult;
using airfix::windows::AirfixWindowsControllerProfileReloadStatus;
using airfix::windows::AirfixWindowsControllerProfileStoreResult;
using airfix::windows::AirfixWindowsControllerProfileStoreStatus;
using airfix::windows::classifyWindowsControllerProfileCommitReadback;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] ControllerInputProfileRecord
profile(const std::uint16_t sensitivityPermille = 1000U,
        const bool inverted = false) noexcept {
  auto result = airfix::input::makeDefaultControllerInputProfileRecord();
  result.axes[0].sensitivityPermille = sensitivityPermille;
  result.axes[1].inverted = static_cast<std::uint8_t>(inverted);
  return result;
}

struct FakeDependencies final {
  std::size_t storeCalls{};
  std::size_t reloadCalls{};
  AirfixWindowsControllerProfileStoreStatus storeStatus{
      AirfixWindowsControllerProfileStoreStatus::committed};
  AirfixWindowsControllerProfileReloadStatus reloadStatus{
      AirfixWindowsControllerProfileReloadStatus::loadedCurrent};
  std::optional<ControllerInputProfileRecord> reloadRecord;
  std::optional<ControllerInputProfileRecord> storedCandidate;
};

[[nodiscard]] AirfixWindowsControllerProfileStoreResult
store(void *const context,
      const ControllerInputProfileRecord &candidate) noexcept {
  auto &fake = *static_cast<FakeDependencies *>(context);
  ++fake.storeCalls;
  fake.storedCandidate = candidate;
  return {.status = fake.storeStatus};
}

[[nodiscard]] AirfixWindowsControllerProfileReloadResult
reload(void *const context) noexcept {
  auto &fake = *static_cast<FakeDependencies *>(context);
  ++fake.reloadCalls;
  return {
      .status = fake.reloadStatus,
      .currentRecord = fake.reloadRecord,
  };
}

[[nodiscard]] AirfixWindowsControllerProfileCoordinator coordinator(
    FakeDependencies &fake,
    const ControllerInputProfileRecord &base = profile(),
    const AirfixWindowsControllerProfilePersistenceState persistenceState =
        AirfixWindowsControllerProfilePersistenceState::available) {
  const auto created =
      AirfixWindowsControllerProfileCoordinator::create(base, persistenceState,
                                                        {
                                                            .store = store,
                                                            .reload = reload,
                                                            .context = &fake,
                                                        });
  require(created.has_value(), "valid coordinator could not be created");
  return *created;
}

[[nodiscard]] ControllerInputProfileLoadResult
loadResult(const ControllerInputProfileRecord &record,
           const ControllerInputProfileLoadSource source,
           const ControllerInputProfileFileStatus currentStatus,
           const ControllerInputProfileFileStatus backupStatus,
           const bool persistenceBlocked = false) {
  const auto resolved = airfix::input::resolveControllerInputProfile(record);
  require(resolved.complete(), "test load record is invalid");
  return {
      .profile = *resolved.profile,
      .source = source,
      .current =
          {
              .status = currentStatus,
              .schemaVersion = std::nullopt,
          },
      .backup =
          {
              .status = backupStatus,
              .schemaVersion = std::nullopt,
          },
      .persistenceBlocked = persistenceBlocked,
  };
}

void creationValidatesAndPreservesTheBase() {
  const auto base = profile(1250U, true);
  const auto created = AirfixWindowsControllerProfileCoordinator::create(
      base, AirfixWindowsControllerProfilePersistenceState::unavailable, {});
  require(created.has_value() && created->persistentBase() == base,
          "coordinator did not preserve its exact valid base");

  auto invalid = base;
  invalid.axes[0].sensitivityPermille = 0U;
  require(!AirfixWindowsControllerProfileCoordinator::create(
               invalid,
               AirfixWindowsControllerProfilePersistenceState::unavailable, {})
               .has_value(),
          "coordinator accepted an invalid base");
}

void committedAndUnchangedOutcomesPreserveTheExactCandidate() {
  {
    FakeDependencies fake;
    auto subject = coordinator(fake);
    const auto candidate = profile(1375U, true);
    const auto outcome = subject.applyPersistentCandidate(candidate);
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::committed &&
                outcome.accepted() && fake.storeCalls == 1U &&
                fake.reloadCalls == 0U && fake.storedCandidate == candidate &&
                subject.persistentBase() == candidate &&
                outcome.persistentBase == candidate,
            "committed candidate was not preserved exactly");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::unchanged;
    const auto candidate = profile(1425U, true);
    auto subject = coordinator(fake, candidate);
    const auto outcome = subject.applyPersistentCandidate(candidate);
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::unchanged &&
                outcome.accepted() && fake.storeCalls == 1U &&
                fake.storedCandidate == candidate &&
                subject.persistentBase() == candidate,
            "unchanged durable candidate had the wrong outcome");
  }
}

void equalRecoveredBaseStillAttemptsARepairWrite() {
  FakeDependencies fake;
  fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::committed;
  const auto recovered = profile(1500U);
  auto subject = coordinator(fake, recovered);

  const auto outcome = subject.applyPersistentCandidate(recovered);
  require(outcome.status ==
                  AirfixWindowsControllerProfileApplyStatus::committed &&
              fake.storeCalls == 1U && fake.storedCandidate == recovered,
          "equal backup/default base was incorrectly short-circuited");
}

void invalidCandidateFailsAtomicallyBeforeStorage() {
  FakeDependencies fake;
  const auto base = profile();
  auto subject = coordinator(fake, base);
  auto invalid = profile();
  invalid.axes[2].outerSaturationQ15 = 0U;

  const auto outcome = subject.applyPersistentCandidate(invalid);
  require(
      outcome.status == AirfixWindowsControllerProfileApplyStatus::invalid &&
          outcome.validationIssue.has_value() && !outcome.accepted() &&
          fake.storeCalls == 0U && fake.reloadCalls == 0U &&
          subject.persistentBase() == base && outcome.persistentBase == base,
      "invalid candidate caused a storage side effect");
}

void persistenceStatesAndStoreFailuresAreTyped() {
  {
    FakeDependencies fake;
    auto subject = coordinator(
        fake, profile(),
        AirfixWindowsControllerProfilePersistenceState::unavailable);
    const auto outcome = subject.applyPersistentCandidate(profile(1200U));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::unavailable &&
                fake.storeCalls == 0U && !subject.persistenceAvailable(),
            "initial unavailable state was not enforced");
  }

  {
    FakeDependencies fake;
    auto subject =
        coordinator(fake, profile(),
                    AirfixWindowsControllerProfilePersistenceState::blocked);
    const auto outcome = subject.applyPersistentCandidate(profile(1200U));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::blocked &&
                fake.storeCalls == 0U && !subject.persistenceAvailable(),
            "initial blocked state was not enforced");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::unavailable;
    auto subject = coordinator(fake);
    require(subject.applyPersistentCandidate(profile(1200U)).status ==
                    AirfixWindowsControllerProfileApplyStatus::unavailable &&
                subject.persistenceState() ==
                    AirfixWindowsControllerProfilePersistenceState::unavailable,
            "store unavailable did not downgrade persistence");
    (void)subject.applyPersistentCandidate(profile(1300U));
    require(fake.storeCalls == 1U,
            "downgraded unavailable store was called again");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::blocked;
    auto subject = coordinator(fake);
    require(subject.applyPersistentCandidate(profile(1200U)).status ==
                    AirfixWindowsControllerProfileApplyStatus::blocked &&
                subject.persistenceState() ==
                    AirfixWindowsControllerProfilePersistenceState::blocked,
            "store blocked did not downgrade persistence");
    (void)subject.applyPersistentCandidate(profile(1300U));
    require(fake.storeCalls == 1U, "downgraded blocked store was called again");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::failed;
    auto subject = coordinator(fake);
    const auto outcome = subject.applyPersistentCandidate(profile(1200U));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::failed &&
                outcome.failure ==
                    AirfixWindowsControllerProfileFailureKind::saveFailed &&
                subject.persistenceAvailable(),
            "transient save failure had the wrong typed outcome");
  }
}

void commitUnknownRequiresExactValidCurrentReadback() {
  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::commitUnknown;
    const auto candidate = profile(1325U, true);
    fake.reloadRecord = candidate;
    auto subject = coordinator(fake);
    const auto outcome = subject.applyPersistentCandidate(candidate);
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::commitUnknown &&
                outcome.accepted() && fake.storeCalls == 1U &&
                fake.reloadCalls == 1U && subject.persistentBase() == candidate,
            "exact valid current did not confirm commit-unknown durability");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::commitUnknown;
    fake.reloadRecord = profile(1350U);
    const auto base = profile();
    auto subject = coordinator(fake, base);
    const auto outcome = subject.applyPersistentCandidate(profile(1400U, true));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::failed &&
                outcome.failure == AirfixWindowsControllerProfileFailureKind::
                                       commitUnknownMismatch &&
                subject.persistentBase() == base,
            "mismatched current falsely confirmed commit-unknown durability");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::commitUnknown;
    auto invalidReadback = profile(1400U);
    invalidReadback.axes[0].sensitivityPermille = 0U;
    fake.reloadRecord = invalidReadback;
    auto subject = coordinator(fake);
    const auto outcome = subject.applyPersistentCandidate(profile(1400U, true));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::failed &&
                outcome.failure ==
                    AirfixWindowsControllerProfileFailureKind::reloadFailed,
            "invalid current readback confirmed commit-unknown durability");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::commitUnknown;
    fake.reloadRecord.reset();
    auto subject = coordinator(fake);
    const auto outcome = subject.applyPersistentCandidate(profile(1400U));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::failed &&
                outcome.failure ==
                    AirfixWindowsControllerProfileFailureKind::reloadFailed,
            "missing current readback confirmed commit-unknown durability");
  }
}

void commitUnknownReloadFailuresAreTypedAndDowngradeCapability() {
  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::commitUnknown;
    fake.reloadStatus = AirfixWindowsControllerProfileReloadStatus::unavailable;
    auto subject = coordinator(fake);
    require(subject.applyPersistentCandidate(profile(1250U)).status ==
                    AirfixWindowsControllerProfileApplyStatus::unavailable &&
                subject.persistenceState() ==
                    AirfixWindowsControllerProfilePersistenceState::unavailable,
            "unavailable reload did not downgrade persistence");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::commitUnknown;
    fake.reloadStatus = AirfixWindowsControllerProfileReloadStatus::blocked;
    auto subject = coordinator(fake);
    require(subject.applyPersistentCandidate(profile(1250U)).status ==
                    AirfixWindowsControllerProfileApplyStatus::blocked &&
                subject.persistenceState() ==
                    AirfixWindowsControllerProfilePersistenceState::blocked,
            "blocked reload did not downgrade persistence");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::commitUnknown;
    fake.reloadStatus = AirfixWindowsControllerProfileReloadStatus::failed;
    auto subject = coordinator(fake);
    const auto outcome = subject.applyPersistentCandidate(profile(1250U));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::failed &&
                outcome.failure ==
                    AirfixWindowsControllerProfileFailureKind::reloadFailed &&
                subject.persistenceAvailable(),
            "reload failure had the wrong typed outcome");
  }
}

void commitReadbackClassifierRejectsBackupAndDefaults() {
  using ReloadStatus = AirfixWindowsControllerProfileReloadStatus;

  const auto candidate = profile(1375U, true);
  const auto defaults =
      loadResult(candidate, ControllerInputProfileLoadSource::defaults,
                 ControllerInputProfileFileStatus::missing,
                 ControllerInputProfileFileStatus::missing);
  require(classifyWindowsControllerProfileCommitReadback(defaults).status ==
              ReloadStatus::failed,
          "default fallback falsely confirmed a commit-unknown save");

  const auto backup =
      loadResult(candidate, ControllerInputProfileLoadSource::backup,
                 ControllerInputProfileFileStatus::malformed,
                 ControllerInputProfileFileStatus::valid);
  require(classifyWindowsControllerProfileCommitReadback(backup).status ==
              ReloadStatus::failed,
          "backup recovery falsely confirmed a commit-unknown save");

  const auto current =
      loadResult(candidate, ControllerInputProfileLoadSource::current,
                 ControllerInputProfileFileStatus::valid,
                 ControllerInputProfileFileStatus::missing);
  const auto confirmed =
      classifyWindowsControllerProfileCommitReadback(current);
  require(confirmed.status == ReloadStatus::loadedCurrent &&
              confirmed.currentRecord == candidate,
          "valid current record did not produce an exact readback");

  const auto blocked =
      loadResult(candidate, ControllerInputProfileLoadSource::current,
                 ControllerInputProfileFileStatus::valid,
                 ControllerInputProfileFileStatus::futureSchema, true);
  require(classifyWindowsControllerProfileCommitReadback(blocked).status ==
              ReloadStatus::blocked,
          "blocked state incorrectly confirmed a current readback");
}

void missingCallbacksFailClosedWithoutCrashing() {
  {
    const auto created = AirfixWindowsControllerProfileCoordinator::create(
        profile(), AirfixWindowsControllerProfilePersistenceState::available,
        {});
    require(
        created.has_value(),
        "available coordinator with a missing callback was not represented");
    auto subject = *created;
    const auto outcome = subject.applyPersistentCandidate(profile(1250U));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::unavailable &&
                subject.persistenceState() ==
                    AirfixWindowsControllerProfilePersistenceState::unavailable,
            "missing store callback did not fail closed");
  }

  {
    FakeDependencies fake;
    fake.storeStatus = AirfixWindowsControllerProfileStoreStatus::commitUnknown;
    const auto created = AirfixWindowsControllerProfileCoordinator::create(
        profile(), AirfixWindowsControllerProfilePersistenceState::available,
        {
            .store = store,
            .reload = nullptr,
            .context = &fake,
        });
    require(created.has_value(),
            "coordinator with a missing reload callback was not represented");
    auto subject = *created;
    const auto outcome = subject.applyPersistentCandidate(profile(1250U));
    require(outcome.status ==
                    AirfixWindowsControllerProfileApplyStatus::unavailable &&
                fake.storeCalls == 1U &&
                subject.persistenceState() ==
                    AirfixWindowsControllerProfilePersistenceState::unavailable,
            "missing reload callback did not fail closed");
  }
}

} // namespace

int main() {
  try {
    creationValidatesAndPreservesTheBase();
    committedAndUnchangedOutcomesPreserveTheExactCandidate();
    equalRecoveredBaseStillAttemptsARepairWrite();
    invalidCandidateFailsAtomicallyBeforeStorage();
    persistenceStatesAndStoreFailuresAreTyped();
    commitUnknownRequiresExactValidCurrentReadback();
    commitUnknownReloadFailuresAreTypedAndDowngradeCapability();
    commitReadbackClassifierRejectsBackupAndDefaults();
    missingCallbacksFailClosedWithoutCrashing();
    std::cout << "Airfix Windows controller profile coordinator tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Airfix Windows controller profile coordinator tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
