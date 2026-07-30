#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::int64_t
    legacyVehicleInitialRestDurationMilliseconds = 1999;
inline constexpr std::int64_t
    legacyVehicleSleepThresholdMilliseconds = 2000;
inline constexpr float
    legacyVehicleGroundRestSpeedSquaredThreshold = 0.03F;
inline constexpr float
    legacyVehicleWaterRestSpeedSquaredThreshold = 0.08F;

// Reconstructs the sleep/rest portion of AfVehicle::ProcessEvent refresh
// events 0x76..0x78. wakeControlValues are the five native float controls
// checked at +0x444 target thrust, +0x440 thrust apply, +0x450 turn,
// +0x44C bank, and +0x448 pitch. The caller must apply control events, which
// wake the native vehicle by clearing its rest duration, before advancing the
// corresponding refresh.
struct LegacyVehicleSleepStepInput final {
    std::array<float, 5U> wakeControlValues{};
    float linearVelocitySquared{};
    bool onGround{};
    bool waterUnit{};
    std::int64_t refreshDeltaMilliseconds{};
};

struct LegacyVehicleSleepStepResult final {
    std::int64_t restDurationMilliseconds{};

    // Native physics runs when the duration at refresh entry is below 2000 ms.
    bool integratePhysics{};

    // Set only on the active step that reaches the sleep threshold. That step
    // resets force/torque and clears six rigid-body dynamic-state floats.
    bool clearDynamics{};

    [[nodiscard]] constexpr bool sleeping() const noexcept {
        return restDurationMilliseconds >=
            legacyVehicleSleepThresholdMilliseconds;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyVehicleSleepStepResult&,
        const LegacyVehicleSleepStepResult&) noexcept = default;
};

// Pure and allocation-free. Active refreshes fail closed for non-finite or
// negative derived velocity input and signed-millisecond overflow. A refresh
// that enters asleep does not inspect active-path inputs, matching the native
// entry gate. Finite negative scheduler deltas remain accepted because the
// recovered native payload is signed.
[[nodiscard]] std::optional<LegacyVehicleSleepStepResult>
legacyVehicleAdvanceSleepStep(
    std::int64_t currentRestDurationMilliseconds,
    const LegacyVehicleSleepStepInput& input) noexcept;

} // namespace airfix::simulation
