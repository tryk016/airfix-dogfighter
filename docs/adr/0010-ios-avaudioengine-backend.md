# ADR-0010: Use AVAudioEngine for native iOS game audio

**Status:** Accepted

**Date:** 2026-07-29

**Deciders:** project owner and implementation lead

## Context

The portable C++20 audio boundary already represents recovered sound behavior
as bounded, monotonically sequenced commands over immutable PCM16 clips.
Windows consumes that contract through XAudio2 2.9. The private iOS product
needs its own native playback implementation without introducing Windows
types, SDL audio, original sample files, or device ownership into simulation.

The first reconstructed aircraft path requires simultaneous voices, looping,
gain changes, and a pitch multiplier from `0.25` through `4.0`. The iOS product
must also respect deliberate gameplay pause, foreground/background state,
audio interruptions, output-route loss, and media-service resets. It targets
iOS 16.4 and does not play audio in the background.

## Decision

Use **AVAudioEngine** with one `AVAudioPlayerNode` and one
`AVAudioUnitVarispeed` per active portable voice.

- Convert each valid mono/stereo interleaved PCM16 registration once into
  AVAudioEngine's standard deinterleaved Float32 representation and retain the
  immutable `AVAudioPCMBuffer` in the backend. Memory admission accounts for
  the converted storage, not only the smaller source view.
- Connect each player through its varispeed node to the engine's main mixer.
  `AVAudioUnitVarispeed.rate` directly implements the portable pitch/rate
  multiplier and has the same documented `0.25...4.0` range.
- Use `AVAudioSessionCategoryAmbient` with the default mode. The game respects
  the Ring/Silent switch, mixes with user audio, requests no microphone, and
  declares no background-audio mode.
- Defer session activation until the player deliberately resumes gameplay.
  Pausing or leaving the foreground pauses the engine, deactivates the session,
  drops one-shot voices, and retains desired looping voices.
- Observe interruption, route-change, media-services-reset, and engine
  configuration-change notifications. All framework callbacks are marshalled
  onto the main thread before backend or game state is touched.
- An interruption, loss of the current output route, or media-services reset
  forces gameplay to pause and neutralizes input. Notification delivery never
  resumes gameplay. Only a later explicit player action may reactivate audio.
- Rebuild a lost graph from immutable clips and retained looping-voice state.
  One-shot effects are never replayed after recovery because their original
  presentation time has passed.
- Treat session/endpoint failure as supported silent output. Command
  validation and sequence consumption remain independent of speaker
  availability.

The backend remains under `apps/airfix-ios`; the shared command and PCM
contracts remain under `src/airfix/audio`.

## Options considered

| Option | Advantages | Reasons not selected |
|---|---|---|
| AVAudioEngine with player and varispeed nodes | Native graph, concurrent buffers, exact required rate range, later mixer/spatial extensions, explicit session lifecycle | Requires graph recovery and one node chain per voice |
| AVAudioPlayer per sound | Small API and built-in looping/rate controls | Its rate contract is narrower and does not provide the same pitch-changing varispeed semantics; future shared mixing/spatial work would require replacement |
| RemoteIO/Audio Unit render callback | Lowest-level latency and complete mixer control | Requires a custom real-time mixer, resampler, scheduler, and recovery path before evidence shows that complexity is necessary |
| SDL3 audio | Potentially fewer platform-specific files | Violates the accepted native-iOS adapter boundary and gives less direct control over `AVAudioSession` lifecycle |
| Third-party middleware | Mature mixing and spatial features | Additional binary, license, package-size, and integration surface without a demonstrated requirement |

## Consequences

### Positive

- Both products consume the same portable command batches while retaining
  native device/session behavior.
- The recovered pitch multiplier maps without clamping or a new conversion
  curve.
- Original PCM never needs to be embedded in public source or CI.
- User audio is not unnecessarily interrupted and the app emits no sound while
  backgrounded.
- Retained looping state gives deterministic recovery after route or media
  service reconfiguration.

### Negative and follow-up work

- Each active voice owns two graph nodes; the first implementation therefore
  retains the explicit 64-voice ceiling.
- One-shot playback positions cannot be reconstructed after graph loss, so
  those voices are intentionally discarded.
- The initial backend does not yet decode owner-local containers, stream long
  clips or music, spatialize listener/emitters, prioritize voices, or implement
  haptics.
- Simulator/device output, interruptions, headphone removal, Bluetooth route
  changes, silent mode, and media-service reset still require physical-device
  acceptance.

## Implementation constraints

- Construction, clip registration, command submission, lifecycle calls, and
  recovery are main-thread operations.
- Notification blocks retain only an invalidatable owner token; they cannot
  call a destroyed C++ backend.
- Clip and voice IDs are explicit and bounded. A start command must reference a
  registered clip, and preflight must prove capacity and generation safety
  before command application.
- PCM16-to-Float32 conversion is deterministic (`sample / 32768.0`) and occurs
  outside the render callback at registration time.
- Gain and pitch are accepted only through the existing portable validation.
- Completion work is dispatched off the audio render callback before graph
  mutation. A voice generation token prevents an old completion from removing
  a replacement voice with the same ID.
- Original or converted samples, paths, captures, and analyzer output remain
  owner-local and outside Git, Actions, caches, and build artifacts.

## Follow-up

1. Bind verified owner-local sound samples and loop metadata to portable clip
   and voice IDs.
2. Feed reconstructed aircraft audio commands from the live 12 ms producer.
3. Add listener/emitter position and bounded voice-priority contracts after
   static/runtime evidence proves their semantics.
4. Run the interruption and route matrix on iPhone 17 Pro Max and iPhone SE
   (3rd generation).

## Official references

- [Apple: AVAudioEngine](https://developer.apple.com/documentation/avfaudio/avaudioengine)
- [Apple: AVAudioPlayerNode](https://developer.apple.com/documentation/avfaudio/avaudioplayernode)
- [Apple: AVAudioFormat](https://developer.apple.com/documentation/avfaudio/avaudioformat)
- [Apple: AVAudioUnitVarispeed](https://developer.apple.com/documentation/avfaudio/avaudiounitvarispeed)
- [Apple: AVAudioUnitVarispeed rate](https://developer.apple.com/documentation/avfaudio/avaudiounitvarispeed/rate)
- [Apple: AVAudioSession](https://developer.apple.com/documentation/avfaudio/avaudiosession)
- [Apple: Handling audio interruptions](https://developer.apple.com/documentation/avfaudio/handling-audio-interruptions)
- [Apple: Responding to audio route changes](https://developer.apple.com/documentation/avfaudio/responding-to-audio-route-changes)
- [Apple: Media services reset notification](https://developer.apple.com/documentation/avfaudio/avaudiosession/mediaserviceswereresetnotification)
