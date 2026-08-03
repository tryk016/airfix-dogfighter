# Aircraft HUD

## Scope and evidence

`EV-20260801-001` covers the first complete renderer-neutral subsection of the
aircraft HUD, `EV-20260801-002` covers its reusable four-digit rolling-number
helper, `EV-20260801-003` covers the complete pair of analog instruments, and
`EV-20260801-004` covers both weapon panels. `EV-20260801-005` covers the final
aircraft-icon, health-number, team-badge, and technology-number cluster.
`EV-20260801-006` covers the constructor, refresh producer, shared snapshot,
four reset-skipping gates, and one-time reset of the HUD elapsed clock.
`EV-20260801-007` covers the two four-digit readouts beneath the analog
instruments, including their input arithmetic, order, anchors, tint, and use
of that shared snapshot. `EV-20260801-008` covers the complete authenticated
HUD render-event transaction, shared clock commit/rollback, six state updates,
and dormant native-order D3D11/Metal consumers. The later Windows full-HUD
capture adds no semantic evidence; it only validates that already accepted
transaction through the real D3D11 backend.
Ghidra 12.1.2 is the primary decompiler; Rizin 0.9.1
independently confirms the function boundaries, calls, and instruction order
in the hash-verified `AirCraft.type` copy with SHA-256
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.

The AirCraft vtable slot `+0xBC` resolves to RVA `0x00006B00`, exact range
`[0x10006B00,0x10007321)`, 2,081 bytes. All recovered draw subsections inside
that exact function now compose into one authenticated event in native order.
This closes the static render-stage composition; it is not a claim that live
AirCraft producers, mission-status UI outside this function, or device visual
parity are complete.

See
[EXP-20260801-098](../../experiments/EXP-20260801-098-aircraft-health-gauge-plan.md)
and
[EXP-20260801-099](../../experiments/EXP-20260801-099-authenticated-health-gauge-textures.md).
The verified screen-call state, native-output submission, and private Windows
validation pass are in
[EXP-20260801-100](../../experiments/EXP-20260801-100-aircraft-health-gauge-submission.md).
The matching fail-closed Metal encoder is in
[EXP-20260801-101](../../experiments/EXP-20260801-101-metal-aircraft-health-gauge-encoder.md).
The rolling-number reconstruction is in
[EXP-20260801-102](../../experiments/EXP-20260801-102-aircraft-hud-rolling-digits.md).
The analog instruments are in
[EXP-20260801-104](../../experiments/EXP-20260801-104-aircraft-hud-analog-instruments.md).
The weapon-panel reconstruction is in
[EXP-20260801-105](../../experiments/EXP-20260801-105-aircraft-hud-weapon-panels.md).
The final identity/status cluster is in
[EXP-20260801-106](../../experiments/EXP-20260801-106-aircraft-hud-identity-status.md).
The shared elapsed-clock lifecycle is in
[EXP-20260801-107](../../experiments/EXP-20260801-107-aircraft-hud-elapsed-clock.md).
The two instrument readouts are in
[EXP-20260801-108](../../experiments/EXP-20260801-108-aircraft-hud-instrument-readouts.md).
The complete render-event transaction is in
[EXP-20260801-109](../../experiments/EXP-20260801-109-aircraft-hud-render-event.md).
The complete Windows visual-validation capture is in
[EXP-20260803-110](../../experiments/EXP-20260803-110-full-hud-validation-capture.md).

## Enclosing gates

The native HUD stage returns in this order when:

1. an application window/overlay is active;
2. the aircraft has no attached camera;
3. the type-level HUD byte is disabled; or
4. a repeated attached-camera read fails after the screen layout was computed.

The portable input carries both camera observations. This avoids silently
collapsing the repeated native read into an assumed invariant. Rejected plans
publish no partial commands.

## Recovered armour/health gauge

The vehicle-type HUD loader identifies two separate texture roles: an
`armour_meter` background and an `armour` foreground. Either reference may be
absent independently. The render sequence is exact:

1. draw the optional meter background;
2. draw every opaque-black damage-mask quad;
3. draw the optional armour foreground.

Both textures use logical origin `(8, top)`. The recovered vertical anchor is:

```text
top = screenWidth < 640 ? 6 : screenHeight - 136
```

The mask is an annular fan centred at `(72, top + 64)`, with outer radius `62`
and inner radius `44`. It starts at `-pi/4`. The displayed sweep is:

```text
damageSweep =
    (1 - ((smoothedDisplayedHealth * 100) / maximumHealth) * 0.01) * pi
```

