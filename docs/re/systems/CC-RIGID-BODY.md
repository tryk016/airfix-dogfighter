# CcRigidBody integration boundary

**Status:** exact static layout and instruction schedule recovered; no portable
kernel implemented
**Evidence:** `EV-20260724-003`, `EV-20260730-004`, `EV-20260730-008`
**Reference build:** SHA-256 values in `docs/evidence/source-manifest.sha256`

This note is the durable system boundary for the `Cc.dll` rigid-body
integrator. The full address ledger, x87 spill schedule, exact-bit vectors,
confidence table, and synthetic test plan are in
[EXP-20260730-068](../../experiments/EXP-20260730-068-rigid-body-integration-kernel.md).

## State and parameters

`CcRigidBody` configures `CcODE` with `count=0x0D` and
`stateOffset=0x40`. Its state is:

```text
+0x40 position[3]
+0x4C quaternion(w,x,y,z)
+0x5C linearMomentum[3]
+0x68 angularMomentum[3]
```

The kernel parameters and derived fields are:

```text
+0x10 binary64 mass
+0x18 body inverse inertia[3x3]
+0x3C angular-momentum damping
+0x74 quaternion rotation R
+0x98 world inverse inertia W
+0xBC linear velocity
+0xC8 angular velocity
+0xD4 accumulated force
+0xE0 accumulated torque
```

The constructor does not initialize the quaternion, world inverse inertia,
velocities, force, or torque. A portable state must be supplied fully formed.

## Equations

With native column-major matrix storage and native row-vector coordinate
application:

```text
v     = linearMomentum / mass
W     = R(q) * bodyInverseInertia * transpose(R(q))
omega = angularMomentum * W

positionDerivative       = v
quaternionDerivative     = 0.5 * ((0,omega) Hamilton-left-multiplied by q)
linearMomentumDerivative = accumulatedForce
angularMomentumDerivative =
    accumulatedTorque - damping * angularMomentum
```

The compact equations are not a complete numeric implementation. Matrix
products use `k=0,2,1` accumulation, quaternion construction has named
retained and spilled intermediates, and angular damping spills X/Y products
but retains Z until subtraction. Consult the experiment before writing code.

## Native phase order

`CcODE::EulerODE` performs:

```text
Derive(state, separateDerivativeScratch)
for each of 13 lanes:
    state = binary32-store(x87(state + dt * derivative))
PostODE() -> normalize updated quaternion
```

The AirCraft path then performs:

```text
ResetForceAndTorque()
CalcAuxiliary()
prepare next-step force and torque
```

`EulerODE` does not perform the reset or post-update auxiliary refresh.
`Derive` leaves auxiliaries representing the pre-update state.

## Current decision

Static-only confidence is `2/medium`. The live x87 precision control, rounding
control, exception masks, and non-finite behavior are not observed. Therefore:

- GO for the documented layout, phase order, and policy-conditioned test
  oracle;
- NO-GO for a bit-parity C++20 kernel or any runtime integration until the
  numeric policy is explicitly selected or captured; and
- no scheduler, collision, renderer, or live-input ownership belongs in this
  isolated boundary.

The process-wide static follow-up in
[EXP-20260730-078](../../experiments/EXP-20260730-078-x87-runtime-policy-static-audit.md)
finds the executable's startup PC53 request and no recognized later
environment writer or additional mutator import in any supplied runtime
module. It narrows the remaining work to a consumer-site CW/SW capture, but it
does not establish RC, exception state, external-library behavior, or
branch-time policy. The bit-parity implementation NO-GO therefore remains.
