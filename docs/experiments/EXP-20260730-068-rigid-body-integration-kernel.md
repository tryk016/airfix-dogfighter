# EXP-20260730-068: CcRigidBody integration kernel

**Date:** 2026-07-30
**Evidence ID:** `EV-20260730-004`
**Status:** complete static kernel report plus policy-conditioned research
oracle; no product runtime code changed
**Decision:** NO-GO for a bit-parity C++20 kernel until the live x87 control
word or an explicit portable numeric policy is accepted; GO for using the
recovered layout and instruction schedule as a policy-conditioned test oracle
**Reference build:** `Cc.dll` SHA-256
`18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF`

## Result

The variable state owned by `CcRigidBody` is exactly 13 consecutive
binary32 values beginning at object offset `+0x40`:

```text
position[3], quaternion(w,x,y,z), linearMomentum[3], angularMomentum[3]
```

`CcODE::EulerODE` asks the virtual `Derive` method for all 13 derivatives,
updates each state element with one x87 `dt * derivative + state` expression
and one binary32 store, then invokes `PostODE`. `CcRigidBody::PostODE`
normalizes only the quaternion. It does not refresh the auxiliary matrices or
velocities.

The native AirCraft refresh surrounds that primitive as follows:

```text
EulerODE(previous accumulators, dt)
ResetForceAndTorque()
CalcAuxiliary()
prepare force and torque for the next EulerODE
```

Consequently:

- `Derive` overwrites the auxiliary rotation, world inverse inertia, linear
  velocity, and angular velocity from the pre-integration state;
- the Euler loop then changes the 13-float state;
- `PostODE` normalizes the changed quaternion;
- the auxiliaries remain stale until the explicit `CalcAuxiliary`;
- the reset writes six positive-zero DWORDs before the post-step auxiliary
  refresh; and
- force and torque prepared later are consumed on the next Euler step.

The algebra and every material binary32 spill are statically recoverable.
Bitwise live parity is not yet recoverable because the functions do not set
the process-wide x87 precision-control, rounding-control, or exception-mask
state. A startup-compatible `PC=53, RC=nearest-even` condition is useful for
conditional vectors, but it is not a branch-time observation. No game,
debugger, GUI, original resource, or executable path was run in this
experiment.

A 2026-08-01 follow-up implements the permitted policy-conditioned oracle as
offline Python research tooling. It does not change this report's confidence,
select a policy for the game, or lift the runtime NO-GO.

## Sources and method

Ghidra 12.1.2 is the canonical decompiler and instruction source. The
hash-verifying headless wrapper selected the requested addresses from an
ignored working copy and an ignored copy of the existing project. It produced
address-selected decompilation, instruction, incoming-reference, and constant
reports without publishing a source or project path.

Rizin 0.9.1 with `rzpipe` 0.6.2 independently analyzed the same hash. The
normalized reports contain path-free PE metadata, function boundaries,
instructions, calls, data references, empty CFG fields, and optional bundled
`rz-ghidra` pseudocode. No control-flow claim relies on those empty CFG fields.
Rizin's instruction discovery and Ghidra agree on all reported boundaries. The
optional Rizin pseudocode recovers the same broad data flow but mis-types
several MSVC hidden return-object parameters; it is secondary to the
instruction stream.

The repeatable public entry points are:

```powershell
tools/Invoke-GhidraAnalysis.ps1 `
  -ProgramName Cc.dll -ReuseProject `
  -PostScript ExportAddressFunctions.java `
  -PostScriptArguments <VA-list> `
  -ReportSuffix rigid-body-kernel-decomp

tools/Invoke-GhidraAnalysis.ps1 `
  -ProgramName Cc.dll -ReuseProject `
  -PostScript ExportFunctionInstructions.java `
  -PostScriptArguments <VA-list> `
  -ReportSuffix rigid-body-kernel-instructions

tools/Invoke-GhidraAnalysis.ps1 `
  -ProgramName Cc.dll -ReuseProject `
  -PostScript ExportMemoryValues.java `
  -PostScriptArguments 1004A0D0 1004A6E4 1004A6E8 1004A6EC `
  -ReportSuffix rigid-body-constants

tools/Invoke-RizinAnalysis.ps1 `
  -InputFile <ignored-working-copy> `
  -ExpectedSha256 18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF `
  -FunctionId <stable-function-id> -Rva <RVA> `
  -RizinHome <verified-rizin-or-cutter-root> `
  -PythonPath <verified-rzpipe-python> `
  -SleighHome <verified-bundled-sleigh-root>
```

