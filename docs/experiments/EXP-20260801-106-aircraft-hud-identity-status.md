# EXP-20260801-106: AirCraft HUD identity and status cluster

**Date:** 2026-08-01

**Evidence ID:** `EV-20260801-005`

**Decision:** GO for the isolated presentation plan, authenticated dynamic
aircraft-icon and fixed team-badge ownership, native-output packet, and dormant
D3D11/Metal consumers. NO-GO for ordinary-frame publication until aircraft
type identity, displayed health, team, technology level, and the shared HUD
clock are supplied by verified live producers.

## Question

Can the final contiguous cluster of the AirCraft HUD function be reconstructed
as a bounded portable contract without hard-coding the aircraft catalog or
presenting static validation values as live gameplay state?

## Static method

Ghidra 12.1.2 is the primary source for the hash-verified `AirCraft.type` copy
with SHA-256
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.
The enclosing function is AirCraft RVA `0x00006B00`, exact VA range
`[0x10006B00,0x10007321)`, 2,081 bytes. Rizin 0.9.1 plus rz-ghidra
independently confirm the final basic blocks, calls, field accesses, constants,
and instruction order after the exact entry and boundary are supplied.

The controlled x32dbg attempt did not reach this code: the original renderer
stalled before loading `AirCraft.type`. It produced zero protocol hits, no
memory or trace artifacts, and no dynamic claim. Raw disassembly,
decompilation, archive payloads, decoded images, analysis databases, and host
paths remain ignored local evidence.

## Recovered draw contract

The cluster follows both weapon panels and preserves this order:

1. Search the type-owned `Ac*.gti` catalog by a full, case-insensitive match
   between the aircraft type name and the archive-derived filename stem. If a
   texture matches, draw its complete `64x64` image at `(50, top + 11)`.
2. Convert `(displayedHealth * 100 / maximumHealth) + 0.5` through the native
   external `_ftol`, advance rolling state `AirCraft+0x538`, and draw four
   digits at `(57, top + 78)`.
3. Call `NfActor::GetTeamId`. Team ID exactly `1` selects `techstar`; every
   other returned ID selects `techcross`. Draw the selected complete `64x64`
   image at `(screenWidth/2 - 32, screenHeight - 72)`.
4. Convert the signed technology-level field `AirCraft+0x418` through the
   native int32-to-binary32-to-`_ftol` path, advance rolling state
   `AirCraft+0x53C`, and draw four digits at
   `(screenWidth/2 - 16, screenHeight - 32)`.

The common left-side anchor remains:

```text
top = screenWidth < 640 ? 6 : screenHeight - 136
```

After this cluster, the enclosing HUD function resets its accumulated elapsed
field at `AirCraft+0x54C`. That reset and the clock producer are not owned by
this isolated presentation plan.

The technology interpretation is stronger than a field-name guess. The
console command `give_techlevel 0..4` emits event `0xDA`, whose AirCraft event
branch writes `+0x418`; event `0xDB` increments the same field. The live event
producer and bounds policy remain separate work.

## Portable and authenticated boundary

`LegacyAircraftHudIdentityStatusPlan` is an allocation-free command list with
a fixed maximum of ten draws: one optional aircraft icon, four optional health
digits, one required team badge, and four optional technology digits. It
preserves the enclosing HUD gates and advances both rolling states atomically.
The boundary accepts already-quantized signed values because the original
process-wide x87 rounding state was not captured dynamically. Missing digit
atlas ownership suppresses digit draws but does not suppress the recovered
state advancement.

`LegacyAircraftHudIdentityStatusTextureSet` resolves `techstar.gti` and
`techcross.gti` from the manifest-selected localization archive and discovers
the authenticated source archive's in-game HUD `Ac*.gti` entries dynamically.
No aircraft name is hard-coded. All accepted assets are selected format 8 at
exactly `64x64`; current read-only owner-local inventory contains fourteen
aircraft-icon entries, but the implementation depends only on the bounded
archive catalog. The full filename stem is retained as the lookup key; unlike
weapon icons, no prefix is removed.

The loader bounds catalog count and key length, source footprints,
decoded/upload/resident bytes, dimensions, mip counts, callbacks, and
cancellation. It revalidates revision and opaque transaction identity before
publication. Missing, ambiguous, malformed, duplicate, oversized, cancelled,
callback, allocation, or identity failures publish no partial set.

`LegacyAircraftHudIdentityStatusSubmission` binds the plan to that exact
identity/status owner and the independently authenticated rolling-digit owner.
It maps only the proven logical `640x480` UI domain through
`NativeRenderLayout`, preserving independent UI scale, full or fractional UVs,
white tint, source-alpha blending, linear clamp sampling, ALWAYS/write depth,
and native command order. It never changes the physical 3D render target.

## Native backend staging

Windows and iOS load the complete identity/status set inside their authenticated
mission transactions. D3D11 creates and validates every SRV before atomic
publication; Metal places every texture in the preflight, heap-plan,
allocation, snapshot, and GPU-budget transaction. Both backends retain the
strong CPU owner and contain fail-closed consumers of the same portable packet.

The ordinary frame deliberately supplies no identity/status packet. The code
therefore does not invent an aircraft name, health percentage, team,
technology level, clock, or visibility. No screenshot is an acceptance
artifact for this dormant slice.

## Synthetic validation

Public tests use only synthetic AFPACK, UDSP, and GTI bytes. They cover gate
precedence, the `639/640` anchor split, exact `640x480` command order and
geometry, team selection, optional icon and digit layers, atomic rolling-state
publication, allocation-free planning, dynamic catalog discovery, full-key
case-insensitive lookup, archive ownership, dense IDs, exact format and
dimensions, catalog and memory budgets, duplicate keys and sources,
cancellation, callbacks, moved sessions, native-output/UI-scale mapping,
fractional digit UVs, forged identities, command reordering, and atomic
rejection.

A clean portable CMake/Ninja compile builds the complete portable graph and
all 138 CTests pass. A clean MSVC 19.51 HostX64/Ninja build compiles all 737
Windows product steps and all 150 native CTests pass, including D3D11 and both
product smokes. Hosted iPhoneOS and iPhoneSimulator compilation remains the
Objective-C++/Metal publication gate for this change.

## Remaining gates

- Publish the exact aircraft type identity, team ID, displayed health,
  maximum health, technology level, and accumulated HUD elapsed value without
  validation substitutes.
- Decide the portable numeric policy at both external `_ftol` boundaries or
  capture live x87 control state if bit identity becomes an acceptance goal.
- Preserve the enclosing function's one-time clock reset only when the full
  HUD event is composed.
- Validate the composed live HUD at all required aspect ratios, safe areas,
  and physical iPhone sizes.
- Perform physical-device Metal visual and timing acceptance.
