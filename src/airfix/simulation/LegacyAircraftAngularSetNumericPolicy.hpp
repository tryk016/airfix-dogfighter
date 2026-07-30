#pragma once

#include <bit>
#include <cstdint>

namespace airfix::simulation {

inline constexpr std::uint32_t legacyAircraftAngularSetScaleBits = 0x3D3020C5U;
inline constexpr float legacyAircraftAngularSetScale =
    std::bit_cast<float>(legacyAircraftAngularSetScaleBits);

// This is a compatibility policy supported by the original process startup
// state. It is not a claim that the live x87 control word has been observed
// while a native event is processed.
enum class LegacyAircraftAngularSetNumericPolicy : std::uint8_t {
    startupPc53RoundToNearestEven,
};

} // namespace airfix::simulation
