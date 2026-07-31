# EXP-20260731-085: full-room diagnostic capture

**Status:** implemented and locally validated
**Confidence:** high (`3`) for camera fit, state isolation, native output
extent, draw submission, and private-boundary behavior; medium (`2`) for the
cutaway's usefulness across rooms not yet inspected

## Question

Can the reconstructed Windows renderer produce one private 1920x1080 frame
that contains the complete selected room, all submitted placed objects, the
player aircraft, decoded textures, and the diagnostics overlay without
changing gameplay camera or simulation state?

## Design

`SceneOverviewCamera` is an explicit developer-capture policy, not recovered
gameplay behavior. It:

1. validates every referenced non-empty mesh, local bound, and instance
   transform;
2. transforms all local AABB corners and accumulates one finite world AABB;
3. derives a fixed elevated view, distance, near plane, and far plane from the
   bounds and requested logical canvas;
4. verifies all eight world-bound corners against the configured viewport
   margin and depth range; and
5. returns an owning `LegacyGameplayCameraClipPacket` without touching the
   mission's SPSC camera exchange.

An external overview of an enclosed room normally shows only its roof and
outer walls. The D3D11 capture therefore uses a diagnostic cutaway: commands
whose static provenance identifies a physical room contributor use a dedicated
front-face-culling rasterizer. Other placed objects and the appended player
aircraft retain the ordinary two-sided rasterizer. Normal gameplay continues
to use `D3D11_CULL_NONE` for every command.

## Safety boundary

The command requires the existing explicit owner-private content root and
mission pair. It never infers or prints logical paths, refuses non-BMP output,
forces the path-free diagnostics overlay, and remains mutually exclusive with
all other capture modes. Original assets, logical paths, packages, and derived
images remain outside Git and CI. Portable tests use only synthetic meshes.

## Verification

Synthetic coverage exercises transformed world bounds and complete projection
fit at 4:3, 16:9, 21:9, and 32:9, plus invalid configuration, empty models,
invalid mesh references, non-finite bounds, and non-finite transforms.

An owner-local Classic capture was then generated through the real hidden SDL3
window and D3D11 readback at exact 1920x1080 and render scale 100%. Visual
inspection confirmed the cutaway room interior, decoded textures, placed
contents, player aircraft, and diagnostic overlay. The overlay reported a
1920x1080 output and render target, 190 scene draw calls, 1,782 scene
triangles, and an estimated 25.2 MiB of GPU memory. The BMP and all source
content remain ignored private files.

A fresh GCC 15.2/Ninja build completed 389 steps and passed all 122 portable
CTests. The native MSVC 19.51/Ninja product build linked the D3D11 application
and passed all 132 CTests, including both product smokes. Public-boundary tests,
the 630-file repository scan, 12 deterministic Rizin normalization tests, and
`git diff --check` pass.

## Decision

**GO** for repeatable private full-room diagnostic captures on Windows.

**NO-GO** for treating the overview camera or cutaway culling as gameplay
behavior. They remain capture-only diagnostics. Additional mission rooms
should be sampled before the default view direction is treated as universally
optimal.
