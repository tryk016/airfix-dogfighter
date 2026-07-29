#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace airfix::simulation {

inline constexpr float legacyAircraftThrottleResponseOffset = 0.3F;
inline constexpr float legacyAircraftThrottleResponseFactor = 0.02F;
inline constexpr float legacyAircraftEngineStartResponseFactor = 0.0004F;
inline constexpr float legacyAircraftEngineStartThrottleThreshold = 0.001F;
inline constexpr float legacyAircraftEngineStartDurationSeconds = 4.0F;

inline constexpr float legacyAircraftInitialThrustIntegrity = 1.0F;
inline constexpr float legacyAircraftCollisionNormalDotThreshold = 0.9F;
inline constexpr float legacyAircraftCollisionImpulseThreshold = 2.0F;
inline constexpr float legacyAircraftCollisionDamageScale = 0.5F;
inline constexpr float legacyAircraftThrustRecoveryRandomScale =
    0x1.0002p-15F;
inline constexpr std::int32_t legacyAircraftMaximumRecoveryRandomSample =
    32767;

// One candidate from the static/BSP collision loop. normalVelocityDot is the
// recovered dot product stored by the native routine. collisionScalar is the
// second factor used only by its strict qualification gate.
struct LegacyAircraftCollisionSample final {
    float normalVelocityDot{};
    float collisionScalar{};
};

// Caller-owned AirCraft control fields consumed in primary vtable slot 45.
// thrustApply at +0x440 persists until a later native control event replaces
// it. targetThrust is +0x444 and smoothedThrust is +0x560.
struct LegacyAircraftThrustControlState final {
    float thrustApply{};
    float targetThrust{};
    float smoothedThrust{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyAircraftThrustControlState&,
        const LegacyAircraftThrustControlState&) noexcept = default;
};

struct LegacyAircraftThrustControlInput final {
    float health{};
    bool engineStartTransitionActive{};
};

// Reconstructs the AirCraft +0x560 update in the flight-force vtable slot.
// engineStartTransitionActive is the byte at +0x564: the post-collision sound
// state machine sets it while the four-second engine-start transition is
// active. No clamp is applied by the native recurrence.
[[nodiscard]] std::optional<float> legacyAircraftAdvanceSmoothedThrust(
    float currentSmoothedThrust,
    float targetThrust,
    bool engineStartTransitionActive) noexcept;

// Reconstructs the ordered throttle-control prefix of one executed AirCraft
// slot-45 force step. Positive health first adds the persistent +0x440 value
// to +0x444 and clamps that target to [0, 1]. Smoothing then always consumes
// the resulting target in the same step. Non-positive health skips the apply
// field entirely but still advances smoothing. The skipped apply value is
// retained without inspection; a later positive-health step rejects it if it
// is non-finite.
//
// Invocation itself means the native rest/sleep entry gate selected the force
// step. Callers composing LegacyVehicleSleepState must use its
// integratePhysics entry decision, not its post-step sleeping() state. This
// contract owns no scheduler, dt, Q15 conversion, event timing, or
// engine-phase transition.
[[nodiscard]] std::optional<LegacyAircraftThrustControlState>
legacyAircraftAdvanceThrustControl(
    LegacyAircraftThrustControlState current,
    LegacyAircraftThrustControlInput input) noexcept;

// Reconstructs the AirCraft +0x568 collision update. Every candidate must pass
// both strict gates before its fourth power contributes. The native routine
// does not clamp the result; its later post-collision refresh does.
[[nodiscard]] std::optional<float> legacyAircraftApplyCollisionThrustDamage(
    float currentThrustIntegrity,
    std::span<const LegacyAircraftCollisionSample> samples) noexcept;

// Reconstructs the later AirCraft +0x568 refresh. When the factor is below one,
// the native MSVCRT rand() integer is multiplied by dt and the exact recovered
// binary32 scale before the result is clamped to [0, 1]. A factor at or above
// one skips both the random sample and dt in the native path.
[[nodiscard]] std::optional<float> legacyAircraftRecoverThrustIntegrity(
    float currentThrustIntegrity,
    float deltaSeconds,
    std::int32_t recoveryRandomSample) noexcept;

} // namespace airfix::simulation
