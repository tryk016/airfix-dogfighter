# Mission dynamic-collision ownership and frame composition

**Date:** 2026-07-29

**Evidence:** `EV-20260728-019`, `EV-20260728-022`

**Scenarios:** `SCN-LEVEL-001`, `SCN-WEAPON-001`, `SCN-CAMERA-001`

**Status:** authenticated mission ownership and allocation-free player/placed
composition implemented

## Question

Can the previously recovered placed-object and player dynamic BSP assets be
owned by one authenticated mission snapshot and consumed by one native-order
line query without copying nested mesh arenas, weakening the global CPU
budget, or allocating in the simulation frame?

## Evidence boundary

This integration adds no new interpretation of the original binaries.
[EXP-20260728-037](EXP-20260728-037-authenticated-player-collision-publication.md)
established the authenticated player collider and live actor publication.
[EXP-20260728-039](EXP-20260728-039-placed-dynamic-bsp-assembly.md)
established the serialized `0x4101` assets, first-use mesh cache, per-room
object ranges, and `CcObject::Link` prepend order.

The composition uses those confirmed contracts:

- mission-scene objects are linked during scene loading;
- later runtime actors are prepended by `CcObject::Link`;
- the line query walks a room's list from its head and keeps only a strictly
  nearer fraction, so the earlier list member wins exact ties; and
- mesh BSPs are immutable after construction.

## Snapshot ownership

`LoadedMissionWorldRoom` now owns a non-optional
`MissionPlacedDynamicBspAssembly`. Even an empty placed scene contains one
range per authenticated runtime room. The loader:

1. builds it from the already parsed, ordered semantic CCF sources and the
   authenticated room catalogue;
2. clamps source, room, and retained-byte limits to the outer mission limits;
3. maps typed dependency/provenance failures without publishing a partial
   room;
4. charges its exact nested retained payload to
   `maximumPublishedCpuBytes`; and
5. revalidates completeness, room count, source provenance, and the exact
   published-byte ledger at the final publication boundary.

The parsed CCF cache can therefore be destroyed after load without invalidating
placed-object collision.

## Segmented immutable mesh view

Copying `LegacyDynamicBspMesh` records into a combined vector would either
duplicate nested vectors or transfer ownership away from the independently
validated assemblies. `LegacyDynamicBspMeshView` instead exposes two spans as
one logical index space:

```text
[0, placedMeshCount)                     -> placed owner
[placedMeshCount, placed + player count) -> player owner
```

The combined and portal-continuing line tracers accept this view directly.
Their existing single-span overloads remain source-compatible and delegate to
a view with an empty secondary span.

## Frame publication

`publishMissionWorldDynamicCollisionFrame` takes the immutable placed
assembly, an optional immutable player assembly, the current player pose,
object ID, active state, and room, plus exact-size caller-owned object/range
buffers.

It performs a complete read-only preflight before writing:

- both assemblies must be complete;
- room and output shapes must match exactly;
- logical mesh counts and offsets must not overflow;
- every actor instance must resolve inside the secondary mesh span; and
- every world/local composition must remain finite, nonsingular, and
  orthonormal at the direct tracer's `1e-5` boundary.

On success, each room receives the live player instances first when it is the
player room, followed by the placed assembly's unchanged native-order range.
Player mesh indices receive the placed-mesh prefix offset. An inactive player
remains in the stable output layout with `active=false`, matching the existing
publisher contract and avoiding buffer resizing.

The returned frame contains only spans into the two immutable owners and the
caller-owned frame buffers. It owns no memory. Failure returns an empty view
and leaves both output buffers bit-for-bit unchanged.

## Verification

Synthetic integration tests establish:

- direct ownership and exact/one-under mission budgets;
- empty and populated placed scenes;
- typed incomplete, room-count, and source-provenance rejection;
- player-before-placed order in a shared room;
- the placed-prefix/player-suffix mesh mapping without copied storage;
- an exact-fraction tie won by the active player and fallback to the placed
  object when the player is inactive;
- compatibility of old single-span tracer callers;
- no partial writes for malformed assemblies, transforms, rooms, or output
  sizes; and
- zero observed heap allocations in successful frame publication.

Fresh clean Release and regenerated Debug/code-intelligence builds compile
256 portable steps and pass 79/79 tests. The compilation database contains
163 entries. Clangd 22.1.8 reports zero code diagnostics in the new frame,
test, loader, and publication units; its seven failures in the large existing
tracer are optional `ExtractFunction` feature probes. The three RE wrapper
suites, 12 Rizin/public-boundary Python tests, 391-file public scan, 263-row
function catalogue with its two pre-existing missing references, `actionlint`,
changed-scope local-path scan, and `git diff --check` pass.

## Remaining boundary

This slice composes the authenticated player and mission-placed objects for
line collision. Publication of other live actors, dynamic-object sphere
collision for camera movement, gameplay ownership of the caller buffers, and
controlled executable traces remain separate work.
