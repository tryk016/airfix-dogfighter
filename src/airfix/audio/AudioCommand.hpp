#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace airfix::audio {

inline constexpr std::size_t maximumAudioCommandsPerBatch = 64U;
inline constexpr float maximumAudioGain = 4.0F;
inline constexpr float minimumAudioPitch = 0.25F;
inline constexpr float maximumAudioPitch = 4.0F;
inline constexpr std::uint32_t minimumPcmSampleRate = 8'000U;
inline constexpr std::uint32_t maximumPcmSampleRate = 192'000U;
inline constexpr std::uint16_t maximumPcmChannelCount = 2U;
inline constexpr std::size_t maximumPcm16ClipBytes = 64U * 1024U * 1024U;

struct AudioClipId final {
    std::uint32_t value{};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }

    friend constexpr bool operator==(AudioClipId,
                                     AudioClipId) noexcept = default;
};

struct AudioVoiceId final {
    std::uint32_t value{};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }

    friend constexpr bool operator==(AudioVoiceId,
                                     AudioVoiceId) noexcept = default;
};

enum class AudioCommandKind : std::uint8_t {
    startVoice,
    stopVoice,
    setVoiceGain,
    setVoicePitch,
    stopAllVoices,
};

struct AudioCommand final {
    AudioCommandKind kind{AudioCommandKind::stopAllVoices};
    AudioVoiceId voice{};
    AudioClipId clip{};
    float gain{1.0F};
    float pitch{1.0F};
    bool looping{};
};

struct AudioCommandBatch final {
    // Consumers accept strictly increasing, non-zero sequence numbers.
    std::uint64_t sequence{};
    std::array<AudioCommand, maximumAudioCommandsPerBatch> commands{};
    std::size_t commandCount{};
};

struct Pcm16ClipView final {
    // The sample span is borrowed for the registration call. An asynchronous
    // backend must copy or otherwise retain owned immutable storage.
    AudioClipId id{};
    std::uint32_t sampleRate{};
    std::uint16_t channelCount{};
    std::span<const std::int16_t> interleavedSamples{};
};

[[nodiscard]] bool validAudioCommand(const AudioCommand& command) noexcept;

[[nodiscard]] bool
validAudioCommandBatch(const AudioCommandBatch& batch) noexcept;

[[nodiscard]] bool validPcm16Clip(const Pcm16ClipView& clip) noexcept;

[[nodiscard]] bool appendAudioCommand(AudioCommandBatch& batch,
                                      const AudioCommand& command) noexcept;

} // namespace airfix::audio
