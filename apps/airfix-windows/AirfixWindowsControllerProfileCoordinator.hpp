#pragma once

#include "airfix/input/ControllerInputProfile.hpp"
#include "airfix/settings/ControllerInputProfileStore.hpp"

#include <cstdint>
#include <optional>

namespace airfix::windows {

enum class AirfixWindowsControllerProfilePersistenceState : std::uint8_t {
  available,
  unavailable,
  blocked,
};

enum class AirfixWindowsControllerProfileStoreStatus : std::uint8_t {
  unchanged,
  committed,
  commitUnknown,
  unavailable,
  blocked,
  failed,
};

struct AirfixWindowsControllerProfileStoreResult final {
  AirfixWindowsControllerProfileStoreStatus status{
      AirfixWindowsControllerProfileStoreStatus::failed};
};

enum class AirfixWindowsControllerProfileReloadStatus : std::uint8_t {
  loadedCurrent,
  unavailable,
  blocked,
  failed,
};

struct AirfixWindowsControllerProfileReloadResult final {
  AirfixWindowsControllerProfileReloadStatus status{
      AirfixWindowsControllerProfileReloadStatus::failed};
  std::optional<airfix::input::ControllerInputProfileRecord> currentRecord;
};

// A commit-unknown save may be confirmed only by a fresh, valid current AFIP
// record. Backup/default recovery can equal the candidate by coincidence and
// never proves that the attempted current-file publication was durable.
[[nodiscard]] AirfixWindowsControllerProfileReloadResult
classifyWindowsControllerProfileCommitReadback(
    const airfix::settings::ControllerInputProfileLoadResult &load) noexcept;

// Storage callbacks are synchronous and non-throwing. They must not mutate or
// rebuild the active input adapter; the candidate becomes effective only after
// a later process start loads it through the normal startup seam.
struct AirfixWindowsControllerProfileCallbacks final {
  using StoreCallback = AirfixWindowsControllerProfileStoreResult (*)(
      void *context,
      const airfix::input::ControllerInputProfileRecord &candidate) noexcept;
  using ReloadCallback =
      AirfixWindowsControllerProfileReloadResult (*)(void *context) noexcept;

  StoreCallback store{};
  ReloadCallback reload{};
  void *context{};
};

enum class AirfixWindowsControllerProfileApplyStatus : std::uint8_t {
  invalid,
  unchanged,
  committed,
  commitUnknown,
  unavailable,
  blocked,
  failed,
};

enum class AirfixWindowsControllerProfileFailureKind : std::uint8_t {
  saveFailed,
  reloadFailed,
  commitUnknownMismatch,
};

struct AirfixWindowsControllerProfileApplyOutcome final {
  AirfixWindowsControllerProfileApplyStatus status{
      AirfixWindowsControllerProfileApplyStatus::failed};
  airfix::input::ControllerInputProfileRecord persistentBase;
  std::optional<airfix::input::ControllerInputProfileIssue> validationIssue;
  std::optional<AirfixWindowsControllerProfileFailureKind> failure;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == AirfixWindowsControllerProfileApplyStatus::unchanged ||
           status == AirfixWindowsControllerProfileApplyStatus::committed ||
           status == AirfixWindowsControllerProfileApplyStatus::commitUnknown;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return accepted();
  }
};

// Synchronous storage-only owner for a complete controller-profile record.
// Every valid candidate reaches the store callback, even when it equals the
// current base, so a backup/default recovery can repair the durable current
// AFIP. Successful outcomes preserve the exact submitted record. No method has
// access to, or can replace, the active native input adapter.
class AirfixWindowsControllerProfileCoordinator final {
public:
  [[nodiscard]] static std::optional<AirfixWindowsControllerProfileCoordinator>
  create(const airfix::input::ControllerInputProfileRecord &persistentBase,
         AirfixWindowsControllerProfilePersistenceState persistenceState,
         AirfixWindowsControllerProfileCallbacks callbacks) noexcept;

  [[nodiscard]] AirfixWindowsControllerProfileApplyOutcome
  applyPersistentCandidate(
      const airfix::input::ControllerInputProfileRecord &candidate) noexcept;

  [[nodiscard]] const airfix::input::ControllerInputProfileRecord &
  persistentBase() const noexcept {
    return persistentBase_;
  }

  [[nodiscard]] AirfixWindowsControllerProfilePersistenceState
  persistenceState() const noexcept {
    return persistenceState_;
  }

  [[nodiscard]] bool persistenceAvailable() const noexcept {
    return persistenceState_ ==
           AirfixWindowsControllerProfilePersistenceState::available;
  }

private:
  AirfixWindowsControllerProfileCoordinator(
      const airfix::input::ControllerInputProfileRecord &persistentBase,
      AirfixWindowsControllerProfilePersistenceState persistenceState,
      AirfixWindowsControllerProfileCallbacks callbacks) noexcept;

  [[nodiscard]] AirfixWindowsControllerProfileApplyOutcome currentOutcome(
      AirfixWindowsControllerProfileApplyStatus status) const noexcept;

  airfix::input::ControllerInputProfileRecord persistentBase_;
  AirfixWindowsControllerProfilePersistenceState persistenceState_{
      AirfixWindowsControllerProfilePersistenceState::unavailable};
  AirfixWindowsControllerProfileCallbacks callbacks_;
};

} // namespace airfix::windows
