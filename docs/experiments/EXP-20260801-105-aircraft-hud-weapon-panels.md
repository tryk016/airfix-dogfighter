# EXP-20260801-105: AirCraft HUD weapon panels

**Date:** 2026-08-01

**Evidence ID:** `EV-20260801-004`

**Decision:** GO for the isolated two-panel presentation plan, authenticated
texture ownership, native-output packet, and dormant D3D11/Metal consumers.
NO-GO for ordinary-frame publication until the primary and selected-secondary
weapon owners, ammunition, and status producers are connected without
capture-only substitutes.

## Question

Can the complete primary/selected-secondary weapon-panel subsection be
reconstructed from static evidence as a bounded C++20 contract and staged on
both native backends without hard-coding the private weapon catalog or
inventing live gameplay values?

## Static method

Ghidra 12.1.2 is the primary source for the hash-verified `AirCraft.type` copy
with SHA-256
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.
The enclosing HUD function is RVA `0x00006B00`, exact VA range
`[0x10006B00,0x10007321)`, 2,081 bytes. Rizin 0.9.1 plus rz-ghidra
independently confirm the relevant basic blocks, calls, constants, and
instruction order after the exact entry and boundary are supplied.

Raw disassembly, decompilation, reports, archive payloads, decoded images,
analysis databases, and host paths remain ignored local evidence. No original
or derived game asset enters Git or CI.

## Recovered draw contract

The two optional format-8 `64x64` panel backgrounds are drawn first. At the
reference `640x480` UI extent their origins are:

```text
primary   = (screenWidth/2 - 97, screenHeight - 72)
secondary = (screenWidth/2 + 33, screenHeight - 72)
```

The primary weapon slot is then processed completely before the selected
secondary slot. For each present weapon, native order is:

1. advance and draw its four rolling ammunition digits at panel offset
   `(16,40)`;
2. compare the weapon type name after exactly two leading bytes with the
   suffixes derived from the `weapon_*.gti` catalog;
3. draw the matching optional `64x32` icon at offset `(6,6)`; and
4. after a catalog match, draw a `52x30` status fill at the same offset.

The status value reaches this block after an external MSVC `_ftol`
conversion. The portable boundary therefore accepts the already-quantized
signed integer. It clamps the value to `[0,4]` and preserves the exact
AARRGGBB palette:

```text
0 -> 0xBF269A1A
1 -> 0xBF94C61C
2 -> 0xBFEDC610
3 -> 0xBFE26D00
4 -> 0xBFBD1700
```

The native solid fill selects `GT_ALPHA` mode 2. Static state recovery maps
that mode to RGB source `ZERO`, destination `SRC_COLOR`, and alpha source
`ZERO`, destination `SRC_ALPHA`; the reconstruction retains this destination-
multiply operation rather than replacing it with ordinary source-alpha
blending. All textured panel calls retain source-alpha blending and
ALWAYS/write depth.

## Portable and authenticated boundary

`LegacyAircraftHudWeaponPanelsPlan` is an allocation-free fixed-capacity
14-command value type. It retains the enclosing HUD gates, both background
calls before weapon content, primary-before-secondary slot order, independent
missing layers, atomic rolling-digit state advancement, exact geometry, and
status palette. Failed planning publishes neither a command prefix nor
partially advanced digit state.

`LegacyAircraftHudWeaponPanelTextureSet` opens both fixed backgrounds from the
manifest-selected localization archive, then enumerates the authenticated
source archive's in-game HUD directory for `weapon_*.gti`. Individual weapon
names are never hard-coded. The two backgrounds must be selected format 8 at
`64x64`; every discovered icon must be selected format 8 at `64x32`. The
loader bounds catalog count, key length, per-file and aggregate source
footprint, decoded/upload/resident bytes, dimensions, and mip counts. It also
revalidates the unchanged revision and opaque transaction identity around
callbacks and publication. Missing, ambiguous, malformed, duplicated,
oversized, cancelled, callback, allocation, or identity failures publish no
partial catalog.

The allocation-free lookup strips exactly two leading bytes from the supplied
weapon type name and compares the remainder ASCII-case-insensitively with the
archive-derived suffix. `LegacyAircraftHudWeaponPanelsSubmission` then binds
the plan to that exact weapon-texture owner and the independently authenticated
rolling-digit atlas owner. Both must have the same revision and transaction
identity. The submission maps only the proven logical `640x480` UI domain
through `NativeRenderLayout`; it does not constrain or rescale the 3D target.

## Native backend staging

Windows and iOS include the complete dynamic panel set in their all-or-nothing
mission resource transactions and GPU budget accounting. D3D11 owns a
dedicated destination-multiply blend state and HLSL solid-overlay entry point.
Metal owns an equivalent immutable pipeline using source-colour destination
multiplication. Both contain a fail-closed consumer of the same portable
packet and validate owner identity, dense IDs, texture resources, geometry,
blend mode, sampler policy, and output extent before drawing.

The ordinary frame deliberately supplies no panel packet. The code therefore
does not present static ammunition, status, selected weapon, or visibility as
live state. No screenshot is an acceptance artifact for this dormant slice.

## Synthetic validation

Public tests use only synthetic AFPACK, UDSP, and GTI bytes. They cover gate
precedence, exact `640x480` geometry and command order, status clamping,
independent optional layers, missing digit-atlas state advancement,
allocation-free planning, dynamic catalog discovery, case-insensitive lookup,
archive ownership, dense IDs, exact format/dimensions, catalog and memory
budgets, duplicates, cancellation, callback and moved-session failures,
native-output/UI-scale mapping, blend and UV policy, forged identity, command
reordering, and atomic rejection.

A clean portable CMake/Ninja build passes all 136 CTests. A complete Windows
x64 clang-cl/Ninja product build compiles the D3D11 renderer, runtime HLSL,
and executable and passes all 148 native CTests, including the renderer and
both product smokes. Hosted iPhoneOS and iPhoneSimulator compilation remains
the Objective-C++/Metal publication gate for this change.

## Remaining gates

- Connect verified primary and selected-secondary owner publication in the
  recovered event order.
- Trace the two live ammunition sources and the status expression through the
  external `_ftol` boundary; capture process x87 control only if bit identity
  is required for this presentation value.
- Publish the shared HUD elapsed clock without batch, replay, or manufactured
  interpolation.
- Validate the composed live HUD at the required aspect ratios, safe areas,
  and physical iPhone sizes.
- Perform physical-device Metal visual and timing acceptance.
