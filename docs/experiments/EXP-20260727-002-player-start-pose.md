# Player start-pose static cross-check

**Date:** 2026-07-27
**Evidence:** `EV-20260727-003`
**Scope:** `AddStartPos`, `CcAxisRot`, `CcMatrixRot`, and player spawn events

## Question

Determine whether the three authored rotation values are degrees or radians,
their exact axis order and signs, and whether a mission start is a room-local
or absolute world transform. No original asset names, setup text, binary
payload, or local installation path may enter the public record.

## Method

The existing Ghidra headless reports were rechecked at the recovered function
boundaries. Rizin independently disassembled working copies of the same
reference modules and confirmed the relevant x87 trigonometric instructions
and call targets. Binary Ninja Free was launched for a planned third manual
view, but window-control approval expired before a file was opened; the empty
session was closed and contributed no evidence.

No game binary was executed. All analysis used read-only working copies, and
all generated reports and tool databases remain in ignored local directories.

## Result

`AddStartPos("room", coord3d(x,y,z), coord3d(rx,ry,rz))` copies the position
components without swizzle or scale. `CcAxisRot::Set` stores `rx`, `ry`, and
`rz` directly. The mode field remains the constructor default of zero.

The matrix starts as identity and applies:

```text
RotateByZAxis(rz)
RotateByXAxis(rx)
RotateByYAxis(ry)
```

Each primitive passes its input directly to `FSIN`/`FCOS`. The inverse
matrix-to-axis path uses `FPATAN` and stores the result without a conversion
factor. The angles are therefore radians.

The confirmed legacy row-vector matrix is converted to the port's
column-vector convention by:

```text
Rruntime = B * transpose(Rlegacy) * inverse(B)
truntime = B * tlegacy * runtimeUnitsPerSourceUnit
```

For identity `B`, positive single-axis fixtures produce:

- `rx`: `+Y` toward `+Z`;
- `ry`: `+X` toward `+Z` and `+Z` toward `-X`;
- `rz`: `+X` toward `+Y`.

The selected `CcOrientation` is copied as a complete record. Player spawn sends
position, matrix rotation, and room membership as separate events. The vehicle
copies position directly and constructs its quaternion directly from the
matrix; no room transform is applied. Runtime semantics are therefore an
absolute world pose plus separate room membership.

## Confidence and remaining unknowns

Confidence is 3/3 for field order, radians, mode zero, Z-X-Y call order, matrix
signs, and absolute-world runtime semantics. The physical length unit remains
unknown, so the default policy continues to use one runtime unit per source
unit without calling it a metre. Aircraft-specific pitch/yaw/roll labels and
the visual nose axis are also intentionally unassigned.

## Portable acceptance

The implementation uses explicit primitive equations rather than a generic
Euler helper. Data-less tests cover zero/identity, positive quarter turns on
all three axes, a mixed-angle Z-X-Y composition, basis conjugation, unit scale,
maximum finite input, non-finite rejection, and singular-basis rejection.
