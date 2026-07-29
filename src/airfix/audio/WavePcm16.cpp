#include "airfix/audio/WavePcm16.hpp"

#include <bit>
#include <limits>
#include <optional>

namespace airfix::audio {
namespace {

constexpr std::uint32_t fourCc(const char a, const char b, const char c,
                               const char d) noexcept {
  return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
}

constexpr std::uint32_t riffId = fourCc('R', 'I', 'F', 'F');
constexpr std::uint32_t waveId = fourCc('W', 'A', 'V', 'E');
constexpr std::uint32_t formatId = fourCc('f', 'm', 't', ' ');
constexpr std::uint32_t dataId = fourCc('d', 'a', 't', 'a');
constexpr std::uint32_t samplerId = fourCc('s', 'm', 'p', 'l');
constexpr std::uint16_t integerPcmEncoding = 1U;
constexpr std::size_t riffHeaderBytes = 12U;
constexpr std::size_t chunkHeaderBytes = 8U;
constexpr std::size_t minimumFormatBytes = 16U;
constexpr std::size_t samplerHeaderBytes = 36U;
constexpr std::size_t samplerLoopBytes = 24U;

[[noreturn]] void fail(const WavePcm16ParseErrorCode code,
                       const std::size_t offset, const char *const message) {
  throw WavePcm16ParseError(code, offset, message);
}

[[nodiscard]] std::uint16_t readU16(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 2U) {
    fail(WavePcm16ParseErrorCode::truncatedChunk, offset,
         "WAVE u16 read exceeds input");
  }
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4U) {
    fail(WavePcm16ParseErrorCode::truncatedChunk, offset,
         "WAVE u32 read exceeds input");
  }
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] bool validLimits(const WavePcm16ParseLimits &limits) noexcept {
  return limits.maximumSourceBytes >= riffHeaderBytes &&
         limits.maximumPcmBytes != 0U &&
         limits.maximumPcmBytes <= maximumPcm16ClipBytes &&
         limits.maximumChunks != 0U && limits.maximumSamplerLoops != 0U;
}

struct ChunkLocation final {
  std::size_t payloadOffset{};
  std::size_t payloadSize{};
};

} // namespace

WavePcm16ParseError::WavePcm16ParseError(const WavePcm16ParseErrorCode code,
                                         const std::size_t offset,
                                         const char *const message)
    : std::runtime_error(message), code_(code), offset_(offset) {}

