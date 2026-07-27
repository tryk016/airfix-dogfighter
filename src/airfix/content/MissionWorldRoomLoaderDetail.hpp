#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace airfix::content::detail {

// Internal accounting primitives shared with focused boundary tests. On
// failure, the output operand is left unchanged.
[[nodiscard]] inline bool
checkedMissionWorldRoomByteAdd(std::uint64_t &total,
                               const std::uint64_t addition) noexcept {
    if (addition > std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    total += addition;
    return true;
}

[[nodiscard]] inline bool
checkedMissionWorldRoomByteProduct(const std::size_t count,
                                   const std::size_t elementSize,
                                   std::uint64_t &product) noexcept {
    if (elementSize != 0U &&
        count > std::numeric_limits<std::size_t>::max() / elementSize) {
        return false;
    }
    const auto value = count * elementSize;
    if constexpr (std::numeric_limits<std::size_t>::max() >
                  std::numeric_limits<std::uint64_t>::max()) {
        if (value > std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
    }
    product = static_cast<std::uint64_t>(value);
    return true;
}

} // namespace airfix::content::detail