The native constants are binary32 `0.1` (`0x3DCCCCCD`) and binary32 pi
(`0x40490FDB`); the initial `-pi/4` is loaded as the exact little-endian
binary64 value `-0.78539818525314331`. The cursor is spilled to binary32 after
each `0.1` step. Each quad retains native vertex order: previous outer, next
outer, next inner, previous inner. A final quad is always submitted, including
the degenerate full-health case. Valid health in `[0,max]` therefore needs at
most 32 mask quads.

The health source is the already-smoothed AirCraft field at `+0x544`; the
refresh path low-pass filters raw actor health into it. The plan deliberately
accepts displayed health rather than owning that gameplay update.

## Portable boundary

`LegacyAircraftHealthGaugePlan` is a bounded, allocation-free C++20 command
list in legacy UI coordinates. It does not bind private textures, issue GPU
work, choose a 3D render resolution, or mutate simulation. The source screen
extent is retained so that later stages can fail closed instead of silently
reinterpreting geometry recovered for another UI domain.

Non-finite health, non-positive maximum health, values outside `[0,max]`, and
zero screen extents fail closed. The native x87 trigonometric path is not
claimed bit-identical on every modern platform while the live process control
word remains uncaptured. The reconstruction preserves constants, operation
order, binary32 cursor steps, topology, and visual coordinates; small libm
differences are presentation-only and cannot feed gameplay or physics.

Synthetic tests cover both camera samples and gate precedence, `639`/`640`
width anchoring, full/half/empty health, optional texture layers, exact command
capacity, vertex orientation, invalid values, bounds-safe access, and
steady-state allocation freedom.

## Authenticated texture ownership

The two recovered texture roles do not come from the same nested archive:

- `Graphics\Ingame\HUD\armour_meter.gti` is resolved from `Resource.up`;
- `Graphics\Ingame\HUD\armour.gti` is resolved from the AFPACK localization
  archive selected by its strict manifest.

Recovery and direct session opening now retain both UDSP views over the same
seekable AFPACK handle and transaction identity. No archive is reopened by a
host path. `LegacyAircraftHealthGaugeTextureSet` requires both unique entries,
format 8, `128x128`, matching upload plans, bounded source/decode/upload/
resident byte totals, and an unchanged content revision. Any missing,
ambiguous, malformed, oversized, cancelled, or mixed-session input rejects the
entire two-texture set.

The texture IDs are dense only inside this gauge-owned namespace, so values
`0` and `1` cannot alias mission textures or weapon-crosshair IDs. Windows and
iOS load the set as part of their private mission transaction, allocate every
D3D11/Metal texture and required mip level before publication, retain the
authenticated CPU ownership token, and include the resources in existing GPU
budget accounting.

## Screen calls and native submission

The two texture calls at AirCraft RVAs `0x00006F8D` and `0x00007196` resolve
through `GtScreen::Blit` at Cc RVA `0x00044CE0` to the concrete full-colour
path at `0x00044EA0`. Because both textures are format 8, `GtImage::IsAlpha`
selects blend mode 3: source alpha / inverse source alpha. The mask calls at
AirCraft RVAs `0x000070CC` and `0x00007170` resolve through `GtScreen::Fill` at
Cc RVA `0x000457C0`; selector zero disables blending and uses the supplied
opaque black. Both paths select screen state 2, already recovered as depth
compare ALWAYS with writes enabled.

`LegacyAircraftHealthGaugeSubmission` joins the plan to one exact
authenticated texture-set identity and maps all four-point commands through
`NativeRenderLayout` into physical output pixels. It accepts only the proven
640x480 UI design domain. This does not constrain the 3D render target: at
100% render scale a 3840x2160 output still renders the scene at 3840x2160.

Independent UI scale is applied around the logical bottom-left anchor, while
the root 640x480 design canvas remains aspect-fitted inside the safe area. The
submission preserves background -> mask -> foreground order, full UVs, white
texture tint, opaque-black mask, blend/depth state, and bounded command count.
Backends revalidate the transaction identity immediately before GPU lookup.

Windows consumes this packet in a dedicated D3D11 shader path. The private
`--capture-health-gauge-validation-frame` mode has produced a real 1920x1080
half-health frame using the installed authenticated mission, aircraft,
textures, mask, and diagnostics overlay. Its fixed 50% input exists only in
the capture harness. Ordinary gameplay does not fabricate a live health value.
Metal retains the same authenticated resources and portable packet contract.
Its 96-byte four-point ABI, alpha and opaque pipelines, linear-clamp sampling,
ALWAYS/write depth, output-target validation, and all-or-nothing command
encoding are implemented. The ordinary `MTKView` callback does not call the
encoder until a verified live producer exists, and physical-device visual
acceptance remains pending.