Only the public scripts and conclusions are repository material. Generated
reports, tool databases, binaries, local paths, and private resources remain
ignored.

## Address and boundary ledger

`Cc.dll` uses image base `0x10000000`, so RVA is `VA - 0x10000000`.

| Function | VA range | RVA range | Ghidra/Rizin |
|---|---|---|---|
| vector-add helper | `[0x10011200,0x10011243)` | `[0x00011200,0x00011243)` | exact |
| `CcODE::CcODE` | `[0x1002A410,0x1002A420)` | `[0x0002A410,0x0002A420)` | exact |
| `CcODE::EulerODE` | `[0x1002A420,0x1002A484)` | `[0x0002A420,0x0002A484)` | exact |
| `CcRigidBody::CcRigidBody` | `[0x1002A6A0,0x1002A71D)` | `[0x0002A6A0,0x0002A71D)` | exact |
| `CcRigidBody::ResetForceAndTorque` | `[0x1002A720,0x1002A747)` | `[0x0002A720,0x0002A747)` | exact |
| `CcRigidBody::AddMassiveBlock` | `[0x1002A750,0x1002A7CB)` | `[0x0002A750,0x0002A7CB)` | exact |
| `CcRigidBody::AddMassiveParticle` | `[0x1002A7D0,0x1002A889)` | `[0x0002A7D0,0x0002A889)` | exact |
| `CcRigidBody::PostODE` | `[0x1002A890,0x1002A898)` | `[0x0002A890,0x0002A898)` | exact |
| `CcRigidBody::Derive` | `[0x1002A8A0,0x1002AB18)` | `[0x0002A8A0,0x0002AB18)` | exact |
| `CcRigidBody::CalcAuxiliary` | `[0x1002AB20,0x1002ABE0)` | `[0x0002AB20,0x0002ABE0)` | exact |
| `Cc3Matrix::operator*(matrix)` | `[0x1002B230,0x1002B296)` | `[0x0002B230,0x0002B296)` | exact |
| `Cc3Matrix::Invert` | `[0x1002B4F0,0x1002B51E)` | `[0x0002B4F0,0x0002B51E)` | exact |
| `Cc3Matrix::Transpose` | `[0x1002B610,0x1002B66C)` | `[0x0002B610,0x0002B66C)` | exact |
| `Cc3Matrix::operator*(coord)` | `[0x1002B670,0x1002B6E1)` | `[0x0002B670,0x0002B6E1)` | exact |
| `CcQuaternion::Normalize` | `[0x1002C2C0,0x1002C30E)` | `[0x0002C2C0,0x0002C30E)` | exact |
| `CcQuaternion::TransformToMatrix` | `[0x1002C310,0x1002C3FA)` | `[0x0002C310,0x0002C3FA)` | exact |
| `CcQuaternion::operator*(float)` | `[0x1002C400,0x1002C455)` | `[0x0002C400,0x0002C455)` | exact |

Rizin found each listed entry automatically in the export-rich `Cc.dll`. Its
normalized first instruction, terminal return or tail jump, and
end-exclusive boundary match Ghidra. Rizin records the two allocation calls in
`EulerODE`, the helper call graph in `Derive` and `CalcAuxiliary`, and the
constructor vtable data reference. Ghidra's incoming-reference export separately
records the `Derive`/`PostODE` vtable references and supplies the
recovered public C++ names and the clearest hidden-return calling convention.

## Object and state layout

All offsets are from `CcRigidBody*`. `M[r,c]` below uses a zero-based row and
column index.

