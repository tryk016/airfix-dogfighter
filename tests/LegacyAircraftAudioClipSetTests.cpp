#include "airfix/content/LegacyAircraftAudioClipSet.hpp"
#include "support/SyntheticContent.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using airfix::audio::AudioVoiceId;
using airfix::audio::WavePcm16ParseErrorCode;
using airfix::content::ContentRevision;
using airfix::content::LegacyAircraftAudioClipLoadIssueKind;
using airfix::content::LegacyAircraftAudioClipLoadLimits;
using airfix::content::LegacyAircraftAudioClipLoadPhase;
using airfix::content::LegacyAircraftAudioClipLoadResult;
using airfix::content::VerifiedContentSession;
using airfix::simulation::LegacyAircraftEngineSound;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::array<std::string_view, 6U> logicalPaths{{
    "Sound\\Ingame\\Models\\Ac\\engineon.wav",
    "Sound\\Ingame\\Models\\Ac\\engineidle.wav",
    "Sound\\Ingame\\Models\\Ac\\engineturn.wav",
    "Sound\\Ingame\\Models\\Ac\\enginestart.wav",
    "Sound\\Ingame\\Models\\Ac\\enginestop.wav",
    "Sound\\Ingame\\Models\\Ac\\enginedive.wav",
}};

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
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
  }
}

