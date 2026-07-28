# Weapon reconstruction

This note records only behavior confirmed in the original PE32/x86 modules.
Working names are descriptive and do not claim recovered source identifiers.
The current portable boundary covers the primary `WpMGun` timing and spawn
request; projectile motion, collision, impact damage, effects, and secondary
weapons remain separate work.

## Evidence boundary

The current join uses hash-verified, read-only working copies of:

- `Game/Types/AirCraft.type`, SHA-256
  `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E`;
- `Game/Types/Projectiles.type`, SHA-256
  `639D9F07DD954457CED364E0F1ED2732309819B493422FDAE827D00DC1D76B9F`;
  and
- the previously imported `AfEngine.dll`, which supplies the exported weapon,
  event, actor, and type-instance interfaces.

Ghidra 12.1.2 is canonical for the MSVC class/import context. Rizin 0.9.1
independently confirms the selected function boundaries and instructions.
Vtable pointers supplied guarded entries where either analyzer initially
missed a function. Generated reports, tool projects, binaries, and working
copies remain ignored.

## Attack-event dispatch

`AfVehicle::ProcessEvent` dispatches `0xB9 EVENT_PRIMARY_ATTACK` through the
vehicle vtable offset `+0xC0` and `0xBA EVENT_SECONDARY_ATTACK` through
`+0xC4`. The AirCraft vtable maps those slots to:

| Module RVA | Working name | Behavior |
|---:|---|---|
| `0x00007430` | `AircraftSetPrimaryAttack` | obtains/activates `WpMgun` if needed, rejects firing in water, sets its firing state, and propagates authoritative ammo/attack events |
| `0x000075B0` | `AircraftSetSecondaryAttack` | rejects a missing weapon or water state, then sets the selected secondary weapon's firing state |

The primary and selected-secondary pointers are respectively `AirCraft+0x490`
and `AirCraft+0x494`. Both handlers call weapon vtable offset `+0x34`; the
exported `AfWeapon::Fire(bool)` stores the value in the weapon firing byte.
An AirCraft flag at `+0x208` causes either handler to call
`AfWeapon::SetAmmo(100)` before changing the firing state. Its broader
gameplay meaning is not named yet.

The primary handler also:

1. refreshes the authoritative ammo value through event `0xDC`;
2. broadcasts primary attack event `0xB9` when it is the server; or
3. sends `0xB9` server-only on the relevant client/type path.

The input event therefore changes persistent weapon state. It does not create
a projectile directly.

## WpMGun type and instance

`Projectiles.type::typeCreate` registers `WpMGun` and the five private
ammunition types `WpMGunAmmoTech0` through `WpMGunAmmoTech4`.

The `WpMGun` type factory at RVA `0x0000A5E0` allocates `0x210` bytes and calls
the instance constructor at `0x0000A6E0`. That constructor:

- constructs the `AfWeapon` base;
- installs the primary vtable at virtual address `0x10013904`;
- installs a time-dependant subobject vtable at `WpMGun+0x18`;
- retains the weapon type at `+0x208`;
- applies technology level zero; and
- loads the separate `FxRicochet` type.

The time-dependant vtable identifies RVA `0x0000A910` as the `WpMGun` refresh.
Its `this` value addresses the subobject at `WpMGun+0x18`. Adding that
adjustment produces these instance fields:

| Instance offset | Working meaning |
|---:|---|
| `+0x84` | attached parent vehicle |
| `+0x88` | accumulated shot time in seconds |
| `+0x8C` | current shot interval in seconds |
| `+0x90` | selected private ammunition type |
| `+0x94` | firing flag inherited from `AfWeapon` |
| `+0x98` | ammunition count |
| `+0xA8` | number of muzzle/barrel attachment positions |
| `+0xB0 + 12*n` | local muzzle position `n` |
| `+0x1FC` | next muzzle/barrel index |
| `+0x200` | projectile speed |

Attachment/type loading supplies the barrel count and positions. The portable
code keeps the count explicit and does not guess that data.

## Technology profiles

The technology setter at RVA `0x0000A7E0` caps an integral level above the
supported range to level four, initializes the accumulated time to the chosen
interval, and writes:

| Level | Shot interval | Binary32 bits | Projectile speed |
|---:|---:|---:|---:|
| 0 | `0.12` s | `0x3DF5C28F` | `40` |
| 1 | `0.11` s | `0x3DE147AE` | `55` |
| 2 | `0.10` s | `0x3DCCCCCD` | `70` |
| 3 | `0.09` s | `0x3DB851EC` | `85` |
| 4 | `0.08` s | `0x3DA3D70A` | `100` |

Primary-vtable offset `+0x48` returns `250`; offset `+0x4C` returns `500`.
These are retained as the recovered initial and maximum ammunition constants.

## Refresh and spawn contract

RVA `0x0000A910` accepts the scheduler's signed 64-bit millisecond delta and
multiplies it by exact binary32 `0.001f` (`0x3A83126F`). A zero delta returns
immediately. Otherwise it adds seconds to the accumulator and proceeds only
when a parent vehicle is attached.

Let:

```text
period = shotInterval * (ammunition == 0 ? 10 : 1)
```

The native state transition is:

```text
if !firing:
    if accumulated > period:
        accumulated = period
    return

if accumulated < period:
    return

accumulated -= period
barrel = nextBarrel
nextBarrel = (nextBarrel + 1) modulo barrelCount
if ammunition != 0:
    ammunition -= 1

projectile = selected WpMGunAmmoTechN.CreatePrivateInstance(parent.player)
if projectile creation succeeds:
    build NfEventProjectile(projectile.uid, 0xE2)
    fill room, muzzle world position, direction, speed, owner and target data
    process the event
```

Only one projectile can be requested by one refresh, even when the retained
accumulator spans several periods. A zero ammunition count is preserved and
uses the tenfold period while still reaching the private-instance creation
path; no stronger semantic label such as “empty” or “infinite” is assigned
without runtime evidence.

## Portable boundary

`LegacyMachineGunFireState` implements only:

- the five exact technology profiles;
- initial/maximum ammunition constants;
- accumulated-time and trigger-state behavior;
- zero-ammunition cadence;
- one-shot-per-refresh behavior;
- barrel selection/wrap and nonzero-ammunition decrement; and
- an allocation-free projectile request carrying event `0xE2`, barrel index,
  and projectile speed.

The helper accepts seconds because the owning scheduler boundary already
performs the recovered millisecond conversion. It rejects non-finite or unsafe
consumed inputs instead of reproducing x87 unordered behavior or invalid
attachment memory. It remains unwired: an eventual runtime adapter must resolve
the private ammunition type, build the complete room/pose/velocity event, and
honor failure without inventing a projectile.

## Remaining work

- Recover and implement the complete `NfEventProjectile` payload boundary.
- Recover `WpMGunAmmoTechN` activation, flight, collision, impact, damage, and
  ricochet behavior.
- Trace sample/effect commands associated with a shot.
- Recover secondary weapon selection and each secondary projectile family.
- Confirm ammunition-zero semantics and scheduler/x87 tolerance with controlled
  runtime traces.
