#pragma once

#include "airfix/audio/AudioCommand.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace airfix::audio {

inline constexpr std::size_t maximumWavePcm16SourceBytes =
    maximumPcm16ClipBytes + 1024U * 1024U;

struct WavePcm16ParseLimits final {
  std::size_t maximumSourceBytes{maximumWavePcm16SourceBytes};
  std::size_t maximumPcmBytes{maximumPcm16ClipBytes};
  std::size_t maximumChunks{256U};
  std::size_t maximumSamplerLoops{16U};
};

enum class WavePcm16ParseErrorCode : std::uint8_t {
  invalidLimits,
  sourceTooSmall,
  sourceLimitExceeded,
  invalidContainer,
  sizeMismatch,
  chunkLimitExceeded,
  truncatedChunk,
  duplicateFormatChunk,
  missingFormatChunk,
  unsupportedEncoding,
  invalidFormat,
  duplicateDataChunk,
  missingDataChunk,
  emptyDataChunk,
  dataLimitExceeded,
  duplicateSamplerChunk,
  malformedSamplerChunk,
  samplerLoopLimitExceeded,
  unsupportedSamplerLoopType,
  invalidSamplerLoop,
};

class WavePcm16ParseError final : public std::runtime_error {
public:
  WavePcm16ParseError(WavePcm16ParseErrorCode code, std::size_t offset,
                      const char *message);

  [[nodiscard]] WavePcm16ParseErrorCode code() const noexcept { return code_; }
  [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

private:
  WavePcm16ParseErrorCode code_;
  std::size_t offset_;
};

struct WavePcm16SamplerLoop final {
  std::uint32_t startFrame{};
  std::uint32_t endFrameInclusive{};
  // RIFF smpl uses zero for infinite playback.
  std::uint32_t playCount{};

  friend bool operator==(const WavePcm16SamplerLoop &,
                         const WavePcm16SamplerLoop &) = default;
};

struct WavePcm16 final {
  std::uint32_t sampleRate{};
  std::uint16_t channelCount{};
  std::vector<std::int16_t> interleavedSamples;
  std::vector<WavePcm16SamplerLoop> samplerLoops;

  [[nodiscard]] std::size_t frameCount() const noexcept {
    return channelCount == 0U ? 0U
                              : interleavedSamples.size() /
                                    static_cast<std::size_t>(channelCount);
  }

  [[nodiscard]] Pcm16ClipView view(AudioClipId id) const noexcept {
    return {
        .id = id,
        .sampleRate = sampleRate,
        .channelCount = channelCount,
        .interleavedSamples = interleavedSamples,
    };
  }
};

// Parses one complete RIFF/WAVE document. Only uncompressed integer PCM with
// 16-bit mono/stereo samples is accepted because that is the platform-neutral
// registration contract. Unknown chunks are skipped structurally; an optional
// smpl chunk is retained only when every loop is a whole-frame forward loop.
[[nodiscard]] WavePcm16 parseWavePcm16(std::span<const std::uint8_t> bytes,
                                       const WavePcm16ParseLimits &limits = {});

} // namespace airfix::audio
