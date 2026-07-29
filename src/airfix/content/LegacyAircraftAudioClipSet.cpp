#include "airfix/content/LegacyAircraftAudioClipSet.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <utility>

namespace airfix::content {
namespace {

struct ClipSpecification final {
  simulation::LegacyAircraftEngineSound sound;
  std::string_view logicalPath;
  bool looping;
};

constexpr std::array<ClipSpecification, legacyAircraftAudioClipCount>
    clipSpecifications{{
        {
            .sound = simulation::LegacyAircraftEngineSound::engineOn,
            .logicalPath = "Sound\\Ingame\\Models\\Ac\\engineon.wav",
            .looping = true,
        },
        {
            .sound = simulation::LegacyAircraftEngineSound::engineIdle,
            .logicalPath = "Sound\\Ingame\\Models\\Ac\\engineidle.wav",
            .looping = true,
        },
        {
            .sound = simulation::LegacyAircraftEngineSound::engineTurn,
            .logicalPath = "Sound\\Ingame\\Models\\Ac\\engineturn.wav",
            .looping = true,
        },
        {
            .sound = simulation::LegacyAircraftEngineSound::engineStart,
            .logicalPath = "Sound\\Ingame\\Models\\Ac\\enginestart.wav",
            .looping = false,
        },
        {
            .sound = simulation::LegacyAircraftEngineSound::engineStop,
            .logicalPath = "Sound\\Ingame\\Models\\Ac\\enginestop.wav",
            .looping = false,
        },
        {
            .sound = simulation::LegacyAircraftEngineSound::engineDive,
            .logicalPath = "Sound\\Ingame\\Models\\Ac\\enginedive.wav",
            .looping = true,
        },
    }};

class SessionIdentityChanged final {};

[[nodiscard]] audio::AudioClipId
clipId(const simulation::LegacyAircraftEngineSound sound) noexcept {
  return {static_cast<std::uint32_t>(sound)};
}

[[nodiscard]] LegacyAircraftAudioClipLoadIssue
makeIssue(const LegacyAircraftAudioClipLoadIssueKind kind) noexcept {
  return {
      .kind = kind,
      .sound = std::nullopt,
      .sourceFileIndex = std::nullopt,
      .waveParseError = std::nullopt,
      .waveErrorOffset = std::nullopt,
  };
}

void addIssue(LegacyAircraftAudioClipLoadResult &result,
              LegacyAircraftAudioClipLoadIssue issue) {
  result.clips.reset();
  result.issues.push_back(std::move(issue));
}

void addIssue(LegacyAircraftAudioClipLoadResult &result,
              const LegacyAircraftAudioClipLoadIssueKind kind) {
  addIssue(result, makeIssue(kind));
}

void requireExpectedSession(
    const VerifiedContentSession &session,
    const VerifiedContentTransactionIdentity &expectedIdentity,
    const ContentRevision &expectedRevision) {
  if (session.transactionIdentity() != expectedIdentity ||
      session.revision() != expectedRevision) {
    throw SessionIdentityChanged{};
  }
}

[[nodiscard]] bool
report(LegacyAircraftAudioClipLoadResult &result,
       const VerifiedContentSession &session,
       const VerifiedContentTransactionIdentity &expectedIdentity,
       const ContentRevision &expectedRevision, const std::stop_token stopToken,
       const LegacyAircraftAudioClipLoadProgressCallback &callback,
       const LegacyAircraftAudioClipLoadPhase phase,
       const std::size_t completedItems, const std::size_t totalItems) {
  requireExpectedSession(session, expectedIdentity, expectedRevision);
  if (stopToken.stop_requested()) {
    addIssue(result, LegacyAircraftAudioClipLoadIssueKind::cancelled);
    return false;
  }
  if (completedItems > totalItems) {
    addIssue(result, LegacyAircraftAudioClipLoadIssueKind::internalFailure);
    return false;
  }
  if (callback) {
    try {
      callback({
          .phase = phase,
          .completedItems = completedItems,
          .totalItems = totalItems,
      });
    } catch (const std::bad_alloc &) {
      throw;
    } catch (...) {
      requireExpectedSession(session, expectedIdentity, expectedRevision);
      addIssue(result,
               LegacyAircraftAudioClipLoadIssueKind::progressCallbackFailure);
      return false;
    }
  }
  requireExpectedSession(session, expectedIdentity, expectedRevision);
  if (stopToken.stop_requested()) {
    addIssue(result, LegacyAircraftAudioClipLoadIssueKind::cancelled);
    return false;
  }
  return true;
}

[[nodiscard]] bool
validLimits(const LegacyAircraftAudioClipLoadLimits &limits) noexcept {
  return limits.maximumSourceBytes >= 12U &&
         limits.maximumSingleSourceFootprintBytes != 0U &&
         limits.maximumTotalSourceFootprintBytes != 0U &&
         limits.maximumPublishedPcmBytes != 0U &&
         limits.wave.maximumSourceBytes >= 12U &&
         limits.wave.maximumPcmBytes != 0U &&
         limits.wave.maximumPcmBytes <= audio::maximumPcm16ClipBytes &&
         limits.wave.maximumChunks != 0U &&
         limits.wave.maximumSamplerLoops != 0U;
}

[[nodiscard]] bool checkedAdd(std::uint64_t &total,
                              const std::uint64_t addition) noexcept {
  if (addition > std::numeric_limits<std::uint64_t>::max() - total) {
    return false;
  }
  total += addition;
  return true;
}

[[nodiscard]] std::uint64_t
sourceFootprint(const udsp::FileEntry &entry) noexcept {
  return static_cast<std::uint64_t>(entry.storedSize) +
         (entry.isCompressed() ? static_cast<std::uint64_t>(entry.unpackedSize)
                               : 0U);
}

[[nodiscard]] bool
hasRequiredFullInfiniteLoop(const audio::WavePcm16 &wave) noexcept {
  if (wave.frameCount() == 0U ||
      wave.frameCount() - 1U > std::numeric_limits<std::uint32_t>::max() ||
      wave.samplerLoops.size() != 1U) {
    return false;
  }
  const auto &loop = wave.samplerLoops.front();
  return loop.startFrame == 0U &&
         loop.endFrameInclusive ==
             static_cast<std::uint32_t>(wave.frameCount() - 1U) &&
         loop.playCount == 0U;
}

} // namespace

bool LoadedLegacyAircraftAudioClips::valid() const noexcept {
  if (!transactionIdentity.valid() || sourceFootprintBytes == 0U ||
      publishedPcmBytes == 0U) {
    return false;
  }

  std::uint64_t accountedPcmBytes = 0U;
  for (std::size_t index = 0U; index < clips.size(); ++index) {
    const auto &clip = clips[index];
    const auto &specification = clipSpecifications[index];
    if (clip.sound != specification.sound ||
        clip.clip != clipId(specification.sound) ||
        clip.looping != specification.looping ||
        clip.logicalPath != specification.logicalPath ||
        !audio::validPcm16Clip(clip.view()) ||
        (clip.looping && !hasRequiredFullInfiniteLoop(clip.wave))) {
      return false;
    }
    for (std::size_t previous = 0U; previous < index; ++previous) {
      if (clips[previous].sourceFileIndex == clip.sourceFileIndex) {
        return false;
      }
    }
    const std::uint64_t pcmBytes =
        static_cast<std::uint64_t>(clip.wave.interleavedSamples.size()) *
        sizeof(std::int16_t);
    if (!checkedAdd(accountedPcmBytes, pcmBytes)) {
      return false;
    }
  }
  return accountedPcmBytes == publishedPcmBytes;
}

bool LoadedLegacyAircraftAudioClips::belongsTo(
    const VerifiedContentSession &session) const noexcept {
  return valid() && transactionIdentity == session.transactionIdentity() &&
         revision == session.revision();
}

std::array<audio::Pcm16ClipView, legacyAircraftAudioClipCount>
LoadedLegacyAircraftAudioClips::clipViews() const noexcept {
  std::array<audio::Pcm16ClipView, legacyAircraftAudioClipCount> views{};
  std::transform(
      clips.begin(), clips.end(), views.begin(),
      [](const LegacyAircraftAudioClip &clip) { return clip.view(); });
  return views;
}

std::optional<simulation::LegacyAircraftAudioBindings>
LoadedLegacyAircraftAudioClips::bindings(
    const std::array<audio::AudioVoiceId, legacyAircraftAudioClipCount> &voices)
    const noexcept {
  simulation::LegacyAircraftAudioBindings result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = {
        .sound = clips[index].sound,
        .clip = clips[index].clip,
        .voice = voices[index],
        .looping = clips[index].looping,
    };
  }
  if (!simulation::validLegacyAircraftAudioBindings(result)) {
    return std::nullopt;
  }
  return result;
}