| Offset | Width | Proven meaning | Initialization and use |
|---:|---:|---|---|
| `+0x00` | 4 | `CcRigidBody` vtable | constructor writes `0x1004A0C8` |
| `+0x04` | 4 | ODE state count | constructor writes `0x0000000D` |
| `+0x08` | 4 | ODE state offset | constructor writes `0x00000040` |
| `+0x0C` | 4 | not read by this kernel | not initialized here |
| `+0x10` | 8 | total mass, binary64 | zeroed by constructor; accumulated from binary32 mass contributions |
| `+0x18..+0x3B` | 36 | 3x3 body inverse-inertia matrix during integration | constructed as inertia, then inverted in place upstream |
| `+0x3C` | 4 | angular-momentum damping coefficient | constructor writes `+0.0f` |
| `+0x40..+0x48` | 12 | position `(x,y,z)` | first three state floats |
| `+0x4C..+0x58` | 16 | quaternion `(w,x,y,z)` | next four state floats; not initialized by this constructor |
| `+0x5C..+0x64` | 12 | linear momentum `P` | next three state floats |
| `+0x68..+0x70` | 12 | world angular momentum `L` | final three state floats |
| `+0x74..+0x97` | 36 | quaternion rotation matrix `R` | constructor writes identity; refreshed by `Derive`/`CalcAuxiliary` |
| `+0x98..+0xBB` | 36 | world inverse inertia `W` | not initialized by constructor; refreshed by `Derive`/`CalcAuxiliary` |
| `+0xBC..+0xC4` | 12 | linear velocity `v` | not initialized by constructor; refreshed from `P / mass` |
| `+0xC8..+0xD0` | 12 | world angular velocity `omega` | not initialized by constructor; refreshed from `L * W` |
| `+0xD4..+0xDC` | 12 | accumulated world force `F` | integer-zeroed only by explicit reset |
| `+0xE0..+0xE8` | 12 | accumulated world torque `T` | integer-zeroed only by explicit reset |

The state span is exactly `13 * 4 = 0x34` bytes, `[+0x40,+0x74)`.
`Derive` computes both candidate-state base pointers as
`argument - stateOffset`; this makes its field references work for any
13-float state buffer with the native layout, while all auxiliary and
configuration writes still target the owning object.

The constructor is deliberately incomplete as a simulation-state initializer:
it does not initialize the quaternion, world inverse inertia, velocities,
force, or torque. A later pure API must accept a fully initialized state,
parameters, and accumulators; it must not treat the C++ constructor listing as
a safe default-state recipe.

## Mass and inertia lifecycle

`AddMassiveBlock` and `AddMassiveParticle` establish that `+0x10` is mass and
that `+0x18` initially accumulates a body inertia tensor:

- mass contribution is loaded as binary32, added to the binary64 field by x87,
  and stored back as binary64;
- block diagonal terms use exact binary32 constant `0x3DAAAAAB`, the encoded
  approximation of `1/12`;
- particle terms match the symmetric tensor
  `m * (dot(r,r) I - outer(r,r))`, including negative off-diagonals; and
- every updated tensor element is stored as binary32.

The AirCraft constructor at `[0x10001EA0,0x10002288)` adds two blocks and one
particle to the subobject at `AirCraft+0x238`. At `0x1000200D` it addresses
`AirCraft+0x250`, exactly `body+0x18`, and the imported call at `0x10002013`
invokes `Cc3Matrix::Invert`, which replaces that matrix in place. This proves
that `Derive` and `CalcAuxiliary` consume body **inverse** inertia, not the
pre-inversion tensor.

No mass validity guard exists in either function. `1.0 / mass` uses exact
binary64 constant bits `0x3FF0000000000000`. A zero, negative, non-finite, or
unconfigured mass follows the current x87 environment rather than a portable
validation policy.

The dedicated Ghidra memory-value report resolves all four requested DWORDs:

| VA | Little-endian bytes | Interpretation |
|---:|---|---|
| `0x1004A0D0` | `00 00 80 3F` | binary32 `1.0f`, bits `0x3F800000` |
| `0x1004A6E4` | `AB AA AA 3D` | binary32 block factor, bits `0x3DAAAAAB` |
| `0x1004A6E8` | `00 00 00 00` | low DWORD of binary64 `1.0` |
| `0x1004A6EC` | `00 00 F0 3F` | high DWORD of binary64 `1.0` |

The last two rows concatenate in little-endian DWORD order to exact binary64
bits `0x3FF0000000000000`. This avoids inferring constant values from
instruction addresses alone.

## Native matrix and quaternion conventions

The nine matrix floats use column-major physical storage:

```text
offset(M[r,c]) = base + 4 * (r + 3*c)
```

`SetCol(c,x,y,z)` writes the three consecutive values at `base + 12*c`.
Matrix multiplication is the conventional `C = A * B` under that indexing.
The coordinate overload, despite its C++ spelling, computes a row-vector
product `v * M`.

For quaternion `q=(w,x,y,z)`, `TransformToMatrix` produces:

```text
R00 = 1 - 2*(y*y + z*z)
R01 = 2*(x*y - w*z)
R02 = 2*(w*y + x*z)

R10 = 2*(x*y + w*z)
R11 = 1 - 2*(x*x + z*z)
R12 = 2*(y*z - w*x)

R20 = 2*(x*z - w*y)
R21 = 2*(w*x + y*z)
R22 = 1 - 2*(x*x + y*y)
```

