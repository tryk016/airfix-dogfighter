# Metal gameplay-camera clip packet

**Date:** 2026-07-28

**Evidence:** `EV-20260728-006`

**Scenario:** `SCN-CAMERA-001`

**Status:** implemented for the private mission-room renderer; dynamic camera
state, physical-device visual acceptance, and missing native collision paths
remain separate

## Question

Can the immutable gameplay-camera pose be consumed by Metal while preserving
the recovered scalar world-to-view, projection, reverse-depth, and 640x480
presentation contracts instead of replacing them with an unverified generic
view-projection matrix?

## Homogeneous adapter

The recovered scalar projection for an accepted camera-space point is:

```text
screenX = centreX + projectScale * cameraX / cameraZ
screenY = centreY - projectScale * cameraY / cameraZ
depth   = near / cameraZ
```

For a Metal viewport using the logical canvas width `W` and height `H`, the
portable clip packet derives:

```text
wClip = cameraZ / near
zClip = 1

xClip = (2 * projectScale / W) * cameraX
      + (2 * centreX / W - 1) * wClip

yClip = (2 * projectScale / H) * cameraY
      + (1 - 2 * centreY / H) * wClip
```

After the homogeneous divide:

```text
xNdc = 2 * screenX / W - 1
yNdc = 1 - 2 * screenY / H
zNdc = near / cameraZ
```

This is an adapter around the recovered operations, not a newly asserted
legacy matrix. CPU tests compare the homogeneous divide against
`LegacyGameplayCameraPoseSnapshot::project` so a sign, centre, FOV, or
reciprocal-depth change fails directly.

Metal's normal homogeneous clip volume rejects `cameraZ < near` because
`zClip = 1` then exceeds `wClip = cameraZ / near`. The separately recovered
inclusive far plane is carried as:

```text
farClipDistance = far - cameraZ
```

and emitted by the vertex shader through Metal's `[[clip_distance]]` output.
The private gameplay pipeline uses depth mode one:

```text
compare = greaterEqual
write   = enabled
clear   = 0
```

The diagnostic public-data path retains its old pipeline and does not pretend
to be the recovered camera.

Metal language and depth behavior are checked against Apple's
[Metal resources and language specification](https://developer.apple.com/metal/resources/),
[depth-testing documentation](https://developer.apple.com/documentation/Metal/calculating-primitive-visibility-using-depth-testing),
and the official
[Working with Metal: Fundamentals](https://developer.apple.com/videos/play/wwdc2014/604/)
session. The unsigned Apple builds remain the authoritative compiler check for
Objective-C++ and Metal source.

## Presentation contract

Private mission rendering asks the existing portable layout contract to
aspect-fit the recovered 640x480 canvas into the drawable. Metal receives that
fitted rectangle as its viewport. The full render target is still cleared, so
the unused area becomes letterbox or pillarbox space.

This is the parity-first port policy already recorded in
[EXP-20260727-007](EXP-20260727-007-viewport-aspect.md). It does not claim that
the 2000 executable implemented adaptive widescreen.

## Explicit bootstrap policy

The retained simulation does not yet publish changing event-5 camera
generations. A private room therefore begins at the recovered `camera0` target
relative to the authenticated player spawn:

```text
speed  = 0.7
offset = (0, 0.1, -0.75)
```

The bootstrap frame is generation one, simulation step zero, uses the
authenticated spawn room, and has unit axis factors. It begins directly at
the target and deliberately invents no previous camera position or smoothing
history. The portable C++20 factory owns and tests this temporary port policy;
the Objective-C++ renderer only supplies the authenticated actor transform.

A future stateful coordinator will replace the bootstrap packet with acquired
event-5 snapshots. Until then, the camera is coherent and correctly projected
but static relative to the initial actor pose.

## Ownership and failure behavior

`LegacyGameplayCameraClipPacket` owns a copy of one immutable pose snapshot and
the logical canvas height. Building and projecting are allocation-free and
`noexcept`. Invalid canvas configuration, point transforms, or derived
homogeneous values expose no partial result.

Private Metal room preparation builds the bootstrap packet before allocating
or publishing the candidate resource owner. Prepared-room validation and the
player-pose endpoint require the packet. A private frame with no packet is
dropped before command encoding.

The renderer explicitly repacks the portable values into a fixed 160-byte,
16-byte-aligned Metal uniform ABI:

- local-to-world matrix;
- three camera basis columns;
- camera translation and cached inverse-square scale;
- near, far, and project scale; and
- projection centre and logical canvas dimensions.

The shader preserves the point operation order: local-to-world, subtraction,
three basis dot products, cached inverse-square multiplication, then the
homogeneous projection equations above.

## Validation

Synthetic portable tests cover:

- exact homogeneous-divide agreement with the recovered scalar projection;
- camera-space position and reciprocal-depth preservation;
- inclusive near and far boundaries;
- rejection geometry before near and beyond far;
- non-default projection centre and logical canvas height;
- invalid configuration, non-finite input, and derived overflow;
- the explicit camera0 bootstrap target, room, generation, step, factors, and
  actor anchor;
- invalid bootstrap actor transforms with no partial camera; and
- 4,096 allocation-counted packet builds and projections.

No proprietary content is required. A fresh Ninja/GCC 15.2 Release build and
the regenerated code-intelligence build each pass all 65 portable tests.
Focused clangd checks, the public-source boundary, workbench wrapper suites,
Rizin normalization tests, function-catalogue uniqueness, `actionlint`, and
`git diff --check` are clean. Pull-request CI also passes on Ubuntu 24.04,
Windows 2025, ARM64 macOS 26, and the code-intelligence preset. Xcode 26.6
successfully compiles both unsigned `iphoneos` and `iphonesimulator` targets,
including the Objective-C++ adapter and Metal shader.

## Result and remaining boundary

The private mission-room path now consumes the recovered gameplay camera,
reverse depth, and parity-first presentation contract. This closes the first
native Metal camera join without claiming complete camera behavior.

Still required:

- publish changing camera generations from the stateful simulation;
- connect the current actor pose to that coordinator;
- implement moving-object BSP and transparent collision-portal behavior;
- compare controlled executable traces; and
- perform physical-device visual, precision, orientation, and performance
  acceptance on both target iPhones.

## Confidence

Confidence is **3/3** for the homogeneous derivation, reverse-depth state,
aspect-fit policy, immutable ownership, bootstrap provenance, and portable
test equivalence.

Confidence remains **2/3** for end-to-end visual parity until physical-device
comparison is complete, and for complete event-5 behavior until the missing
dynamic paths and traces are available.

## Related material

- [Camera render pipeline](EXP-20260727-004-camera-render-pipeline.md)
- [Reverse depth](EXP-20260727-006-reverse-depth.md)
- [Viewport and aspect](EXP-20260727-007-viewport-aspect.md)
- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Retained-static pose snapshot](EXP-20260728-019-camera-retained-static-pose-snapshot.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
