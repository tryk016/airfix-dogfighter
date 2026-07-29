# Audio system

**Status:** portable command boundary, AirCraft engine/dive composition,
authenticated owner-local PCM binding, Windows XAudio2 2.9, and iOS
AVAudioEngine backends implemented; live scheduling and physical-output
acceptance pending

## Product boundary

Windows and iOS share the recovered sound timing, state machines, clip/voice
identity, and bounded audio commands. They do not share device APIs:

```text
Recovered AirCraft state (portable C++20)
    -> exact phase / destroyed-dive / modulation composition
    -> bounded monotonic AudioCommandBatch
        -> XAudio2 2.9 on Windows
        -> AVAudioEngine on iOS
```

No original file name, decoded sample, device handle, Windows header, Apple
framework, or mixer object may enter the simulation layer. Original and
converted audio remains owner-private and outside Git and public CI.

## Recovered AirCraft roles

The currently confirmed sound roles are:

| Native ID | Portable role | Recovered behavior |
|---:|---|---|
| `0x1A` | `engineOn` | running layer with load-derived gain and pitch |
| `0x1B` | `engineIdle` | complementary idle gain and load-derived pitch |
| `0x1C` | `engineTurn` | running pitch and orientation-modulated gain |
| `0x1D` | `engineStart` | starts the four-second engine transition |
| `0x1E` | `engineStop` | plays when thrust shuts a starting/running engine down |
| `0x20` | `engineDive` | health-gated destroyed dive with vertical-speed gain |

`LegacyAircraftEngineAudioState` advances its timer every 12 ms aircraft call
but performs sound work only every fifth call. On that audio call,
`LegacyAircraftAudioCoordinator` preserves this exact order:

1. engine start/running/stop phase transitions;
2. destroyed-dive start/stop and parameter changes;
3. common engine-on, turn, and idle modulation.

Skipped cadence calls do not inspect destroyed-dive health or velocity. A
failed transition is transactional: caller-owned state is unchanged and no
partial command batch is returned.

## Private clip and voice binding

The coordinator accepts six explicit `LegacyAircraftAudioBinding` records.
Every recovered role must appear exactly once, clip and voice IDs must be
nonzero, and voice IDs must be unique within one aircraft instance. Clip IDs
may be shared, which lets public tests drive all roles with one synthetic PCM
fixture.

The owner-local content layer now:

1. resolves all six roles by exact logical entry in the nested `Resource.up`;
2. preflights per-file and aggregate source memory before any sample read;
3. reads only through one unchanged authenticated `VerifiedContentSession`;
4. decodes bounded RIFF/WAVE PCM16 into owned portable storage;
5. preserves the recovered numeric role as its clip ID;
6. allocates unique voice IDs for the actor instance; and
7. retains all packages, decoded proprietary content, and local paths outside
   Git.

The observed full-buffer infinite sampler loop is required for `engineOn`,
`engineIdle`, `engineTurn`, and `engineDive`. `engineStart` and `engineStop`
remain one-shots because the recovered runtime stops or naturally replaces
them; authored sampler metadata in those two files does not redefine runtime
behavior. The seventh nearby `engineoff` source is not assigned to a recovered
role.

The parser accepts only bounded RIFF/WAVE integer PCM, mono/stereo, 16-bit
frame-aligned data at 8–192 kHz. Chunk counts, sampler-loop counts, source
bytes, transient source footprint, and published PCM all have independent
ceilings. Missing, ambiguous, malformed, cancelled, over-budget, or
transaction-changed input produces no partial clip set.

## Portable command contract

`AudioCommandBatch` has a nonzero, monotonically increasing sequence and at
most 64 commands. It supports:

- start voice with clip, gain, pitch, and loop policy;
- stop voice;
- set voice gain;
- set voice pitch; and
- stop all voices.

PCM registration accepts bounded mono/stereo signed 16-bit interleaved data at
8–192 kHz. Platform backends must copy or otherwise own the registered data.
The current command range is gain `0..4` and pitch `0.25..4`. Recovered values
outside that safety contract cause composition to fail closed; they are not
silently clamped.

The original `UpdateSounds(dt)` operation is unnecessary for the immediate
command consumer. The original model-parameter write is represented by the
coordinator's returned final `smoothedThrust` and remains a simulation concern,
not a device command.

## Windows execution

The Windows backend securely loads inbox `XAUDIO2_9.DLL`, owns registered PCM,
creates source voices, and applies the shared command stream. Missing-voice
gain/pitch/stop operations are safe no-ops, matching the coordinator's ability
to modulate roles before or after their active phase.

The public product smoke uses one short synthetic PCM clip and distinct voice
IDs. It performs:

1. four skipped calls and the fifth-call engine start;
2. timer advancement and running-layer activation;
3. engine shutdown plus destroyed-dive activation in one composed cadence;
4. positive-health destroyed-dive stop; and
5. the same validation when no physical output endpoint exists.

This proves the portable-to-native path, not audible parity.

For owner-local development, `afpack-install` writes an AFPACK into an explicit
private content root using the same atomic installer used by iOS. The Windows
application accepts `--content-root <private-directory>` for normal startup and
`--validate-content-root <private-directory>` for a hidden load/register check.
It authenticates the AFAC-selected package before loading samples. Passing the
original installation directory or loose WAV files is unsupported.

## iOS execution

The iOS backend converts each bounded interleaved PCM16 registration once into
AVAudioEngine's standard deinterleaved Float32 representation. Every active
voice uses its own `AVAudioPlayerNode -> AVAudioUnitVarispeed -> main mixer`
chain. The varispeed node's documented `0.25...4.0` rate range is identical to
the portable pitch contract and preserves coupled rate/pitch behavior.

`AVAudioSessionCategoryAmbient` respects the Ring/Silent switch, mixes with
user audio, requests no microphone, and has no background mode. The session is
activated only after deliberate gameplay resume. Pause, inactive, background,
and view-loss paths pause the engine and deactivate the session.

Interruption start, loss of the current output route, and media-services reset
force the portable game session to pause and neutralize input. Notification
delivery never resumes gameplay. Graph recovery retains only desired looping
voices; one-shot effects are discarded because their presentation time has
already passed. Completion callbacks only enqueue main-thread graph cleanup,
and generation tags prevent an old callback from removing a replacement voice.

Unsigned `iphoneos` and `iphonesimulator` builds validate the Objective-C++ API,
iOS 16.4 availability, linkage, and data-less bundle. They do not prove audible
output, route timing, or physical-device recovery.

The content worker loads the six clips from the same authenticated session as
the mission and moves them inside the opaque mission snapshot. Main prepares a
new AVAudioEngine backend, registers all six copied clips, and derives the
actor bindings before consuming the mission ticket. The prepared renderer,
backend, bindings, spawn state, and exact ticket then commit by no-fail
ownership swaps; a failed or stale preparation leaves the previous committed
mission untouched.

## Pending acceptance

- feed the coordinator from the live 12 ms AirCraft producer;
- retain/restart desired looping voices across Windows output-device
  recreation;
- recover listener/emitter position, attenuation, priority, and broader sound
  effects;
- test Windows default-device changes plus iOS interruptions, silent mode,
  wired/Bluetooth route changes, media-service reset, and real output on both
  iPhones;
- compare event timing against controlled original-runtime traces.

Waveform identity is not required by default; timing, role, gain, pitch,
looping, priority, and spatial behavior are the parity contract.
