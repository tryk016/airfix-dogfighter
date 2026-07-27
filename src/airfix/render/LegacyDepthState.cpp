#include "airfix/render/LegacyDepthState.hpp"

namespace airfix::render {

std::optional<LegacyDepthState> legacyDepthStateForMode(
    const std::uint32_t rawMode) noexcept {
    switch (rawMode) {
    case 1U:
        return LegacyDepthState{
            .mode = LegacyDepthMode::opaqueDepthTestWrite,
            .compare = LegacyDepthCompare::greaterEqual,
            .writeEnabled = true,
        };
    case 2U:
        return LegacyDepthState{
            .mode = LegacyDepthMode::unconditionalDepthWrite,
            .compare = LegacyDepthCompare::always,
            .writeEnabled = true,
        };
    case 3U:
        return LegacyDepthState{
            .mode = LegacyDepthMode::layerDepthTestNoWrite,
            .compare = LegacyDepthCompare::greaterEqual,
            .writeEnabled = false,
        };
    case 4U:
        return LegacyDepthState{
            .mode = LegacyDepthMode::overlayNoDepthNoWrite,
            .compare = LegacyDepthCompare::always,
            .writeEnabled = false,
        };
    default:
        return std::nullopt;
    }
}

} // namespace airfix::render