This is the Hamilton `(w,x,y,z)` active local-to-world rotation matrix when
read with conventional column vectors. Native coordinate helpers use row
vectors, so the kernel forms and applies:

```text
W     = R * I_body_inverse * transpose(R)
omega = L * W
```

The same convention is independently supported by the quaternion derivative:

```text
qdot = 0.5 * ((0, omega) Hamilton-multiplied-on-the-left by q)
```

or, expanded before native spills:

```text
qdot.w   = -0.5 * dot(omega, q.xyz)
qdot.xyz =  0.5 * (q.w * omega + cross(omega, q.xyz))
```

These matrix/quaternion identities have medium, static-only confidence under
the recovered instruction condition. The labels “local-to-world”, “world angular
momentum”, and “body inverse inertia” also agree with the upstream in-place
inversion and force/collision uses, but remain static semantic
identifications, not measured physical units.

## Exact EulerODE schedule

If `count == 0`, `EulerODE` returns without allocation, virtual calls, or
state writes. For `count > 0` it:

1. allocates `count * 4` bytes;
2. sets `S = this + stateOffset`;
3. calls virtual slot 0 as `Derive(S,D)`;
4. for `i=0..count-1`, executes:

   ```text
   FLD   dt
   FMUL  D[i]
   FADD  S[i]
   FSTP  S[i]
   ```

5. calls virtual slot 1 as `PostODE()`;
6. deletes `D`.

`D` is a distinct heap allocation in this path and cannot alias `S`. Each
element has exactly one final binary32 store; the multiplication and addition
remain on the x87 stack between loads and that store. Precision control can
round both arithmetic instructions. Rounding control affects those
instructions and the final `FSTP`.

For `CcRigidBody`, virtual slot 0 is `Derive` and slot 1 is `PostODE`.
`PostODE` is a two-instruction tail jump from `this+0x4C` to
`CcQuaternion::Normalize`.

## Exact Derive schedule

The following notation makes every explicit native binary32 spill visible:

- `pc(op)` is the current x87 result after the instruction under the live
  precision and rounding controls;
- `s32(x)` is the following binary32 memory store under the live rounding
  control;
- a bit copy performs no floating-point operation.

### Auxiliary refresh

Given candidate state `(p,q,P,L)`:

1. build `R(q)` with the helper schedule below and bit-copy it to `+0x74`;
2. compute retained `inverseMass = pc(binary64(1.0) / mass)`;
3. for `i=x,y,z`, write
   `v[i] = s32(pc(inverseMass * P[i]))`; each component has its own store;
4. bit-transpose `R` into a temporary;
5. compute `M = R * I_body_inverse`, with one store per matrix element;
6. compute `W = M * transpose(R)`, again with one store per element;
7. bit-copy `W` to `+0x98`;
8. compute `omega = L * W`, with one store per component;
9. bit-copy `v` to `D.position`.

`Derive` therefore mutates the owning object's auxiliaries even when the
candidate state is an external buffer.

### Quaternion derivative

The scalar term is evaluated and stored in this exact order:

```text
s = s32(negate(pc(pc(pc(q.y * omega.y) +
                        pc(q.z * omega.z)) +
                     pc(omega.x * q.x))))
```

The cross-product terms are each separately stored:

```text
c.x = s32(pc(pc(q.z * omega.y) - pc(q.y * omega.z)))
c.y = s32(pc(pc(q.x * omega.z) - pc(q.z * omega.x)))
c.z = s32(pc(pc(q.y * omega.x) - pc(omega.y * q.x)))
```

The scalar-times-omega terms are also separately stored:

```text
v.x = s32(pc(q.w * omega.x))
v.y = s32(pc(q.w * omega.y))
v.z = s32(pc(q.w * omega.z))
```

The vector-add helper loads `c[i]`, adds memory operand `v[i]`, and stores each
sum:

```text
u[i] = s32(pc(c[i] + v[i]))
```

Finally, `CcQuaternion::operator*(float)` loads exact binary32 `0.5f`,
multiplies each stored component of `(s,u.x,u.y,u.z)`, and stores each
derivative component:

```text
D.quaternion[i] = s32(pc(0.5f * storedQuaternionTerm[i]))
```

The compact Hamilton equation is semantically correct, but a portable
implementation that collapses these stores or contracts operations is not the
native numeric schedule.

### Linear and angular momentum derivatives

The accumulated force is bit-copied:

```text
D.linearMomentum = F
```

There is no linear force damping in `Derive`. The coefficient at `+0x3C`
damps angular momentum:

