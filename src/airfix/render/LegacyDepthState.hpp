#pragma once

#include <cstdint>
#include <optional>

namespace airfix::render {

// Numeric values match the four recovered GtScreen::SetRenderState modes.
enum class LegacyDepthMode : std::uint8_t {
    opaqueDepthTestWrite = 1U,
    unconditionalDepthWrite = 2U,
    layerDepthTestNoWrite = 3U,
    overlayNoDepthNoWrite = 4U,
};

enum class LegacyDepthCompare : std::uint8_t {
    greaterEqual,
    always,
};

struct LegacyDepthState final {
    LegacyDepthMode mode{LegacyDepthMode::opaqueDepthTestWrite};
    LegacyDepthCompare compare{LegacyDepthCompare::greaterEqual};
    bool writeEnabled{true};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyDepthState&,
        const LegacyDepthState&) noexcept = default;
};

inline constexpr float legacyReverseDepthClearValue = 0.0F;

// Maps an untrusted/raw legacy mode number to the active Direct3D path's
// recovered compare/write contract. The dormant hardware-W path is excluded.
[[nodiscard]] std::optional<LegacyDepthState>
legacyDepthStateForMode(std::uint32_t rawMode) noexcept;

} // namespace airfix::render
