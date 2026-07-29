#include "AirfixXAudio2Backend.hpp"

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace airfix::windows {
namespace {

inline constexpr std::size_t maximumRegisteredClips = 128U;
inline constexpr std::size_t maximumActiveVoices = 64U;
inline constexpr std::size_t maximumRegisteredPcmBytes = 256U * 1024U * 1024U;

using XAudio2CreateFunction = HRESULT(WINAPI *)(IXAudio2 **, UINT32,
                                                XAUDIO2_PROCESSOR);

class EngineCallback final : public IXAudio2EngineCallback {
public:
  void STDMETHODCALLTYPE OnProcessingPassStart() noexcept override {}
  void STDMETHODCALLTYPE OnProcessingPassEnd() noexcept override {}

  void STDMETHODCALLTYPE
  OnCriticalError(const HRESULT error) noexcept override {
    criticalError_.store(static_cast<std::int32_t>(error),
                         std::memory_order_release);
  }

  [[nodiscard]] HRESULT criticalError() const noexcept {
    return static_cast<HRESULT>(criticalError_.load(std::memory_order_acquire));
  }

  void clear() noexcept {
    criticalError_.store(static_cast<std::int32_t>(S_OK),
                         std::memory_order_release);
  }

private:
  std::atomic<std::int32_t> criticalError_{static_cast<std::int32_t>(S_OK)};
};

struct RegisteredClip final {
  audio::AudioClipId id{};
  std::uint32_t sampleRate{};
  std::uint16_t channelCount{};
  std::vector<std::int16_t> samples{};
};

struct ActiveVoice final {
  audio::AudioVoiceId id{};
  IXAudio2SourceVoice *source{};
};

[[nodiscard]] std::uint32_t errorCode(const HRESULT result) noexcept {
  return static_cast<std::uint32_t>(result);
}

} // namespace

class AirfixXAudio2Backend::Impl final {
public:
  Impl() {
    clips_.reserve(maximumRegisteredClips);
    voices_.reserve(maximumActiveVoices);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(comResult)) {
      ownsComInitialization_ = true;
    } else if (comResult != RPC_E_CHANGED_MODE) {
      failInitialization(comResult);
      return;
    }

    library_ =
        LoadLibraryExW(L"XAUDIO2_9.DLL", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (library_ == nullptr) {
      failInitialization(HRESULT_FROM_WIN32(GetLastError()));
      return;
    }

    const FARPROC createAddress = GetProcAddress(library_, "XAudio2Create");
    if (createAddress == nullptr) {
      failInitialization(HRESULT_FROM_WIN32(GetLastError()));
      return;
    }
    static_assert(
        sizeof(createFunction_) == sizeof(createAddress),
        "Windows function pointers must have a stable representation");
    std::memcpy(&createFunction_, &createAddress, sizeof(createFunction_));
    initializeEngine();
  }

  ~Impl() {
    shutdownEngine();
    if (library_ != nullptr) {
      FreeLibrary(library_);
    }
    if (ownsComInitialization_) {
      CoUninitialize();
    }
  }

  [[nodiscard]] AirfixXAudio2OutputState outputState() const noexcept {
    return outputState_;
  }

  [[nodiscard]] std::uint32_t lastErrorCode() const noexcept {
    return lastErrorCode_;
  }

  [[nodiscard]] AirfixAudioClipRegistrationResult
  registerPcm16Clip(const audio::Pcm16ClipView &clip) {
    if (!audio::validPcm16Clip(clip)) {
      return AirfixAudioClipRegistrationResult::invalidClip;
    }
    if (findClip(clip.id) != clips_.end()) {
      return AirfixAudioClipRegistrationResult::duplicateId;
    }
    if (clips_.size() >= maximumRegisteredClips) {
      return AirfixAudioClipRegistrationResult::capacityExceeded;
    }
    const std::size_t clipBytes =
        clip.interleavedSamples.size() * sizeof(std::int16_t);
    if (clipBytes > maximumRegisteredPcmBytes - registeredPcmBytes_) {
      return AirfixAudioClipRegistrationResult::capacityExceeded;
    }

    clips_.push_back({
        .id = clip.id,
        .sampleRate = clip.sampleRate,
        .channelCount = clip.channelCount,
        .samples = std::vector<std::int16_t>(clip.interleavedSamples.begin(),
                                             clip.interleavedSamples.end()),
    });
    registeredPcmBytes_ += clipBytes;
    return AirfixAudioClipRegistrationResult::registered;
  }

  [[nodiscard]] AirfixAudioSubmissionResult
  submit(const audio::AudioCommandBatch &batch) {
    if (!audio::validAudioCommandBatch(batch) ||
        batch.sequence <= lastSequence_) {
      return {
          .errorCode = errorCode(E_INVALIDARG),
      };
    }

    for (std::size_t index = 0U; index < batch.commandCount; ++index) {
      const audio::AudioCommand &command = batch.commands[index];
      if (command.kind == audio::AudioCommandKind::startVoice &&
          findClip(command.clip) == clips_.end()) {
        return {
            .errorCode = errorCode(HRESULT_FROM_WIN32(ERROR_NOT_FOUND)),
        };
      }
    }

    if (callback_.criticalError() != S_OK) {
      initializeEngine();
    }

    lastSequence_ = batch.sequence;
    if (outputState_ == AirfixXAudio2OutputState::outputUnavailable) {
      return {
          .accepted = true,
          .outputAvailable = false,
          .appliedCommandCount = batch.commandCount,
          .errorCode = lastErrorCode_,
      };
    }
    if (outputState_ != AirfixXAudio2OutputState::ready) {
      return {
          .errorCode = lastErrorCode_,
      };
    }

    collectFinishedVoices();
    std::size_t appliedCommandCount = 0U;
    for (std::size_t index = 0U; index < batch.commandCount; ++index) {
      const HRESULT result = apply(batch.commands[index]);
      if (FAILED(result)) {
        lastErrorCode_ = errorCode(result);
        stopAllVoices();
        return {
            .outputAvailable = true,
            .appliedCommandCount = appliedCommandCount,
            .errorCode = lastErrorCode_,
        };
      }
      ++appliedCommandCount;
    }

    lastErrorCode_ = 0U;
    return {
        .accepted = true,
        .outputAvailable = true,
        .appliedCommandCount = appliedCommandCount,
    };
  }

  void setActive(const bool active) noexcept {
    active_ = active;
    if (engine_ == nullptr || outputState_ != AirfixXAudio2OutputState::ready) {
      return;
    }

    if (active_) {
      const HRESULT result = engine_->StartEngine();
      if (FAILED(result)) {
        callback_.OnCriticalError(result);
        lastErrorCode_ = errorCode(result);
      }
    } else {
      engine_->StopEngine();
    }
  }

  [[nodiscard]] AirfixXAudio2OutputState recover() noexcept {
    if (library_ == nullptr || createFunction_ == nullptr) {
      return outputState_;
    }
    if (outputState_ == AirfixXAudio2OutputState::ready &&
        callback_.criticalError() == S_OK) {
      return outputState_;
    }
    initializeEngine();
    return outputState_;
  }

private:
  using ClipIterator = std::vector<RegisteredClip>::iterator;
  using ConstClipIterator = std::vector<RegisteredClip>::const_iterator;
  using VoiceIterator = std::vector<ActiveVoice>::iterator;

  [[nodiscard]] ClipIterator findClip(const audio::AudioClipId id) noexcept {
    return std::find_if(
        clips_.begin(), clips_.end(),
        [id](const RegisteredClip &clip) { return clip.id == id; });
  }

  [[nodiscard]] ConstClipIterator
  findClip(const audio::AudioClipId id) const noexcept {
    return std::find_if(
        clips_.cbegin(), clips_.cend(),
        [id](const RegisteredClip &clip) { return clip.id == id; });
  }

  [[nodiscard]] VoiceIterator findVoice(const audio::AudioVoiceId id) noexcept {
    return std::find_if(
        voices_.begin(), voices_.end(),
        [id](const ActiveVoice &voice) { return voice.id == id; });
  }

  void failInitialization(const HRESULT result) noexcept {
    outputState_ = AirfixXAudio2OutputState::initializationFailed;
    lastErrorCode_ = errorCode(result);
  }

  void initializeEngine() noexcept {
    shutdownEngine();
    callback_.clear();

    IXAudio2 *engine = nullptr;
    const HRESULT createResult =
        createFunction_(&engine, 0U, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(createResult)) {
      failInitialization(createResult);
      return;
    }
    engine_ = engine;

    const HRESULT callbackResult = engine_->RegisterForCallbacks(&callback_);
    if (FAILED(callbackResult)) {
      failInitialization(callbackResult);
      shutdownEngine();
      return;
    }
    callbackRegistered_ = true;

    const HRESULT masteringResult = engine_->CreateMasteringVoice(
        &masteringVoice_, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE,
        0U, nullptr, nullptr, AudioCategory_GameMedia);
    if (FAILED(masteringResult)) {
      outputState_ = AirfixXAudio2OutputState::outputUnavailable;
      lastErrorCode_ = errorCode(masteringResult);
      shutdownEngine();
      outputState_ = AirfixXAudio2OutputState::outputUnavailable;
      lastErrorCode_ = errorCode(masteringResult);
      return;
    }

    outputState_ = AirfixXAudio2OutputState::ready;
    lastErrorCode_ = 0U;
    if (!active_) {
      engine_->StopEngine();
    }
  }

  void shutdownEngine() noexcept {
    stopAllVoices();
    if (masteringVoice_ != nullptr) {
      masteringVoice_->DestroyVoice();
      masteringVoice_ = nullptr;
    }
    if (engine_ != nullptr && callbackRegistered_) {
      engine_->UnregisterForCallbacks(&callback_);
      callbackRegistered_ = false;
    }
    if (engine_ != nullptr) {
      engine_->Release();
      engine_ = nullptr;
    }
  }

  void stopAllVoices() noexcept {
    for (ActiveVoice &voice : voices_) {
      if (voice.source != nullptr) {
        voice.source->Stop();
        voice.source->DestroyVoice();
      }
    }
    voices_.clear();
  }

  void collectFinishedVoices() noexcept {
    auto iterator = voices_.begin();
    while (iterator != voices_.end()) {
      XAUDIO2_VOICE_STATE state{};
      iterator->source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
      if (state.BuffersQueued != 0U) {
        ++iterator;
        continue;
      }
      iterator->source->DestroyVoice();
      iterator = voices_.erase(iterator);
    }
  }

  [[nodiscard]] HRESULT apply(const audio::AudioCommand &command) noexcept {
    switch (command.kind) {
    case audio::AudioCommandKind::startVoice:
      return startVoice(command);
    case audio::AudioCommandKind::stopVoice:
      stopVoice(command.voice);
      return S_OK;
    case audio::AudioCommandKind::setVoiceGain: {
      const VoiceIterator voice = findVoice(command.voice);
      return voice == voices_.end() ? S_OK
                                    : voice->source->SetVolume(command.gain);
    }
    case audio::AudioCommandKind::setVoicePitch: {
      const VoiceIterator voice = findVoice(command.voice);
      return voice == voices_.end()
                 ? S_OK
                 : voice->source->SetFrequencyRatio(command.pitch);
    }
    case audio::AudioCommandKind::stopAllVoices:
      stopAllVoices();
      return S_OK;
    }
    return E_INVALIDARG;
  }

  [[nodiscard]] HRESULT
  startVoice(const audio::AudioCommand &command) noexcept {
    stopVoice(command.voice);
    if (voices_.size() >= maximumActiveVoices) {
      return HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES);
    }

    const ConstClipIterator clip = findClip(command.clip);
    if (clip == clips_.cend()) {
      return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = clip->channelCount;
    format.nSamplesPerSec = clip->sampleRate;
    format.wBitsPerSample = 16U;
    format.nBlockAlign =
        static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8U);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    IXAudio2SourceVoice *source = nullptr;
    HRESULT result = engine_->CreateSourceVoice(&source, &format, 0U,
                                                audio::maximumAudioPitch);
    if (FAILED(result)) {
      return result;
    }

    result = source->SetVolume(command.gain);
    if (SUCCEEDED(result)) {
      result = source->SetFrequencyRatio(command.pitch);
    }

    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes =
        static_cast<UINT32>(clip->samples.size() * sizeof(std::int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE *>(clip->samples.data());
    buffer.LoopCount = command.looping ? XAUDIO2_LOOP_INFINITE : 0U;

    if (SUCCEEDED(result)) {
      result = source->SubmitSourceBuffer(&buffer);
    }
    if (SUCCEEDED(result)) {
      result = source->Start();
    }
    if (FAILED(result)) {
      source->DestroyVoice();
      return result;
    }

    voices_.push_back({
        .id = command.voice,
        .source = source,
    });
    return S_OK;
  }

  void stopVoice(const audio::AudioVoiceId id) noexcept {
    const VoiceIterator voice = findVoice(id);
    if (voice == voices_.end()) {
      return;
    }
    voice->source->Stop();
    voice->source->DestroyVoice();
    voices_.erase(voice);
  }

  HMODULE library_{};
  XAudio2CreateFunction createFunction_{};
  IXAudio2 *engine_{};
  IXAudio2MasteringVoice *masteringVoice_{};
  EngineCallback callback_{};
  std::vector<RegisteredClip> clips_{};
  std::vector<ActiveVoice> voices_{};
  AirfixXAudio2OutputState outputState_{
      AirfixXAudio2OutputState::initializationFailed};
  std::uint32_t lastErrorCode_{};
  std::uint64_t lastSequence_{};
  std::size_t registeredPcmBytes_{};
  bool ownsComInitialization_{};
  bool callbackRegistered_{};
  bool active_{true};
};

AirfixXAudio2Backend::AirfixXAudio2Backend()
    : impl_(std::make_unique<Impl>()) {}

AirfixXAudio2Backend::~AirfixXAudio2Backend() = default;

AirfixXAudio2OutputState AirfixXAudio2Backend::outputState() const noexcept {
  return impl_->outputState();
}

std::uint32_t AirfixXAudio2Backend::lastErrorCode() const noexcept {
  return impl_->lastErrorCode();
}

AirfixAudioClipRegistrationResult
AirfixXAudio2Backend::registerPcm16Clip(const audio::Pcm16ClipView &clip) {
  return impl_->registerPcm16Clip(clip);
}

AirfixAudioSubmissionResult
AirfixXAudio2Backend::submit(const audio::AudioCommandBatch &batch) {
  return impl_->submit(batch);
}

void AirfixXAudio2Backend::setActive(const bool active) noexcept {
  impl_->setActive(active);
}

AirfixXAudio2OutputState AirfixXAudio2Backend::recover() noexcept {
  return impl_->recover();
}

} // namespace airfix::windows
