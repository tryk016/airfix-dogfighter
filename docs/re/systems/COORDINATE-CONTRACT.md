# Coordinate, matrix, and winding contract

**State:** static contract confirmed and portable conversion implemented

**Evidence:** `EV-20260721-030`

## Source basis

The recovered engine uses a right-handed basis with `+X` right, `+Y` up, and
`+Z` forward. `CcPolyVertex::Project` at `Cc.dll` RVA `0x0000CCE0` divides X and
Y by positive Z, adds projected X to screen center, and subtracts projected Y
from screen center. `CcMesh::RecalcVertexNormals` uses `(0, 1, 0)` as its
degenerate fallback, independently supporting Y-up.

This is a source/runtime geometry convention. Metal clip-space depth, viewport
Y mapping, FOV, and near/far projection belong to the camera/backend layer and
must not be baked into decoded CCF vertices.

## Orientation matrices

The CCF `0xF070/0xF050` orientation contains three sequential `0xF040` vectors.
The loader at RVA `0x00029DA0` copies them to three contiguous 12-byte matrix
columns. `Cc3Matrix::SetCol` (`0x0002B5E0`) writes contiguous values, while
`SetRow` (`0x0002B5C0`) writes with a 12-byte stride.

Legacy application is row-vector algebra:

```text
legacyApply(v) = { dot(v, c0), dot(v, c1), dot(v, c2) }
```

`Cc3Matrix::operator*` at RVA `0x0002B670` confirms that order. A conventional
runtime column-vector matrix is therefore the transpose of the mathematical
legacy matrix. With a source-to-runtime basis `B`, the adapter is:

```text
runtimeRotation = B * transpose(legacyMatrix) * inverse(B)
```

The raw three CCF columns remain preserved as evidence. They are not copied
directly into a column-vector shader matrix.

## Triangle winding and normals

`CcMesh::RecalcVertexNormals` at RVA `0x00012E90` computes:

```text
n = normalize(cross(p2 - p0, p1 - p0))
```

`CcMesh::InvertNormals` at RVA `0x00012350` only swaps indices 1 and 2.
Visibility culling at RVA `0x0001FDD0` uses the same reverse-cross convention.
For a source-to-runtime basis with negative determinant, conversion swaps
indices 1 and 2 exactly once so the source-facing convention survives the
reflection. Identity conversion preserves index order.

The asset layer does not select `MTLWinding`: legacy projection flips screen Y,
and the final projection/viewport can change parity again. The first backend
diagnostic runs with culling disabled, visualizes front-facing state, and then
locks the encoder setting in the render contract.

The API-neutral implementation is `src/airfix/render/LegacyGeometry.*`. It
normalizes the recovered reverse-cross result and uses source `+Y` for a
degenerate triangle. Under a general basis `B`, that normal fallback follows
the inverse transpose, then normalization; a negative determinant is already
handled by the one index swap. Checked conversion rejects non-finite values,
singular bases, invalid scale, resource-limit violations, and out-of-range
indices.

## UVs, units, and unresolved fields

- CCF `0xF060` stores `[u0,v0,u1,v1,u2,v2]`; each pair belongs to the matching
  triangle-index ordinal. The loader does not transform these values.
- Default conversion preserves UVs exactly. A V flip is an explicit policy but
  remains disabled until a private textured-model diagnostic proves the source
  V origin.
- GTI rows are decoded in stored order, but that alone does not prove the CCF V
  origin.
- No physical world unit is yet proven. The default scale is one runtime unit
  per source unit and is not described as metres.
- The mesh float currently named `scalar` is preserved raw. Matrix-mode runtime
  code does not prove that it should scale vertices.
- Optional per-vertex vectors do not yet have proven normal/tangent semantics;
  faithful face normals are recomputed with the legacy formula.

## Data-less acceptance fixtures

The portable contract is protected with asymmetric identity geometry, explicit
row/column matrix application, a +90-degree X rotation, the recovered reverse
cross, a reflected basis that requires one index swap, exact UV preservation
plus opt-in V flip, and invalid basis/float/index/limit cases. No original mesh
or derived geometry belongs in the public fixtures.
