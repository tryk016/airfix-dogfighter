#pragma once

#include "AirfixD3D11Renderer.hpp"

#include "airfix/render/RenderPresentationSettings.hpp"
#include "airfix/settings/RenderPresentationSettingsStore.hpp"

#include <cstdint>
#include <optional>

namespace airfix::windows {

enum class AirfixWindowsRenderSettingsPersistenceState : std::uint8_t {
  available,
  unavailable,
  blocked,
};

enum class AirfixWindowsRenderSettingsStoreStatus : std::uint8_t {
  committed,
  commitUnknown,
  unavailable,
  blocked,
  failed,
};

struct AirfixWindowsRenderSettingsStoreResult final {
  AirfixWindowsRenderSettingsStoreStatus status{
      AirfixWindowsRenderSettingsStoreStatus::failed};
};

enum class AirfixWindowsRenderSettingsReloadStatus : std::uint8_t {
  loaded,
  unavailable,
  blocked,
  failed,
};

struct AirfixWindowsRenderSettingsReloadResult final {
  AirfixWindowsRenderSettingsReloadStatus status{
      AirfixWindowsRenderSettingsReloadStatus::failed};
  airfix::render::RenderPresentationSettings settings;
};

// A commit-unknown save may be confirmed only by the freshly loaded current
// record. Defaults and backup recovery can equal the candidate by coincidence
// but do not prove that the attempted current-file publication was durable.
[[nodiscard]] AirfixWindowsRenderSettingsReloadResult
classifyWindowsRenderSettingsCommitReadback(
    const airfix::settings::RenderSettingsLoadResult &load) noexcept;

// All callbacks are deliberately non-throwing. The production adapter owns
// translation from renderer/store exceptions into these storage-neutral
// results; tests can inject a deterministic synchronous transaction.
struct AirfixWindowsRenderSettingsCallbacks final {
  using ApplyRendererCallback = RenderPresentationSettingsApplyResult (*)(
      void *context,
      const airfix::render::RenderPresentationSettings &candidate,
      RenderPresentationSettingsPublicationGate publicationGate) noexcept;
  using StoreCallback = AirfixWindowsRenderSettingsStoreResult (*)(
      void *context, const airfix::render::RenderPresentationSettings
                         &persistentCandidate) noexcept;
  using ReloadCallback =
      AirfixWindowsRenderSettingsReloadResult (*)(void *context) noexcept;

  ApplyRendererCallback applyRenderer{};
  void *rendererContext{};
  StoreCallback store{};
  ReloadCallback reload{};
  void *persistenceContext{};
};

enum class AirfixWindowsRenderSettingsApplyStatus : std::uint8_t {
  success,
  invalid,
  prepareOrRenderFailure,
  persistenceFailure,
  commitUnknownDurable,
};

enum class AirfixWindowsRenderSettingsPersistenceFailureKind : std::uint8_t {
  unavailable,
  blocked,
  saveFailed,
  reloadFailed,
  commitUnknownMismatch,
};

struct AirfixWindowsRenderSettingsApplyOutcome final {
  AirfixWindowsRenderSettingsApplyStatus status{
      AirfixWindowsRenderSettingsApplyStatus::prepareOrRenderFailure};
  airfix::render::RenderPresentationSettings effectiveSettings;
  std::optional<airfix::render::RenderPresentationSettingsIssue>
      validationIssue;
  std::optional<RenderPresentationSettingsApplyIssueKind> rendererIssue;
  std::optional<AirfixWindowsRenderSettingsPersistenceFailureKind>
      persistenceFailure;
  bool rendererChanged{};

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == AirfixWindowsRenderSettingsApplyStatus::success ||
           status ==
               AirfixWindowsRenderSettingsApplyStatus::commitUnknownDurable;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return accepted();
  }
};

// Synchronous owner for the Windows render-settings Apply transaction.
//
// persistentBase is the complete durable/UI value. sessionOverrides is the
// launch-only command-line layer and is never passed to storage. The renderer
// receives resolve(persistent candidate, session overrides).
// activeEffectiveSettings passed to create is the renderer's actual snapshot;
// it may differ from that initial resolution when startup publication failed.
// When the desired effective value changes, the store callback runs from the
// renderer's publication gate: after private GPU preparation and immediately
// before the renderer's no-fail publication step.
class AirfixWindowsRenderSettingsCoordinator final {
public:
  [[nodiscard]] static std::optional<AirfixWindowsRenderSettingsCoordinator>
  create(
      const airfix::render::RenderPresentationSettings &persistentBase,
      const airfix::render::RenderPresentationSettingsOverride
          &sessionOverrides,
      const airfix::render::RenderPresentationSettings &activeEffectiveSettings,
      AirfixWindowsRenderSettingsPersistenceState persistenceState,
      AirfixWindowsRenderSettingsCallbacks callbacks) noexcept;

  [[nodiscard]] AirfixWindowsRenderSettingsApplyOutcome
  applyPersistentCandidate(
      const airfix::render::RenderPresentationSettings &candidate) noexcept;

  [[nodiscard]] const airfix::render::RenderPresentationSettings &
  persistentBase() const noexcept {
    return persistentBase_;
  }

  [[nodiscard]] const airfix::render::RenderPresentationSettingsOverride &
  sessionOverrides() const noexcept {
    return sessionOverrides_;
  }

  [[nodiscard]] const airfix::render::RenderPresentationSettings &
  effectiveSettings() const noexcept {
    return effectiveSettings_;
  }

  [[nodiscard]] AirfixWindowsRenderSettingsPersistenceState
  persistenceState() const noexcept {
    return persistenceState_;
  }

  [[nodiscard]] bool persistenceAvailable() const noexcept {
    return persistenceState_ ==
           AirfixWindowsRenderSettingsPersistenceState::available;
  }

private:
  struct PersistenceAttempt final {
    bool durable{};
    bool commitWasUnknown{};
    std::optional<AirfixWindowsRenderSettingsPersistenceFailureKind> failure;
  };

  struct PublicationContext final {
    AirfixWindowsRenderSettingsCoordinator *coordinator{};
    const airfix::render::RenderPresentationSettings *persistentCandidate{};
    const airfix::render::RenderPresentationSettings *effectiveCandidate{};
    bool called{};
    PersistenceAttempt persistence;
  };

  AirfixWindowsRenderSettingsCoordinator(
      const airfix::render::RenderPresentationSettings &persistentBase,
      const airfix::render::RenderPresentationSettingsOverride
          &sessionOverrides,
      const airfix::render::RenderPresentationSettings &effectiveSettings,
      AirfixWindowsRenderSettingsPersistenceState persistenceState,
      AirfixWindowsRenderSettingsCallbacks callbacks) noexcept;

  [[nodiscard]] PersistenceAttempt
  persist(const airfix::render::RenderPresentationSettings &candidate) noexcept;

  [[nodiscard]] static bool
  publicationGate(void *context,
                  const airfix::render::RenderPresentationSettings
                      &effectiveCandidate) noexcept;

  [[nodiscard]] AirfixWindowsRenderSettingsApplyOutcome
  currentOutcome(AirfixWindowsRenderSettingsApplyStatus status) const noexcept;

  airfix::render::RenderPresentationSettings persistentBase_;
  airfix::render::RenderPresentationSettingsOverride sessionOverrides_;
  airfix::render::RenderPresentationSettings effectiveSettings_;
  AirfixWindowsRenderSettingsPersistenceState persistenceState_{
      AirfixWindowsRenderSettingsPersistenceState::unavailable};
  AirfixWindowsRenderSettingsCallbacks callbacks_;
};

} // namespace airfix::windows
