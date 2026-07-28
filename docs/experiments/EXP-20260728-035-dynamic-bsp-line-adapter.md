# Dynamic-object BSP and combined line adapter

**Date:** 2026-07-28

**Evidence:** `EV-20260728-019`

**Scenarios:** `SCN-WEAPON-001`, `SCN-CAMERA-001`

**Status:** bounded per-mesh dynamic BSP construction and allocation-free
non-portal static/dynamic line competition implemented

## Question

How does the native engine construct collision BSPs for movable mesh objects,
and which part of `PhLine::GetBspCollision` can be made portable without yet
inventing actor ownership, room-list publication, scale behavior, or recursive
portal continuation?

## Sources and safety

The analysis used a hash-verified read-only working copy of `Cc.dll`:

```text
SHA-256 18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF
```

Ghidra 12.1.2 reused the canonical local project. Rizin 0.9.1 independently
regenerated reports with user configuration disabled. The original
installation was not executed or modified. The DLL, work copy, Ghidra
project, Rizin JSON, and decompiler reports remain ignored and outside Git.

## Native construction

`CcSrtNode::CreateDynamicBsp` RVA `0x0000F490` optionally dispatches the same
operation to child nodes. `CcObject::CreateDynamicBsp` RVA `0x00015E80`
constructs a tree only when the object is eligible, has a mesh, and has no
existing dynamic tree. The completed tree is cached at mesh offset `+0x13C`,
so multiple instances of one mesh share immutable object-local collision
geometry.

For every mesh triangle in ascending source order, the object creates a
`CcBspPoly` from its three local vertices and prepends it to the build list.
The first splitter search therefore sees the highest source triangle first.
The dynamic polygon plane uses:

```text
edge01 = point1 - point0
edge12 = point2 - point1
faceCross = edge01 cross edge12
faceNormal = normalize(faceCross)
```

This is deliberately distinct from the recovered reverse-cross convention
used by the rendering geometry path.

`CcBspPolyList::CreateBspTree` RVA `0x00024970` evaluates every candidate in
current list order. For `count` input polygons its score is:

```text
(2 - (negative + positive) / count)
    * (2 - abs(negative - positive) / count)
```

A crossing polygon contributes to both side counts. The comparison is strict,
so the first exact-score tie wins. Plane classification uses strict
`-0.000001` and `+0.000001` tolerances. Actual clipping uses plane distance
zero, sends negative fragments to child A and nonnegative fragments to child
B, and discards fragments with area below `0.0001`.

Both `AddToList` and `AddToNode` prepend. Consequently child build lists and
coplanar node lists reverse their insertion order. Split fragments preserve
the source polygon's triangle/material metadata; their clipped vertices
participate in later partitioning, while the retained collision record keeps
the original triangle plane and edges.

`CcMesh::CalcSphere` RVA `0x00012B50` returns the greatest Euclidean distance
of a local mesh vertex from the origin. The optional limb relation does not
participate in the selected/current aircraft path because the observed limb
identifier is zero. `CcObject::GetCurrentRadius` multiplies that mesh radius
by the SRT scale.

## Combined line query

The non-portal portion of `PhLine::GetBspCollision` RVA `0x00038BD0`:

1. traces current-room static trees;
2. visits enabled room objects in list order;
3. rejects an object when the line midpoint is farther from its translation
   than `object radius + half segment length`;
4. transforms both endpoints into object-local space;
5. traces the object's shared dynamic BSP with the same strict nearest
   fraction; and
6. transforms a winning dynamic normal forward into world space and
   normalizes it.

Static geometry wins an exact static/dynamic tie. Among dynamic objects, the
earlier room-list object wins an exact tie. Only a strictly nearer fraction
replaces the current hit.

Visible type-zero portal recursion occurs after this pass. It is intentionally
not hidden inside the new adapter: a later coordinator must combine portal
continuation, native room membership, actor lookup/gates, and an explicit
portable transition limit.

## Tool comparison

