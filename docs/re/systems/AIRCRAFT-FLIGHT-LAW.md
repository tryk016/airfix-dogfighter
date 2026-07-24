# Aircraft flight-law static contract

**Status:** complete static algebra for the reference force step; not yet
implemented for parity

**Evidence:** `EV-20260724-001`, `EV-20260724-002`,
`EV-20260724-003`, `EV-20260724-004`

**Reference build:** SHA-256 values in
`docs/evidence/source-manifest.sha256`

This document is the durable, implementation-facing contract for
`FN-AIRCRAFT-00003F40`, the working
`AircraftFlightForceStep(AirCraft*, float dt)` name. It records exact branches,
constants, state transitions, and rigid-body calls recovered from the reference
executable. It does not assign aerodynamic names or physical units where the
binary does not prove them.

The main result is:

- the scheduler interval for AirCraft is 12 ms;
- `AfVehicle::ProcessEvent` multiplies the 64-bit interval by `0.001f`;
- the nominal force-step argument is therefore `dt = 0.012f` seconds
  (83 1/3 Hz);
- the method contains 15 `ApplyForce`, five `ApplyTorqueOnly`, and one
  `ApplyForceOnly` sites;
- every one of those sites and every direct state write in the method is
  mapped below; and
- the method still prepares force and torque for the *next* Euler integration,
  not the current one.

Raw decompilation, instruction listings, and scalar exports stay in the ignored
`artifacts/` tree. Only the concise contract is versioned.

## Timing and lifecycle

The active chain is:

```text
QPC
  -> Dogfighter AdvanceMainClock
  -> NfMain::SetTime / ProcessEvent
  -> NfTimeHeap::RefreshDependants
  -> NfActor::Refresh(int64 interval)
  -> AfVehicle::ProcessEvent
  -> AircraftFlightForceStep(float dt)
```

`AdvanceMainClock` converts the performance-counter delta with a `1000.0f`
factor, so the integer clock is in milliseconds. `NfTimeDependant` starts with
a 1000 ms interval, but `AfVehicle::AfVehicle` replaces it according to the
vehicle-type predicates:

| Type predicate result | Interval |
|---|---:|
| ground unit | 25 ms |
| water unit | 33 ms |
| neither | 12 ms |

The AirCraft type vtable routes both ground-unit and water-unit predicates to
the shared zero-return function at `AfEngine.dll` RVA `0x0001A930`. AirCraft
therefore uses 12 ms. `AfVehicle::ProcessEvent` converts the scheduler payload
with the `0.001f` constant at AfEngine RVA `0x0007C7C4`.

The physics order remains:

```text
EulerODE(previous force/torque, dt)
ResetForceAndTorque()
CalcAuxiliary()
AircraftFlightForceStep(dt)
collision and portal processing
post-collision refresh
```

Force and torque accumulated here are consumed by the next `EulerODE` call.
Collision impulses can change the current state independently after the force
step.

## Notation

Let `A` be the AirCraft instance and `T` its type at `A+0x524`.

```text
B       = f32[A+0x3FC]
s       = f32[A+0x98]
pitch   = f32[A+0x448]
bank    = f32[A+0x44C]
p       = vec3[A+0x278]
v       = vec3[A+0x2F4]
q       = vec4[A+0x284]
contact = u8[A+0x464]
```

`B` is copied from type field `+0x70` and scales both rigid-body mass
contributions and force equations. That join is proven; a real-world mass unit
is not.

The quaternion fields produce this exact 3x3 basis:

```text
m00 = 1 - 2(q2^2 + q3^2)
m01 = 2(q3*q0 + q2*q1)
m02 = 2(q3*q1 - q2*q0)

m10 = 2(q2*q1 - q3*q0)
m11 = 1 - 2(q1^2 + q3^2)
m12 = 2(q1*q0 + q3*q2)

m20 = 2(q2*q0 + q3*q1)
m21 = 2(q3*q2 - q1*q0)
m22 = 1 - 2(q1^2 + q2^2)
```

Define:

```text
X = (m00, m01, m02)
Y = (m10, m11, m12)
Z = (m20, m21, m22)
R(x,y,z) = x*X + y*Y + z*Z
```

`R` is the exact algebra of the helper at AirCraft RVA `0x000065B0`.
Axis names such as forward, up, and right are intentionally not assigned.

The method also transforms `v` through the transposed basis and defines:

