# Audio system

**Status:** portable command boundary, AirCraft engine/dive composition, and
Windows XAudio2 2.9 backend implemented; private content and iOS playback
pending

## Product boundary

Windows and iOS share the recovered sound timing, state machines, clip/voice
identity, and bounded audio commands. They do not share device APIs:

```text
Recovered AirCraft state (portable C++20)
    -> exact phase / destroyed-dive / modulation composition
    -> bounded monotonic AudioCommandBatch
        -> XAudio2 2.9 on Windows
        -> native Apple audio backend on iOS (pending)
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

The owner-local content layer will eventually:

1. resolve and decode the lawfully owned sample for each role;
2. register immutable PCM under its clip ID;
3. allocate voice IDs unique to the actor instance;
4. select looping behavior from verified sample/type metadata; and
5. retain all source paths and decoded proprietary content outside Git.

Looping is deliberately a binding property. The current evidence proves when
the game calls play and stop, but it does not yet prove every sample's authored
loop metadata.

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

## Pending acceptance

- bind and decode owner-local samples without publishing them;
- feed the coordinator from the live 12 ms AirCraft producer;
- retain/restart desired looping voices across output-device recreation;
- recover listener/emitter position, attenuation, priority, and broader sound
  effects;
- implement the native iOS audio session/device backend;
- test Windows default-device changes and both iPhones with real output;
- compare event timing against controlled original-runtime traces.

Waveform identity is not required by default; timing, role, gain, pitch,
looping, priority, and spatial behavior are the parity contract.