The bounded dynamic follow-up for producer/consumer order is documented in
[Controlled aircraft health-gauge capture](../../toolchain/AIRCRAFT-HEALTH-GAUGE-CAPTURE.md).

## Recovered four-digit rolling numbers

The HUD owns six independent helper states, initialized from decimal values
`0`, `100`, `0`, `0`, `100`, and `0`. The initializer at AirCraft RVA
`0x00009210` formats the signed value with native `%04d`, consumes exactly the
first four characters, and stores `character - '0'` as four binary32
positions. Consequently values wider than four characters are truncated and a
leading minus sign becomes position `-3`; the portable state deliberately
preserves both behaviours instead of silently normalizing them.

The update helper at RVA `0x00009280` receives a float that the original first
converts through MSVC `_ftol`. The portable boundary accepts the resulting
signed 32-bit integer because the live process-wide x87 rounding policy is not
yet dynamically accepted. For each digit it then:

1. selects the cyclic alternate target only when the absolute distance is
   strictly greater than `5`;
2. caches `float(pow(0.0005, elapsedSeconds))` when the shared accumulated HUD
   elapsed value changes;
3. applies `factor * current + (1 - factor) * target` in the recovered order;
4. wraps once into `[0,10)`; and
5. resets any still-invalid or non-finite position to exact positive zero.

This retained fractional position is the animation. The draw helper at RVA
`0x000093A0` emits one call per valid slot through `GtScreen::Blit` at Cc RVA
`0x00044D90`. Destination slot `i` is `(originX + i*8, originY + 1, 7, 9)`;
its source window is `(0, position*11 + 1, 7, position*11 + 10)`. A fractional
position therefore slides a 7x9 window vertically between glyphs. Invalid
slots are skipped independently and the supplied ARGB tint is unchanged.

The type-level HUD loader registers the `digits` role below the in-game HUD
texture root. Read-only owner-local archive inventory confirms that this GTI is
format 4/8 capable, exactly `16x128`, and has one mip. Those dimensions and
the recovered screen rectangles authenticate a vertical ten-digit atlas
contract without publishing or copying the private image.

`LegacyAircraftHudRollingDigitsState` and
`LegacyAircraftHudRollingDigitsPlan` are bounded, allocation-free C++20 value
types. The exact atlas now loads through the authenticated session as a
dedicated one-texture HUD-local set. A fixed-capacity, identity-bound
submission maps the recovered rectangles and fractional atlas windows into
native output pixels, retaining caller tint, source-alpha blending,
linear-clamp sampling and ALWAYS/write depth. D3D11 and Metal stage the atlas
inside their atomic mission transactions and own dormant fail-closed
consumers. Ordinary frames do not call those consumers until the live values,
clock and enclosing HUD gates are verified.

Synthetic tests cover native formatting edge cases, factor caching, shortest
cyclic motion, the exact-distance-five branch, wrap/reset behaviour,
authenticated format/dimension enforcement, fractional atlas coordinates,
native-output/UI-scale mapping, provenance rejection, per-slot suppression,
fail-closed invalid inputs, bounds-safe command access, and steady-state
allocation freedom.

The PC53-compatible double staging is a documented presentation policy, not a
claim of live x87 bit identity. The six live value producers remain explicit
later slices; the enclosing clock lifecycle is now isolated but intentionally
not wired to a live scheduler or ordinary render event.

## Recovered shared elapsed clock

The constructor stores exact positive zero at `AirCraft+0x54C`. Every completed
post-collision refresh performs `FLD delta`, `FADD +0x54C`, and `FSTP +0x54C`.
Five static HUD call sites load the field; the weapon-panel site executes for
two slots, so six rolling-number states consume one accumulated snapshot. The
full eligible HUD stage stores positive zero once at VA `0x1000730F`.

All four enclosing early exits occur before that reset. A rejected active-
window, entry-camera, type-HUD, or repeated-camera gate retains elapsed time.
After all gates pass, missing optional elements do not suppress the final
reset.

`LegacyAircraftHudElapsedClockState` models this as an allocation-free,
single-thread-confined `advance -> begin -> commit/abort` lifecycle. A begin
publishes one immutable snapshot for the six consumers. Commit clears once;
abort, invalid input, stale token, or rejected gate leaves accumulated time
unchanged. It remains dormant: no application, renderer, scheduler, or live
counter producer calls it.

