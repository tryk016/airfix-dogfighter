#pragma once

#include "airfix/input/ActionIds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace airfix::input {

// Symmetric signed Q15. -32768 is deliberately excluded so negation is safe.
using Q15 = std::int16_t;
inline constexpr Q15 q15Min = static_cast<Q15>(-32767);
inline constexpr Q15 q15Zero = 0;
inline constexpr Q15 q15One = 32767;
inline constexpr Q15 controllerTriggerActuationQ15 = 16384;
inline constexpr Q15 uiNavigationActuationQ15 = 16384;
inline constexpr Q15 uiNavigationReleaseQ15 = 8192;

[[nodiscard]] constexpr Q15 clampQ15(const std::int32_t value) noexcept {
    if (value < static_cast<std::int32_t>(q15Min)) {
        return q15Min;
    }
    if (value > static_cast<std::int32_t>(q15One)) {
        return q15One;
    }
    return static_cast<Q15>(value);
}

inline constexpr std::uint16_t inputFrameSchemaVersion = 1U;
inline constexpr std::uint8_t noWeaponSelection =
    std::numeric_limits<std::uint8_t>::max();
inline constexpr std::uint8_t weaponSlotCount = 8U;
inline constexpr std::size_t digitalBitWordCount = (digitalActionCount + 63U) / 64U;

struct InputFrame final {
    std::uint16_t schemaVersion{inputFrameSchemaVersion};
    std::uint64_t simulationTick{};
    std::array<Q15, analogAxisCount> analogValues{};
    std::array<std::uint64_t, digitalBitWordCount> pressedBits{};
    std::array<std::uint64_t, digitalBitWordCount> releasedBits{};
    std::array<std::uint64_t, digitalBitWordCount> heldBits{};
    std::uint8_t weaponSelection{noWeaponSelection};

    [[nodiscard]] constexpr Q15 analog(const AnalogAxis axis) const noexcept {
        const auto index = toIndex(axis);
        return index < analogValues.size() ? analogValues[index] : q15Zero;
    }

    [[nodiscard]] constexpr bool pressed(const DigitalAction action) const noexcept {
        return test(pressedBits, action);
    }

    [[nodiscard]] constexpr bool released(const DigitalAction action) const noexcept {
        return test(releasedBits, action);
    }

    [[nodiscard]] constexpr bool held(const DigitalAction action) const noexcept {
        return test(heldBits, action);
    }

    [[nodiscard]] constexpr bool hasWeaponSelection() const noexcept {
        return weaponSelection != noWeaponSelection;
    }

    [[nodiscard]] constexpr bool hasValidWeaponSelection() const noexcept {
        return !hasWeaponSelection() || weaponSelection < weaponSlotCount;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const InputFrame&, const InputFrame&) noexcept = default;

private:
    [[nodiscard]] static constexpr bool test(
        const std::array<std::uint64_t, digitalBitWordCount>& bits,
        const DigitalAction action) noexcept {
        const auto index = toIndex(action);
        if (index >= digitalActionCount) {
            return false;
        }
        return (bits[index / 64U] & (std::uint64_t{1U} << (index % 64U))) != 0U;
    }
};

} // namespace airfix::input
