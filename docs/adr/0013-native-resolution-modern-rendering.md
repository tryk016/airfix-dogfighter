# ADR-0013: native-resolution modern rendering contract

- Status: accepted
- Date: 2026-07-29
- Deciders: project owner and implementation lead

## Context

The reconstruction must preserve Airfix Dogfighter's observable gameplay and
distinctive model-kit presentation, but the target products are contemporary
Windows x64 and iOS applications. Reproducing a legacy 640x480 canvas and then
enlarging it would preserve neither image quality nor the capabilities expected
from a modern native renderer.

The recovered 640x480 projection is still valuable evidence. It defines a
reference camera and an optional comparison composition; it does not define the
physical resolution of a render target. Similarly, the current Windows
aspect-fitted 4:3 mission path is a parity bring-up tool, not the final default.

This decision extends ADR-0008. The shared C++20 render front end remains
backend-neutral, Windows remains on D3D11/DXGI/HLSL, and iOS remains on native
Metal. No DirectX 7 interface or fixed legacy framebuffer becomes part of the
target architecture.

## Decision

### Resolution domains

The renderer must keep these domains explicit and independently testable:

| Domain | Meaning |
|---|---|
| Reference camera canvas | Recovered logical 4:3 projection semantics used to establish parity; currently 640x480 |
| UI design space | Resolution-independent layout coordinates, safe areas, text metrics, and input hit regions |
| Output/backbuffer extent | Physical pixels presented by DXGI or the Metal drawable |
| 3D render extent | Physical pixels used to rasterize the scene before final presentation |
| Render scale | User- or policy-selected multiplier from output extent to 3D render extent, independent of UI |

At render scale 100%, the 3D render extent must equal the output extent in both
dimensions. For example, a 3840x2160 output must rasterize the 3D scene into a
3840x2160 target. It must not enlarge a 640x480, 1120x480, or other historical
or hidden logical framebuffer. Optional temporal or spatial upscaling may be
used only when render scale is below 100% or when the user explicitly selects
it; it is never a mandatory replacement for native rendering.

The products must support native or user-selected output at 1080p, 1440p, 4K,
5K, and ultrawide extents where the display and platform allow it. Render scale
must expose at least a 50-200% range, subject to platform resource and texture
size limits. UI and text render at output-appropriate resolution independently
of 3D render scale and remain sharp.

### Aspect ratio, camera, UI, and input

The standard presentation is widescreen Hor+:

- the vertical field of view and vertical scene coverage match the reference
  4:3 camera by default;
- wider aspect ratios reveal more horizontal scene content without stretching
  geometry or cropping vertical content;
- a safe user FOV adjustment is applied independently of aspect-ratio
  correction;
- increasing vertical FOV is an explicit option, never an implicit consequence
  of widescreen; and
- projection of weapons, the reticle, HUD, screen-space effects, and pointer or
  touch coordinates is correct in every supported viewport.

An `Original 4:3` presentation remains available for controlled comparison. It
uses aspect-fit bars where needed and never stretches the image. It is not the
default limitation of the renderer.

Safe-area layout, UI scaling, and input mapping use the UI design space and the
actual presentation viewport, not the 3D render extent. Changing render scale
must not change HUD size, text size, touch hit regions, or controller cursor
coordinates.

### Shared and platform architecture

The portable C++20 front end owns:

- camera and projection descriptions;
- canonical geometry, material, texture, light, fog, and particle data;
- backend-neutral render passes, draw commands, resource identities, and
  diagnostics;
- quality settings and the `Classic`/`Enhanced` visual policy; and
- validation that presentation work cannot feed back into deterministic
  gameplay or physics.

Thin native backends execute that contract:

```text
Portable C++20 render front end
    +-- D3D11 + DXGI + HLSL -- Windows x64
    `-- Metal                  -- iOS