```text
dx = s32(pc(damping * L.x))
dy = s32(pc(damping * L.y))
dz = pc(damping * L.z)          // retained, not stored

D.angularMomentum.x = s32(pc(T.x - dx))
D.angularMomentum.y = s32(pc(T.y - dy))
D.angularMomentum.z = s32(pc(T.z - dz))
```

The X/Y versus Z asymmetry is instruction-backed. Replacing it with one
uniform vector expression changes valid binary32 outputs.

## TransformToMatrix store schedule

The final formulas above hide intentional temporaries. Under native x87:

- `z*z` remains retained across the diagonal calculations;
- `y*y` and `x*x` are each spilled to binary32;
- `x*y` and `w*z` remain retained for both `R10` and `R01`;
- `x*z` and `w*y` are each spilled before `R20` and reused as stored values
  for `R02`;
- `y*z` and `w*x` remain retained for both `R21` and `R12`; and
- every final matrix element has its own binary32 store.

In physical store order:

| Offset | Element | Native staged expression |
|---:|---|---|
| `+0x00` | `R00` | `s32(1 - 2*(s32(y*y) + retained(z*z)))` |
| `+0x04` | `R10` | `s32(2*(retained(w*z) + retained(x*y)))` |
| `+0x08` | `R20` | `s32(2*(s32(x*z) - s32(w*y)))` |
| `+0x0C` | `R01` | `s32(2*(retained(x*y) - retained(w*z)))` |
| `+0x10` | `R11` | `s32(1 - 2*(s32(x*x) + retained(z*z)))` |
| `+0x14` | `R21` | `s32(2*(retained(w*x) + retained(y*z)))` |
| `+0x18` | `R02` | `s32(2*(s32(w*y) + s32(x*z)))` |
| `+0x1C` | `R12` | `s32(2*(retained(y*z) - retained(w*x)))` |
| `+0x20` | `R22` | `s32(1 - 2*(s32(x*x) + s32(y*y)))` |

Here each addition, subtraction, and doubling is still an x87 operation
subject to precision control before the shown final store.

## Matrix helper schedules

For `C=A*B`, each element is accumulated in unusual `k=0,2,1` order:

```text
C[r,c] = s32(pc(
    pc(pc(B[0,c] * A[r,0]) + pc(B[2,c] * A[r,2]))
    + pc(A[r,1] * B[1,c])))
```

The helper stores elements in row traversal order
`(0,0),(0,1),(0,2),(1,0),...`, even though their physical representation is
column-major.

For the row-vector coordinate product `out = v*M`:

```text
out.x = s32(pc(pc(M[1,0]*v.y + M[2,0]*v.z) + M[0,0]*v.x))
out.y = s32(pc(pc(M[1,1]*v.y + M[0,1]*v.x) + M[2,1]*v.z))
out.z = s32(pc(pc(M[1,2]*v.y + M[0,2]*v.x) + M[2,2]*v.z))
```

Each product above is a distinct x87 multiply. `Transpose` itself performs
only DWORD copies into a hidden return object.

## Exact Normalize schedule

`CcQuaternion::Normalize` loads all four original components and evaluates:

```text
sum = pc(pc(pc(pc(q.w*q.w) + pc(q.x*q.x))
                + pc(q.y*q.y))
                + pc(q.z*q.z))
norm = pc(sqrt(sum))
scale = pc(binary64(1.0) / norm)
```

It retains `scale`, discards the four loaded originals from the x87 stack,
then processes memory in `(w,x,y,z)` order:

```text
q.w = s32(pc(scale * q.w))
q.x = s32(pc(scale * q.x))
q.y = s32(pc(scale * q.y))
q.z = s32(pc(scale * q.z))
```

`scale` is computed before any component write and remains live across all
four stores. There is no zero, finite, epsilon, or already-normalized gate.
With masked default exceptions a zero quaternion enters divide-by-zero and
invalid-operation behavior; exact NaN payload and signed-zero results remain
environment-dependent and are not promoted to a portable contract.

## Synthetic exact-bit vectors

Unless a row says otherwise, the expected values are invariant for PC24, PC53,
and PC64 under round-to-nearest/even because every shown arithmetic result is
exact. Hex values are binary32 bits; mass is the exact binary64 value shown.

### V1: stationary identity

```text
mass       = 1.0 (0x3FF0000000000000)
I_body_inv = identity
damping    = 0x00000000
state      = p=(0,0,0), q=(0x3F800000,0,0,0), P=(0,0,0), L=(0,0,0)
F,T        = all 0
dt         = 0x3C449BA6 (binary32 0.012)
```

