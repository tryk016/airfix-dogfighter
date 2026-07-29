# Windows x64 product requirements

**Status:** accepted product direction; first data-less renderer shell
implemented; playable integration pending

**Scope:** requirements unique to the playable Windows x64 product. Shared
gameplay scope and parity requirements are defined in `V1-SCOPE.md`.

## Product role

Windows x64 is both a playable desktop reconstruction and the project's primary
environment for rapid debugging and comparison with the original. It runs
native 64-bit reconstructed code; it is not a wrapper, emulator, or port of the
original PE32 executable, and it does not recreate the DirectX 7 API.

The repository now builds a native data-less SDL3/D3D11 shell. It creates a
Win32 window through SDL, executes a shared synthetic draw plan through HLSL
and D3D11/DXGI, handles focus and resize, falls back from hardware D3D11 to
WARP, and verifies the rendered back buffer in a hidden CTest smoke mode. This
proves the product and renderer boundary but is not a playable game. Product
status becomes playable only after physical input, audio, owner-local content,
and reconstructed gameplay are integrated.

## Shared-core contract

The Windows product must use the same portable C++20 implementation as iOS for:

- resource formats, identifiers, validation, and authenticated content
  sessions;
- simulation scheduling, flight, physics, collision, damage, weapons, AI,
  mission logic, and campaign flow;
- semantic actions, bindings, and immutable per-tick input frames;
- platform-neutral render and audio command streams;
- saves, localization, diagnostics, replay, and parity-state encoding.

Windows-specific headers, handles, key codes, graphics objects, audio devices,
paths, and clocks must remain behind platform interfaces. Platform code may
translate operating-system events; it may not redefine gameplay behavior.

## Platform boundaries

| Area | P0 requirement |
|---|---|
| Window and lifecycle | SDL3 x64 window/events, clean startup/shutdown, focus loss, resize, windowed/fullscreen presentation, display changes, and recoverable device recreation |
| Rendering | D3D11/DXGI faithful backend with HLSL, consuming shared draw/camera commands with resolution-independent presentation and optional enhancements kept behind explicit modes |
| Input | SDL3 keyboard/mouse and supported game controllers mapped into the shared semantic action model, including disconnect and focus-loss neutralization |
| Audio | Native output backend consuming shared audio commands, with device change, focus, pause, and absent-music behavior |
| Files and content | Owner-local import, validation, activation, rollback, saves, and diagnostics without persisting source installation paths |

ADR-0008 selects SDL3 for window/events/input and D3D11/DXGI with HLSL for
rendering. SDL3 must not issue game draw calls. Minimum supported Windows
version, D3D feature-level floor, Windows audio API, and final packaging format
remain focused follow-up decisions.

## Controls

The complete campaign must be playable with:

1. a documented keyboard and mouse layout; and
2. a supported game controller after selection.

Both paths map to the same actions used by touch and controllers on iOS.
Remapping, calibration/deadzones, controller hot-plug, controller glyphs, focus
loss, and synthetic release of held actions are release requirements.

## Debug and parity facilities

The Windows product must provide:

- Debug and Release builds with symbols, assertions, bounded structured logs,
  and crash diagnostics;
- deterministic scenario input capture/replay and canonical state hashes;
- frame, camera, draw-command, input-frame, and audio-event capture;
- a faithful/reference presentation mode that can disable all modern effects;
- repeatable screenshots and frame captures with documented viewport and
  configuration;
- a side-by-side procedure that treats the original executable as an external
  read-only reference in a controlled environment; and
- synthetic smoke scenarios that require no proprietary content.

Reference observations and private captures remain outside Git. Only aggregate
facts, authored specifications, synthetic fixtures, and independently written
tests may enter the public repository.

## Private content

The application imports an owner-created, validated `.afpack` or a documented
successor generated locally from a lawfully owned installation. The same
runtime format is consumed by Windows and iOS.

Original executables, archives, artwork, audio, converted packages,
content-bearing installers, tool databases, private traces, and machine-local
paths must not enter Git, public Actions logs, caches, artifacts, or release
downloads. The public Windows build must boot without data and explain how the
owner supplies a local package.

## Release acceptance

The Windows x64 target is release-ready when:

1. a clean checkout builds a native x64 executable through documented CMake
   presets and passes data-less CI;
2. the application boots without content and imports a validated owner-local
   package with actionable errors and safe rollback;
3. the selected single-player campaign path, menus, HUD, saves, localization,
   effects, and available audio are complete;
4. keyboard/mouse-only and controller-only acceptance passes;
5. window, display, focus, input-device, and audio-device transitions are safe;
6. reference scenarios meet the same simulation tolerances used for iOS and
   the headless core; and
7. packaged output contains no original or converted proprietary content unless
   it is an explicitly private owner-only artifact kept outside public
   infrastructure.

Multiplayer, House Editor, Paint Room, unavailable CD music, and public
redistribution of original content remain outside version 1.