void writeU32(Bytes &bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void appendId(Bytes &bytes, const std::string_view id) {
  require(id.size() == 4U, "synthetic WAVE ID is invalid");
  bytes.insert(bytes.end(), id.begin(), id.end());
}

void appendChunk(Bytes &wave, const std::string_view id, const Bytes &payload) {
  appendId(wave, id);
  appendU32(wave, static_cast<std::uint32_t>(payload.size()));
  wave.insert(wave.end(), payload.begin(), payload.end());
  if ((payload.size() & 1U) != 0U) {
    wave.push_back(0U);
  }
}

struct WaveLoop final {
  std::uint32_t start{};
  std::uint32_t end{};
  std::uint32_t playCount{};
};

[[nodiscard]] Bytes makeWave(const std::optional<WaveLoop> loop,
                             const std::int16_t seed = 1) {
  Bytes wave;
  appendId(wave, "RIFF");
  appendU32(wave, 0U);
  appendId(wave, "WAVE");

  Bytes format;
  appendU16(format, 1U);
  appendU16(format, 1U);
  appendU32(format, 22'050U);
  appendU32(format, 44'100U);
  appendU16(format, 2U);
  appendU16(format, 16U);
  appendChunk(wave, "fmt ", format);

  Bytes data;
  for (std::int16_t offset = 0; offset < 4; ++offset) {
    appendU16(data, std::bit_cast<std::uint16_t>(
                        static_cast<std::int16_t>(seed + offset)));
  }
  appendChunk(wave, "data", data);

  if (loop.has_value()) {
    Bytes sampler(36U, 0U);
    writeU32(sampler, 28U, 1U);
    appendU32(sampler, 1U);
    appendU32(sampler, 0U);
    appendU32(sampler, loop->start);
    appendU32(sampler, loop->end);
    appendU32(sampler, 0U);
    appendU32(sampler, loop->playCount);
    appendChunk(wave, "smpl", sampler);
  }

  writeU32(wave, 4U, static_cast<std::uint32_t>(wave.size() - 8U));
  return wave;
}

[[nodiscard]] std::vector<UdspInputEntry> representativeEntries() {
  std::vector<UdspInputEntry> entries;
  entries.reserve(logicalPaths.size());
  for (std::size_t index = 0U; index < logicalPaths.size(); ++index) {
    const bool looping = index < 3U || index == 5U;
    const std::optional<WaveLoop> loop =
        looping ? std::optional<WaveLoop>{WaveLoop{.start = 0U, .end = 3U}}
        : index == 3U
            ? std::optional<WaveLoop>{WaveLoop{.start = 2U, .end = 3U}}
            : std::optional<WaveLoop>{WaveLoop{.start = 1U, .end = 1U}};
    entries.push_back({
        .logicalPath = std::string(logicalPaths[index]),
        .bytes = makeWave(loop, static_cast<std::int16_t>(10 + index)),
    });
  }
  return entries;
}

class PackFixture final {
public:
  explicit PackFixture(std::vector<UdspInputEntry> entries)
      : pack_(airfix::testing::makeSyntheticAfPack(entries)),
        path_(workspace_.path() / "private-content.afpack") {
    airfix::testing::detail::writeFile(path_, pack_.bytes);
  }

  [[nodiscard]] VerifiedContentSession
  open(const std::uint64_t generation = 73U) const {
    auto input = std::make_unique<std::ifstream>(path_, std::ios::binary |
                                                            std::ios::ate);
    require(static_cast<bool>(*input),
            "failed to open synthetic authenticated content");
    return VerifiedContentSession::open(std::move(input),
                                        "synthetic owner-local content",
                                        ContentRevision{
                                            .generation = generation,
                                            .pack =
                                                {
                                                    .size = pack_.size,
                                                    .sha256 = pack_.sha256,
                                                },
                                        });
  }

private:
  airfix::testing::detail::TempWorkspace workspace_;
  SyntheticAfPack pack_;
  std::filesystem::path path_;
};

void requireAtomicFailure(const LegacyAircraftAudioClipLoadResult &result,
                          const LegacyAircraftAudioClipLoadIssueKind kind,
                          const std::string_view message) {
  require(!result.clips.has_value() && result.issues.size() == 1U &&
              result.issues.front().kind == kind,
          message);
}

void testLoadsCompleteAuthenticatedClipSet() {
  PackFixture fixture(representativeEntries());
  auto session = fixture.open();
  const auto result = airfix::content::loadLegacyAircraftAudioClips(session);
  require(result.success(), "representative aircraft clips did not load");
  const auto &loaded = *result.clips;
  require(loaded.valid() && loaded.belongsTo(session) &&
              loaded.sourceFootprintBytes != 0U &&
              loaded.publishedPcmBytes == 48U,
          "authenticated clip-set provenance or accounting changed");

  auto equalRevisionDifferentHandle = fixture.open();
  require(!loaded.belongsTo(equalRevisionDifferentHandle),
          "clip set was detached from its authenticated stream identity");

  constexpr std::array<std::uint32_t, 6U> expectedClipIds{0x1AU, 0x1BU, 0x1CU,
                                                          0x1DU, 0x1EU, 0x20U};
  constexpr std::array<bool, 6U> expectedLooping{true,  true,  true,
                                                 false, false, true};
  const auto views = loaded.clipViews();
  for (std::size_t index = 0U; index < loaded.clips.size(); ++index) {
    const auto &clip = loaded.clips[index];
    require(clip.logicalPath == logicalPaths[index] &&
                clip.clip.value == expectedClipIds[index] &&
                clip.looping == expectedLooping[index] &&
                clip.wave.frameCount() == 4U && views[index].id == clip.clip &&
                airfix::audio::validPcm16Clip(views[index]),
            "loaded clip role, policy, identity or PCM changed");
  }

  constexpr std::array<AudioVoiceId, 6U> voices{{
      {101U},
      {102U},
      {103U},
      {104U},
      {105U},
      {106U},
  }};
  const auto bindings = loaded.bindings(voices);
  require(bindings.has_value() &&
              airfix::simulation::validLegacyAircraftAudioBindings(*bindings),
          "valid actor voice allocation did not produce bindings");
  for (std::size_t index = 0U; index < bindings->size(); ++index) {
    require((*bindings)[index].clip.value == expectedClipIds[index] &&
                (*bindings)[index].voice == voices[index] &&
                (*bindings)[index].looping == expectedLooping[index],
            "actor binding changed recovered clip or loop policy");
  }

  auto duplicateVoices = voices;
  duplicateVoices[5] = duplicateVoices[0];
  require(!loaded.bindings(duplicateVoices).has_value(),
          "duplicate actor voice allocation was accepted");
}

void testLookupFailuresAreAtomicAndAttributed() {
  {
    auto entries = representativeEntries();
    entries.erase(entries.begin() + 2);
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result = airfix::content::loadLegacyAircraftAudioClips(session);
    requireAtomicFailure(result,
                         LegacyAircraftAudioClipLoadIssueKind::sampleNotFound,
                         "missing sample did not fail atomically");
    require(result.issues.front().sound ==
                LegacyAircraftEngineSound::engineTurn,
            "missing sample issue lost its role");
  }
  {
    auto entries = representativeEntries();
    entries.push_back(entries.front());
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result = airfix::content::loadLegacyAircraftAudioClips(session);
    requireAtomicFailure(result,
                         LegacyAircraftAudioClipLoadIssueKind::sampleAmbiguous,
                         "ambiguous sample did not fail atomically");
    require(result.issues.front().sound == LegacyAircraftEngineSound::engineOn,
            "ambiguous sample issue lost its role");
  }
}

void testBudgetsFailBeforePublication() {
  PackFixture fixture(representativeEntries());
  {
    auto session = fixture.open();
    LegacyAircraftAudioClipLoadLimits limits;
    limits.maximumSourceBytes = 12U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftAudioClips(session, limits),
        LegacyAircraftAudioClipLoadIssueKind::sourceLimitExceeded,
        "per-source output budget was ignored");
  }
  {
    auto session = fixture.open();
    LegacyAircraftAudioClipLoadLimits limits;
    limits.maximumSingleSourceFootprintBytes = 1U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftAudioClips(session, limits),
        LegacyAircraftAudioClipLoadIssueKind::sourceFootprintLimitExceeded,
        "single-source footprint budget was ignored");
  }
  {
    auto session = fixture.open();
    LegacyAircraftAudioClipLoadLimits limits;
    limits.maximumTotalSourceFootprintBytes = 1U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftAudioClips(session, limits),
        LegacyAircraftAudioClipLoadIssueKind::
            aggregateSourceFootprintLimitExceeded,
        "aggregate source footprint budget was ignored");
  }
  {
    auto session = fixture.open();
    LegacyAircraftAudioClipLoadLimits limits;
    limits.maximumPublishedPcmBytes = 1U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftAudioClips(session, limits),
        LegacyAircraftAudioClipLoadIssueKind::publishedPcmLimitExceeded,
        "published PCM budget was ignored");
  }
  {
    auto session = fixture.open();
    LegacyAircraftAudioClipLoadLimits limits;
    limits.wave.maximumChunks = 0U;
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftAudioClips(session, limits),
        LegacyAircraftAudioClipLoadIssueKind::invalidLimits,
        "invalid WAVE limits were accepted");
  }
}