Under round-to-nearest/even, `Derive` is zero in every lane except that the
explicit `FCHS` makes `D.quaternion.w` negative zero (`0x80000000`). `R` and
`W` are exact identity, both velocities are positive zero, Euler leaves the
state unchanged, normalization leaves identity unchanged, and reset leaves
six positive-zero accumulators.

### V2: exact translation and post-step auxiliary refresh

```text
mass       = 2.0 (0x4000000000000000)
I_body_inv = identity
damping    = 0
p          = (0,0,0)
q          = (0x3F800000,0,0,0)
P          = (0x40000000,0xC0800000,0x41000000)       // 2,-4,8
L          = (0,0,0)
F          = (0x3F800000,0xC0000000,0x40800000)       // 1,-2,4
T          = (0,0,0)
dt         = 0x3F000000                               // 0.5
```

Before integration:

```text
v = (0x3F800000,0xC0000000,0x40800000)                // 1,-2,4
```

After `EulerODE`:

```text
p = (0x3F000000,0xBF800000,0x40000000)                // 0.5,-1,2
q = (0x3F800000,0,0,0)
P = (0x40200000,0xC0A00000,0x41200000)                // 2.5,-5,10
L = (0,0,0)
```

After reset plus `CalcAuxiliary`:

```text
v = (0x3FA00000,0xC0200000,0x40A00000)                // 1.25,-2.5,5
R = W = identity
omega = F = T = (0,0,0)
```

This vector detects an implementation that incorrectly refreshes auxiliaries
inside `EulerODE` or consumes newly prepared force in the same step.

### V3: matrix/quaternion convention

```text
mass       = 1.0 (0x3FF0000000000000)
q          = (0x3F000000,0x3F000000,0x3F000000,0x3F000000)
I_body_inv = diagonal(0x40000000,0x40800000,0x41000000) // 2,4,8
L          = (0x3F800000,0x40000000,0x40400000)         // 1,2,3
P          = (0,0,0)
```

Under round-to-nearest/even:

```text
R rows = [0,0,1], [1,0,0], [0,1,0]
R physical DWORDs =
  00000000 3F800000 00000000
  00000000 00000000 3F800000
  3F800000 00000000 00000000

W = diagonal(0x41000000,0x40000000,0x40800000)        // 8,2,4
omega = (0x41000000,0x40800000,0x41400000)            // 8,4,12
```

This distinguishes column-major storage plus native row-vector application
from the common but wrong `M * columnVector` reconstruction.

### V4: quaternion derivative and normalization

```text
mass       = 1.0 (0x3FF0000000000000)
q          = (0x3F800000,0x00000000,0x00000000,0x00000000)
I_body_inv = identity
L          = (0x40000000,0x00000000,0x00000000)       // omega=(2,0,0)
damping,F,T,P,p = positive binary32 zero
dt         = 0x3F800000                               // 1
```

With the explicitly positive-zero inputs shown above, `Derive` must produce
quaternion derivative
`(0x80000000,0x3F800000,0x00000000,0x00000000)`: `FCHS` makes the
scalar zero negative. Euler first writes unnormalized `q=(1,1,0,0)`. Under
PC24/RNE, PC53/RNE, and PC64/RNE, `PostODE` then stores:

```text
q = (0x3F3504F3,0x3F3504F3,0,0)
```

The vector detects right-multiplication, `(x,y,z,w)` field order, skipped
`PostODE`, and a normalization performed before the Euler update.

### V5: X/Y damping spill versus retained Z

```text
mass    = 1.0 (0x3FF0000000000000)
q       = (0x3F800000,0,0,0)
I_body_inv = identity
damping = 0x3DCCCCCD                                  // binary32 0.1
L.x=L.y=L.z = 0x3F000011
T.x=T.y=T.z = 0x3F800000                              // 1
P,p,F   = 0
```

Under PC53/RNE or PC64/RNE, the X/Y intermediate stores are:

```text
s32(damping * L.x) = s32(damping * L.y) = 0x3D4CCCE8
```

and the derivative bits are:

```text
D.L.x = 0x3F733332
D.L.y = 0x3F733332
D.L.z = 0x3F733331
```

Making all three lanes use a stored product incorrectly changes Z to
`0x3F733332`. Retaining all three products incorrectly changes X and Y to
`0x3F733331`.

Under PC24/RNE the retained Z multiply is itself precision-rounded to the
same value as the explicit X/Y binary32 stores, so all three derivative lanes
are `0x3F733332`. V5 distinguishes the native spill asymmetry only when the
live arithmetic precision retains more than 24 significant bits.

