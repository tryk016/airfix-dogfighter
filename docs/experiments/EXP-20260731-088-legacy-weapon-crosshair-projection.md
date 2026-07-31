# EXP-20260731-088: legacy weapon crosshair projection

**Status:** implemented and locally validated

**Evidence ID:** `EV-20260731-004`

**Confidence:** high (`3`) for native function boundaries, gates, constants,
texture dimensions, first-visible-size latch, and final centring; medium (`2`)
for portable numeric parity because the original x87 control state is not yet
captured dynamically

## Question

Can the recovered weapon-crosshair sizing and centring stage consume the
native output-pixel projection without tying HUD size to the 3D render scale or
inventing an off-screen visibility rule?

## Evidence

The canonical `AfEngine.dll` working copy has SHA-256
`A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E`.
Ghidra 12.1.2 identifies:

| Function | VA range | Relevant evidence |
|---|---:|---|
| `AfWeapon::AfWeapon` | `[0x100226B0, 0x100227C2)` | zeroes both current and target crosshair dimensions, sets draw state `0x7FFFFFFF`, sets first-visible latch, and initializes aim to `{0,0,1}` |
| `AfWeapon::Activate` | `[0x10022810, 0x10022845)` | marks the weapon active and resets the first-visible-size latch |
| `AfWeapon::RenderCrosshair` | `[0x10024120, 0x1002461D)` | applies the owner/texture/camera/screen gates, world projection, size calculation, centring, and textured screen draw |

Rizin 0.9.1 independently reports the same three ranges and sizes: `274`,
`53`, and `1277` bytes. Its guessed calling convention remains non-canonical;
the MSVC class ABI and pseudocode come from Ghidra.

The read-only `Game/Types/Projectiles.type` working copy has SHA-256
`639D9F07DD954457CED364E0F1ED2732309819B493422FDAE827D00DC1D76B9F`.
Rizin string references confirm data-driven loads from
`Graphics\Ingame\HUD`: `MGsight`, `ROsight`, and `BOsight`. The existing GTI
decoder independently reports format `8` and original size `32x32` for all
three. No texture, decoded image, binary, or local working path is repository
material.

## Recovered final stage

`RenderCrosshair` begins with collision fraction `1.0`. When its aim-line BSP
query hits, it replaces that value with the returned line fraction. For local
aim-offset Z and original texture dimensions it calculates:

```text
logicalDistanceScale = 2.0 - collisionFraction * localAimOffsetZ
targetWidth  = originalTextureWidth  * logicalDistanceScale
targetHeight = originalTextureHeight * logicalDistanceScale
```

The exact `2.0f` and `0.5f` constants are stored at virtual addresses
`0x1007D1E8` and `0x1007D1F8`. On the first successful `ScalePoint` result after
construction or activation, target width/height become current width/height
and the latch clears. The final native draw uses:

```text
x = projectedX - currentWidth  * 0.5
y = projectedY - currentHeight * 0.5
```

The base method continues to calculate target dimensions separately from the
retained current dimensions. No unproved interpolation or subclass update is
added to the portable boundary.

## Portable composition

`LegacyWeaponCrosshairProjection` owns only this final state transition and
rectangle plan. It calls `projectGameplayWorldPointToOutput`, keeps the
recovered dimensions in the `640x480` UI design domain, and applies:

```text
outputSize = currentLogicalSize * layout.uiScale()
                               * userUiScalePercent / 100
```

The projected centre therefore follows Hor+, Original 4:3, and safe FOV while
size follows the independent UI scale. Render scale never enters either
calculation. Invalid dimensions, non-finite data, an out-of-range collision
fraction or UI scale, non-positive derived size, and nested screen-projection
failure reject transactionally and retain the input size state.

An off-screen or out-of-depth target is still a complete plan carrying both
labels. A later live consumer must make the hide or directional-indicator
decision; this slice does not present that port policy as original behavior.

## Verification

Portable tests cover exact `32x32` placement on the recovered canvas, the
`32..64` default aim/collision scale range, activation-latch behavior, 4:3,
16:10, 16:9, 19.5:9, 21:9, and 32:9, render scales `50`, `100`, and `200`, user
UI scale endpoints `75` and `150`, Original 4:3 pillarboxing, explicit
off-screen/depth labels, every fail-closed input class, nested projection
failure, and 4,096 allocation-counted projections.

A fresh GCC 15.2/Ninja Release build passes all `125/125` portable CTests. A
separate fresh MSVC 19.51/Ninja Release build, including the SDL3/D3D11/XAudio2
Windows product, passes all `135/135` CTests and both native product smokes.
The public repository-boundary test, all `12/12` Rizin export-normalization
tests, clang-format verification, and `git diff --check` pass.

## Decision

**GO** for the backend-neutral final crosshair rectangle/state planner and its
native-resolution/UI-scale composition.

**NO-GO** for claiming a rendered HUD or live weapon parity. Authenticated GTI
loading, selected-weapon texture binding, the changing owner pose, native aim
refresh, line collision, draw submission, and D3D11/Metal sprite passes remain
separate consumers.
