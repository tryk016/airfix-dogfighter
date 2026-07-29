#include "airfix/audio/WavePcm16.hpp"

#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using airfix::audio::WavePcm16ParseError;
using airfix::audio::WavePcm16ParseErrorCode;
using airfix::audio::WavePcm16ParseLimits;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void appendU16(Bytes &bytes, const std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(Bytes &bytes, const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
  }
}

void writeU16(Bytes &bytes, const std::size_t offset,
              const std::uint16_t value) {
  bytes.at(offset) = static_cast<std::uint8_t>(value);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(Bytes &bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void appendId(Bytes &bytes, const std::string_view id) {
  require(id.size() == 4U, "synthetic WAVE chunk ID is invalid");
  bytes.insert(bytes.end(), id.begin(), id.end());
}

void appendChunk(Bytes &document, const std::string_view id,
                 const Bytes &payload) {
  appendId(document, id);
  appendU32(document, static_cast<std::uint32_t>(payload.size()));
  document.insert(document.end(), payload.begin(), payload.end());
  if ((payload.size() & 1U) != 0U) {
    document.push_back(0U);
  }
}

struct WaveOptions final {
  std::uint16_t encoding{1U};
  std::uint16_t channels{1U};
  std::uint32_t sampleRate{22'050U};
  std::uint16_t bitsPerSample{16U};
  std::uint16_t blockAlign{2U};
  std::uint32_t byteRate{44'100U};
  std::vector<std::int16_t> samples{
      std::int16_t{-32'768},
      std::int16_t{-1},
      std::int16_t{0},
      std::int16_t{32'767},
  };
  bool includeFormat{true};
  bool duplicateFormat{};
  bool includeData{true};
  bool duplicateData{};
  bool includeSampler{true};
  bool duplicateSampler{};
  std::uint32_t samplerLoopCount{1U};
  std::uint32_t samplerLoopType{};
  std::uint32_t samplerStart{};
  std::uint32_t samplerEnd{3U};
  std::uint32_t samplerFraction{};
  std::uint32_t samplerPlayCount{};
};

[[nodiscard]] Bytes makeFormat(const WaveOptions &options) {
  Bytes format;
  appendU16(format, options.encoding);
  appendU16(format, options.channels);
  appendU32(format, options.sampleRate);
  appendU32(format, options.byteRate);
  appendU16(format, options.blockAlign);
  appendU16(format, options.bitsPerSample);
  return format;
}

[[nodiscard]] Bytes makeData(const WaveOptions &options) {
  Bytes data;
  for (const std::int16_t sample : options.samples) {
    appendU16(data, std::bit_cast<std::uint16_t>(sample));
  }
  return data;
}

[[nodiscard]] Bytes makeSampler(const WaveOptions &options) {
  Bytes sampler(36U, 0U);
  writeU32(sampler, 28U, options.samplerLoopCount);
  for (std::uint32_t index = 0U; index < options.samplerLoopCount; ++index) {
    appendU32(sampler, index + 1U);
    appendU32(sampler, options.samplerLoopType);
    appendU32(sampler, options.samplerStart);
    appendU32(sampler, options.samplerEnd);
    appendU32(sampler, options.samplerFraction);
    appendU32(sampler, options.samplerPlayCount);
  }
  return sampler;
}

[[nodiscard]] Bytes makeWave(const WaveOptions &options = {}) {
  Bytes wave;
  appendId(wave, "RIFF");
  appendU32(wave, 0U);
  appendId(wave, "WAVE");
  if (options.includeFormat) {
    const auto format = makeFormat(options);
    appendChunk(wave, "fmt ", format);
    if (options.duplicateFormat) {
      appendChunk(wave, "fmt ", format);
    }
  }
  appendChunk(wave, "JUNK", {0xA5U});
  if (options.includeData) {
    const auto data = makeData(options);
    appendChunk(wave, "data", data);
    if (options.duplicateData) {
      appendChunk(wave, "data", data);
    }
  }
  if (options.includeSampler) {
    const auto sampler = makeSampler(options);
    appendChunk(wave, "smpl", sampler);
    if (options.duplicateSampler) {
      appendChunk(wave, "smpl", sampler);
    }
  }
  writeU32(wave, 4U, static_cast<std::uint32_t>(wave.size() - 8U));
  return wave;
}

void requireError(const WavePcm16ParseErrorCode expected,
                  const std::function<void()> &action) {
  try {
    action();
  } catch (const WavePcm16ParseError &error) {
    require(error.code() == expected, "wrong WAVE parse error code");
    return;
  }
  throw std::runtime_error("expected WAVE parse error was not thrown");
}

void testParsesPcmAndSamplerLoop() {
  const auto bytes = makeWave();
  const auto wave = airfix::audio::parseWavePcm16(bytes);
  require(wave.sampleRate == 22'050U && wave.channelCount == 1U,
          "PCM format changed");
  require(wave.interleavedSamples ==
              std::vector<std::int16_t>({-32'768, -1, 0, 32'767}),
          "signed PCM conversion changed");
  require(wave.frameCount() == 4U, "frame count changed");
  require(wave.samplerLoops.size() == 1U &&
              wave.samplerLoops.front().startFrame == 0U &&
              wave.samplerLoops.front().endFrameInclusive == 3U &&
              wave.samplerLoops.front().playCount == 0U,
          "forward infinite loop metadata changed");
  require(
      airfix::audio::validPcm16Clip(wave.view(airfix::audio::AudioClipId{7U})),
      "parsed PCM view is invalid");
}

void testParsesStereoWithoutSampler() {
  WaveOptions options{
      .channels = 2U,
      .blockAlign = 4U,
      .byteRate = 88'200U,
      .includeSampler = false,
  };
  const auto wave = airfix::audio::parseWavePcm16(makeWave(options));
  require(wave.channelCount == 2U && wave.frameCount() == 2U &&
              wave.samplerLoops.empty(),
          "stereo PCM without smpl changed");
}

void testContainerAndChunkFailures() {
  auto tooSmall = Bytes(11U, 0U);
  requireError(WavePcm16ParseErrorCode::sourceTooSmall,
               [&] { (void)airfix::audio::parseWavePcm16(tooSmall); });

  auto invalidContainer = makeWave();
  invalidContainer[0] = 'X';
  requireError(WavePcm16ParseErrorCode::invalidContainer,
               [&] { (void)airfix::audio::parseWavePcm16(invalidContainer); });

  auto sizeMismatch = makeWave();
  writeU32(sizeMismatch, 4U, 12U);
  requireError(WavePcm16ParseErrorCode::sizeMismatch,
               [&] { (void)airfix::audio::parseWavePcm16(sizeMismatch); });

  auto truncatedPadding = makeWave();
  truncatedPadding.resize(22U);
  writeU32(truncatedPadding, 4U,
           static_cast<std::uint32_t>(truncatedPadding.size() - 8U));
  requireError(WavePcm16ParseErrorCode::truncatedChunk,
               [&] { (void)airfix::audio::parseWavePcm16(truncatedPadding); });

  const auto ordinary = makeWave();
  WavePcm16ParseLimits sourceLimit;
  sourceLimit.maximumSourceBytes = ordinary.size() - 1U;
  requireError(WavePcm16ParseErrorCode::sourceLimitExceeded, [&] {
    (void)airfix::audio::parseWavePcm16(ordinary, sourceLimit);
  });

  WavePcm16ParseLimits chunkLimit;
  chunkLimit.maximumChunks = 1U;
  requireError(WavePcm16ParseErrorCode::chunkLimitExceeded, [&] {
    (void)airfix::audio::parseWavePcm16(ordinary, chunkLimit);
  });
}

void testRequiredAndDuplicateChunksFail() {
  requireError(WavePcm16ParseErrorCode::missingFormatChunk, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.includeFormat = false}));
  });
  requireError(WavePcm16ParseErrorCode::duplicateFormatChunk, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.duplicateFormat = true}));
  });
  requireError(WavePcm16ParseErrorCode::missingDataChunk, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.includeData = false}));
  });
  requireError(WavePcm16ParseErrorCode::duplicateDataChunk, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.duplicateData = true}));
  });
  requireError(WavePcm16ParseErrorCode::duplicateSamplerChunk, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.duplicateSampler = true}));
  });
}