### V6: Euler PC24/PC53/PC64 discriminator

This is an isolated `CcODE::EulerODE` lane test with a synthetic virtual
`Derive` supplying the stated derivative; it does not invoke the full
`CcRigidBody::Derive`. For that one state lane:

```text
state      = 0x3F800000
dt         = 0x39803009
derivative = 0x397FA012
```

The product is exact before precision-control rounding:

```text
dt * derivative = 2^-24 + 162 * 2^-71
exact sum        = 1 + 2^-24 + 162 * 2^-71
```

Expected final binary32:

| x87 condition | Stored bits | Reason |
|---|---:|---|
| PC24, nearest/even | `0x3F800000` | the multiply rounds to `2^-24`; the PC24 addition is a tie and selects even lower |
| PC53, nearest/even | `0x3F800000` | the PC53 addition rounds to the exact binary32 midpoint; the final tie selects even lower |
| PC64, nearest/even | `0x3F800001` | PC64 retains enough of the positive offset to select the upper binary32 |
| PC24, PC53, or PC64, downward/toward-zero for this positive value | `0x3F800000` | directed rounding selects the lower value |
| PC24, PC53, or PC64, upward | `0x3F800001` | directed rounding selects the upper value |

This is a static discriminator, not evidence that any precision mode was live
in the game at this branch.

## Aliasing and observable stores

The observed native path supplies separate state and derivative allocations.
The matrix operations use compiler-generated return temporaries. The relevant
helpers also stage complete results before copying them to their hidden return
objects, but no public pure API should depend on undocumented overlap:

- require state input and derivative scratch to be logically distinct;
- compute the complete derivative before updating state;
- retain the pre-step versus post-step auxiliary distinction;
- normalize the updated quaternion in place;
- expose accumulator clearing as the explicit native AirCraft step phase; and
- do not make the kernel own a scheduler, renderer, collision system, or live
  input source.

## Synthetic test plan

A later implementation must start with an exact software reference that names
its x87 policy. The minimum test matrix is:

1. compile-time layout assertions for all offsets and the 13-float state
   ordering;
2. V1 stationary identity, including positive-zero accumulator stores;
3. V2 translation, checking pre-Euler auxiliaries, updated state, explicit
   reset, and post-Euler auxiliaries separately;
4. V3 quaternion matrix physical storage, `R*I*R^T`, and row-vector `L*W`;
5. V4 left-Hamilton quaternion derivative and post-update normalization;
6. V5 exact X/Y-versus-Z damping spill discriminator;
7. V6 under PC24/RNE, PC53/RNE, and PC64/RNE reference policies;
8. one-lane signed-zero, smallest-subnormal, largest-finite, infinity, and
   quiet/signaling-NaN cases for each operation group, without asserting a
   native NaN payload until exception masks and control word are captured;
9. mass `+0`, `-0`, finite negative, infinity, and NaN policy tests;
10. zero quaternion and non-finite quaternion policy tests;
11. randomized finite differential tests against an instruction-scheduled
    arbitrary-precision reference for PC24, PC53, PC64, and every RC mode;
12. tests that forbid fused multiply-add or removal/movement of any documented
    binary32 spill;
13. a no-alias API contract test plus an internal scratch-poison test proving
    no derivative lane is consumed before all derivatives are formed; and
14. repeatability under optimization and on every target architecture.

Tests 8 through 10 must distinguish “native behavior under a captured x87
environment” from “portable fail-closed policy”. They must not silently turn
the constructor's uninitialized fields into zeros or normalize a quaternion
at a different phase.

## Policy-oracle implementation follow-up (2026-08-01)

`tools/re/cc_rigid_body_oracle.py` now provides the approved offline reference
model. It:

- decodes finite binary32 and binary64 inputs to exact rational values while
  retaining signed zero;
- applies an explicit `PC24`, `PC53`, or `PC64` precision and one of the four
  x87 rounding directions after every recovered arithmetic instruction;
- represents each documented binary32 spill explicitly, including the matrix
  `k=0,2,1` order and X/Y-versus-Z damping asymmetry;
- models pre-step auxiliaries, all 13 derivative and Euler lanes,
  post-update quaternion normalization, positive-zero accumulator clearing,
  and the later auxiliary refresh; and
- fails closed for non-finite inputs, invalid mass or time step, zero
  quaternion, divide-by-zero, unsupported policy, and unrepresentable stores.