```text
V  = v * transpose(M)
vz = max(V.z, 0)
D  = max((2 - vz) * 0.01, 0)
C  = (0.5*vz + 1) * f32[T+0xDC]
```

## Address-ordered execution skeleton

The equations below are grouped by purpose, not execution order. The actual
state-dependent order is:

1. apply the entry gate, build the basis, transform `v`, and initialize the
   local `contactAccumulator` to zero;
2. update the throttle target when `s > 0`, otherwise apply the two zero-pitch
   forces when their gate is true;
3. update `A+0x560`, then apply gravity, the conditional lift-like force,
   thrust, propeller-node animation, and pitch torque in address order;
4. branch on the **contact value from the previous probe pass**:
   - when it is zero, apply the bank torque, update `A+0x558`, apply the next
     two stabilization torques, and apply the side force;
   - when it is nonzero and bank is nonzero, apply the alternate side force;
5. update the probe cadence counter; only a probe pass calls `DisableBsp`,
   clears `contact`, and performs the seven BSP queries;
6. update and apply the too-high and water branches;
7. always call `EnableBsp`, then apply final state damping.

Consequently, the current step's thrust and propeller animation use the newly
smoothed `A+0x560`. All direct contact-gated forces/torques and the `A+0x558`
low-pass update use the contact state inherited from the preceding probe pass,
not the value that may be cleared or set later in the current step.

## Entry gate

The method returns before any other calculation when:

```text
s32[A+0x45C] > 0
OR
(s32[A+0x45C] >= 0 AND u32[A+0x458] > 1999)
```

The physical/gameplay meanings of `+0x458` and `+0x45C` remain unknown.

## Type-constructor join

`RegisterAircraftTypes` passes a name plus ten values `T1..T10` to
`AircraftTypeConstructor`:

| Tuple value | Type field | Confirmed transformation/use |
|---:|---:|---|
| `T1` | `+0xE0` | direct float; instance extents use `T1 * 0.25` |
| `T2` | `+0x78` | multiplied by `0.01`; consumed by thrust |
| `T3` | `+0xDC` | multiplied by `0.01`; consumed by force/damping |
| `T4` | `+0x70` | copied to instance `+0x3FC` as `B` |
| `T5` | `+0x48` | copied to instance `+0x3F4` and `+0x98` |
| `T6` | `+0x6C` | copied to instance `+0x3F8` and `+0x400` |
| `T7` | none | unused by the constructor |
| `T8` | `+0xE4` | direct 32-bit value, observed as 1 or 3 |
| `T9` | `+0xE8` | direct float bits |
| `T10` | `+0xEC` | direct float bits |

The constructor also scales inherited `+0xD8` by `0.01`, writes
`+0x74 = 6.0`, and creates five engine-sound dependencies.

The instance constructor adds two massive blocks with weights `0.375*B` and
one massive particle with weight `0.125*B`, then resets force and torque. This
proves the mass-scaling role of `B`, but not a kilogram conversion.

## Direct force and torque sites

The table is address-ordered. `r` is the transformed offset passed as the
second argument to `ApplyForce`; the binary does not add actor position to it.
Every row is additionally subject to the entry gate.

| Call VA | Additional condition | Exact operation |
|---:|---|---|
| `0x100042B5` | `s <= 0 && pitch == 0` | `F=R(0,-0.01*B,0)`, `r=R(2,0,0)` |
| `0x1000434C` | `s <= 0 && pitch == 0` | `F=R(0,+0.01*B,0)`, `r=R(-4,0,0)` |
| `0x100044C1` | always | `G=-0.54555553*B*(s<=0 ? 8 : 1)`, `F=(0,G,0)`, `r=R(0,0.1*D,D)` |
| `0x100045BC` | `L > 0.05*B` | `L=min(0.7*vz*B,0.2*B)`, `F=R(0,L,0)`, `r=R(0,0,D)` |
| `0x100046C8` | always | `Q=(s<=0 ? 0.5*T[0x78]*B : 2*A[0x568]*A[0x560]*T[0x78]*B)`, `F=R(0,0,Q)`, `r=0` |
| `0x100049AF` | `pitch != 0` | `K=pitch*B*C*(s<=0 ? 0.0008 : 0.008)`, `tau=R(0,0,-1) x R(0,K,0)` |
| `0x10004AB3` | `!contact && bank != 0` | `K=C*B*bank*0.014`, `tau=R(-1,0,0) x R(0,K,0)` |
| `0x10004B98` | `!contact` | `K=m01*B*C*0.024`, `tau=R(-1,0,0) x R(0,K,0)` |
| `0x10004C77` | `!contact` | `K=abs(m01*B*C*0.014)`, `tau=R(0,0,1) x R(0,K,0)` |
| `0x10004D31` | `!contact` | `K=m01*B*C*0.03`, `F=R(K,0,0)`, `r=R(0,0,-0.75)` |
| `0x10004E0D` | `contact && bank != 0` | `K=-0.02*B*bank`, `F=R(K,0,0)`, `r=R(0,0,-0.75)` |

