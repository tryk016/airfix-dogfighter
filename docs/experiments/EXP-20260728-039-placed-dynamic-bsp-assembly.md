# Placed-object dynamic BSP assembly

**Date:** 2026-07-28

**Evidence:** `EV-20260728-022`

**Scenarios:** `SCN-WEAPON-001`, `SCN-CAMERA-001`

**Status:** bounded material/cache/room-list assembly implemented; mission
ownership integration deferred

## Question

Can the typed `0x4101` trees recovered in
[EXP-20260728-038](EXP-20260728-038-placed-dynamic-bsp-4101.md) be converted
into the existing allocation-free dynamic line-query representation without
inventing object ownership, losing native first-use cache/list order, or
letting forged retained metadata bypass the mission budget?

## Native load and list contract

Ghidra 12.1.2 Headless remains the canonical static source. The verified local
`Cc.dll` reports establish this sequence:

1. `CcRoom::LoadSceneCcf` at RVA `0x00027340` creates and attaches the placed
   object before it opens that object's direct children.
2. On `0x4101`, a null `CcMesh + 0x13c` cache causes each direct `F0C0` root to
   be prepended at `CcObject + 0x15c`.
3. After every nested child, the loader saves the object's current room, calls
   `SetRoom(receiverRoot, false)`, then calls `SetRoom(savedRoom, false)`.
4. `CcObject::Unlink` removes an existing dynamic-list link.
   `CcObject::Link` at RVA `0x000152B0` tests `CcObject + 0x15c`; when non-null
   it copies the current `room + 0` head to `object + 0x114` and stores the
   object as the new head.
5. The loader then writes the object tree pointer to `CcMesh + 0x13c`.
6. A later `0x4101` for the same physical mesh copies that cached pointer into
   `CcObject + 0x15c` and skips every nested chunk. This branch contains no
   room relink, so it does not insert that later object into `room + 0`.

The last point is preserved as observed, not silently corrected. No selected
corpus record reuses a serialized dynamic-BSP mesh, so the no-relink branch is
covered synthetically and remains a controlled-trace target.

`CcObject::ResetMembers` at RVA `0x00014C10` initializes the dynamic-BSP
visibility gate at `object + 0x198` to one. `CcObject::GetDynamicBsp` at RVA
`0x0000E580` returns `object + 0x15c` only while that gate is nonzero. Loaded
first-use objects are therefore published active.

## Independent Rizin check

A fresh automatic Rizin 0.9.1/rzpipe 0.6.2 report from the hash-verified,
read-only working copy identifies `CcObject::Link` at
`[0x100152B0, 0x10015335)`, exactly 133 bytes. Its independent instruction
stream shows:

- the room store at `object + 0xf4`;
- the category/mesh link stores at `object + 0x10c` and predecessor
  `+0x110`;
- the `object + 0x15c` non-null test;
- the old `*[room]` head copied to `object + 0x114`; and
- `object` written to `*[room]`.

Rizin labels the calling convention incorrectly as `cdecl`; the exported
C++ symbol, ECX receiver, and `ret 4` establish `__thiscall`. Ghidra remains
the authority for types and pseudocode. The generated Rizin report, binary,
and analysis databases remain ignored.

## Portable assembly

`buildMissionPlacedDynamicBspAssembly` accepts the ordered semantic mission
sources and an authenticated `MissionWorldRoomCatalog`. It:

- replays and compares the room catalogue before deriving any physical-to-
  runtime room mapping;
- resolves each enabled placed scene and rejects ambiguous object, mesh, room,
  parent, or portal dependencies;
- creates one immutable `LegacyDynamicBspMesh` for the first serialized
  `0x4101` use of each `{sourceIndex, physicalMeshIndex}` cache key;
- binds every retained polygon index to the resolved physical mesh triangle,
  raw material reference, and uniquely resolved optional `0x2152` collision
  mode;
- reverses physical root and per-node polygon order to reproduce native
  prepend lists;
- publishes only first-use builder objects, reverses load order separately in
  each runtime room, and flattens those lists into exact room ranges;
