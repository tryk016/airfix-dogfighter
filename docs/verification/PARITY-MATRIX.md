# Parity matrix

Statuses: `not-investigated`, `observed`, `specified`, `implemented`, `verified`,
`deferred`.

| Area | Priority | Status | Reference scenarios | Notes |
|---|---:|---|---|---|
| Bootstrap and resource loading | P0 | not-investigated | SCN-BOOT-001 | Launcher/ICD boundary unknown |
| Main menu and settings | P1 | not-investigated | SCN-UI-001 | |
| Level/room loading | P0 | implemented | SCN-LEVEL-001 | Bounded atomic authenticated aggregate loader with ordered multi-CCF room/start lookup, shared physical CCF cache, global texture namespace, combined room/player draw model, retained all-room BSP arena, and native publication boundary; stateful room application and device acceptance pending |
| Aircraft flight model | P0 | observed | SCN-FLIGHT-001 | Scheduler, controls, and complete static equations recovered; physical units, runtime traces, and numeric tolerance remain unknown |
| Deterministic player-control state | P0 | implemented | SCN-FLIGHT-001 | Frozen-pose flight/throttle/combat/camera/mission intent bridge; explicitly not behavior parity |
| Player spawn and visual identity | P0 | implemented | SCN-SPAWN-001 | Authenticated primary actor, start room, selected-skin hierarchy, textures, exact spawn transform, combined draw submission, and allocation-free publication proof implemented; physical-device visual acceptance pending |
| Dynamic instance-pose transport | P0 | implemented | SCN-RENDER-001 | Authenticated actor binding feeds a bounded bit-verified step-zero frame and allocation-free accepted-step publications through a per-snapshot SPSC exchange into Metal; changing world pose awaits the recovered movement law |
| Camera | P0 | observed | SCN-CAMERA-001 | Exact world-to-camera point, positive-Z projection/depth, four depth states, gameplay presets, vehicle quaternion matrix, stateless chase, collision/factor primitives, retained BSP line/portal queries, runtime/source-world basis adaptation, static sphere plus line correction, atomic position/room/factor completion, immutable SPSC frame publication, unit-scale look-at pose snapshot, ordered world projection, and parity-first 640x480 aspect-fit implemented; dynamic-object and transparent collision-portal paths, controlled runtime traces, and Metal integration remain |
| Collision and damage | P0 | observed | SCN-COLLISION-001 | Static/BSP resolver and rigid-body impulse order located; damage semantics incomplete |
| Primary and secondary weapons | P0 | observed | SCN-WEAPON-001 | Attack events and setters identified; projectile spawn and damage remain unknown |
| Ground/water/interactive actors | P1 | not-investigated | SCN-ACTOR-001 | |
| AI | P1 | observed | SCN-AI-001 | Eight-channel control mapping and event dispatch recovered; behavior logic incomplete |
| Mission triggers and progression | P0 | not-investigated | SCN-MISSION-001 | |
| HUD and mission status | P1 | not-investigated | SCN-HUD-001 | |
| Sound effects and voices | P1 | not-investigated | SCN-AUDIO-001 | |
| Music subsystem with content absent | P1 | specified | SCN-AUDIO-002 | Original CD/audio unavailable |
| Save/load and rosters | P1 | not-investigated | SCN-SAVE-001 | |
| Localization | P1 | observed | SCN-LOC-001 | EN/DA/NO/SV packages found |
| Intro videos | P2 | observed | SCN-VIDEO-001 | Two AVI files found |
| Dogfight/multiplayer | P3 | deferred | SCN-DOGFIGHT-001 | Out of v1.0 scope |
| House Editor | P3 | deferred | SCN-EDITOR-001 | Out of v1.0 scope |
| Paint Room | P3 | deferred | SCN-PAINT-001 | Out of v1.0 scope |
| Semantic actions and deterministic input frames | P0 | implemented | SCN-INPUT-001 | Bounded multi-source router plus immutable fixed-tick frames |
| iOS touch flight controls | P0 | implemented | SCN-IOS-002 | Full baseline action transport and adaptive overlay; physical-device acceptance pending |
| Touch layout profiles/editor | P1 | specified | SCN-INPUT-004 | Phone/tablet/handedness |
| Controller gameplay and menus | P1 | implemented | SCN-IOS-003 | Baseline extended-gamepad action transport; finished menu UI and device acceptance pending |
| Controller hot-plug/disconnect | P1 | implemented | SCN-IOS-004 | Generation-gated reset, release, pause, and touch recovery; device acceptance pending |
| Input remapping/calibration | P1 | specified | SCN-INPUT-006 | Deadzone/curve/invert/conflicts |
| Phone/controller haptics | P1 | specified | SCN-INPUT-007 | Capability fallback and stop rules |
| iOS lifecycle/interruption recovery | P0 | implemented | SCN-IOS-005 | Input cancellation and explicit resume implemented; audio path pending |
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
