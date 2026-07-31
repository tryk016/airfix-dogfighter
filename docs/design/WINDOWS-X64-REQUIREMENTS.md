# Windows x64 product requirements

**Status:** accepted product direction; data-less renderer, authenticated
owner-local mission rendering, baseline physical input, native audio, and
product presentation-settings shells implemented; live playable integration
pending

**Scope:** requirements unique to the playable Windows x64 product. Shared
gameplay scope and parity requirements are defined in `V1-SCOPE.md`.

## Product role

Windows x64 is both a playable desktop reconstruction and the project's primary
environment for rapid debugging and comparison with the original. It runs
native 64-bit reconstructed code; it is not a wrapper, emulator, or port of the
original PE32 executable, and it does not recreate the DirectX 7 API.

The repository now builds a native SDL3/D3D11 shell. Its public mode remains
data-less: it creates a Win32 window through SDL, executes a shared synthetic
draw plan through HLSL and D3D11/DXGI, handles focus and resize, falls back
from hardware D3D11 to WARP, and verifies the rendered back buffer in a hidden
CTest smoke mode. An explicit owner-local launch can instead authenticate one
AFPACK revision, build a selected mission manifest and room, prepare complete
D3D11 geometry and texture resources, optionally join an authenticated
MODL-root player aircraft at its selected start pose, bootstrap the recovered
gameplay camera, and render through reverse depth in a full-target Hor+
viewport backed by the shared native-resolution layout.
The hidden private validator requires visible D3D11 output without exposing
the requested logical paths. An explicit one-shot capture mode writes the same
validated back buffer to a new owner-private BMP for local visual comparison;
it never overwrites a file, rejects a requested capture extent that differs
from the physical backbuffer, and keeps all captures outside Git. A
separate public settings-panel capture exercises the real D3D11 composition
path with a synthetic scene and never opens the private content or preference
roots. The interactive product has a DPI-aware pause and Display settings
surface rasterized with DirectWrite/Direct2D and composed after the 3D scene.
It exposes render scale, Hor+/Original 4:3, a safe vertical-FOV increase,
Classic/Enhanced, and renderer statistics through keyboard, mouse, and
standard-controller input. Apply is a
durable prepare-save-publish transaction; Resume is a distinct action and is
disabled until a resumable mission exists. Sparse command-line overrides
remain session-only and never contaminate the persisted base.
Rows, hit targets, status text, and font size use one adaptive physical layout
scale when the preferred DPI-scaled panel would not fit. The interactive
window has a 640x360 logical minimum, and the rasterizer rejects any view
snapshot whose geometry escapes the actual backbuffer.
A separate SDL3 adapter now feeds fixed-rate semantic frames from the baseline
keyboard/mouse and standardized-controller layout, including focus loss,
hot-plug, disconnect, ordered taps, dead-zone handling, and lifecycle neutral
gates. A native XAudio2 2.9 adapter validates bounded portable audio commands,
owns copied PCM16 data, follows focus state, recovers the engine outside its
callback, and safely consumes commands when no output endpoint exists. The
synthetic product smoke test exercises rendering and the shared AirCraft
audio coordinator through start, running, shutdown, destroyed-dive, and
recovery phases without proprietary data. The authenticated room currently
remains at its loaded spawn/camera state: input frames are not yet applied to a
live reconstructed flight simulation. Product status therefore remains
non-playable until changing gameplay state, runtime camera publication,
mission logic, HUD, and menus are integrated.

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
| Rendering | D3D11/DXGI/HLSL backend consuming shared draw/camera/light/material commands; native 3D targets at 100% scale, Hor+ widescreen, independent UI, and explicit Classic/Enhanced profiles |
| Input | SDL3 keyboard/mouse and supported game controllers mapped into the shared semantic action model, including disconnect and focus-loss neutralization |
| Audio | XAudio2 2.9 backend consuming shared bounded audio commands; default-device virtualization, focus/pause, recovery, no-device, and absent-music behavior |
| Files and content | Owner-local import, validation, activation, rollback, saves, and diagnostics without persisting source installation paths |

ADR-0008 selects SDL3 for window/events/input and D3D11/DXGI with HLSL for
rendering. ADR-0009 selects inbox XAudio2 2.9 for native audio; SDL3 audio stays
disabled. SDL3 must not issue game draw calls. The exact supported Windows 10
release, D3D feature-level floor, and final packaging format remain focused
follow-up decisions.

## Resolution and visual-quality contract

ADR-0013 is mandatory for the Windows product:

- the selected DXGI output/backbuffer extent, the 3D render-target extent, the
  viewport, the reference camera canvas, and the UI design space are distinct;
- 100% render scale means an exact one-to-one physical 3D target, including
  3840x2160 rendering for a 3840x2160 output;