- converts authored F050 world transforms into the runtime basis and publishes
  the native default active state, portal type/room/visibility, and explicit
  source provenance; and
- accounts outer records plus every nested pointer-free arena in one exact
  retained-byte ledger.

The adapter accepts only unit-scale orthonormal placed transforms and an
orthonormal source-to-runtime basis. This is deliberate:
`LegacyDynamicBspLineObject` has no scale/shear field, and all 1,160 selected
serialized objects use scalar one and matrix orientation. Unsupported values
fail typed rather than being approximated.

## Coordinate conversion

The existing dynamic walker transforms a runtime segment into object-local
runtime coordinates and then directly invokes the pointer-free local BSP
walker. Serialized source-local BSP values therefore cannot remain in raw CCF
space.

For an orthonormal basis `B` and unit conversion `s`, the assembly stores:

```text
point'       = s * B * point
edge'        = s * B * edge
normal'      = B * normal
faceCross'   = s^2 * B * faceCross
```

When `det(B) < 0`, the converted mesh convention swaps triangle corners one
and two. Serialized polygon edges are rewritten to the same order:

```text
edge01' = s * B * (edge01 + edge12)
edge12' = s * B * (-edge12)
```

This keeps the serialized tree consistent with `convertLegacyGeometry` and
the collision-facing convention. The documented all-`0xFFC00000` legacy
face-normal sentinel is bit-preserved; the line walker continues to reject it
as a polygon match exactly as it does for retained room BSP.

## Corpus constraints

A read-only aggregate diagnostic over all selected records found:

| Observation | Result |
|---|---:|
| serialized `0x4101` objects | 1,160 |
| physical meshes with repeated serialized blocks | 0 |
| non-unit / non-matrix placed transforms | 0 / 0 |
| missing or ambiguous owner-room targets | 0 |
| missing or ambiguous referenced materials | 0 |
| ordinary portal type `-1` | 1,160 |
| nonzero portal-room targets | 0 |
| non-Boolean active/visibility source flags | 0 |

These are aggregate semantic facts only. No private path, asset name, hash,
geometry, payload, executable, or original-derived report is committed.

## Validation boundary

Synthetic C++20 tests cover:

- multiple physical roots and per-node polygons in reverse native order;
- two first-use mesh caches plus a later cached object whose forged nested tree
  proves the native skip branch;
- per-room prepend order, physical mesh provenance, material mode/reference,
  bounding radius, and exact flat ranges;
- direct consumption by `traceMissionWorldRuntimeCombinedLine` and the flat
  room ranges by `traceMissionWorldRuntimeCombinedPortalLine`;
- external receiver/root fallback, a visible type-zero portal transition, and
  native no-later-hit clearing;
- reflected-axis plus unit-scale point, edge, normal, transform, and collision
  behavior;
- an empty valid assembly;
- exact and one-under aggregate retained-byte budgets;
- polygon-owner mismatch, unsupported placed scale, an orthonormality drift
  rejected at the same threshold as the line consumer, tampered room
  catalogue, invalid depth limit, and retained-ledger tampering; and
- atomic clearing of every publishable vector on failure.

This slice does not yet:

- install the assembly in `LoadedMissionWorldRoom`;
- compose it with the authenticated player frame in one live room publication;
- create runtime-generated BSPs for other moving actors;
- implement dynamic-object sphere collision; or
- prove the cached later-object no-relink behavior with a controlled executable
  trace.

## Confidence

Confidence is **3/3** for first-use construction, active default, tree/polygon
prepend order, material binding, first-use room-list insertion, and coordinate
conversion: native instructions, Ghidra pseudocode, the independent Rizin
`Link` boundary/stores, existing geometry contracts, corpus invariants, and
end-to-end synthetic line queries agree.

Confidence is **2/3** for intentionally omitting a later cached instance from
the room list. The native cached branch and `Link` implementation are explicit,
but the selected corpus contains no repeated serialized cache key and a
controlled runtime trace has not yet exercised that branch.
