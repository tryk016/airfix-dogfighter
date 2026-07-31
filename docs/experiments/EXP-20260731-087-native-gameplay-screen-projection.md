# EXP-20260731-087: native gameplay screen projection

**Status:** implemented and locally validated

**Confidence:** high (`3`) for the portable composition and output-pixel
mapping; medium (`2`) for later HUD/effect use until native visual consumers
are connected

## Question

Can recovered gameplay world points be mapped into the actual native output
viewport without coupling them to render scale, stretching the recovered 4:3
camera, or silently discarding points needed by future HUD indicators?

## Design

`NativeRenderLayout` now owns both forms of the scene viewport: render-target
pixels for GPU rasterization and output pixels for screen-space consumers. It
provides an unclipped camera-logical-to-output mapping paired with the existing
output-to-camera mapping. A result labels whether the point lies inside the
scene viewport; it does not clamp off-screen values.

`projectGameplayWorldPointToOutput` composes three already-defined domains:

1. the recovered immutable gameplay camera projects a world point into its
   reference screen canvas;
2. the native layout translates that point into the centred Hor+ or safe-FOV
   logical camera canvas; and
3. the layout maps it through the actual scene viewport in output pixels.

The operation rejects a camera/layout canvas or horizontal-FOV mismatch and
preserves the nested recovered projection failure. Near/far visibility and
scene-viewport inclusion remain separate labels. An off-screen point within
the recovered depth interval is therefore a valid result that a future HUD can
hide or turn into a directional indicator. The function is stateless,
allocation-free, and does not change camera, simulation, UI safe-area, render
target, or presentation state.

This slice deliberately does not invent reticle art, weapon projection rules,
screen-effect behavior, HUD visibility policy, or a live aircraft producer.

## Verification

Portable tests cover 4:3, 16:10, 16:9, 19.5:9, 21:9, and 32:9 at 50%, 100%,
125%, and 200% render scale. The vehicle anchor remains at the exact output
centre in every case. Original 4:3 at 1920x1080 maps through the exact
`{240, 0, 1440, 1080}` output viewport; a 25-degree safe-FOV increase expands
around the unchanged centre. Tests also cover reversible camera/output
mapping, off-screen preservation, independent near/far labels, invalid world
coordinates, camera/layout mismatch, finite-to-non-finite output rejection,
and 4,096 projections without allocation.

The complete GCC 15.2/Ninja portable suite passes 124/124 CTests. The native
MSVC 19.51/Ninja Windows product builds and passes 134/134 CTests, including
the D3D11 renderer and both product smokes. Hosted Apple compilation remains
the publication gate for the shared C++20 code and Metal product; native HUD
visual acceptance remains a later consumer slice.

## Decision

**GO** for the backend-neutral world/reference-camera-to-native-output bridge
as the common coordinate source for later reticle, weapon, HUD, and
screen-space effect consumers.

**NO-GO** for claiming a finished HUD, visual parity, live camera motion, or a
recovered off-screen indicator policy from this infrastructure alone.