The instruction listing at `0x10004E0B` confirms `ECX` is the rigid body at
`A+0x238` before the indirect `ApplyForce` call. This removes the ambiguity in
the decompiler expression for the final row.

## BSP probe force sites

Probe work runs when the old signed counter at `A+0x5E4` is at least 50.
Otherwise the counter increments by one and no BSP probe runs. On a probe pass:

```text
A[0x5E4] -= 50
DisableBsp()
contact = 0
... probes ...
```

The AirCraft vtable entries at offsets `0x3C` and `0x38` resolve through import
thunks to the exported `AfVehicle::DisableBsp` and `AfVehicle::EnableBsp`.
`EnableBsp` is not called immediately after the probes: the height and water
branches run first. The method then always calls `EnableBsp` before final
damping, including steps where the cadence counter did not call `DisableBsp`.

A `qualifiedHit` means:

```text
GetBspCollision() == true
AND mesh polygon != null
AND polygon[0x14] != null
AND polygon[0x14][0x5C] != 0xF
```

Let `h` be the scalar written by the line-collision query and
`k=(1-h)*B`. The range and physical unit of `h` are not yet proven.

| Call VA | Query segment (`start -> end`) | Additional condition | Exact operation |
|---:|---|---|---|
| `0x10005028` | `p -> p+R(0,-0.12,0)` | qualified, `k>0` | `ApplyForceOnly(k*Y,0)` |
| `0x10005264` | `p+R(0.2,0,0.2) -> p+R(0.2,-0.12,0.2)` | qualified, `k>0` | `F=+k*Y`, `r=R(0.2,0,0.2)` |
| `0x1000549F` | `p+R(-0.2,0,0.2) -> p+R(-0.2,-0.12,0.2)` | qualified, `k>0` | `F=+k*Y`, `r=R(-0.2,0,0.2)` |
| `0x100056E8` | `p+R(0,0,-0.2) -> p+R(0,-0.12,-0.2)` | qualified, `k>0` | `F=-k*Y`, `r=R(0,0,-0.2)` |
| `0x100058F2` | `p -> p+R(0.6,0,0.6)` | qualified, `k>0` | `F=-k*Y`, `r=R(0.2,0,0.2)` |
| `0x10005B2C` | `p -> p+R(-0.6,0,0.6)` | qualified, `k>0` | `F=+k*Y`, `r=R(-0.2,0,0.2)` |
| `0x10005DF6` | `p -> p+R(0,0.75,0)` | both wide responses absent; `m11<0`; qualified; `J>0` | `F=-J*Y`, `r=0.2*Z` |
| `0x10005E66` | same query as previous row | same branch as previous row | `tau=0.5*J*Z` |

For the final pair:

```text
J = (1-h) * dot(n,v) * B * 12
```

`n` is the three-float value returned through the collision structure. Its use
supports the working collision-normal interpretation, but the original type
name and unit have not been recovered.

The first four qualified central/small probes set `contact=1` even when `k` is
not positive. A positive response resets `A+0x5E4` to 50. The two wide probes
and the upper/inverted probe do not set `contact`.

The local value later used by final damping starts at zero. A qualified central
hit writes:

```text
contactAccumulator = (1 - hCentral) * 0.5
```

Each qualified hit among the next three small probes then applies, in address
order:

```text
contactAccumulator =
    ((1 - hCurrentProbe) + contactAccumulator) * 0.5
```

Wide and upper/inverted probes do not update this accumulator. The active water
branch later overwrites it with `0.1`.

For a qualified wide hit, and for an applied upper/inverted response:

```text
A[0x42C] *= h * (1 - abs(m10))
A[0x430] *= h * (1 - abs(m11))
A[0x434] *= h * (1 - abs(m12))
```

No clamp on those factors appears in the function.

## Height and water state

The exported predicate names establish these branches:

