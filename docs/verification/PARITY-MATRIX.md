# Parity matrix

Statuses: `not-investigated`, `observed`, `specified`, `implemented`, `verified`,
`deferred`.

| Area | Priority | Status | Reference scenarios | Notes |
|---|---:|---|---|---|
| Bootstrap and resource loading | P0 | not-investigated | SCN-BOOT-001 | Launcher/ICD boundary unknown |
| Main menu and settings | P1 | not-investigated | SCN-UI-001 | |
| Level/room loading | P0 | implemented | SCN-LEVEL-001 | Bounded atomic authenticated aggregate loader with ordered multi-CCF room/start lookup, shared physical CCF cache, global texture namespace, combined room/player draw model, retained all-room BSP arena, owned placed/player dynamic colliders, exact CPU budget, native publication boundary, and non-moving iOS runtime ownership with exact reusable line-frame buffers; stateful producer application and device acceptance pending |
| Aircraft flight model | P0 | observed | SCN-FLIGHT-001 | Scheduler, controls, complete static equations, signed 64-bit rest/sleep gate, engine-start smoothing branch, and collision-driven thrust-integrity lifecycle are recovered; isolated pure transitions are implemented while physical units, runtime traces, PRNG ownership, and numeric tolerance remain unknown |
| Deterministic player-control state | P0 | implemented | SCN-FLIGHT-001 | Frozen-pose flight/throttle/combat/camera/mission intent bridge; explicitly not behavior parity |
| Player spawn and visual identity | P0 | implemented | SCN-SPAWN-001 | Authenticated primary actor, start room, selected-skin hierarchy, textures, exact spawn transform, combined draw submission, per-mesh collision asset, and allocation-free native-order live player plus placed-object collider frame implemented; physical-device acceptance and live movement input pending |
| Dynamic instance-pose transport | P0 | implemented | SCN-RENDER-001 | Authenticated actor binding feeds a bounded bit-verified step-zero frame and allocation-free accepted-step publications through a per-snapshot SPSC exchange into Metal; changing world pose awaits the recovered movement law |
| Camera | P0 | observed | SCN-CAMERA-001 | Exact world-to-camera point, positive-Z projection/depth, four depth states, gameplay presets, vehicle quaternion matrix, chase, AirCraft factor recovery with scheduler delta in seconds plus named live health/inactive inputs, collision/factor primitives, retained BSP line/portal queries, runtime/source-world basis adaptation, static sphere plus line correction, per-mesh dynamic BSP and non-portal combined line query, atomic position/room/factor completion, immutable SPSC frame publication, unit-scale look-at pose snapshot, ordered world projection, transactional producer-side step composition, homogeneous Metal packet, bounded complete-packet exchange and render lease, preallocated mission runtime with weak replacement-safe iOS endpoint, reverse-depth gameplay pipeline, tested camera0 bootstrap, and parity-first 640x480 viewport implemented; complete live producer publication, dynamic-object sphere contacts, transparent collision-portal composition, controlled runtime traces, and device acceptance remain |
| Collision and damage | P0 | observed | SCN-COLLISION-001 | Static/BSP resolver, rigid-body impulse order, strict thrust-degradation gates, averaged fourth-power damage, recovery, and clamp are recovered; the isolated primary-ammo actor damage command is implemented while general actor-health integration and runtime traces remain incomplete |
| Primary and secondary weapons | P0 | observed | SCN-WEAPON-001 | Primary/secondary attack dispatch plus `WpMGun` technology, cadence, barrel selection, ammunition, private creation, complete event `0xE2`, target lead, shared flight/lifetime, strict-nearest static/dynamic query order, portal continuation, actor gates, actor damage, and surface/ricochet behavior are recovered; allocation-free timing, transactional shot preparation, payload, a deterministic fixed-capacity generation-tagged activation runtime, room-ID mapping, unobstructed motion, contact contracts, single-hit decision, bounded per-mesh dynamic BSP, static/dynamic competition, bounded automatic visible-portal continuation, runtime-owned authenticated placed/player collider publication/tracing, bounded projectile-level query repetition, runtime room/material mapping, an atomic concrete primary-player actor resolver, fail-closed terminal state/damage/surface command reduction, and creator `DisableBsp`/conditional `EnableBsp` bracketing around the complete transaction are implemented while the changing producer, other actor publication/resolution, active-slot collision advancement, concrete live callback/effect dispatch, tracer/effects, secondary families, runtime traces, and integration remain pending |
| Ground/water/interactive actors | P1 | not-investigated | SCN-ACTOR-001 | |
| AI | P1 | observed | SCN-AI-001 | Eight-channel control mapping and event dispatch recovered; behavior logic incomplete |
| Mission triggers and progression | P0 | not-investigated | SCN-MISSION-001 | |
| HUD and mission status | P1 | not-investigated | SCN-HUD-001 | |
| Sound effects and voices | P1 | observed | SCN-AUDIO-001 | AirCraft five-call engine cadence, strict start/running/stop phases, five engine roles, ordered pitch-volume stream, and health-gated `enginedive` state with vertical-speed volume are recovered as pure bounded transitions; their exact phase/dive/modulation composition now produces the generic bounded command stream consumed by Windows XAudio2 2.9 and iOS AVAudioEngine, while live aircraft scheduling, private clips, spatial parameters, broader effects, audible parity, and device acceptance remain pending |
| Music subsystem with content absent | P1 | specified | SCN-AUDIO-002 | Original CD/audio unavailable |
| Save/load and rosters | P1 | not-investigated | SCN-SAVE-001 | |
| Localization | P1 | observed | SCN-LOC-001 | EN/DA/NO/SV packages found |
| Intro videos | P2 | observed | SCN-VIDEO-001 | Two AVI files found |
| Dogfight/multiplayer | P3 | deferred | SCN-DOGFIGHT-001 | Out of v1.0 scope |
| House Editor | P3 | deferred | SCN-EDITOR-001 | Out of v1.0 scope |
| Paint Room | P3 | deferred | SCN-PAINT-001 | Out of v1.0 scope |
| Semantic actions and deterministic input frames | P0 | implemented | SCN-INPUT-001 | Bounded multi-source router plus immutable fixed-tick frames |
| Cross-product portable simulation contract | P0 | specified | SCN-PLATFORM-001 | Windows x64 and iOS must use one C++20 game, physics, resource, save, input-frame, render-command, and audio-command implementation |
| Windows x64 native product shell | P0 | implemented | SCN-WIN-001 | Data-less native SDL3 window/events/input, D3D11/DXGI/HLSL rendering, and XAudio2 2.9 audio shells are implemented; filesystem/private-content and playable integration remain pending |
| Windows keyboard/mouse gameplay | P0 | implemented | SCN-WIN-INPUT-001 | Baseline SDL3 keyboard/mouse transport, fixed-rate semantic frames, relative-look reset, focus-loss neutralization, and data-less tests implemented; remapping and campaign acceptance pending |
| Windows controller gameplay and menus | P0 | implemented | SCN-WIN-INPUT-002 | Baseline standardized-gamepad mapping, ordered taps, hot-plug, disconnect recovery, generation and neutral gates implemented; selection UI, calibration, glyphs, and controller-only campaign acceptance pending |
| Windows faithful renderer and presentation | P0 | specified | SCN-WIN-RENDER-001 | D3D11/DXGI backend with HLSL over shared draw/camera commands; windowed/fullscreen and reference/enhanced separation |
| Windows audio and device transitions | P0 | implemented | SCN-WIN-AUDIO-001 | XAudio2 2.9 consumes bounded monotonic shared commands and copied PCM16, uses the virtualized default device, pauses on focus loss, recreates outside the callback, tolerates no endpoint, and runs a muted reconstructed AirCraft phase/dive command smoke; default-device/physical-output acceptance, private clip decoding, live scheduling, spatialization, and streaming remain |
| Windows parity/debug capture | P0 | specified | SCN-WIN-PARITY-001 | Primary rapid-debug path with replay, state hashes, frame/draw/input/audio capture, and controlled external-original comparison |
| Windows private `.afpack` import | P0 | specified | SCN-WIN-DATA-001 | Owner-local validated package; original and converted data remain outside Git and public CI |
| iOS touch flight controls | P0 | implemented | SCN-IOS-002 | Full baseline action transport and adaptive overlay; physical-device acceptance pending |
| Touch layout profiles/editor | P1 | specified | SCN-INPUT-004 | Phone/tablet/handedness |
| Controller gameplay and menus | P1 | implemented | SCN-IOS-003 | Baseline extended-gamepad action transport; finished menu UI and device acceptance pending |
| Controller hot-plug/disconnect | P1 | implemented | SCN-IOS-004 | Generation-gated reset, release, pause, and touch recovery; device acceptance pending |
| Input remapping/calibration | P1 | specified | SCN-INPUT-006 | Deadzone/curve/invert/conflicts |
| Phone/controller haptics | P1 | specified | SCN-INPUT-007 | Capability fallback and stop rules |
| iOS lifecycle/interruption recovery | P0 | implemented | SCN-IOS-005 | Input cancellation plus audio pause/deactivation and explicit resume implemented; physical-device acceptance pending |
| iOS audio and route transitions | P1 | implemented | SCN-IOS-007 | AVAudioEngine consumes the shared command/PCM contract through bounded player/varispeed graphs; interruption, route loss, media-service reset, loop-only recovery, and no-automatic-resume policy implemented; private clips and physical-output acceptance pending |
| iOS atomic saves and migration | P0 | specified | SCN-IOS-006 | Recovery from interrupted write |
| iOS safe-area and adaptive UI | P0 | implemented | SCN-IOS-011 | Compact/large landscape overlay implemented; target-device acceptance pending |
| iOS performance/thermal behavior | P1 | specified | SCN-IOS-009 | Sustained device test |
| iPhone 17 Pro Max / iOS 26.6 | P0 | specified | SCN-IOS-DEVICE-001 | Large Dynamic Island/120 Hz/high quality |
| iPhone SE 3 / iOS 26.3 | P0 | specified | SCN-IOS-DEVICE-002 | Compact Home-button/A15 floor |
| iOS 16.4 deployment compatibility | P0 | specified | SCN-IOS-BUILD-001 | Build/availability only; no runtime test |
| GitHub Actions signed IPA | P0 | specified | SCN-IOS-CI-001 | Protected secrets, signature, min OS, ARM64 |
| Private `.afpack` import | P0 | specified | SCN-IOS-DATA-001 | Original data never enters CI |
| Faithful renderer | P0 | not-investigated | SCN-RENDER-001 | |
| Enhanced renderer | P3 | deferred | SCN-RENDER-101 | Begins after faithful slice |
