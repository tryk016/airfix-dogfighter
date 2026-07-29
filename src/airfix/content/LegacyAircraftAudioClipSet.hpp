#pragma once

#include "airfix/audio/WavePcm16.hpp"
#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/simulation/LegacyAircraftAudioCoordinator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace airfix::content {

inline constexpr std::size_t legacyAircraftAudioClipCount =
    simulation::legacyAircraftAudioSoundCount;

struct LegacyAircraftAudioClip final {
  simulation::LegacyAircraftEngineSound sound{
      simulation::LegacyAircraftEngineSound::none};
  audio::AudioClipId clip{};
  bool looping{};
  std::size_t sourceFileIndex{};
  std::string logicalPath;
  audio::WavePcm16 wave;

  [[nodiscard]] audio::Pcm16ClipView view() const noexcept {
    return wave.view(clip);
  }
};

using LegacyAircraftAudioClips =
    std::array<LegacyAircraftAudioClip, legacyAircraftAudioClipCount>;

struct LoadedLegacyAircraftAudioClips final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  LegacyAircraftAudioClips clips;
  std::uint64_t sourceFootprintBytes{};
  std::uint64_t publishedPcmBytes{};

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] bool
  belongsTo(const VerifiedContentSession &session) const noexcept;

  [[nodiscard]] std::array<audio::Pcm16ClipView, legacyAircraftAudioClipCount>
  clipViews() const noexcept;

  [[nodiscard]] std::optional<simulation::LegacyAircraftAudioBindings>
  bindings(const std::array<audio::AudioVoiceId, legacyAircraftAudioClipCount>
               &voices) const noexcept;
};

struct LegacyAircraftAudioClipLoadLimits final {
  audio::WavePcm16ParseLimits wave{};
  std::size_t maximumSourceBytes{4U * 1024U * 1024U};
  std::uint64_t maximumSingleSourceFootprintBytes{8U * 1024U * 1024U};
  std::uint64_t maximumTotalSourceFootprintBytes{32U * 1024U * 1024U};
  std::uint64_t maximumPublishedPcmBytes{32U * 1024U * 1024U};
};

enum class LegacyAircraftAudioClipLoadPhase : std::uint8_t {
  validatingInput,
  lookingUpSamples,
  readingSamples,
  parsingSamples,
  complete,
};

struct LegacyAircraftAudioClipLoadProgress final {
  LegacyAircraftAudioClipLoadPhase phase{};
  std::size_t completedItems{};
  std::size_t totalItems{};
};

using LegacyAircraftAudioClipLoadProgressCallback =
    std::function<void(const LegacyAircraftAudioClipLoadProgress &)>;

enum class LegacyAircraftAudioClipLoadIssueKind : std::uint8_t {
  cancelled,
  invalidLimits,
  sessionIdentityChanged,
  sampleNotFound,
  sampleAmbiguous,
  sourceLimitExceeded,
  sourceFootprintLimitExceeded,
  aggregateSourceFootprintLimitExceeded,
  sourceReadFailure,
  waveParseFailure,
  requiredLoopEvidenceMissing,
  publishedPcmLimitExceeded,
  integerOverflow,
  allocationFailure,
  progressCallbackFailure,
  internalFailure,
};

struct LegacyAircraftAudioClipLoadIssue final {
  LegacyAircraftAudioClipLoadIssueKind kind{
      LegacyAircraftAudioClipLoadIssueKind::internalFailure};
  std::optional<simulation::LegacyAircraftEngineSound> sound;
  std::optional<std::size_t> sourceFileIndex;
  std::optional<audio::WavePcm16ParseErrorCode> waveParseError;
  std::optional<std::size_t> waveErrorOffset;
};

struct LegacyAircraftAudioClipLoadResult final {
  // Published only after all six samples pass lookup, budget, WAVE and loop
  // checks against one unchanged authenticated content transaction.
  std::optional<LoadedLegacyAircraftAudioClips> clips;
  std::vector<LegacyAircraftAudioClipLoadIssue> issues;

  [[nodiscard]] bool success() const noexcept {
    return clips.has_value() && issues.empty();
  }
};

// Resolves the six recovered aircraft sound roles from the authenticated nested
// Resource.up owned by session. No logical path is opened as a host path and no
// partial clip set is published on failure.
[[nodiscard]] LegacyAircraftAudioClipLoadResult loadLegacyAircraftAudioClips(
    VerifiedContentSession &session,
    const LegacyAircraftAudioClipLoadLimits &limits = {},
    std::stop_token stopToken = {},
    LegacyAircraftAudioClipLoadProgressCallback progress = {});

} // namespace airfix::content
