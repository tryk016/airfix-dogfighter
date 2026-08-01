# EXP-20260801-100: aircraft health-gauge submission

**Status:** backend-neutral submission and private D3D11 validation implemented;
the dormant Metal encoder follows in EXP-20260801-101

**Evidence ID:** `EV-20260801-001`
**Decision:** GO for authenticated native-output submission and the private
Windows validation pass; NO-GO for invented live health or ordinary HUD
publication

## Question

Can the recovered health-gauge plan and authenticated texture set be joined to
the modern UI layout and a real native backend without guessing old screen
state, changing simulation, or treating the 640x480 UI domain as a render
resolution?

## Independent screen-call verification

Ghidra 12.1.2 was the primary static source. Rizin 0.9.1 independently checked
every function boundary and call chain in the hash-verified `Cc.dll` copy with
SHA-256
`18002a3af1405d932579c6d6888256ce26b8a479735093d33441f10fc27aa8af`.
The original and working copies were not modified.

The AirCraft HUD calls resolve as follows:

| AirCraft RVA | Layer | `GtScreen` slot | Resolved operation |
|---:|---|---:|---|
| `0x00006F8D` | optional meter background | `+0x54` | full-image `Blit` |
| `0x000070CC` | stepped damage mask | `+0x70` | coloured four-point `Fill` |
| `0x00007170` | residual damage mask | `+0x70` | coloured four-point `Fill` |
| `0x00007196` | optional armour foreground | `+0x54` | full-image `Blit` |

`GtScreen::Blit` is exact Cc RVA `0x00044CE0`, range
`[0x10044CE0,0x10044D0B)`, 43 bytes. It forwards the image's full extent through
RVA `0x00044D40`, exact range `[0x10044D40,0x10044D83)`, to the concrete
full-colour path at RVA `0x00044EA0`, exact range
`[0x10044EA0,0x100451D0)`, 816 bytes. The concrete path uses white
`0xFFFFFFFF` vertex colour, depth mode 2, and selects material blend mode 3
when `GtImage::IsAlpha()` is true.

`GtImage::IsAlpha()` is exact RVA `0x00040BD0`, 10 bytes, delegating to exact
RVA `0x00042300`, 34 bytes. Formats `2`, `4`, `6`, and `8` are classified as
alpha. Both authenticated gauge textures are recovered format 8; their state
is therefore source-alpha / inverse-source-alpha blending with depth compare
ALWAYS and depth writes enabled.

`GtScreen::Fill` is exact RVA `0x000457C0`, range
`[0x100457C0,0x100457D4)`, 20 bytes. It forwards selector zero to RVA
`0x000457E0`, exact range `[0x100457E0,0x10045932)`, 338 bytes. That path
expands the four supplied points into two triangles, copies the supplied
`0xFF000000` colour, disables blending, and selects the same depth mode 2.
Consequently the exact composition contract is:

1. alpha-blended background with full UV and white tint;
2. one or more opaque-black mask quads with blending disabled;
3. alpha-blended foreground with full UV and white tint;
4. depth compare ALWAYS and writes enabled for every command.

No alpha-test branch appears in the inspected path. Texture filtering follows
the native global filter option; the port selects linear clamp for this bounded
HUD submission as an explicit modern policy. Full `[0,1]` UVs keep address-mode
edge behaviour irrelevant.

## Portable submission

`LegacyAircraftHealthGaugeSubmission` is a fixed-capacity, value-only C++20
packet. It binds every textured command to the health-gauge-local dense ID,
content revision, and exact authenticated transaction identity. Backends must
call `belongsTo()` against their currently installed immutable texture set
before indexing GPU resources. Invalid plans, mixed sessions, forged IDs,
unknown commands, non-finite geometry, incompatible UI domains, and invalid UI
scale fail atomically.

The recovered source plan now retains its source screen extent. Submission is
accepted only for the proven 640x480 legacy UI domain and a modern layout whose
UI design extent is also 640x480. This is metadata, not a render target. The 3D
scene still renders at the selected native-resolution render extent.

The independent UI-scale policy is applied around the logical bottom-left
anchor:

```text
widgetX = legacyX * uiScalePercent / 100
widgetY = 480 - (480 - legacyY) * uiScalePercent / 100
output  = NativeRenderLayout::outputPointFromUi(widgetX, widgetY)
```

The stable root canvas remains aspect-fitted inside the output safe area. The
widget grows up and right without multiplying the root canvas or changing
camera projection.

## Windows validation path

The D3D11 backend compiles dedicated gauge vertex, textured-fragment, and
solid-fragment shaders. A 96-byte constant packet carries four physical output
points, output extent, and tint. Each recovered command issues two triangles
in original order. Texture commands use source-alpha blending; mask commands
disable blending; all use the existing ALWAYS/write depth state and linear
clamp sampler.

`--capture-health-gauge-validation-frame` is deliberately private. It requires
an installed authenticated mission and gauge set, supplies fixed 50% health
only to the presentation planner, and renders one validation frame. It neither
publishes nor mutates a simulation health value. Ordinary gameplay continues
to issue no gauge submission while the live producer is disconnected.

A local owner-private 1920x1080 Enhanced-profile capture completed through the
real D3D11 backend with scene, aircraft, authenticated textures, 16 mask
commands, foreground, and diagnostics overlay. The BMP remains under ignored
capture storage and is not public source material.

## Validation

Synthetic tests prove:

- authenticated background/mask/foreground order and dedicated texture IDs;
- full UV, white texture tint, opaque-black mask, blend/depth/sampling modes;
- exact 16:9 output mapping at 100% and bottom-left anchoring at 150%;
- rejection of a non-ready plan, invalid texture set, wrong legacy/UI extent,
  out-of-policy or NaN UI scale, unknown command, non-finite point, forged
  role-local ID, and equal revision from another authenticated handle;
- bounds-safe command lookup and all-or-nothing publication.

A clean GCC 15.2/Ninja portable build compiles the complete portable graph and
passes 130/130 CTests. A clean MSVC 19.51 HostX64/Ninja Windows build compiles
the complete application and HLSL and passes 142/142 CTests. The synthetic
public-boundary suite, 688-file repository scan, 12 Rizin exporter tests, nine
runtime-capture validator tests, all three reverse-engineering wrapper suites,
changed-range formatting, and `git diff --check` pass. Hosted platform Actions
remain the publication gate.

## Decision

**GO** for the authenticated backend-neutral packet, native-output UI mapping,
and private D3D11 validation renderer.

**NO-GO** for ordinary gameplay publication until a live aircraft health
producer supplies the already-smoothed field under the correct refresh/render
ordering. Metal has the same authenticated textures and can consume the common
packet through the dormant encoder in EXP-20260801-101; neither experiment
claims an ordinary live gauge draw.