| Function | Ghidra 12.1.2 | Rizin 0.9.1 | Result |
|---|---|---|---|
| `CcMesh::CalcSphere` `0x12B50` | typed vertex loop and optional limb relation | automatic `[0x10012B50, 0x10012D08)`, 440 bytes, 128 instructions | agree |
| `CcObject::CreateDynamicBsp` `0x15E80` | typed eligibility, mesh cache, triangle construction, recursion gate | automatic `[0x10015E80, 0x1001601E)`, 414 bytes, 122 instructions | agree |
| `CcBspPolyList::CreateBspTree` `0x24970` | readable score, classification, prepend calls and child recursion | automatic `[0x10024970, 0x10024C6D)`, 765 bytes, 264 instructions | agree |
| `CcBspPoly::Split` `0x24D90` | readable clipping branches, fragment creation and area rejection | automatic `[0x10024D90, 0x10025E84)`, 4,340 bytes, 1,112 instructions | agree on boundary, branches, calls, and constants |
| `PhLine::GetBspCollision` `0x38BD0` | typed object transforms and owner result | earlier automatic report confirms boundary, list order and calls | agree |

Ghidra remains canonical for MSVC class types and high-level pseudocode.
Rizin provides an independent automatic-boundary, instruction, call, and
constant cross-check.

## Portable boundary

`buildLegacyDynamicBsp` accepts one already converted, public-data
`ConvertedMeshGeometry`. It performs a bounded mission-load construction and
publishes:

- one immutable synthetic local-room arena that reuses the recovered
  pointer-free BSP walker;
- source triangle and material provenance for every retained polygon;
- resolved optional material collision values;
- the local bounding radius; and
- logical retained-byte accounting and typed failure issues.

Vertex, triangle, material-binding, node, retained polygon, cumulative working
polygon/vertex, depth, and retained-byte limits are explicit. Material
bindings are sorted once for deterministic duplicate detection and logarithmic
triangle lookup. Invalid indices, non-finite or degenerate geometry, duplicate
bindings, allocation failure, and overflow fail closed. No original asset is
required by the API or tests.

`traceMissionWorldRuntimeCombinedLine` traces retained static room BSP and
caller-supplied dynamic objects through one shared strict-nearest result. The
runtime operation is `noexcept`, read-only, and allocation-free. It preserves
static or dynamic provenance, actor object ID, source triangle/material,
collision material value, fraction, point, normal, facing, and segment-range
state.

The object relation is deliberately limited to the confirmed selected F050
contract: finite orthonormal rotation/reflection, unit scale, and translation.
The selected loader writes scale and cached inverse scale as `1.0`. A
non-orthonormal relation fails closed instead of silently guessing how native
dynamic radius and inverse transforms behave for scale or shear.

The caller still owns:

- construction/cache reuse per authenticated mesh;
- hierarchy traversal and live room-list order;
- actor lifetime, state gates, and ID resolution;
- dynamic-object sphere collision for the camera;
- visible type-zero portal continuation and its portable bound; and
- projectile callback/effect dispatch.

## Validation

Synthetic tests cover:

- one-triangle construction, standard-cross normal, radius, material and
  source provenance;
- exact coplanar node-list ties and deterministic balanced splitter choice;
- negative-child A, nonnegative-child B, true crossing splits, and the
  `0.0001` small-fragment discard;
- static-nearer, dynamic-nearer, exact static/dynamic, and exact
  dynamic/dynamic tie policies;
- inactive objects, midpoint/radius rejection, rotation/translation and
  transformed normals;
- empty valid colliders, invalid geometry, duplicate bindings, all principal
  build/runtime limits, invalid object relations, and invalid arenas;
- epsilon-admitted out-of-segment hits; and
- 4,096 combined traces with zero observed heap allocations.

Full clean builds, portable tests, clangd checks, reverse-engineering wrapper
tests, public-boundary scans, and CI results are recorded in the progress log
when this slice is published.

## Confidence

Confidence is **3/3** for function boundaries, construction/list order,
splitter scoring, side mapping, tolerances, radius calculation, broad phase,
strict nearest/tie policy, and the unit-scale selected F050 path.

Confidence is **2/3** for bitwise split intersection/area arithmetic and full
runtime parity until controlled x87 traces are available. Non-unit object
scale, live object publication, portal recursion, and dynamic sphere
collision remain explicitly unimplemented.
