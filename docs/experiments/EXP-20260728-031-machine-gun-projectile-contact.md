# Machine-gun projectile payload and contact contract

**Date:** 2026-07-28

**Evidence:** `EV-20260728-016`

**Scenario:** `SCN-WEAPON-001`

**Status:** `WpMGunAmmoTechN` type/instance creation, complete event `0xE2`,
activation, shared ballistic step, actor damage, surface deactivation, and
ricochet request recovered

## Question

What does the primary machine-gun spawn request contain, how does its private
ammo instance move and expire, and which collision reactions can be preserved
now without importing the original type database, scene runtime, or effects?

## Sources and safety

The pass used only hash-verified, read-only working copies:

| Source | SHA-256 | Role |
|---|---|---|
| `Game/Types/Projectiles.type` | `639D9F07DD954457CED364E0F1ED2732309819B493422FDAE827D00DC1D76B9F` | machine-gun ammo types, instances, contact reactions, and ricochet request |
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | packed projectile event, activation, shared motion, lifetime, and collision dispatch |

The original installation was not modified or executed. Ghidra 12.1.2 used
the existing typed MSVC/import project. Rizin 0.9.1 independently opened the
same copies read-only with user configuration disabled. Reports, projects,
copies, binaries, and original data remain ignored.

## Type and instance join

`Projectiles.type::typeCreate` creates five `0x90`-byte ammo types through RVA
`0x0000ADB0`. The technology value selects exact impact damage:

```text
technology      0   1   2   3   4
damage          3   4   5   6   7
```

All five aircraft types pass a zero broad-hit radius. The distinct ground
machine-gun type passes `0.07`, which the constructor squares. This proves that
the subclass scan at RVA `0x0000B650` immediately returns for aircraft ammo
after its base event handling.

The vtable-proven factory at RVA `0x0000AEC0` allocates `0x198` bytes and calls
RVA `0x0000B090`. The instance constructor installs three adjusted vtables,
sets lifetime `4.0f`, and stores acceleration bits:

```text
x = 0x00000000
y = 0xBF96D5CF
z = 0x00000000
```

The middle value is approximately `-1.1784f`. A normal `-1.1784F` source
literal rounds to a neighboring binary32 value on the current compiler, so the
portable implementation constructs the original value from its bits.

## Complete `NfEventProjectile`

The exported assignment operator copies thirteen dwords plus one byte, proving
a packed size of 53 bytes. The `WpMGun` refresh writes:

| Payload offset | Value |
|---:|---|
| `+0x11` | muzzle world X |
| `+0x15` | muzzle world Y |
| `+0x19` | muzzle world Z |
| `+0x1D` | velocity X |
| `+0x21` | velocity Y |
| `+0x25` | velocity Z |
| `+0x29` | creator room ID |
| `+0x2D` | creator vehicle UID |
| `+0x31` | zero |

The start position is creator position plus the selected muzzle attachment
after the creator SRT rotation. The base direction is the creator-relative aim
point minus that rotated muzzle offset.

When a selected target resolves to an active actor, lead time is:

```text
distance(targetPosition, muzzleWorldPosition) / projectileSpeed
```

The target velocity multiplied by this time is added to the direction. The
routine then falls back to `{0, 1, 0}` for exact zero length, normalizes when
length squared differs from exact one, and scales by projectile speed. The
selected target is only a lead source; the event's target UID remains zero.

`AfAmmo::ProcessEvent` case `0xE2` copies the fields into shared projectile
state, resolves the room, places/orients the optional tracer, and marks the
instance active.

## Shared flight and lifetime

`NfProjectile::ProcessEvent` accumulates seconds from a signed millisecond
event. While age is at most the ammo lifetime:

```text
end = position + velocity * dt + acceleration * dt * dt * 0.5
trace(position, end)
velocity = velocity + acceleration * dt
```

Age strictly greater than four invokes the virtual deactivation slot without
advancing. The local refresh at RVA `0x0000B5D0` first synchronizes the optional
tracer from the projectile position and then calls the shared refresh.

The portable step deliberately models only an unobstructed commit. The
room/BSP and dynamic-actor trace must remain an explicit adapter because it
depends on live mission objects and portal traversal.

## Damage and surface reaction

RVA `0x0000B2C0` accepts a hit actor. It emits damage event `0x7D` with the
technology damage and creator UID only when the creator is nonzero and is not
the hit actor. A valid damage event is followed by deactivation.

RVA `0x0000B330` handles a shared surface collision:

1. if the polygon owner resolves to the creator actor, return without effect or
   deactivation;