LegacyAircraftAudioClipLoadResult loadLegacyAircraftAudioClips(
    VerifiedContentSession &session,
    const LegacyAircraftAudioClipLoadLimits &externalLimits,
    const std::stop_token stopToken,
    LegacyAircraftAudioClipLoadProgressCallback progress) {
  LegacyAircraftAudioClipLoadResult result;
  try {
    const auto expectedIdentity = session.transactionIdentity();
    const ContentRevision expectedRevision = session.revision();
    if (!expectedIdentity.valid()) {
      addIssue(result,
               LegacyAircraftAudioClipLoadIssueKind::sessionIdentityChanged);
      return result;
    }

    const LegacyAircraftAudioClipLoadLimits limits = externalLimits;
    if (!validLimits(limits)) {
      addIssue(result, LegacyAircraftAudioClipLoadIssueKind::invalidLimits);
      return result;
    }
    if (!report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress, LegacyAircraftAudioClipLoadPhase::validatingInput, 0U,
                legacyAircraftAudioClipCount)) {
      return result;
    }

    std::array<std::size_t, legacyAircraftAudioClipCount> sourceIndices{};
    std::uint64_t totalSourceFootprint = 0U;
    const auto &archive = session.sourceArchive();
    for (std::size_t index = 0U; index < clipSpecifications.size(); ++index) {
      const auto &specification = clipSpecifications[index];
      const udsp::FileLookupResult lookup =
          archive.lookup(specification.logicalPath);
      if (lookup.status != udsp::LookupStatus::unique) {
        auto issue = makeIssue(
            lookup.status == udsp::LookupStatus::ambiguous
                ? LegacyAircraftAudioClipLoadIssueKind::sampleAmbiguous
                : LegacyAircraftAudioClipLoadIssueKind::sampleNotFound);
        issue.sound = specification.sound;
        addIssue(result, std::move(issue));
        return result;
      }

      sourceIndices[index] = lookup.fileIndex;
      const auto &entry = archive.files().at(lookup.fileIndex);
      if (entry.unpackedSize > limits.maximumSourceBytes ||
          entry.unpackedSize > limits.wave.maximumSourceBytes) {
        auto issue = makeIssue(
            LegacyAircraftAudioClipLoadIssueKind::sourceLimitExceeded);
        issue.sound = specification.sound;
        issue.sourceFileIndex = lookup.fileIndex;
        addIssue(result, std::move(issue));
        return result;
      }
      const std::uint64_t footprint = sourceFootprint(entry);
      if (footprint > limits.maximumSingleSourceFootprintBytes) {
        auto issue = makeIssue(
            LegacyAircraftAudioClipLoadIssueKind::sourceFootprintLimitExceeded);
        issue.sound = specification.sound;
        issue.sourceFileIndex = lookup.fileIndex;
        addIssue(result, std::move(issue));
        return result;
      }
      if (!checkedAdd(totalSourceFootprint, footprint)) {
        addIssue(result, LegacyAircraftAudioClipLoadIssueKind::integerOverflow);
        return result;
      }
      if (totalSourceFootprint > limits.maximumTotalSourceFootprintBytes) {
        addIssue(result, LegacyAircraftAudioClipLoadIssueKind::
                             aggregateSourceFootprintLimitExceeded);
        return result;
      }
      if (!report(result, session, expectedIdentity, expectedRevision,
                  stopToken, progress,
                  LegacyAircraftAudioClipLoadPhase::lookingUpSamples,
                  index + 1U, legacyAircraftAudioClipCount)) {
        return result;
      }
    }

    LoadedLegacyAircraftAudioClips candidate{
        .revision = expectedRevision,
        .transactionIdentity = expectedIdentity,
        .clips = {},
        .sourceFootprintBytes = totalSourceFootprint,
        .publishedPcmBytes = 0U,
    };
    for (std::size_t index = 0U; index < clipSpecifications.size(); ++index) {
      const auto &specification = clipSpecifications[index];
      const std::size_t sourceFileIndex = sourceIndices[index];
      requireExpectedSession(session, expectedIdentity, expectedRevision);

      std::vector<std::uint8_t> bytes;
      try {
        bytes =
            session.readSourceFile(sourceFileIndex, limits.maximumSourceBytes);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        requireExpectedSession(session, expectedIdentity, expectedRevision);
        auto issue =
            makeIssue(LegacyAircraftAudioClipLoadIssueKind::sourceReadFailure);
        issue.sound = specification.sound;
        issue.sourceFileIndex = sourceFileIndex;
        addIssue(result, std::move(issue));
        return result;
      }
      requireExpectedSession(session, expectedIdentity, expectedRevision);
      if (!report(result, session, expectedIdentity, expectedRevision,
                  stopToken, progress,
                  LegacyAircraftAudioClipLoadPhase::readingSamples, index + 1U,
                  legacyAircraftAudioClipCount)) {
        return result;
      }

      audio::WavePcm16 wave;
      try {
        wave = audio::parseWavePcm16(bytes, limits.wave);
      } catch (const audio::WavePcm16ParseError &error) {
        auto issue =
            makeIssue(LegacyAircraftAudioClipLoadIssueKind::waveParseFailure);
        issue.sound = specification.sound;
        issue.sourceFileIndex = sourceFileIndex;
        issue.waveParseError = error.code();
        issue.waveErrorOffset = error.offset();
        addIssue(result, std::move(issue));
        return result;
      }
      if (specification.looping && !hasRequiredFullInfiniteLoop(wave)) {
        auto issue = makeIssue(
            LegacyAircraftAudioClipLoadIssueKind::requiredLoopEvidenceMissing);
        issue.sound = specification.sound;
        issue.sourceFileIndex = sourceFileIndex;
        addIssue(result, std::move(issue));
        return result;
      }

      const std::uint64_t pcmBytes =
          static_cast<std::uint64_t>(wave.interleavedSamples.size()) *
          sizeof(std::int16_t);
      if (!checkedAdd(candidate.publishedPcmBytes, pcmBytes)) {
        addIssue(result, LegacyAircraftAudioClipLoadIssueKind::integerOverflow);
        return result;
      }
      if (candidate.publishedPcmBytes > limits.maximumPublishedPcmBytes) {
        addIssue(
            result,
            LegacyAircraftAudioClipLoadIssueKind::publishedPcmLimitExceeded);
        return result;
      }

      candidate.clips[index] = {
          .sound = specification.sound,
          .clip = clipId(specification.sound),
          .looping = specification.looping,
          .sourceFileIndex = sourceFileIndex,
          .logicalPath = std::string(specification.logicalPath),
          .wave = std::move(wave),
      };
      if (!report(result, session, expectedIdentity, expectedRevision,
                  stopToken, progress,
                  LegacyAircraftAudioClipLoadPhase::parsingSamples, index + 1U,
                  legacyAircraftAudioClipCount)) {
        return result;
      }
    }

    if (!report(result, session, expectedIdentity, expectedRevision, stopToken,
                progress, LegacyAircraftAudioClipLoadPhase::complete,
                legacyAircraftAudioClipCount, legacyAircraftAudioClipCount)) {
      return result;
    }
    requireExpectedSession(session, expectedIdentity, expectedRevision);
    if (!candidate.valid()) {
      addIssue(result, LegacyAircraftAudioClipLoadIssueKind::internalFailure);
      return result;
    }
    result.clips = std::move(candidate);
    return result;
  } catch (const SessionIdentityChanged &) {
    result.clips.reset();
    result.issues.clear();
    addIssue(result,
             LegacyAircraftAudioClipLoadIssueKind::sessionIdentityChanged);
    return result;
  } catch (const std::bad_alloc &) {
    result.clips.reset();
    result.issues.clear();
    addIssue(result, LegacyAircraftAudioClipLoadIssueKind::allocationFailure);
    return result;
  } catch (...) {
    result.clips.reset();
    result.issues.clear();
    addIssue(result, LegacyAircraftAudioClipLoadIssueKind::internalFailure);
    return result;
  }
}

} // namespace airfix::content