## Recovered analog instruments

AirCraft RVA `0x00007330`, exact range `[0x10007330,0x1000742F)`, draws one
optional `64x64` face and one optional rotated indicator. The HUD calls it for
the right centre `(width/2+138,height-40)` before the left centre
`(width/2-138,height-40)`. The right value is the already-smoothed field at
`+0x540`; the left value is the ratio `+0x400/+0x3F8`. The latter remains
described as a remaining/initial ratio rather than receiving an unproved
stronger gameplay label. Right tint is white; left tint comes from `+0x550`.

The shared indicator consumes `(0,0)-(7,30)` from the exact format-8 `16x32`
asset and rotates across `(0.5-value)*3.9269909858703613` radians around the
recovered `3.5,23` pivot. Both format-8 `64x64` faces come from the selected
localization archive; the indicator comes from the source archive. One bounded
authenticated owner and one identity-bound native-output packet retain this
cross-archive provenance, right-before-left order, UVs, tint, source-alpha
blend, linear clamp and ALWAYS/write depth. D3D11 and Metal stage all three
resources atomically and own dormant fail-closed arbitrary-quad consumers.

The portable planner accepts the already-produced presentation values and is
allocation-free. It does not update smoothing, label the ratio as fuel,
manufacture live values, or constrain native 3D resolution. Ordinary frames
remain disconnected until a verified runtime producer publishes the packet.

## Recovered instrument readouts

Two rolling-number states immediately follow the analog-instrument calls.
The right state at `AirCraft+0x528` consumes the length of a three-component
binary32 vector returned through virtual slot `+0x54`, multiplied by exact
binary32 `100`. The left state at `AirCraft+0x52C` consumes
`(+0x400/+0x3F8)*100+0.5`. The second expression is bounded as a
remaining/initial percentage; the first vector receives no stronger gameplay
label until its producer is independently established.

Both values cross the external MSVC `_ftol` boundary before the rolling helper
formats them. The portable plan therefore accepts already-quantized signed
int32 values and does not guess live x87 halfway behaviour. Right update/draw
finishes before left update/draw starts, and both receive the exact same
elapsed-clock snapshot.

Their rolling-number origins are relative to the analog centres:

```text
right = (screenWidth/2 + 138 - 15, screenHeight - 40 + 10)
left  = (screenWidth/2 - 138 - 15, screenHeight - 40 + 10)
```

At the recovered `640x480` logical extent, the draw helper's one-pixel inset
places the first glyphs at `(443,451)` and `(167,451)`. Right tint is white;
left tint comes from `AirCraft+0x550`. Missing state pointers suppress only
their own update/draw.

`LegacyAircraftHudInstrumentReadoutsPlan` advances both retained states
atomically, preserves all enclosing gates and advances state even when an
optional atlas cannot draw. Its paired authenticated submission reuses the
existing digit-atlas packet, native-output/UI-scale mapping, and dormant
D3D11/Metal digit consumers. No backend shader or resource is duplicated, and
ordinary frames still publish no fabricated vector, ratio, tint, clock, or
quantized value.

## Recovered weapon panels

The complete primary/selected-secondary subsection draws both optional panel
backgrounds before processing primary and then secondary weapon content. At
the reference `640x480` UI extent, the `64x64` background origins are
`(223,408)` and `(353,408)`. Each present slot advances and draws its four
ammunition digits at offset `(16,40)`, then resolves an optional `64x32`
weapon icon at offset `(6,6)`, followed by a `52x30` status fill after a
catalog match.

The icon catalog is not a hard-coded list. The authenticated source archive is
enumerated for in-game HUD `weapon_*.gti` entries, and the archive-derived
suffix is compared case-insensitively with the weapon type name after exactly
two leading bytes are removed. The fixed `weapon1.gti` and `weapon2.gti`
backgrounds come from the selected localization archive. All are format 8;
backgrounds must be `64x64` and dynamic icons `64x32`. One bounded loader
publishes the entire set only after exact provenance, dimension, mip, budget,
revision, and transaction checks pass.

The status value is accepted after the unresolved external `_ftol` conversion,
clamped to `[0,4]`, and mapped to exact AARRGGBB values `0xBF269A1A`,
`0xBF94C61C`, `0xBFEDC610`, `0xBFE26D00`, and `0xBFBD1700`. Native
`GT_ALPHA` mode 2 is retained as destination multiplication by source colour;
it is not treated as ordinary source-alpha blending. Texture layers keep
source-alpha blending, linear clamp, and ALWAYS/write depth.