2. for material `15`, interpolate the contact fraction from the previous point
   to the current point;
3. if a room exists and private `FxRicochet` creation succeeds, process:

   ```text
   event 0xA0  float 1.0
   event 0xA2  float 0.2
   event 0xB7  collision normal
   event 0xB6  material integer
   event 0xA6  room ID and impact position
   ```

4. deactivate even when the room/effect path is unavailable.

The scalar parameter semantics are left unnamed until the effect consumer is
recovered.

## Tool comparison

| Function | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| ammo type constructor `0xADB0` | typed `AfAmmoType`, damage switch, squared radius, vtable | automatic `[0x1000ADB0,0x1000AE4B)`, 47 instructions | agree |
| ammo instance constructor `0xB090` | typed base construction, exact fields, tracer setup | automatic `[0x1000B090,0x1000B246)`, 123 instructions | agree |
| actor hit `0xB2C0` | typed `NfEventDamage` and creator gate | vtable-guarded `[0x1000B2C0,0x1000B325)`, 31 instructions | agree |
| surface callback `0xB330` | strongest event/effect pseudocode | vtable-guarded `[0x1000B330,0x1000B5C4)`, 170 instructions | agree |
| visual refresh `0xB5D0` | typed tracer sync and base refresh | vtable-guarded `[0x1000B5D0,0x1000B648)`, 35 instructions | agree |
| subclass event scan `0xB650` | zero-radius aircraft gate and optional ground scan | vtable-guarded `[0x1000B650,0x1000B996)`, 237 instructions | agree |
| `AfAmmo::ProcessEvent` `0x247A0` | canonical packed `0xE2` field mapping | automatic `[0x100247A0,0x100257A2)`, 1,108 instructions | combined |
| `NfProjectile::ProcessEvent` `0x35BD0` | typed time/motion/lifetime switch | automatic `[0x10035BD0,0x10035E77)`, 186 instructions | agree |
| `NfProjectile::DetectCollisions` `0x35EF0` | typed portal/actor dispatch | automatic `[0x10035EF0,0x10036353)`, 305 instructions | agree |
| event constructor `0x58010` | exported `NfEventProjectile` identity | automatic `[0x10058010,0x10058030)`, 12 instructions | agree |

The local callbacks missed by automatic Rizin analysis were created only in
memory after the exact type/instance vtables independently established their
entry addresses. The reports record this distinction. Ghidra remains canonical
for exported types, ABI, and high-level event meaning; Rizin materially
confirms boundaries, field offsets, immediates, calls, and branches.

## Reconstruction decision

`LegacyMachineGunProjectile` is allocation-free and owns no original type,
scene, room, actor, polygon, tracer, effect, or networking object. It provides:

- exact ammo profiles;
- a complete semantic spawn payload;
- target-velocity lead;
- active state and unobstructed ballistic/lifetime transition;
- an actor damage command; and
- a surface result with an optional bounded ricochet request.

The runtime must supply the already-rotated muzzle attachment, creator/target
snapshots, room-spatial collision, actor lookup, and live effect consumption.
The portable fixed-capacity pool now replaces private allocation and activates
the complete event-`0xE2` state while preserving the recovered branch order;
see
[EXP-20260729-047](EXP-20260729-047-portable-projectile-runtime-pool.md).
Invalid/non-finite values and collision fractions outside `[0,1]` are rejected
at the portable boundary rather than reproducing unsafe memory or x87
unordered behavior.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all 241
steps and pass 74/74 portable tests. Clangd 22.1.8 reports zero errors in the
production and test translation units. The Ghidra/working-copy/Rizin wrapper
suites, 12 Rizin normalization tests, public-boundary tests and 367-file scan,
251-row catalogue validation, `actionlint`, local-path scan, and
`git diff --check` pass. GitHub Actions remains the publication gate for this
slice.

No proprietary data is required by the code or tests.

## Confidence

Confidence is **3/3** for:

- five damage values, zero aircraft broad-hit radius, exact lifetime and
  acceleration bits;
- all semantic `0xE2` fields and the zero target UID;
- muzzle position, lead, fallback, normalization, and velocity construction;
- shared unobstructed ballistic equation and strict lifetime comparison;
- actor creator/self gate and damage event; and
- surface creator gate, material-15 interpolation, five ricochet events, and
  deactivation behavior.

Confidence is **2/3** for cross-architecture x87-versus-ARM rounding and the
descriptive aim/effect parameter names until controlled runtime traces define
tolerances and effect-consumer semantics.
