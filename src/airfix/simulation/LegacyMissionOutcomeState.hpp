#pragma once

#include <cstdint>

namespace airfix::simulation {

enum class LegacyMissionOutcomeCall : std::uint8_t {
  missionFail = 0x47,
  missionSuccess = 0x48,
};

// These booleans reconstruct the two independent bytes owned by NfMission.
// They deliberately do not collapse the native state to a single enum:
// conflicting calls can leave both bytes set.
struct LegacyMissionOutcomeState final {
  bool failed{};
  bool accomplished{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyMissionOutcomeState &,
             const LegacyMissionOutcomeState &) noexcept = default;
};

enum class LegacyMissionOutcomeApplyStatus : std::uint8_t {
  applied,
  unsupportedCall,
};

struct LegacyMissionOutcomeStep final {
  LegacyMissionOutcomeApplyStatus status{
      LegacyMissionOutcomeApplyStatus::unsupportedCall};
  LegacyMissionOutcomeState state{};

  // The native first terminal call executes "pause" and then "menu".
  // Repeated or conflicting terminal calls still set their byte but do not
  // request either command again.
  bool requestPauseThenMenu{};

  [[nodiscard]] constexpr bool applied() const noexcept {
    return status == LegacyMissionOutcomeApplyStatus::applied;
  }
};

// Reconstructs NfMission::Call cases 0x47 and 0x48. One already-formed call is
// applied at a time and no payload is inspected. Unsupported call identifiers
// fail closed and return the supplied state unchanged.
//
// This pure boundary owns no script VM, trigger refresh, console, pause state,
// menu, mission loading, persistence, audio, renderer, or platform adapter.
[[nodiscard]] LegacyMissionOutcomeStep
legacyMissionApplyOutcomeCall(LegacyMissionOutcomeState current,
                              LegacyMissionOutcomeCall call) noexcept;

// Reconstructs the outcome-byte effect of NfMission::Fail. Unlike the
// MissionFail script call above, this direct method only sets the failed byte;
// it does not execute the pause/menu commands.
[[nodiscard]] LegacyMissionOutcomeState
legacyMissionMarkFailed(LegacyMissionOutcomeState current) noexcept;

// Reconstructs only the two outcome-byte writes performed by
// NfMission::Reset. The remainder of mission reset stays outside this type.
[[nodiscard]] LegacyMissionOutcomeState
legacyMissionResetOutcomeState() noexcept;

} // namespace airfix::simulation
