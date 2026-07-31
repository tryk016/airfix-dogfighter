# EXP-20260731-093: crosshair render-event composition

**Date:** 2026-07-31

**Evidence ID:** `EV-20260731-007`

**Status:** selected-secondary/primary order and crosshair-substage gates
implemented as a value-only C++20 composition plan; live producer deliberately
not connected

**Confidence:** high (`3`) for event `0x06`, the shown gate order, weapon-field
ownership, both virtual calls, and selected-secondary-before-primary order;
medium (`2`) for the descriptive AirCraft HUD label because the complete
2,081-byte slot body has not been semantically reconstructed

## Question

When both native AirCraft weapon slots exist, which crosshair is submitted
first, which render-event gates enclose the calls, and can that confirmed
knowledge be represented without inventing live weapon, aim, collision, or
visibility state?

## Verified inputs and tools

The inspected `AfEngine.dll` working copy has SHA-256
`A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E`.
The inspected `AirCraft.type` working copy has SHA-256
`9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E`.
Both are verified read-only copies; neither binary nor any original resource is
tracked.

Ghidra 12.1.2 supplies the canonical function ownership and MSVC class/vtable
interpretation. Rizin 0.9.1 plus rzpipe 0.6.2 independently supplies normalized
instruction reports. Rizin did not discover the AirCraft slot automatically;
after Ghidra fixed its exact entry, the report created only an in-memory
function at that requested VA and records that fact explicitly.

## Event dispatch

`AfVehicle::ProcessEvent` is
`[0x1001B6C0,0x1001FA6C)`. Its packed event type is the DWORD at event `+0x05`.
For the low event range it subtracts one, reads the selector byte from the table
at `0x1001FAD4`, then jumps through the DWORD table at `0x1001FA6C`.
Decoding those tables on the hash-verified copy maps event `0x06` through
selector `3` to VA `0x1001DB91`. Ghidra independently renders the same block as
`case 6`.

The exact crosshair-substage instruction order is:

```text
1001DB91  require byte [vehicle.type + 0x04] != 0
1001DBA4  call vehicle virtual slot +0xBC
1001DBAA  require vehicle inactive byte +0x460 == 0
1001DBB8  require attached camera +0x22C != null
1001DBC6  load selected-secondary weapon +0x494
1001DBD2  if non-null, call weapon virtual slot +0x3C
1001DBD5  load primary weapon +0x490
1001DBE1  if non-null, call weapon virtual slot +0x3C
```

The previously recovered `AfWeapon` vtable identifies slot `+0x3C` as
`AfWeapon::RenderCrosshair` at
`[0x10024120,0x1002461D)`. Consequently, simultaneous native calls are
**selected secondary first, primary second**. There is no sort, deduplication,
replacement, or batching between the two calls. A missing selected-secondary
slot does not suppress or substitute the primary slot.

## Earlier HUD stage

An AirCraft instance installs its primary vtable at `0x1000D4D0`. Slot `+0xBC`
therefore resolves to `[0x10006B00,0x10007321)`. Ghidra and Rizin agree on the
entry, end, and first gates: no active overlay window, an attached camera, and
the same type byte. The body then accesses the debug font, screen textures,
health/weapon state, and both `+0x490`/`+0x494` weapon slots. `AirCraft HUD
render stage` is a useful descriptive label, not a recovered symbol or a claim
that every one of its drawing operations is understood.

The event-`0x06` caller invokes this HUD stage after the type gate but before
the vehicle-inactive and camera gates that enclose the two crosshair calls.
After both crosshair calls, the same event branch continues into other
player/actor presentation work. Crosshair composition is therefore one ordered
substage, not the whole event or whole HUD.

## Portable boundary

`composeLegacyWeaponCrosshairRenderEvent` accepts:

- the observed type-eligibility, inactive, and attached-camera state;
- the currently installed immutable authenticated sight set; and
- optional, already-built selected-secondary and primary sprite packets.

It publishes at most two owned entries in exact native order. Each present
packet must belong to the supplied sight set. One invalid packet rejects the
complete composition atomically, preventing a forged mixed-provenance partial
frame. Missing packets remain valid absence and do not affect the other slot.

The function starts at the crosshair substage. Its caller still owns the
earlier independent HUD stage and must explicitly decide `draw` or `suppress`
while building each sprite packet. The composition does not reinterpret
viewport/depth labels, create a weapon identity, trace aim, sample collision,
sort packets, or connect normal renderer frames.

## Verification

Synthetic owner-content tests prove:

- selected-secondary then primary order when both packets exist;
- primary-only and empty ready compositions without substitution;
- independent type, inactive, and camera gates;
- no publication from a forged HUD-local texture identity; and
- bounds-safe ordered entry access.

The focused authenticated crosshair test passes. The clean portable GCC/Ninja
build passes all 127 tests. The code-intelligence preset also passes all 127
tests and produces 263 compilation-database entries without Apple-only sources.
The native MSVC 19.51/Ninja product builds `AirfixDogfighter.exe` and passes all
137 tests, including both D3D11 product smoke tests.

Both repeatable Ghidra and Rizin wrappers pass; the normalized Rizin exporter
passes all 12 tests. Ghidra and Rizin agree on the exact event branch and
`[0x10006B00,0x10007321)` AirCraft slot boundary. The public-boundary scanner
passes all 661 tracked files. Formatting, changed-range clangd diagnostics,
CSV shape/identity/source validation, relative links, local-path checks, and
`git diff --check` are clean. Hosted platform builds remain the final
publication gate for this branch.

## Decision

**GO** for the recovered event-`0x06` crosshair-substage gates and the
authenticated selected-secondary-before-primary value plan.

**NO-GO** for ordinary live crosshair wiring. Changing weapon ownership,
owner/aim/collision inputs, the explicit visibility decision, the earlier
AirCraft HUD stage, and complete frame lifecycle ownership remain separate
requirements.
