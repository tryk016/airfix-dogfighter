# Placed-object serialized dynamic BSP

**Date:** 2026-07-28

**Evidence:** `EV-20260728-021`

**Scenarios:** `SCN-WEAPON-001`, `SCN-CAMERA-001`

**Status:** bounded semantic CCF decoding implemented; runtime assembly
continued in
[EXP-20260728-039](EXP-20260728-039-placed-dynamic-bsp-assembly.md)

## Question

Can the preserved object property `0x4101` be decoded without inventing a
second BSP format, losing its raw descriptors, recursively owning attacker-
controlled C++ objects, or prematurely claiming that placed dynamic collision
is live?

## Native loader contract

Ghidra 12.1.2 Headless is the canonical static source. Address-selected
decompilation and instructions from the verified local `Cc.dll` project
confirm:

- `CcRoom::LoadSceneCcf` at RVA `0x00027340` opens every direct `0x4101`
  child and decodes it only when its ID is `0xF0C0`;
- the first physical instance stores the resulting linked list at
  `CcObject + 0x15c` and caches it at `CcMesh + 0x13c`;
- later instances of that mesh reuse the cached list and skip their serialized
  children;
- `CcBspNode::Load` at RVA `0x0002A230` reads the same child-presence words,
  two `F040` plane vectors, and trailing `F0C1` records used by room BSP;
- the polygon loader at RVA `0x0002A2F0` reads five `F040` vectors followed by
  `polygonIndex` and one second `u32`; and
- the 39-byte helper at RVA `0x0002A3E0` appends
  `{nodePolygon, polygonIndex, secondWord}` to a deferred global list. After the
  scene is complete, `LoadSceneCcf` resolves the second word as an object
  reference, calls `CcObject::GetPolygon(object, polygonIndex)`, and assigns
  the result to `CcBspNodePoly + 0x44`.

This establishes that the second word is `placedObjectReference` in dynamic as
well as room trees. Each loaded root is prepended as a `CcBspTree`, so native
runtime list order is the reverse of physical root order.

## Independent Rizin check

Fresh automatic Rizin 0.9.1/rzpipe 0.6.2 reports from the hash-verified,
read-only working copy independently identify:

| Function | Automatic boundary | Size | Agreement |
|---|---:|---:|---|
| polygon loader `0x0002A2F0` | `[0x1002A2F0, 0x1002A391)` | 161 bytes | five vector-loader calls, two integer reads, then deferred-link allocation/call |
| deferred link `0x0002A3E0` | `[0x1002A3E0, 0x1002A407)` | 39 bytes | stores next, node-polygon pointer, polygon index, and object reference |

The helper instruction stream independently shows the global-list read/write
and stores its three arguments at offsets `+4`, `+8`, and `+0x0c`. Generated
reports, the binary, and tool databases remain ignored and were not committed.

## Portable representation

`CcfPlacedObjectMetadata` now retains:

1. the raw `0x4101` wrapper with its direct descriptors in physical order; and
2. a typed vector of recognized `F0C0` roots using the existing flat
   `CcfBspTreeMetadata` node and polygon arenas.

The new tree kind is `dynamicObjectTree` and its source is
`placedObject4101`. Unknown direct wrapper children remain raw and are skipped,
as in the native loader. Known roots reuse the same iterative decoder and
shared per-CCF depth/tree/node/polygon/descriptor budgets as static and portal
trees. No recursive owning structure or native pointer enters the public
model.

Mission retained-byte accounting includes the copied raw child descriptors,
typed trees, nodes, polygons, per-node polygon-index vectors, and trailing raw
descriptors. Thus accepting semantic `0x4101` data cannot bypass the atomic
mission CPU publication ceiling.

## Corpus validation

A read-only diagnostic parsed all 286 selected CCF files from the original
archive through the updated public parser and separately walked each raw
wrapper:

| Observation | Result |
|---|---:|
| wrappers / roots | 1,160 / 1,160 |
| empty wrappers / maximum roots per wrapper | 0 / 1 |
| nodes / polygons | 10,641 / 16,212 |
| maximum depth | 29 |
| maximum nodes / polygons in one tree | 267 / 315 |
| total / maximum wrapper bytes | 2,225,058 / 46,116 |
| non-Boolean presence words | 0 |
| owner-reference mismatches | 0 |
| out-of-range mesh polygon indices | 0 |

Only aggregate shape and parity facts are published. No asset name, path,
hash, geometry, binary, or extracted payload is included.

## Validation boundary

Synthetic tests cover multiple physical roots, nested A children, polygon
fields, source/kind tags, exact physical order, unknown extension children,
duplicate wrappers, invalid child IDs, malformed polygon sizes, malformed raw
chunk sequences, and the 1,024-depth ceiling. A complete synthetic mission
load includes one decoded placed-object tree in its exact/one-under retained
metadata budget test. Existing room-BSP limit and descriptor-bomb tests
continue to exercise the shared implementation.

This slice intentionally stops at authenticated metadata. It does not yet:

- bind each polygon to the resolved placed object, mesh triangle, or material;
- reproduce first-use mesh-cache ownership;
- construct per-room dynamic-object lists in native prepend order;
- publish placed dynamic portals or live object transforms; or
- connect these trees to line or sphere collision.

Those operations are implemented by the separate atomic assembly in
[EXP-20260728-039](EXP-20260728-039-placed-dynamic-bsp-assembly.md). Mission
ownership and live composition remain later integration boundaries.

## Confidence

Confidence is **3/3** for the serialized layout, the meaning of both `F0C1`
words, physical order, and the native first-use cache boundary because Ghidra,
Rizin instructions, the loader's deferred resolution, the shared room format,
and all selected corpus records agree.

Confidence remains **2/3** for final runtime behavior until a later assembly
reproduces native list/cache order and is compared with controlled executable
traces.