- 1080p, 1440p, 4K, 5K, 16:10, 16:9, 21:9, and 32:9 are supported when the
  active display and D3D11 limits allow them;
- standard widescreen is Hor+ with the reference vertical FOV, while Original
  4:3 is an optional aspect-fitted comparison mode;
- vertical FOV can be increased explicitly from 0 through 25 degrees without
  changing the authored camera state, physical render target, UI scale, or
  deterministic simulation;
- render scale supports at least 50-200% independently of sharp UI/text and
  input-coordinate mapping;
- `Classic` preserves the original visual intent at high resolution and
  `Enhanced` adds modern lighting/material/effect stages; and
- `Low`, `Medium`, `High`, and `Ultra` plus separate shadow, MSAA, effect, and
  render-scale controls provide scalable performance.

The authenticated mission renderer uses the shared full-target Hor+ policy and
exact native target at 100% scale. The recovered 640x480 projection remains
reference-camera evidence only. Original 4:3, the 50-200% render-scale range,
and safe FOV are exposed through the Windows Display settings surface. D3D11 owns complete
offscreen color/depth targets and the linear presentation pass for non-100%
scales; publication retains the previous complete snapshot if preparation,
durable persistence, or final validation fails. D3D11 remains responsible for
native rasterization; neither SDL3 nor a legacy DirectX 7 emulation layer owns
drawing. The broader Low/Medium/High/Ultra quality controls remain staged with
their associated lighting, shadow, material, and post-processing work.

The Windows performance target is 60 FPS at 1080p and 1440p on reasonable
contemporary hardware, with scalable settings for 4K. Native 4K at 100% render
scale is a correctness acceptance case even when a lower quality or dynamic
scale is needed for sustained performance on a particular GPU.

## Controls

The complete campaign must be playable with:

1. a documented keyboard and mouse layout; and
2. a supported game controller after selection.

Both paths map to the same actions used by touch and controllers on iOS.
Remapping, calibration/deadzones, controller hot-plug, controller glyphs, focus
loss, and synthetic release of held actions are release requirements.

### Implemented baseline layout

| Action | Keyboard/mouse | Standard gamepad |
|---|---|---|
| Bank / pitch | arrow keys | left stick |
| Increase / decrease thrust | `W` / `S` | D-pad up / down |
| Camera look | relative mouse motion | right stick |
| Primary / secondary fire | left / right mouse button or `Space` / left `Ctrl` | right / left trigger |
| Next weapon | mouse wheel or `Tab` | right shoulder |
| Rear view | `R` or mouse X1 | left shoulder |
| Camera cycle / recenter | `C` / `F`, middle mouse cycles | west face / right-stick click |
| Mission status | `M` or mouse X2 | north face |
| Pause | `Escape` | Start/Menu |
| Menu navigation | arrows, mouse/wheel, `Enter`, `Escape`, `Q` / `E` tabs | left stick, D-pad, south/east face, shoulders |

SDL scancodes are converted to stable USB HID control IDs. Controller Y axes
are normalized so positive pitch/look means up, matching the shared semantic
contract. Relative mouse look is a one-input-tick pulse and returns to neutral
on the next 60 Hz sample. Persistent remapping, configurable calibration,
device selection, and glyph presentation are deliberately follow-up layers;
the table above is the tested fallback profile.

## Debug and parity facilities

The Windows product must provide:

- Debug and Release builds with symbols, assertions, bounded structured logs,
  and crash diagnostics;
- deterministic scenario input capture/replay and canonical state hashes;
- frame, camera, draw-command, input-frame, and audio-event capture;
- separately selectable `Classic` and `Enhanced` presentation profiles;
- a developer overlay showing output and 3D render extents, render scale, FPS,
  CPU/GPU time, draw calls, triangles, lights, and labelled GPU-memory data;
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

The implemented baseline is an exclusive `--import-afpack` product operation.
It resolves an undisclosed per-user `content/` directory through SDL, creates
the canonical transaction UUID internally, rejects a competing same-session
importer, and
maps path-bearing installer diagnostics to fixed actionable categories.
`--installed-content` and `--validate-installed-content` reuse that root; the
explicit-root development interface remains separate. Native picker/progress
and rollback-choice presentation are staged follow-ups over the same portable
transaction.

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
7. the ADR-0013 native-resolution, Hor+, Original 4:3, UI/input scaling,
   diagnostic, screenshot, quality-profile, and 3840x2160-at-100% acceptance
   cases pass; and
8. packaged output contains no original or converted proprietary content unless
   it is an explicitly private owner-only artifact kept outside public
   infrastructure.

Multiplayer, House Editor, Paint Room, unavailable CD music, and public
redistribution of original content remain outside version 1.
