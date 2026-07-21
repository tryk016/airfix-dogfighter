# Parity matrix

Statuses: `not-investigated`, `observed`, `specified`, `implemented`, `verified`,
`deferred`.

| Area | Priority | Status | Reference scenarios | Notes |
|---|---:|---|---|---|
| Bootstrap and resource loading | P0 | not-investigated | SCN-BOOT-001 | Launcher/ICD boundary unknown |
| Main menu and settings | P1 | not-investigated | SCN-UI-001 | |
| Level/room loading | P0 | not-investigated | SCN-LEVEL-001 | Blocked by asset formats |
| Aircraft flight model | P0 | not-investigated | SCN-FLIGHT-001 | |
| Camera | P0 | not-investigated | SCN-CAMERA-001 | |
| Collision and damage | P0 | not-investigated | SCN-COLLISION-001 | |
| Primary and secondary weapons | P0 | not-investigated | SCN-WEAPON-001 | |
| Ground/water/interactive actors | P1 | not-investigated | SCN-ACTOR-001 | |
| AI | P1 | not-investigated | SCN-AI-001 | |
| Mission triggers and progression | P0 | not-investigated | SCN-MISSION-001 | |
| HUD and mission status | P1 | not-investigated | SCN-HUD-001 | |
| Sound effects and voices | P1 | not-investigated | SCN-AUDIO-001 | |
| Music | P1 | not-investigated | SCN-AUDIO-002 | CD audio may be missing |
| Save/load and rosters | P1 | not-investigated | SCN-SAVE-001 | |
| Localization | P1 | observed | SCN-LOC-001 | EN/DA/NO/SV packages found |
| Intro videos | P2 | observed | SCN-VIDEO-001 | Two AVI files found |
| Dogfight/multiplayer | P2 | not-investigated | SCN-DOGFIGHT-001 | Scope decision deferred |
| House Editor | P2 | not-investigated | SCN-EDITOR-001 | Scope decision deferred |
| Paint Room | P2 | not-investigated | SCN-PAINT-001 | Scope decision deferred |
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
| Faithful renderer | P0 | not-investigated | SCN-RENDER-001 | |
| Enhanced renderer | P3 | deferred | SCN-RENDER-101 | Begins after faithful slice |