void testWaveAndLoopFailuresPreserveEvidence() {
  {
    auto entries = representativeEntries();
    entries[1].bytes = {0x00U, 0x01U, 0x02U};
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result = airfix::content::loadLegacyAircraftAudioClips(session);
    requireAtomicFailure(result,
                         LegacyAircraftAudioClipLoadIssueKind::waveParseFailure,
                         "malformed WAVE did not fail atomically");
    require(result.issues.front().sound ==
                    LegacyAircraftEngineSound::engineIdle &&
                result.issues.front().waveParseError ==
                    WavePcm16ParseErrorCode::sourceTooSmall &&
                result.issues.front().waveErrorOffset == 0U,
            "WAVE parse evidence was not retained");
  }
  {
    auto entries = representativeEntries();
    entries[0].bytes = makeWave(std::nullopt);
    PackFixture fixture(std::move(entries));
    auto session = fixture.open();
    const auto result = airfix::content::loadLegacyAircraftAudioClips(session);
    requireAtomicFailure(
        result,
        LegacyAircraftAudioClipLoadIssueKind::requiredLoopEvidenceMissing,
        "missing continuous-loop evidence was accepted");
    require(result.issues.front().sound == LegacyAircraftEngineSound::engineOn,
            "loop evidence issue lost its role");
  }
}

void testCancellationCallbackAndIdentityGuards() {
  PackFixture fixture(representativeEntries());
  {
    auto session = fixture.open();
    std::stop_source source;
    source.request_stop();
    requireAtomicFailure(airfix::content::loadLegacyAircraftAudioClips(
                             session, {}, source.get_token()),
                         LegacyAircraftAudioClipLoadIssueKind::cancelled,
                         "pre-requested cancellation was ignored");
  }
  {
    auto session = fixture.open();
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftAudioClips(
            session, {}, {},
            [](const auto &) {
              throw std::runtime_error("synthetic callback failure");
            }),
        LegacyAircraftAudioClipLoadIssueKind::progressCallbackFailure,
        "callback failure was not contained");
  }
  {
    auto session = fixture.open(91U);
    auto replacement = fixture.open(91U);
    bool replaced = false;
    const auto result = airfix::content::loadLegacyAircraftAudioClips(
        session, {}, {}, [&](const auto &progress) {
          if (!replaced &&
              progress.phase ==
                  LegacyAircraftAudioClipLoadPhase::lookingUpSamples) {
            session = std::move(replacement);
            replaced = true;
          }
        });
    require(replaced, "identity replacement callback did not run");
    requireAtomicFailure(
        result, LegacyAircraftAudioClipLoadIssueKind::sessionIdentityChanged,
        "same-revision authenticated handle replacement escaped detection");
  }
  {
    auto session = fixture.open();
    auto moved = std::move(session);
    requireAtomicFailure(
        airfix::content::loadLegacyAircraftAudioClips(session),
        LegacyAircraftAudioClipLoadIssueKind::sessionIdentityChanged,
        "moved-from authenticated session was accepted");
    require(airfix::content::loadLegacyAircraftAudioClips(moved).success(),
            "moved-to authenticated session became unusable");
  }
}

} // namespace

int main() {
  try {
    testLoadsCompleteAuthenticatedClipSet();
    testLookupFailuresAreAtomicAndAttributed();
    testBudgetsFailBeforePublication();
    testWaveAndLoopFailuresPreserveEvidence();
    testCancellationCallbackAndIdentityGuards();
    std::cout << "Legacy aircraft audio clip-set tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft audio clip-set tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