```

The command API must allow new passes, material properties, light types, and
post-processing stages without moving gameplay logic into either backend.
Graphics timing, dynamic resolution, exposure, animation interpolation, and
visual randomness cannot alter the fixed simulation clock, collision, AI,
weapons, mission outcomes, or deterministic state hashes.

### Visual profiles and quality settings

Visual intent and performance quality are orthogonal settings:

- `Classic` preserves the original art direction, composition, material
  response, fog, and effect character as closely as practical while still
  rendering at the selected modern resolution.
- `Enhanced` adds modern lighting, shadows, materials, particles, atmosphere,
  and post-processing without losing the plastic model-kit scale or household
  setting.
- `Low`, `Medium`, `High`, and `Ultra` select performance/quality budgets.
  Shadows, MSAA, post-effects, and render scale also remain individually
  adjustable.

Neither profile permits a low-resolution upscale at 100% render scale.
`Classic` means faithful visual policy, not a legacy framebuffer.

### Ordered image-quality implementation

The implementation is staged so reconstruction can continue in parallel:

1. **Native presentation foundation:** explicit resolution domains, exact
   backbuffer/render-target extents, resize handling, configurable render scale,
   diagnostic counters, and sharp independent UI.
2. **Projection and layout:** Hor+ camera math, optional Original 4:3, safe user
   FOV, all target aspect ratios, safe areas, HUD/reticle/weapon/screen-effect
   projection, and input-coordinate transforms.
3. **Sampling and color correctness:** linear-light shading, explicit sRGB
   resource/output handling, adequate depth precision, mipmapping, correct
   texture filtering, anisotropic filtering, MSAA, and appropriate final-image
   anti-aliasing.
4. **Lighting and materials:** directional, point, and spot lights; adjustable
   dynamic shadows; a PBR or simplified physically based material model tuned
   to the game's painted plastic and domestic surfaces; and normal mapping when
   suitable authored or separately prepared material data exists.
5. **HDR and atmosphere:** internal HDR rendering, tone mapping, adjustable
   exposure, controlled bloom, fog, atmospheric effects, and ambient occlusion
   when the performance budget permits it.
6. **Effects and polish:** scalable high-quality particles, smoke, fire,
   explosions, projectile trails, and individually switchable post-processing.
7. **Performance closure:** platform/device profiling, memory budgets, quality
   defaults, dynamic render-scale policy where useful, and screenshot/device
   acceptance.

Effects are adopted only when they improve readability or the intended
model-kit aesthetic. Feature count alone is not a goal.

## Performance targets

| Product/device | Target |
|---|---|
| iPhone 17 Pro Max | 60 FPS at native resolution or a controlled dynamic render scale |
| iPhone SE (3rd generation) | Stable 30 FPS minimum; target 60 FPS with an appropriate quality profile |
| Windows x64 | 60 FPS at 1080p and 1440p on reasonable contemporary hardware; scalable settings for 4K |

The exact device-specific defaults require measurements. Dynamic render scale
may protect frame time, but it must be visible in diagnostics, bounded by the
selected policy, independent of UI resolution, and disabled when validating
the 100% native-resolution acceptance case.

## Validation contract

Portable tests must cover projection, viewport selection, aspect-ratio
conversion, safe-area layout, UI scale, and input-coordinate mapping at least
for:

- 4:3;
- 16:10;
- 16:9;
- 19.5:9;
- 21:9; and
- 32:9.

Every major rendering stage must:

1. build and run the Windows and iOS validation gates;
2. record the output extent, 3D render extent, and render scale;
3. capture the same representative scene at 1080p, 1440p, 4K, and ultrawide;
4. compare the original reference, `Classic`, and `Enhanced` outputs where
   those modes exist; and
5. present the material comparison captures in the active project work chat;
   and
6. update the architecture, roadmap, status, and acceptance evidence.

A developer overlay must be able to display:

- output/backbuffer resolution;
- 3D render resolution and render scale;
- FPS and CPU/GPU frame time;
- draw-call and triangle counts;
- active light count; and
- estimated or backend-reported GPU memory usage, clearly labelled according to
  the accuracy available on each platform.

Screenshots and captures made from owner-provided content are private derived
artifacts. They remain outside Git and public CI even when their aggregate
results are documented.

## Acceptance criteria

This decision is satisfied only when all of the following are true:

1. At 3840x2160 output and 100% render scale, the 3D render target is verified
   as 3840x2160 and the presented image is not sourced from an enlarged
   historical framebuffer.
2. Standard widescreen is Hor+ and preserves the reference vertical FOV; the
   optional 4:3 comparison mode does not stretch.
3. UI/text and input coordinates remain correct when output resolution, aspect
   ratio, safe area, and 3D render scale change independently.
4. `Classic` and `Enhanced` are separately selectable and do not alter
   deterministic simulation results.
5. Quality profiles and individual costly effects can scale to the stated
   Windows and iPhone performance targets.
6. The required math, platform-build, screenshot, and diagnostic evidence is
   recorded for each implemented stage.

## Alternatives considered

### Upscale the recovered 640x480 or another logical framebuffer

Rejected. It cannot meet the native-resolution acceptance criterion, produces
inferior geometry and texture sampling, couples legacy projection to output
pixels, and wastes the selected native graphics APIs.

### Stretch or crop the original 4:3 view

Rejected as the default. Stretching distorts geometry; vertical cropping loses
gameplay information. Original 4:3 remains a comparison mode.

### Recreate DirectX 7 before adding modern presentation

Rejected. Observable legacy states are evidence to translate into the portable
command model; the obsolete API is not an architectural dependency.

### Use one platform renderer abstraction for both products

Rejected for the current targets. ADR-0008 already selects native D3D11 and
Metal backends, which preserve platform diagnostics and the iOS simulator build
path while sharing the higher-level rendering contract.

## Consequences and trade-offs

- Native resolution and Hor+ become product requirements, not optional polish.
- Modern lighting and post-processing remain ordered enhancements and do not
  block continued gameplay, format, or simulation reconstruction.
- The temporary parity-first 640x480 viewport must be isolated as reference
  camera evidence and replaced by an explicit resolution/presentation model.
- Both HLSL and Metal shader paths must implement compatible material and pass
  semantics, with backend-specific tests and captures.
- Higher resolutions and effects increase GPU time and memory use, requiring
  explicit budgets, scalable settings, telemetry, and device measurements.
- `Classic` requires high-resolution correctness even when enhancements are
  disabled; it cannot be implemented as an upscale shortcut.

## Action items

- Introduce typed output, 3D render, viewport, safe-area, UI, and render-scale
  descriptions in the portable renderer.
- Add portable Hor+, FOV, viewport, UI-scale, safe-area, and input-coordinate
  tests for the required aspect ratios.
- Add native backbuffer/render-target extent verification and the diagnostic
  overlay on D3D11 and Metal.
- Implement the ordered image-quality stages above, maintaining separate
  `Classic` and `Enhanced` screenshot baselines.
- Keep all original and converted assets and owner-derived captures outside
  Git, public CI, caches, and release artifacts.
