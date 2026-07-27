# Portal-BSP line tracing and room transitions

**Date:** 2026-07-27
**Evidence:** `EV-20260727-012`
**Scope:** retained mission BSP, `PhLine::TraceBspNode`,
`PhLine::GetPortalBspCollision`, and `CcRoom::TracePortals`

## Question

Recover enough of the original source-world BSP contract to retain collision
data beyond CCF parsing and implement the camera's allocation-free line and
portal queries. Keep the still-incomplete sphere/contact resolver outside this
slice.

## Inputs and method

`Cc.dll` SHA-256
`18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF`
was analyzed from a read-only working copy with Ghidra Headless and Rizin.
Function boundaries, x86 instructions, comparisons, field offsets, and list
construction were cross-checked independently. No original module was
executed, patched, uploaded, or committed.

## Runtime layout and serialized mapping

`CcBspNode` stores child A at `+0x04`, child B at `+0x08`, split normal at
`+0x0C`, a point on the plane at `+0x18`, and its prepend list of
`CcBspNodePoly` records at `+0x24`.

For:

```text
d(x) = dot(x - pointOnPlane, splitNormal)
```

child A is the `d < 0` side and child B is the `d >= 0` side. This confirms
the semantic direction of the parser's previously neutral A/B fields.

The legacy loader prepends both trees to their room list and polygons to their
node list. Runtime traversal order is therefore the reverse of physical CCF
order. Equal fractions do not replace a prior hit because the nearest test is
strict.

The existing `CcfBspNodeMetadata` and `CcfBspPolygonMetadata` retain every
query-critical source-world value. `resolveCcfRoomScene` authenticates each
polygon through:

```text
placedObjectReference
  -> instantiated placed object
  -> resolved mesh
  -> mesh triangle
  -> optional portal physical room
```

The mission arena translates both owner and portal target from
`{semantic source, physical room}` to the runtime world-room index. It copies
only pointer-free BSP values and the three confirmed portal-follow gates.

## Exact node traversal

For segment `S -> E`, direction `D = E - S`, and interval `[lo, hi]`:

```text
dS = dot(S - p, n)
dE = dot(E - p, n)
t  = dS / (dS - dE)
```

The original x87 path explicitly spills the three components of `D`, `S - p`,
`E - p`, `point0 - S`, the reconstructed `-(edge01 + edge12)`, `dS`, `dE`,
`t`, `lo`, and `hi` to binary32. Split-plane dots accumulate in `z + y + x`
instruction order. The portable implementation reproduces those stores and
that grouping, so two distinct mathematical crossings which become the same
binary32 fraction preserve the first visited tree/polygon.

1. If `t >= lo`, visit the start-side child on
   `[lo, min(t, hi)]`.
2. Test node polygons when endpoint signs differ or either absolute signed
   distance is below the binary32 `1e-6` value, and only while
   `t < nearest`.
3. If `t < hi`, visit the end-side child on
   `[max(t, lo), hi]`.

The root interval is `[0,1]`, while nearest starts at `FLT_MAX`. The polygon
gate has no explicit `t` clamp and division-by-zero is not specially handled.
An unordered `t` skips the near child, enters the legacy unordered polygon
path, and still visits the far child with the prior lower bound. The portable
query preserves those finite-input binary32 comparisons and returns the
unclamped fraction. It rejects malformed arena structure and non-finite query
endpoints instead of exposing partial output.

The returned hit point also follows the recovered x87 schedule rather than a
generic fused vector expression: X/Y `t * D` products spill before adding
`S`, while the Z product remains extended until its add and final binary32
store. A 1-ULP regression locks this observable order.

## Exact triangle test

`faceNormal`, not `faceCross`, selects the facing path. The default `PhLine`
is one-sided. A separately exposed two-sided option reverses `D`, swaps the
endpoint-plane roles, and records a reverse-facing hit.

Let:

```text
e0 = edge01
e1 = edge12
e2 = -(e0 + e1)
R  = point0 - originalLineStart
pos(x) = (0 < x)
```

The hit requires the inclusive endpoint-plane epsilon tests followed by all
three strict-sign XOR tests:

```text
pos(dot(e0 x L, R)) != pos(dot(e0 x L, R + e1))
pos(dot(e1 x L, R)) != pos(dot(e1 x L, R - e2))
pos(dot(e2 x L, R)) != pos(dot(e2 x L, R + e0))
```

On success, the stored fraction is `t`, the hit normal is the node split
normal, and the bound polygon supplies the portal owner. The documented NaN
`faceNormal` sentinel never produces a hit, including in the two-sided path.

## Portal follow gate

For the camera's default `PhLine + 0x34 == 1`, a nearest portal hit is followed
only when:

```text
mesh.selectionFlagB != 0 &&
placedObject.portalType == 0 &&
placedObject.rawFlag != 0
```

`selectionFlagA` is unrelated. Any nonzero `portalType` blocks the transition.
The next segment starts exactly at `S + t * D`, keeps the prior end, switches
to the hit object's portal room, and repeats.

The original recursion has no cycle guard. The portable API adds an explicit
transition budget, rejects budgets above the hard maximum of 256 transitions,
and fails closed when a self/cyclic portal exhausts it. This is an intentional
safety divergence for pathological content; a caller cannot disable it with
an unbounded value.

`CcRoom::TracePortals` does not read its third float argument in this build.
The exact caller values `0.2f` and `0.1f` remain documented ABI observations,
not tolerances.

## Portable result

Implemented:

- `MissionWorldSpatialArena`, retained by `LoadedMissionWorldRoom`;
- source-aware owner and target-room translation;
- canonical rebuild-and-compare authentication of the supplied room catalog;
- reverse tree/polygon order matching legacy prepend lists;
- pre-allocation aggregate physical-room/tree/node/polygon and retained-byte
  limits plus atomic failure with released vector capacity;
- allocation-free, bounded source-world static/portal line trace;
- exact portal mesh/type/visibility gate and bounded multi-room continuation;
- optional strict per-hop `[0,1]` validation for portable state publishers;
- publication-time range, topology, float, target, and exact byte-accounting
  validation;
- synthetic tests independent of proprietary content.

[EXP-20260727-012](EXP-20260727-012-runtime-spatial-adapter.md) adds the
runtime/source-world basis boundary and strict per-hop fraction policy used by
future camera state publication.

Deferred:

- sphere contact collection and iterative constraint resolution;
- dynamic-object collision;
- stateful camera pose/room publication;
- controlled executable traces for numeric tolerances and pathological
  epsilon/tie cases.

## Confidence

Confidence is 2/3 for the portable behavioral contract: layout, ordering,
binary32 spills, unordered branches, constants, and gates agree between
Ghidra and Rizin, while controlled runtime traces and x87 behavior outside the
observed explicit stores remain unverified.
