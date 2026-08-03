#pragma once

#include <cstdint>

namespace airfix::texture {

// Texture source selection is intentionally independent from the visual
// presentation profile. Classic always means authenticated legacy GTI input;
// Enhanced means an owner-local reviewed replacement is attempted per texture
// and falls back to that same GTI input on every failure.
enum class TextureMode : std::uint8_t {
  classic,
  enhanced,
};

} // namespace airfix::texture
