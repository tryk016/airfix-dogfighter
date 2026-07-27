#pragma once

#include <cstdint>
#include <stdexcept>

namespace airfix::assets {

class ParseError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::uint32_t fourCC(
    const char a,
    const char b,
    const char c,
    const char d) noexcept {
    return static_cast<std::uint8_t>(a) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
}

} // namespace airfix::assets
