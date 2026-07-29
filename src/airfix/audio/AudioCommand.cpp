#include "airfix/audio/AudioCommand.hpp"

#include <cmath>

namespace airfix::audio {
namespace {

[[nodiscard]] bool finiteInRange(const float value, const float minimum,
                                 const float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

[[nodiscard]] bool hasDefaultParameters(const AudioCommand& command) noexcept {
    return !command.clip.valid() && command.gain == 1.0F &&
           command.pitch == 1.0F && !command.looping;
}

} // namespace

bool validAudioCommand(const AudioCommand& command) noexcept {
    switch (command.kind) {
    case AudioCommandKind::startVoice:
        return command.voice.valid() && command.clip.valid() &&
               finiteInRange(command.gain, 0.0F, maximumAudioGain) &&
               finiteInRange(command.pitch, minimumAudioPitch,
                             maximumAudioPitch);
    case AudioCommandKind::stopVoice:
        return command.voice.valid() && hasDefaultParameters(command);
    case AudioCommandKind::setVoiceGain:
        return command.voice.valid() && !command.clip.valid() &&
               finiteInRange(command.gain, 0.0F, maximumAudioGain) &&
               command.pitch == 1.0F && !command.looping;
    case AudioCommandKind::setVoicePitch:
        return command.voice.valid() && !command.clip.valid() &&
               command.gain == 1.0F &&
               finiteInRange(command.pitch, minimumAudioPitch,
                             maximumAudioPitch) &&
               !command.looping;
    case AudioCommandKind::stopAllVoices:
        return !command.voice.valid() && hasDefaultParameters(command);
    }
    return false;
}

bool validAudioCommandBatch(const AudioCommandBatch& batch) noexcept {
    if (batch.sequence == 0U || batch.commandCount > batch.commands.size()) {
        return false;
    }

    for (std::size_t index = 0U; index < batch.commandCount; ++index) {
        if (!validAudioCommand(batch.commands[index])) {
            return false;
        }
    }
    return true;
}

bool validPcm16Clip(const Pcm16ClipView& clip) noexcept {
    if (!clip.id.valid() || clip.sampleRate < minimumPcmSampleRate ||
        clip.sampleRate > maximumPcmSampleRate || clip.channelCount == 0U ||
        clip.channelCount > maximumPcmChannelCount ||
        clip.interleavedSamples.empty() ||
        clip.interleavedSamples.size() % clip.channelCount != 0U) {
        return false;
    }

    constexpr std::size_t maximumSampleCount =
        maximumPcm16ClipBytes / sizeof(std::int16_t);
    return clip.interleavedSamples.size() <= maximumSampleCount;
}

bool appendAudioCommand(AudioCommandBatch& batch,
                        const AudioCommand& command) noexcept {
    if (!validAudioCommand(command) ||
        batch.commandCount >= batch.commands.size()) {
        return false;
    }
    batch.commands[batch.commandCount] = command;
    ++batch.commandCount;
    return true;
}

} // namespace airfix::audio
