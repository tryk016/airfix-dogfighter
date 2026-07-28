# Authenticated player collision publication

**Date:** 2026-07-28

**Evidence:** `EV-20260727-004`, `EV-20260728-019`

**Scenarios:** `SCN-SPAWN-001`, `SCN-WEAPON-001`, `SCN-CAMERA-001`

**Status:** player-only collision asset and frame publication implemented

## Question

Can the already authenticated player visual become the first real dynamic
collider consumed by the reconstructed combined line query without resolving
files again, trusting names, allocating on the frame path, or claiming that
all live actors are implemented?

## Evidence boundary

No new binary interpretation is introduced in this slice. It composes two
previously confirmed contracts:

- `AfVehicle::SetSkin` creates the selected slot-zero hierarchy, applies the
  recovered actor-local root pose, and propagates one actor identity; and
- `CcSrtNode::CreateDynamicBsp` / `CcObject::CreateDynamicBsp` build
  object-local BSPs for the hierarchy's eligible physical meshes.

Ghidra remains the canonical source for those native contracts. The dynamic
BSP construction itself was independently checked with Rizin in
[EXP-20260728-035](EXP-20260728-035-dynamic-bsp-line-adapter.md). This
integration reads no original file directly and adds no private payload,
analysis database, local path, or tool output to Git.

## Mission-load asset

`PlayerActorCollisionAssembly` consumes the same parsed player CCF and the same
actor-local `PlayerActorVisualDrawAssembly` that already passed the verified
mission loader. It never resolves a model by a second name or path.

The builder:

1. validates the parallel mesh/instance provenance and slot-zero selection;
2. resolves physical meshes and material collision values by authenticated
   indices and numeric references;
3. builds one immutable `LegacyDynamicBspMesh` per first-used physical mesh;
4. retains actor-local instances in blueprint DFS order; and
5. accounts outer records plus every nested arena and material-reference byte.

Mesh, instance, vertex, triangle, material, BSP workspace, depth, and aggregate
retained-byte ceilings are explicit. Missing/duplicate material bindings,
invalid provenance, non-unit transforms, geometry errors, BSP failures,
integer overflow, and allocation failure are typed. Every failure clears all
publishable output.

The complete asset is now part of `LoadedMissionWorldRoom`. It is built before
the visual is moved into the final scene, retained only after the same content
session and revision checks succeed, charged to the global published-CPU
budget, and covered by the allocation-free publication proof. Player
descriptor, render provenance, collision provenance, actor-local transforms,
mesh counts, instance counts, and retained bytes must all agree.

## Frame publication

`publishPlayerActorCollisionFrame` composes a caller-provided live actor-world
pose with every immutable actor-local collision instance. It fills exact-size
caller-owned object and room-range spans:

- instance order is unchanged;
- each object carries the caller's actor object ID and active flag;
- all player objects are ordinary portal type `-1`;
- exactly the selected room owns the contiguous object span; and
- all other room ranges are empty.

The function performs a complete validation pass before writing. Invalid
assembly, room, output size, or non-orthonormal composition leaves both output
spans untouched. A successful publication is `noexcept` and allocation-free,
so a preallocated simulation owner can refresh it every accepted actor pose.

The output is directly consumable by
`traceMissionWorldRuntimeCombinedPortalLine`. This slice deliberately publishes
only the authenticated primary player. It does not invent AI/ground actor
sources, dynamic portal objects, cross-actor ordering, lifetime ownership, or
the remaining projectile-level portal loop.

## Validation

Synthetic public-data tests cover:

- two physical meshes with three ordered hierarchy instances;
- material collision values and exact retained-byte accounting;
- actor-world composition, room-range publication, and actor identity;
- a published dynamic hit through the complete combined portal-line query;
- upstream visual, material, provenance, transform, and byte-limit failures;
- forged nested byte accounting and publication-boundary mutations;
- exact/one-under collision admission inside the mission loader;
- failure-without-output-mutation; and
- 4,096 frame publications with zero observed heap allocations.

Cross-platform build, clangd, static-tool, WSL, and GitHub Actions results are
recorded in the progress log when the slice is published.

## Confidence

Confidence is **3/3** for provenance, construction order, material propagation,
bounded ownership, and frame publication because this is a checked composition
of two already implemented evidence-backed boundaries.

Confidence is **2/3** for final runtime parity until the recovered live
AirCraft pose/room/object-ID producer and other room actors are connected and
compared against controlled executable traces.