```text
if IsTooHigh:
    A[0x5EC] += dt
else if A[0x5EC] > 0:
    A[0x5EC] -= 2*dt

if IsInWater:
    A[0x5F0] += dt
else:
    A[0x5F0] = 0
```

Nonpositive accumulators are clamped to zero. When `A+0x5EC > 0`:

```text
A[0x430] = 0
F = (0,-B*A[0x5EC],0)
r = R(0,0.1,0.1)
ApplyForce(F,r,0)
A[0x294] = 0
A[0x29C] = 0
```

When `A+0x5F0 > 0`:

```text
A[0x430] = 0
contactAccumulator = 0.1
F = (0,+B*A[0x5F0],0)
r = R(0,0.1,0.1)
ApplyForce(F,r,0)
```

Because `dt` is seconds, `+0x5EC` and `+0x5F0` are second-based accumulators.
Their unclamped effect grows linearly for as long as the predicates remain
true.

## Throttle, propeller, and persistent state

When `s > 0`, the target throttle is updated without `dt`:

```text
A[0x444] = clamp(A[0x444] + A[0x440], 0, 1)
```

The smoothed value at `+0x560` is updated on every accepted force step:

```text
x = A[0x560]
t = A[0x444]

if u8[A+0x564] == 0:
    x += (x + 0.3) * (t - x) * 0.02
else:
    x += (t - x) * 0.0004
```

There is no final clamp on `x`. `A+0x568` starts at `1.0` and is read as a
thrust multiplier, but is not written here.

Up to four nodes found through the `propeller`, `prop1`, and `prop2` naming
paths are rotated only when `s > 0`. For node index `i`:

```text
angle = dt * A[0x560] * 120 / (1 + 0.3*i)
sn = sin(angle)
cs = cos(angle)

node[a0]' = sn*node[a4] + cs*node[a0]
node[a4]' = cs*node[a4] - sn*node[a0]

node[ac]' = cs*node[ac] + sn*node[b0]
node[b0]' = cs*node[b0] - sn*node[ac]

node[b8]' = cs*node[b8] + sn*node[bc]
node[bc]' = cs*node[bc] - sn*node[b8]
```

The old value of each pair is retained until both new values are calculated.
The method then calls `CcSrtNode::NotifyChange`.

When `contact == 0`, one additional low-pass field is updated:

```text
A[0x558] += 0.05 * (m01 - A[0x558])
```

Its consumer and semantic name remain unknown.

## Final state damping

After the probe block and height/water branches, the method calls `EnableBsp`.
It then transforms the vector at `A+0x294` into the temporary basis. Let:

```text
W      = vec3[A+0x294]
L      = W * transpose(M)
baseXY = 0.93 + 0.01*f32[T+0xDC]
baseZ  = 0.994
c      = contactAccumulator
```

Then:

```text
dampXY = baseXY - (c == 0 ? 0 : 0.2*c)
dampZ  = baseZ  - (c == 0 ? 0 : 0.007*c)
L'     = (L.x*dampXY, L.y*dampXY, L.z*dampZ)
vec3[A+0x294] = L' * M

A[0x2A0] *= 0.91
A[0x2A4] *= 0.91
A[0x2A8] *= 0.91
```

The force step reads but never writes `A+0x2F4..0x2FC`. The final damping
targets are `+0x294..0x29C` and `+0x2A0..0x2A8`.

## Remaining implementation boundary

The static function body is now algebraically understood, including its time
unit, interval, branches, constants, constructor joins, and call ABI. A parity
implementation is still intentionally deferred until these claims are joined
to another evidence source:

- physical meanings and units for the state vectors, forces, and torques;
- exact range and meaning of the BSP collision scalar;
- controlled traces covering free flight, ground contact, inverted contact,
  water, and too-high branches;
- the meanings and producers of `+0x564`, `+0x568`, `+0x458`, and `+0x45C`;
- the dynamic actor-to-render publication and projectile-spawn path; and
- the required x87-versus-portable-float tolerance for deterministic parity.

The portable iOS input pump currently publishes at 60 Hz, while the recovered
physics interval has a nominal 12 ms cadence (83 1/3 update slots per second).
A future implementation must keep input sampling and physics stepping as
separate clocks; changing the recovered step to 60 Hz would not be a
parity-preserving shortcut.

Until the runtime evidence exists, `PlayerAircraftState` remains a frozen-pose
transport. It must not encode these equations as claimed gameplay behavior
merely because the static algebra is complete.
