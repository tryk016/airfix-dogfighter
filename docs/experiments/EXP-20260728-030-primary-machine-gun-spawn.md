# Primary machine-gun spawn contract

**Date:** 2026-07-28

**Evidence:** `EV-20260728-015`

**Scenario:** `SCN-WEAPON-001`

**Status:** primary attack dispatch, `WpMGun` technology/timing state, barrel
selection, ammunition update, private projectile creation, and event `0xE2`
join confirmed by Ghidra and independently checked with Rizin

## Question

Where does held primary-fire intent become a projectile, and which portion can
be reconstructed now without depending on the original plugin, attachment
data, or a Windows object runtime?

## Sources and safety

The pass used only hash-verified working copies:

| Source | SHA-256 | Role |
|---|---|---|
| `Game/Types/AirCraft.type` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` | attack event handlers and weapon ownership |
| `Game/Types/Projectiles.type` | `639D9F07DD954457CED364E0F1ED2732309819B493422FDAE827D00DC1D76B9F` | `WpMGun` factory, state, cadence, and projectile event |

The original installation was not modified or executed. Ghidra 12.1.2
provided the canonical typed MSVC/import context. Rizin 0.9.1 opened the same
copies read-only with user configuration disabled and independently exported
the selected instruction ranges. Reports, projects, copies, binaries, and
original data remain ignored.

## Event-to-weapon join

The previously recovered `AfVehicle::ProcessEvent` routes
`0xB9 EVENT_PRIMARY_ATTACK` to the AirCraft vtable slot at `+0xC0`. The
AirCraft vtable pointer proves RVA `0x00007430`, working name
`AircraftSetPrimaryAttack`.

The handler:

1. reacquires `WpMgun` into `AirCraft+0x490` and activates it if needed;
2. returns when no primary weapon exists or the aircraft is in water;
3. optionally applies the `AirCraft+0x208` ammunition override;
4. calls weapon vtable `+0x34`, the exported `AfWeapon::Fire(bool)` state
   setter; and
5. propagates authoritative ammo event `0xDC` and attack event `0xB9` through
   the server path.

The secondary slot at RVA `0x000075B0` performs the parallel firing-state
change on `AirCraft+0x494` but has no reacquisition or event propagation in
that local routine.

This closes the first separation: the input event stores a held firing flag;
it does not spawn a projectile.

## Type, instance, and time-dependant join

`Projectiles.type::typeCreate` registers `WpMGun` and
`WpMGunAmmoTech0..4`. The type factory at RVA `0x0000A5E0` allocates a
`0x210`-byte weapon and calls the constructor at `0x0000A6E0`.

The constructor installs:

```text
primary vtable             = 0x10013904
time-dependant subobject   = WpMGun + 0x18
time-dependant vtable      = 0x100138FC
refresh entry              = 0x1000A910
```

The adjacent destructor adjustor subtracts `0x18`, independently confirming
that the refresh offsets are relative to the subobject. The durable field map
and complete spawn sequence are recorded in
[Weapon reconstruction](../re/systems/WEAPONS.md).

## Technology constants

RVA `0x0000A7E0` sets:

```text
level       0       1       2       3       4
interval    .12     .11     .10     .09     .08 seconds
speed       40      55      70      85      100
```

It also initializes the accumulator to the interval, making a newly configured
weapon ready on its first positive-delta firing refresh. Levels at or above
five cap to four. Vtable getters independently return initial ammunition `250`
and maximum ammunition `500`.

## Exact firing transition

The refresh at RVA `0x0000A910`:

- returns for a zero signed 64-bit scheduler delta;
- converts milliseconds with exact `0.001f`;
- accumulates seconds even before checking the parent pointer;
- caps a released trigger to one ready period;
- waits for `interval` with nonzero ammunition or `10 * interval` at zero;
- subtracts exactly one period and emits at most one shot per refresh;
- cycles the selected barrel and decrements only nonzero ammunition;
- creates a private instance of the selected `WpMGunAmmoTechN`; and
- on success, builds and processes `NfEventProjectile` type `0xE2` with room,
  muzzle pose, direction, speed, owner, and optional target lead.

The zero-ammunition path still reaches projectile creation at the slower
period. The static evidence does not establish whether zero is an empty,
fallback, or sentinel state, so the reconstruction preserves the numeric
behavior without naming it.

## Tool comparison

| Function | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| AirCraft `0x7430` | typed weapon/event calls and server branches | `[0x10007430,0x100075A8)`, 115 instructions | agree; Ghidra canonical for ABI |
| AirCraft `0x75B0` | typed secondary setter | `[0x100075B0,0x100075F3)`, 21 instructions | agree |
| `WpMGun` factory `0xA5E0` | allocation size, constructor call, type gates | `[0x1000A5E0,0x1000A620)`, 26 instructions | agree |
| technology `0xA7E0` | structured switch and exact field roles | `[0x1000A7E0,0x1000A8C4)`, 51 instructions | agree |
| ammo getters `0xA8E0/0xA8F0` | vtable-proven guarded function creation, returns `250/500` | two six-byte functions after the same guarded creation | agree |
| refresh `0xA910` | strongest class/import and event pseudocode | exact x87 comparisons, field accesses, calls, and terminal returns; report includes alignment through `0x1000ADA5` | combined |

Rizin found the technology function automatically. The other selected
plugin-local entries required explicit in-memory creation after their exact
vtable/factory addresses were confirmed independently. No analyzer database
was treated as evidence for its own missing boundary.

## Reconstruction decision

The new pure C++20 helper intentionally stops at a projectile request. It owns
no type database, actor pointer, room, original attachment record, effect,
sample, allocation, or platform API. Its caller supplies the barrel count
rather than assuming the observed weapon layout, and the output carries only:

```text
event type 0xE2
selected barrel index
projectile speed
```

The future runtime adapter must create the private ammunition object and fill
the complete projectile event. A failed creation produces no projectile;
projectile motion, collision, impact, damage, ricochet, and rendering are not
claimed by this slice.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds pass all 73 portable
tests. The code-intelligence build compiles 238 steps, and clangd 22.1.8
reports zero errors in the new production and test translation units with the
trusted, narrowly allowlisted MinGW query driver.

The Ghidra, working-copy, and Rizin wrapper suites pass, as do all 12 Rizin
report-normalization tests. Public-boundary tests and the 363-file public scan
pass. The 237-entry, 14-column function catalogue retains unique IDs and
module/RVA pairs. `actionlint`, the local-path scan, and `git diff --check` are
clean. GitHub Actions remains the final publication gate.

No proprietary data is required by the code or tests.

## Confidence

Confidence is **3/3** for:

- primary/secondary handler addresses and stored firing-state behavior;
- `WpMGun` factory/constructor and time-dependant refresh join;
- five interval/speed profiles and `250/500` ammunition constants;
- one-shot-per-refresh, barrel cycling, nonzero decrement, and zero-count
  cadence;
- private ammunition instance creation followed by event `0xE2`; and
- the isolated portable timing transition.

Confidence is **2/3** for descriptive field names and cross-architecture
x87-versus-ARM rounding until controlled runtime traces define tolerances.
