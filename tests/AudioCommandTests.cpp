#include "airfix/audio/AudioCommand.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::audio::appendAudioCommand;
using airfix::audio::AudioClipId;
using airfix::audio::AudioCommand;
using airfix::audio::AudioCommandBatch;
using airfix::audio::AudioCommandKind;
using airfix::audio::AudioVoiceId;
using airfix::audio::maximumAudioCommandsPerBatch;
using airfix::audio::Pcm16ClipView;
using airfix::audio::validAudioCommand;
using airfix::audio::validAudioCommandBatch;
using airfix::audio::validPcm16Clip;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

AudioCommand startCommand() {
    return {
        .kind = AudioCommandKind::startVoice,
        .voice = AudioVoiceId{7U},
        .clip = AudioClipId{11U},
        .gain = 0.75F,
        .pitch = 1.25F,
        .looping = true,
    };
}

void testValidCommandsAndBatch() {
    AudioCommandBatch batch{.sequence = 42U};
    require(appendAudioCommand(batch, startCommand()),
            "valid start command was rejected");
    require(appendAudioCommand(batch,
                               {
                                   .kind = AudioCommandKind::setVoiceGain,
                                   .voice = AudioVoiceId{7U},
                                   .gain = 0.5F,
                               }),
            "valid gain command was rejected");
    require(appendAudioCommand(batch,
                               {
                                   .kind = AudioCommandKind::setVoicePitch,
                                   .voice = AudioVoiceId{7U},
                                   .pitch = 0.5F,
                               }),
            "valid pitch command was rejected");
    require(appendAudioCommand(batch,
                               {
                                   .kind = AudioCommandKind::stopVoice,
                                   .voice = AudioVoiceId{7U},
                               }),
            "valid stop command was rejected");
    require(
        appendAudioCommand(batch, {.kind = AudioCommandKind::stopAllVoices}),
        "valid stop-all command was rejected");
    require(batch.commandCount == 5U, "batch command count changed");
    require(validAudioCommandBatch(batch), "valid batch was rejected");
}

void testInvalidCommandsFailClosed() {
    AudioCommand invalidStart = startCommand();
    invalidStart.voice = {};
    require(!validAudioCommand(invalidStart),
            "start without a voice ID was accepted");

    invalidStart = startCommand();
    invalidStart.gain = std::numeric_limits<float>::quiet_NaN();
    require(!validAudioCommand(invalidStart), "non-finite gain was accepted");

    invalidStart = startCommand();
    invalidStart.pitch = 4.01F;
    require(!validAudioCommand(invalidStart),
            "out-of-range pitch was accepted");

    const AudioCommand malformedStop{
        .kind = AudioCommandKind::stopVoice,
        .voice = AudioVoiceId{1U},
        .clip = AudioClipId{2U},
    };
    require(!validAudioCommand(malformedStop),
            "stop command with clip state was accepted");

    AudioCommandBatch zeroSequence{};
    require(!validAudioCommandBatch(zeroSequence),
            "zero batch sequence was accepted");
    require(!appendAudioCommand(zeroSequence, invalidStart),
            "invalid command was appended");
}

void testCapacityIsBounded() {
    AudioCommandBatch batch{.sequence = 1U};
    for (std::size_t index = 0U; index < maximumAudioCommandsPerBatch;
         ++index) {
        require(appendAudioCommand(batch,
                                   {
                                       .kind = AudioCommandKind::stopAllVoices,
                                   }),
                "bounded batch filled too early");
    }
    require(
        !appendAudioCommand(batch, {.kind = AudioCommandKind::stopAllVoices}),
        "bounded batch overflow was accepted");
    require(validAudioCommandBatch(batch), "full valid batch was rejected");
}

void testPcm16Validation() {
    const std::array<std::int16_t, 8U> stereoSamples{0,   100, 200, 300,
                                                     400, 500, 600, 700};
    const Pcm16ClipView valid{
        .id = AudioClipId{3U},
        .sampleRate = 48'000U,
        .channelCount = 2U,
        .interleavedSamples = stereoSamples,
    };
    require(validPcm16Clip(valid), "valid PCM16 clip was rejected");

    Pcm16ClipView invalid = valid;
    invalid.id = {};
    require(!validPcm16Clip(invalid), "clip without ID was accepted");

    invalid = valid;
    invalid.channelCount = 0U;
    require(!validPcm16Clip(invalid), "zero-channel clip was accepted");

    invalid = valid;
    invalid.interleavedSamples =
        std::span<const std::int16_t>{stereoSamples}.first(7U);
    require(!validPcm16Clip(invalid),
            "misaligned interleaved PCM was accepted");

    invalid = valid;
    invalid.sampleRate = 1U;
    require(!validPcm16Clip(invalid), "unsupported sample rate was accepted");
}

} // namespace

int main() {
    try {
        testValidCommandsAndBatch();
        testInvalidCommandsFailClosed();
        testCapacityIsBounded();
        testPcm16Validation();
        std::cout << "Audio command tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio command tests failed: " << error.what() << '\n';
        return 1;
    }
}
