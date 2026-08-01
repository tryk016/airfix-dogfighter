# Aircraft HUD

## Scope and evidence

`EV-20260801-001` covers the first complete renderer-neutral subsection of the
aircraft HUD. Ghidra 12.1.2 is the primary decompiler; Rizin 0.9.1 independently
confirms the function boundary, calls, and instruction order in the
hash-verified `AirCraft.type` copy with SHA-256
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.

The AirCraft vtable slot `+0xBC` resolves to RVA `0x00006B00`, exact range
`[0x10006B00,0x10007321)`, 2,081 bytes. The complete function also draws
instrument, weapon, aircraft, team, and numeric elements. Only the internally
complete armour/health-gauge subsection is implemented here; this is not a
claim that the whole HUD or mission-status UI is reconstructed.

See
[EXP-20260801-098](../../experiments/EXP-20260801-098-aircraft-health-gauge-plan.md)
and
[EXP-20260801-099](../../experiments/EXP-20260801-099-authenticated-health-gauge-textures.md).
The verified screen-call state, native-output submission, and private Windows
validation pass are in
[EXP-20260801-100](../../experiments/EXP-20260801-100-aircraft-health-gauge-submission.md).

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
Metal retains the same authenticated resources and portable packet contract,
but its visible submission remains a later backend slice.

The bounded dynamic follow-up for producer/consumer order is documented in
[Controlled aircraft health-gauge capture](../../toolchain/AIRCRAFT-HEALTH-GAUGE-CAPTURE.md).

## Remaining HUD work

- Recover and classify the two clock/instrument calls and their live values.
- Reconstruct four-digit rolling-number state and exact glyph-atlas binding.
- Complete primary/selected-secondary icon, ammunition, and status-bar plans.
- Recover aircraft/team indicators and the relationship to mission status.
- Connect the verified smoothed-health producer to ordinary render-event
  publication without substituting capture-only values.
- Implement Metal consumption of the existing authenticated common packet.
- Validate the composed HUD at the required aspect ratios and safe areas.