The allocation-free 14-command plan advances both digit states atomically and
retains background-before-slots and primary-before-secondary order. Its
identity-bound native-output submission maps the logical UI through
`NativeRenderLayout` independently of the 3D target. D3D11 and Metal stage the
dynamic resources in the mission transaction and own dormant fail-closed
consumers. Ordinary frames do not fabricate selected weapons, ammunition,
status, or visibility.

## Recovered aircraft identity and status cluster

The final contiguous cluster of AirCraft RVA `0x00006B00` follows both weapon
panels. It first performs a full, case-insensitive lookup of the aircraft type
name against the archive-derived stems of `Ac*.gti`; no prefix is removed. A
match draws the complete `64x64` icon at `(50, top+11)`. It then advances the
health rolling-number state at `AirCraft+0x538` from the already-quantized
result of `(displayedHealth*100/maximumHealth)+0.5` and draws at
`(57, top+78)`.

`NfActor::GetTeamId` selects `techstar` only for team ID exactly `1`; every
other returned ID selects `techcross`. The complete `64x64` badge is drawn at
`(width/2-32,height-72)`. Finally, the signed field at `AirCraft+0x418`
advances rolling state `+0x53C` and draws at `(width/2-16,height-32)`. Console
event `0xDA` writes that field through `give_techlevel 0..4`, and event `0xDB`
increments it, confirming the technology-level interpretation. Both numeric
paths cross the unresolved native `_ftol` policy, so the portable plan accepts
already-quantized signed values.

One bounded authenticated owner loads the fixed format-8 `64x64` badges from
the selected localization archive and dynamically discovers format-8 `64x64`
aircraft icons in the source archive. Current read-only owner-local inventory
contains fourteen icons, but neither names nor count are hard-coded. Catalog,
key, source/decode/upload/resident, dimension, mip, cancellation, callback,
revision, and transaction limits are enforced before atomic publication.

The allocation-free ten-command plan advances both number states atomically
and retains optional icon/digit behavior, team selection, and exact native
order. Its cross-owner native-output submission preserves full/fractional UVs,
source-alpha blending, linear clamp, ALWAYS/write depth, independent UI scale,
and separation from the native-resolution 3D target. D3D11 and Metal stage all
resources inside the complete mission transaction and own dormant fail-closed
consumers. Ordinary frames do not manufacture aircraft identity, health,
team, technology, clock, or visibility.

## Complete render-event transaction

`LegacyAircraftHudRenderEvent` composes the already verified subsections in
the enclosing function's structural order: analog instruments, paired digital
readouts, both weapon panels, armour/health gauge, then aircraft identity and
status. No subsection is sorted or independently published.

The composer begins one `LegacyAircraftHudElapsedClockState` stage, obtains
one bit-identical elapsed snapshot for all six possible rolling displays, and
injects it into the three paired planners. It builds every plan and every
authenticated native-output submission before publishing either the event or
the six replacement states. A rejected gate, invalid value, mapping failure,
mixed revision/transaction identity, or failed nested packet aborts the stage
and retains the accumulated clock. Only a completely validated event commits
the exact positive-zero clock reset.

D3D11 and Metal now contain matching dormant full-event consumers. Both
prevalidate all five owner sets, GPU resources, transaction identity, command
counts, and nested submissions, then traverse the fixed native order. Metal
requires the caller to discard the command buffer unless the complete expected
count is returned. Ordinary frame callbacks do not call either consumer; no
camera gate, health, instrument, weapon, team, technology, digit, or `_ftol`
result is manufactured.

Windows additionally exposes a private, one-shot full-HUD validation capture.
It selects the first authenticated weapon and aircraft icon catalogue entries,
supplies a fixed representative value set to the existing composer, and draws
the complete event plus a centred authenticated sight. The command forces the
path-free diagnostic overlay and a hidden session window. It neither mutates
the simulation nor makes the ordinary frame callback consume the event.

## Remaining HUD work

- Recover and connect one coherent live producer for all verified camera
  gates, instrument/tint, weapon/ammunition/status, smoothed-health, aircraft
  identity/team/technology, and six digit values without capture-only state.
- Connect the refresh scheduler to the isolated elapsed clock while preserving
  zero, one, or many refreshes per render event.
- Accept a live x87 conversion policy before product code quantizes the float
  inputs that crossed native `_ftol`.
- Validate the composed HUD on both physical iPhones and complete touch-safe-
  area acceptance. Windows native 16:9, 4K, and ultrawide capture validation is
  complete; it does not substitute for live producer wiring.