WavePcm16 parseWavePcm16(const std::span<const std::uint8_t> bytes,
                         const WavePcm16ParseLimits &limits) {
  if (!validLimits(limits)) {
    fail(WavePcm16ParseErrorCode::invalidLimits, 0U,
         "WAVE parser limits are invalid");
  }
  if (bytes.size() < riffHeaderBytes) {
    fail(WavePcm16ParseErrorCode::sourceTooSmall, 0U,
         "WAVE source is too small");
  }
  if (bytes.size() > limits.maximumSourceBytes) {
    fail(WavePcm16ParseErrorCode::sourceLimitExceeded, 0U,
         "WAVE source exceeds configured limit");
  }
  if (readU32(bytes, 0U) != riffId || readU32(bytes, 8U) != waveId) {
    fail(WavePcm16ParseErrorCode::invalidContainer, 0U,
         "source is not a RIFF/WAVE document");
  }

  const std::uint64_t declaredBytes =
      static_cast<std::uint64_t>(readU32(bytes, 4U)) + 8U;
  if (declaredBytes != bytes.size()) {
    fail(WavePcm16ParseErrorCode::sizeMismatch, 4U,
         "RIFF declared size does not match source");
  }

  std::optional<ChunkLocation> format;
  std::optional<ChunkLocation> data;
  std::optional<ChunkLocation> sampler;
  std::size_t cursor = riffHeaderBytes;
  std::size_t chunkCount = 0U;
  while (cursor != bytes.size()) {
    if (chunkCount == limits.maximumChunks) {
      fail(WavePcm16ParseErrorCode::chunkLimitExceeded, cursor,
           "WAVE chunk count exceeds configured limit");
    }
    ++chunkCount;
    if (cursor > bytes.size() || bytes.size() - cursor < chunkHeaderBytes) {
      fail(WavePcm16ParseErrorCode::truncatedChunk, cursor,
           "WAVE chunk header is truncated");
    }

    const std::uint32_t id = readU32(bytes, cursor);
    const std::size_t payloadSize = readU32(bytes, cursor + 4U);
    const std::size_t payloadOffset = cursor + chunkHeaderBytes;
    if (payloadOffset > bytes.size() ||
        payloadSize > bytes.size() - payloadOffset) {
      fail(WavePcm16ParseErrorCode::truncatedChunk, cursor,
           "WAVE chunk payload is truncated");
    }
    const std::size_t payloadEnd = payloadOffset + payloadSize;
    const std::size_t padding = payloadSize & 1U;
    if (padding > bytes.size() - payloadEnd) {
      fail(WavePcm16ParseErrorCode::truncatedChunk, payloadEnd,
           "WAVE chunk padding is truncated");
    }

    const ChunkLocation location{
        .payloadOffset = payloadOffset,
        .payloadSize = payloadSize,
    };
    if (id == formatId) {
      if (format.has_value()) {
        fail(WavePcm16ParseErrorCode::duplicateFormatChunk, cursor,
             "WAVE contains duplicate fmt chunks");
      }
      format = location;
    } else if (id == dataId) {
      if (data.has_value()) {
        fail(WavePcm16ParseErrorCode::duplicateDataChunk, cursor,
             "WAVE contains duplicate data chunks");
      }
      data = location;
    } else if (id == samplerId) {
      if (sampler.has_value()) {
        fail(WavePcm16ParseErrorCode::duplicateSamplerChunk, cursor,
             "WAVE contains duplicate smpl chunks");
      }
      sampler = location;
    }
    cursor = payloadEnd + padding;
  }

  if (!format.has_value()) {
    fail(WavePcm16ParseErrorCode::missingFormatChunk, 0U,
         "WAVE has no fmt chunk");
  }
  if (!data.has_value()) {
    fail(WavePcm16ParseErrorCode::missingDataChunk, 0U,
         "WAVE has no data chunk");
  }
  if (format->payloadSize < minimumFormatBytes) {
    fail(WavePcm16ParseErrorCode::invalidFormat, format->payloadOffset,
         "WAVE fmt chunk is too small");
  }

  const std::uint16_t encoding = readU16(bytes, format->payloadOffset);
  if (encoding != integerPcmEncoding) {
    fail(WavePcm16ParseErrorCode::unsupportedEncoding, format->payloadOffset,
         "WAVE encoding is not integer PCM");
  }
  const std::uint16_t channelCount = readU16(bytes, format->payloadOffset + 2U);
  const std::uint32_t sampleRate = readU32(bytes, format->payloadOffset + 4U);
  const std::uint32_t byteRate = readU32(bytes, format->payloadOffset + 8U);
  const std::uint16_t blockAlign = readU16(bytes, format->payloadOffset + 12U);
  const std::uint16_t bitsPerSample =
      readU16(bytes, format->payloadOffset + 14U);
  const std::uint32_t expectedBlockAlign =
      static_cast<std::uint32_t>(channelCount) * sizeof(std::int16_t);
  const std::uint64_t expectedByteRate =
      static_cast<std::uint64_t>(sampleRate) * expectedBlockAlign;
  if (channelCount == 0U || channelCount > maximumPcmChannelCount ||
      sampleRate < minimumPcmSampleRate || sampleRate > maximumPcmSampleRate ||
      bitsPerSample != 16U ||
      expectedBlockAlign > std::numeric_limits<std::uint16_t>::max() ||
      blockAlign != expectedBlockAlign ||
      expectedByteRate > std::numeric_limits<std::uint32_t>::max() ||
      byteRate != expectedByteRate) {
    fail(WavePcm16ParseErrorCode::invalidFormat, format->payloadOffset,
         "WAVE PCM format fields are inconsistent or unsupported");
  }
  if (data->payloadSize == 0U) {
    fail(WavePcm16ParseErrorCode::emptyDataChunk, data->payloadOffset,
         "WAVE data chunk is empty");
  }
  if (data->payloadSize > limits.maximumPcmBytes ||
      data->payloadSize > maximumPcm16ClipBytes) {
    fail(WavePcm16ParseErrorCode::dataLimitExceeded, data->payloadOffset,
         "WAVE PCM data exceeds configured limit");
  }
  if (data->payloadSize % blockAlign != 0U) {
    fail(WavePcm16ParseErrorCode::invalidFormat, data->payloadOffset,
         "WAVE PCM data is not frame aligned");
  }

  WavePcm16 result{
      .sampleRate = sampleRate,
      .channelCount = channelCount,
      .interleavedSamples = {},
      .samplerLoops = {},
  };
  const std::size_t sampleCount = data->payloadSize / sizeof(std::int16_t);
  result.interleavedSamples.resize(sampleCount);
  for (std::size_t index = 0U; index < sampleCount; ++index) {
    const std::size_t offset =
        data->payloadOffset + index * sizeof(std::int16_t);
    result.interleavedSamples[index] =
        std::bit_cast<std::int16_t>(readU16(bytes, offset));
  }

  if (sampler.has_value()) {
    if (sampler->payloadSize < samplerHeaderBytes) {
      fail(WavePcm16ParseErrorCode::malformedSamplerChunk,
           sampler->payloadOffset, "WAVE smpl header is truncated");
    }
    const std::uint32_t loopCount =
        readU32(bytes, sampler->payloadOffset + 28U);
    const std::uint32_t samplerDataBytes =
        readU32(bytes, sampler->payloadOffset + 32U);
    if (loopCount > limits.maximumSamplerLoops) {
      fail(WavePcm16ParseErrorCode::samplerLoopLimitExceeded,
           sampler->payloadOffset + 28U,
           "WAVE smpl loop count exceeds configured limit");
    }
    const std::uint64_t requiredBytes =
        samplerHeaderBytes +
        static_cast<std::uint64_t>(loopCount) * samplerLoopBytes +
        samplerDataBytes;
    if (requiredBytes > sampler->payloadSize) {
      fail(WavePcm16ParseErrorCode::malformedSamplerChunk,
           sampler->payloadOffset, "WAVE smpl payload is truncated");
    }

    const std::size_t frameCount = result.frameCount();
    result.samplerLoops.reserve(loopCount);
    for (std::uint32_t index = 0U; index < loopCount; ++index) {
      const std::size_t loopOffset =
          sampler->payloadOffset + samplerHeaderBytes +
          static_cast<std::size_t>(index) * samplerLoopBytes;
      const std::uint32_t loopType = readU32(bytes, loopOffset + 4U);
      const std::uint32_t startFrame = readU32(bytes, loopOffset + 8U);
      const std::uint32_t endFrame = readU32(bytes, loopOffset + 12U);
      const std::uint32_t fraction = readU32(bytes, loopOffset + 16U);
      const std::uint32_t playCount = readU32(bytes, loopOffset + 20U);
      if (loopType != 0U) {
        fail(WavePcm16ParseErrorCode::unsupportedSamplerLoopType,
             loopOffset + 4U, "WAVE smpl loop is not a forward loop");
      }
      if (fraction != 0U || startFrame > endFrame || endFrame >= frameCount) {
        fail(WavePcm16ParseErrorCode::invalidSamplerLoop, loopOffset,
             "WAVE smpl loop range is invalid");
      }
      result.samplerLoops.push_back({
          .startFrame = startFrame,
          .endFrameInclusive = endFrame,
          .playCount = playCount,
      });
    }
  }

  if (!validPcm16Clip(result.view(AudioClipId{1U}))) {
    fail(WavePcm16ParseErrorCode::invalidFormat, 0U,
         "parsed WAVE does not satisfy PCM16 registration contract");
  }
  return result;
}

} // namespace airfix::audio