void testFormatFailures() {
  requireError(WavePcm16ParseErrorCode::unsupportedEncoding, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.encoding = 3U}));
  });
  requireError(WavePcm16ParseErrorCode::invalidFormat, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.channels = 3U}));
  });
  requireError(WavePcm16ParseErrorCode::invalidFormat, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.sampleRate = 7'999U}));
  });
  requireError(WavePcm16ParseErrorCode::invalidFormat, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.bitsPerSample = 8U}));
  });
  requireError(WavePcm16ParseErrorCode::invalidFormat, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.blockAlign = 4U}));
  });
  requireError(WavePcm16ParseErrorCode::invalidFormat, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.byteRate = 1U}));
  });

  auto emptyData = makeWave({.samples = {}});
  requireError(WavePcm16ParseErrorCode::emptyDataChunk,
               [&] { (void)airfix::audio::parseWavePcm16(emptyData); });

  const auto ordinary = makeWave();
  WavePcm16ParseLimits dataLimit;
  dataLimit.maximumPcmBytes = 4U;
  requireError(WavePcm16ParseErrorCode::dataLimitExceeded, [&] {
    (void)airfix::audio::parseWavePcm16(ordinary, dataLimit);
  });
}

void testSamplerFailures() {
  requireError(WavePcm16ParseErrorCode::samplerLoopLimitExceeded, [] {
    WavePcm16ParseLimits limits;
    limits.maximumSamplerLoops = 1U;
    (void)airfix::audio::parseWavePcm16(makeWave({.samplerLoopCount = 2U}),
                                        limits);
  });
  requireError(WavePcm16ParseErrorCode::unsupportedSamplerLoopType, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.samplerLoopType = 1U}));
  });
  requireError(WavePcm16ParseErrorCode::invalidSamplerLoop, [] {
    (void)airfix::audio::parseWavePcm16(
        makeWave({.samplerStart = 3U, .samplerEnd = 2U}));
  });
  requireError(WavePcm16ParseErrorCode::invalidSamplerLoop, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.samplerEnd = 4U}));
  });
  requireError(WavePcm16ParseErrorCode::invalidSamplerLoop, [] {
    (void)airfix::audio::parseWavePcm16(makeWave({.samplerFraction = 1U}));
  });
}

} // namespace

int main() {
  try {
    testParsesPcmAndSamplerLoop();
    testParsesStereoWithoutSampler();
    testContainerAndChunkFailures();
    testRequiredAndDuplicateChunksFail();
    testFormatFailures();
    testSamplerFailures();
    std::cout << "WAVE PCM16 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "WAVE PCM16 tests failed: " << error.what() << '\n';
    return 1;
  }
}