The V1-V5 public tables remain restricted to round-to-nearest/even because no
directed-rounding full-vector table was published. V6 is computed under all
12 precision/rounding combinations. The capture validator now obtains V1-V6
expectations from the executable oracle instead of duplicating hard-coded
results. Independent tests keep the published tables as fixed fixtures, check
signed-zero and finite boundary encodings, reject unsupported domains and
shapes, and exercise deterministic repeated finite steps. No game binary,
original resource, debugger trace, native process, scheduler, or product
runtime is involved.

## Minimal later dynamic experiment

No dynamic experiment was run. If separately authorized, the minimum
experiment needed to lift the numeric NO-GO is:

[EXP-20260730-078](EXP-20260730-078-x87-runtime-policy-static-audit.md)
now rules out recognized later environment writers and additional mutator
imports in all supplied runtime modules. It does not observe RC, exception
state, external Windows/DirectX/driver code, or the consumer thread, so this
dynamic requirement remains.

1. break at `EulerODE`, `Derive`, and `Normalize` in the verified PE32 process;
2. record x87 control and status words immediately at each entry and exit;
3. record the 13 state DWORDs, mass double, body inverse inertia, damping,
   six accumulators, and `dt` before the call;
4. record derivative scratch, every post-call state DWORD, and auxiliary
   DWORD;
5. repeat with V5/V6-equivalent controlled finite inputs in an isolated
   harness or safely controlled object;
6. verify thread identity and whether any library call changes the control
   word; and
7. compare exact bytes against PC24, PC53, PC64, and directed-rounding models.

This experiment must remain separate from ordinary gameplay, owner content,
renderer timing, input sampling, and the 12 ms scheduler.

## Original static validation

- Ghidra and Rizin raw instruction bytes and end-exclusive boundaries agree
  for all 17 selected functions; all nine Ghidra reports have
  `unresolvedAddresses=0`, including the four-of-four constant report.
- `Invoke-GhidraAnalysisTests.ps1` and `Invoke-RizinAnalysisTests.ps1` pass.
- `RizinExportTests.py` passes 12/12 and `PublicBoundaryTests.py` passes.
- The public-boundary scanner passes 551 files.
- The function catalog imports as 298 rows by 14 columns with 298 unique IDs;
  changed-document links and the changed-scope local-path scan pass.
- `git diff --check` passes with only Git's existing CRLF conversion warnings.
- An independent instruction-level review closed the signed-zero, PC24,
  empty-CFG, transpose-call, self-contained-vector, and constant-report
  findings; the final re-review found no further issue.

At the time of the static report no portable source changed, so no CMake build
or runtime CTest suite was needed. No game, GUI, debugger, or dynamic process
was started.

The later oracle follow-up is registered as a synthetic CTest and is validated
with the existing capture-validator suite. Its build and repository-boundary
results are recorded in the project progress log rather than retroactively
changing the original evidence counts above.

## Decision and confidence

| Claim | Confidence | Basis |
|---|---|---|
| function boundaries and direct calls | 2/medium | static Ghidra/Rizin agreement |
| 13-float state layout and field semantics | 2/medium | constructor constants plus complete read/write data flow |
| matrix storage, multiplication, and quaternion conventions | 2/medium | helper instructions plus joined use in `Derive`/`CalcAuxiliary` |
| exact x87 instruction and binary32 store schedule | 2/medium | complete bounded instruction listings |
| force/torque/damping lifecycle | 2/medium | `Derive`, reset, accumulator helpers, and AirCraft call order |
| PC24/RNE, PC53/RNE, and PC64/RNE conditional vectors | 2/medium | exact integer/rational derivation under named conditions |
| live process-wide x87 PC/RC/exception state | unknown | not observed dynamically |
| NaN payload, signaling behavior, and trap behavior | unknown | depends on unobserved x87 environment |
| bit-identical portable C++20 parity | unknown | no implementation or controlled runtime comparison |

The report is **NO-GO** for implementing or wiring a supposedly bit-identical
pure kernel now. The structural contract is sufficient to build a
policy-conditioned oracle, but selecting PC24, PC53, PC64, a directed mode,
or a portable non-finite policy without explicit authority would be guessing.

Once the numeric policy is accepted or dynamically captured, the recovered
layout, equations, phase ordering, spill schedule, and exact discriminators
are sufficient for an isolated one-step implementation. Such a kernel should
accept state, parameters, accumulators, `dt`, and a numeric policy explicitly;
it should have no ownership of input, scheduling, collisions, rendering, or
runtime publication.
