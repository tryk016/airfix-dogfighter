#pragma once

#include <cstdint>

namespace airfix::simulation::detail {

// Exact FILD -> FMUL -> PC53/RNE -> FST m32 compatibility result shared by
// the native TURN_SET, PITCH_SET, and BANK_SET decoders.
[[nodiscard]] std::uint32_t
legacyAircraftDecodeAngularSetBits(std::int32_t payload) noexcept;

} // namespace airfix::simulation::detail
