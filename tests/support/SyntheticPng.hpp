#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace airfix::testing {

using SyntheticPngBytes = std::vector<std::uint8_t>;

namespace synthetic_png_detail {

inline void appendBigEndian(SyntheticPngBytes &bytes,
                            const std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

[[nodiscard]] inline std::uint32_t
crc32(const std::span<const std::uint8_t> bytes) {
  std::uint32_t value = 0xFFFF'FFFFU;
  for (const auto byte : bytes) {
    value ^= byte;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
      const auto mask =
          static_cast<std::uint32_t>(-static_cast<std::int32_t>(value & 1U));
      value = (value >> 1U) ^ (0xEDB8'8320U & mask);
    }
  }
  return value ^ 0xFFFF'FFFFU;
}

[[nodiscard]] inline std::uint32_t
adler32(const std::span<const std::uint8_t> bytes) {
  constexpr std::uint32_t modulus = 65'521U;
  std::uint32_t first = 1U;
  std::uint32_t second = 0U;
  for (const auto byte : bytes) {
    first = (first + byte) % modulus;
    second = (second + first) % modulus;
  }
  return (second << 16U) | first;
}

inline void appendChunk(SyntheticPngBytes &png,
                        const std::array<char, 4U> &type,
                        const std::span<const std::uint8_t> data) {
  appendBigEndian(png, static_cast<std::uint32_t>(data.size()));
  const auto typeOffset = png.size();
  for (const auto character : type) {
    png.push_back(static_cast<std::uint8_t>(character));
  }
  png.insert(png.end(), data.begin(), data.end());
  appendBigEndian(png, crc32(std::span(png).subspan(
                           typeOffset, type.size() + data.size())));
}

} // namespace synthetic_png_detail

[[nodiscard]] inline SyntheticPngBytes
makeSolidRgba8(const std::uint32_t width, const std::uint32_t height,
               const std::array<std::uint8_t, 4U> color) {
  SyntheticPngBytes pixels(static_cast<std::size_t>(width) * height * 4U);
  for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
    std::copy(color.begin(), color.end(),
              pixels.begin() + static_cast<std::ptrdiff_t>(offset));
  }
  return pixels;
}

[[nodiscard]] inline SyntheticPngBytes
makeSyntheticRgba8Png(const std::uint32_t width, const std::uint32_t height,
                      const std::span<const std::uint8_t> rgba) {
  const auto rowBytes = static_cast<std::size_t>(width) * 4U;
  if (width == 0U || height == 0U || rgba.size() != rowBytes * height) {
    throw std::runtime_error("synthetic PNG dimensions are invalid");
  }

  SyntheticPngBytes filtered;
  filtered.reserve((rowBytes + 1U) * height);
  for (std::uint32_t row = 0U; row < height; ++row) {
    filtered.push_back(0U);
    const auto begin =
        rgba.begin() + static_cast<std::ptrdiff_t>(row * rowBytes);
    filtered.insert(filtered.end(), begin,
                    begin + static_cast<std::ptrdiff_t>(rowBytes));
  }
  if (filtered.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("synthetic PNG fixture exceeds one stored block");
  }

  SyntheticPngBytes zlib{0x78U, 0x01U, 0x01U};
  const auto length = static_cast<std::uint16_t>(filtered.size());
  const auto inverse = static_cast<std::uint16_t>(~length);
  zlib.push_back(static_cast<std::uint8_t>(length));
  zlib.push_back(static_cast<std::uint8_t>(length >> 8U));
  zlib.push_back(static_cast<std::uint8_t>(inverse));
  zlib.push_back(static_cast<std::uint8_t>(inverse >> 8U));
  zlib.insert(zlib.end(), filtered.begin(), filtered.end());
  synthetic_png_detail::appendBigEndian(
      zlib, synthetic_png_detail::adler32(filtered));

  SyntheticPngBytes ihdr;
  synthetic_png_detail::appendBigEndian(ihdr, width);
  synthetic_png_detail::appendBigEndian(ihdr, height);
  ihdr.insert(ihdr.end(), {8U, 6U, 0U, 0U, 0U});

  SyntheticPngBytes png{0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
  synthetic_png_detail::appendChunk(png, {'I', 'H', 'D', 'R'}, ihdr);
  synthetic_png_detail::appendChunk(png, {'I', 'D', 'A', 'T'}, zlib);
  synthetic_png_detail::appendChunk(png, {'I', 'E', 'N', 'D'}, {});
  return png;
}

} // namespace airfix::testing
