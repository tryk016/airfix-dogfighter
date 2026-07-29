# ADR-0009: Use XAudio2 2.9 for Windows game audio

**Status:** Accepted

**Date:** 2026-07-29

**Deciders:** project owner and implementation lead

## Context

ADR-0007 makes Windows x64 a playable product over the same portable C++20
game as iOS. ADR-0008 assigns window and physical-input handling to SDL3 and
rendering to D3D11/DXGI, but deliberately leaves audio as a separate native
adapter.

The game needs low-latency effects and voices, concurrent looping aircraft
sounds, gain and pitch updates, later 3D positioning, focus/pause handling,
default-device changes, and useful failure behavior when no output endpoint is
present. Original CD music is unavailable and remains an optional absent
content set. Public builds and tests cannot contain original sound data.

The shared game must produce the same bounded, platform-neutral audio commands
for Windows and iOS. It must not know about Windows devices, COM, XAudio2
voices, Apple audio sessions, or SDL audio.

## Decision

Use the inbox **XAudio2 2.9** runtime on Windows 10 and Windows 11.

- Load `XAUDIO2_9.DLL` from the Windows system directory and resolve
  `XAudio2Create` at runtime. This preserves the XAudio2 2.9 requirement while
  allowing the additional local MinGW compatibility build, whose import
  library may expose only an older XAudio2 version.
- Create the mastering voice for the default device by passing a null device
  identifier and use `AudioCategory_GameMedia`. On Windows 10 and later this
  opts into the virtualized WASAPI client and automatic default-device
  switching.
- Treat an unavailable default output as a supported silent state. Continue
  validating and consuming commands so simulation never depends on the
  presence of speakers.
- Receive `OnCriticalError` on the engine callback, store only the error signal
  there, and recreate the engine on the game thread. Do not allocate, log,
  mutate game state, or recreate the engine from the callback.
- Pause/resume the engine for focus and lifecycle transitions. Engine recovery
  drops platform voices; the shared game republishes current loop state through
  later commands.
- Use immutable PCM16 clip registrations and bounded voice/clip tables in the
  first implementation. Decoding and private package ownership remain outside
  the platform backend.
- Add X3DAudio later for listener/emitter spatialization while retaining the
  same XAudio2 voice graph.
- Keep iOS on a native Apple audio backend behind the same portable command
  contract.

SDL3 remains compiled with audio disabled. The project does not use SDL audio,
DirectSound, XAudio2 2.7, the legacy DirectX SDK, or a direct application-owned
WASAPI render loop.

## Options considered

| Option | Advantages | Reasons not selected |
|---|---|---|
| XAudio2 2.9 | Game-oriented voices, mixing, pitch/rate conversion, buffering, effects graph, low latency, and direct path to X3DAudio | Windows-only backend and explicit voice/device recovery are still required |
| Direct WASAPI | Maximum endpoint and buffer control; lowest-level Windows contract | Substantially more mixer, scheduling, conversion, and recovery code without a demonstrated project benefit |
| SDL3 audio | One library already exists in the Windows shell | Blurs the deliberate native backend boundary and gives less direct access to Windows game-audio diagnostics and X3DAudio |
| DirectSound or XAudio2 2.7 | Superficially closer to legacy Windows audio | Obsolete compatibility direction, legacy SDK/runtime dependency, and no value for behavioral parity |
| Third-party cross-platform middleware | Potentially common backend code | Additional dependency, licensing, package size, and abstraction cost when both target platforms have suitable native APIs |

## Consequences

### Positive

- Windows has a concrete native audio API with a smaller implementation burden
  than direct WASAPI.
- Default-device changes are normally handled below the game when the null
  device identifier selects the virtualized client.
- Missing speakers or a headless Actions runner do not make a data-less build
  fail.
- Dynamic loading avoids accidentally linking the local XAudio2 2.8 import
  library and makes a missing 2.9 runtime an explicit initialization error.
- Synthetic muted PCM can exercise the command and native voice path in public
  CI without redistributing game content.

### Negative and follow-up work

- Windows 10 becomes the API-family floor; the exact supported Windows 10
  release and packaging floor still require acceptance testing.
- A critical engine recreation loses native voices. The higher-level audio
  owner must keep enough state to republish loops and parameters.
- The initial PCM16 contract does not yet decode WAV/container data, mix music,
  stream long clips, spatialize emitters, capture reference audio, or implement
  iOS playback.
- Physical-device tests must cover endpoint removal, default-device switching,
  focus loss, pause, suspend/resume, rapid voice churn, and long runs.

## Implementation constraints

- Portable command batches use monotonic sequence numbers, fixed maximum
  command counts, explicit clip/voice IDs, finite bounded gain/pitch values,
  and fail-closed validation.
- A clip is copied into backend-owned immutable storage before asynchronous
  playback. Per-clip and aggregate PCM budgets prevent unbounded input.
- Platform types remain under `apps/airfix-windows`; `src/airfix/audio` stays
  portable C++20.
- Original and converted audio remains owner-local and outside Git, Actions,
  caches, test artifacts, and release downloads.

## Official references

- [Microsoft: XAudio2 introduction](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-introduction)
- [Microsoft: XAudio2 versions](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-versions)
- [Microsoft: Initialize XAudio2](https://learn.microsoft.com/en-us/windows/win32/xaudio2/how-to--initialize-xaudio2)
- [Microsoft: IXAudio2::CreateMasteringVoice](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-createmasteringvoice)
- [Microsoft: IXAudio2EngineCallback::OnCriticalError](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2enginecallback-oncriticalerror)
- [Microsoft: X3DAudio overview](https://learn.microsoft.com/en-us/windows/win32/xaudio2/x3daudio-overview)
- [Microsoft: WASAPI overview](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi)
