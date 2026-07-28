# Retained-static line completion and gameplay-camera pose snapshot

**Date:** 2026-07-28

**Evidence:** `EV-20260728-005`

**Scenario:** `SCN-CAMERA-001`

**Status:** implemented as a backend-neutral retained-static pose boundary;
dynamic-object collision and Metal consumption remain separate

## Question

Can one acquired camera generation be completed through the recovered
vehicle-to-camera line stage and turned into a validated look-at,
world-to-view, and gameplay projection value without publishing partial state
or inventing an SRT scale?

## Native order and retained-static boundary

The event-5 order recovered in
[EXP-20260727-010](EXP-20260727-010-gameplay-camera-modes.md) is:

1. sphere correction;
2. per-axis factor reduction;
3. portal update of the sphere-corrected position;
4. line query from the vehicle world anchor to the camera;
5. on a hit, move to the hit fraction and perform a second portal update;
6. reset and rotate the camera to look at the vehicle; and
7. notify consumers.

`completeLegacyGameplayCameraRetainedStaticFrameState` now joins stages 4 and
5 to the prior all-or-nothing sphere result. A static miss preserves the
intermediate position, room, and factors. A static hit uses the exact
runtime-space point adapted from the retained source-world tracer, then
proposes the second room update from the intermediate camera position to that
point. Factors remain those produced by the sphere stage.

The returned state exists only after the line query and, when applicable, the
entire second portal chain succeed. Invalid input, arena/basis/room failures,
depth or transition limits, and out-of-segment hits retain diagnostics but no
proposed state. The camera owner now accepts only this completed result.

The name is intentionally narrow. Native `PhLine::GetBspCollision` can also
query moving-object BSP and recursively handle transparent collision portals.
The retained mission runtime does not yet own moving-object BSP, and its
current static line adapter queries one camera room. This implementation does
not claim those missing behaviors.

## Unit SRT proof

The gameplay engine constructs `CcCamera`, calls
`CcSrtNode::SetMatrixRotation`, and later performs the final look-at by
resetting the camera `CcMatrixRot` before `RotateByAxisRot`.

Canonical Ghidra 12.1.2 decompilation and an independent read-only Rizin 0.9.1
export agree:

| Function | RVA | Relevant writes |
|---|---:|---|
| `CcMatrixRot::Reset` | `0x00002070` | identity 3x3, scale `+0x24 = 1.0`, cached inverse-square `+0x28 = 1.0` |
| `CcSrtNode::SetMatrixRotation` | `0x0000E8C0` | identity 3x3 at node `+0xA0`, scale `+0xC4 = 1.0`, cached inverse-square `+0xC8 = 1.0` |
| `CcCamera::CcCamera` | `0x0001A0F0` | delegates to the SRT-node constructor, which calls `SetMatrixRotation` |

The final camera pose therefore has a unit uniform scale and `q = 1.0`; the
portable pose builder does not expose an unproven caller-selectable scale.
The generated local Rizin reports and tool databases remain ignored and
outside Git.

## Immutable pose snapshot

`buildLegacyGameplayCameraPoseSnapshot` consumes:

- one owning `LegacyGameplayCameraFrameSnapshot`;
- the matching vehicle world anchor; and
- an explicit projection configuration.

It validates the frame and anchor, reconstructs the final look-at basis,
builds an immutable unit-scale `LegacyCameraTransform`, builds the immutable
legacy scalar projection, and transforms the anchor as a final coherence
check. The anchor must land on positive camera-space Z.

The default projection is the recovered gameplay setup:

```text
near = 0.25
far = 200
horizontal FOV = 90 degrees
logical window width = 640
logical centre = (320, 240)
```

The snapshot owns copies of step, generation, position, room, factors, anchor,
look-at, transform, projection, and the checked camera-space anchor. It exposes
no pointers or views into the state owner.

`project(worldPoint)` preserves the recovered two-stage operation order:

```text
cameraPoint = worldToView(worldPoint)
screenPoint = legacyProjection(cameraPoint)
```

It returns camera Z, legacy `near / Z`, and the near-fallback flag together.
It does not perform clipping, construct an NDC/Metal matrix, aspect-fit the
canvas, or choose a raster-space Y convention.

## Safety and validation

Both new boundaries are allocation-free, bounded, read-only with respect to
their inputs, and `noexcept`. Synthetic tests cover:

- retained-static line miss;
- line hit plus the second portal-room transition;
- invalid intermediate, line, and transition-limit failures with no partial
  proposal;
- preservation of sphere-stage factors;
- one-frame step/generation ownership;
- default and arbitrary-direction look-at poses;
- positive-Z vehicle-anchor coherence;
- ordered world-to-view and screen projection;
- invalid generation, state, anchor, zero direction, projection, and world
  point failures; and
- 4,096 allocation-counted completion/build/project calls.

No proprietary content is required by the fixtures.

A fresh Ninja/GCC 15.2 Release build passes all 64 portable tests. The
`code-intelligence` preset also regenerates `compile_commands.json`, builds,
and passes the same 64 tests. Focused clangd 22 checks pass for all three
changed camera translation units after allowing clangd to query the trusted
user-local MinGW driver; no machine path is stored in the repository. The
public-boundary scan checks 324 files, the Ghidra/working-copy/Rizin wrapper
tests and 12 Rizin normalization tests pass, and `actionlint` plus
`git diff --check` are clean.

## Result and next boundary

The portable renderer can now acquire one coherent camera generation and build
one immutable, backend-neutral gameplay pose and scalar projection from it.
The next boundary is to package this value for the existing Metal renderer
without changing the recovered arithmetic, depth policy, or 4:3 presentation
contract.

Dynamic-object camera collision, transparent collision-portal traversal,
controlled executable traces, and physical-device visual acceptance remain
required before full camera parity can be claimed.

## Confidence

Confidence is **3/3** for the all-or-nothing retained-static completion,
unit-scale SRT observation, immutable snapshot ownership, and tested portable
composition.

Confidence remains **2/3** for complete native event-5 parity because the
moving-object and transparent collision-portal paths have not been
implemented or runtime-traced.

## Related material

- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Camera static state integration](EXP-20260728-017-camera-static-state-integration.md)
- [Camera state owner](EXP-20260728-018-camera-state-owner-snapshot.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
