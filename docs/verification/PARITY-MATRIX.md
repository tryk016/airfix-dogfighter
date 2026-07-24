# Parity matrix

Statuses: `not-investigated`, `observed`, `specified`, `implemented`, `verified`,
`deferred`.

| Area | Priority | Status | Reference scenarios | Notes |
|---|---:|---|---|---|
| Bootstrap and resource loading | P0 | not-investigated | SCN-BOOT-001 | Launcher/ICD boundary unknown |
| Main menu and settings | P1 | not-investigated | SCN-UI-001 | |
| Level/room loading | P0 | not-investigated | SCN-LEVEL-001 | Blocked by asset formats |
| Aircraft flight model | P0 | observed | SCN-FLIGHT-001 | Force/torque and collision stages located; update order and equations unknown |
| Deterministic player-control state | P0 | implemented | SCN-FLIGHT-001 | Frozen-pose Q15/fire-intent bridge; explicitly not flight parity |
| Camera | P0 | not-investigated | SCN-CAMERA-001 | |
| Collision and damage | P0 | not-investigated | SCN-COLLISION-001 | |
| Primary and secondary weapons | P0 | not-investigated | SCN-WEAPON-001 | |
| Ground/water/interactive actors | P1 | not-investigated | SCN-ACTOR-001 | |
| AI | P1 | not-investigated | SCN-AI-001 | |
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
| Semantic actions and deterministic input frames | P0 | specified | SCN-INPUT-001 | ADR-0002 |
| iOS touch flight controls | P0 | specified | SCN-IOS-002 | Stick, throttle, fire, camera |
| Touch layout profiles/editor | P1 | specified | SCN-INPUT-004 | Phone/tablet/handedness |
| Controller gameplay and menus | P1 | specified | SCN-IOS-003 | Bluetooth/USB extended gamepad |
| Controller hot-plug/disconnect | P1 | specified | SCN-IOS-004 | Release, pause, touch recovery |
| Input remapping/calibration | P1 | specified | SCN-INPUT-006 | Deadzone/curve/invert/conflicts |
| Phone/controller haptics | P1 | specified | SCN-INPUT-007 | Capability fallback and stop rules |
| iOS lifecycle/interruption recovery | P0 | specified | SCN-IOS-005 | No stuck input/audio |
| iOS atomic saves and migration | P0 | specified | SCN-IOS-006 | Recovery from interrupted write |
| iOS safe-area and adaptive UI | P0 | specified | SCN-IOS-011 | Phone/tablet variants |
| iOS performance/thermal behavior | P1 | specified | SCN-IOS-009 | Sustained device test |
| iPhone 17 Pro Max / iOS 26.6 | P0 | specified | SCN-IOS-DEVICE-001 | Large Dynamic Island/120 Hz/high quality |
| iPhone SE 3 / iOS 26.3 | P0 | specified | SCN-IOS-DEVICE-002 | Compact Home-button/A15 floor |
| iOS 16.4 deployment compatibility | P0 | specified | SCN-IOS-BUILD-001 | Build/availability only; no runtime test |
| GitHub Actions signed IPA | P0 | specified | SCN-IOS-CI-001 | Protected secrets, signature, min OS, ARM64 |
| Private `.afpack` import | P0 | specified | SCN-IOS-DATA-001 | Original data never enters CI |
| Faithful renderer | P0 | not-investigated | SCN-RENDER-001 | |
| Enhanced renderer | P3 | deferred | SCN-RENDER-101 | Begins after faithful slice |
