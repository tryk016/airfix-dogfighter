# Projectile creator BSP guard

**Date:** 2026-07-29

**Evidence:** `EV-20260728-012`, `EV-20260728-018`,
`EV-20260729-001`

**Scenario:** `SCN-WEAPON-001`

**Status:** allocation-free creator `DisableBsp`/`EnableBsp` transaction
implemented

## Question

Can the complete published `NfProjectile::DetectCollisions` transaction be
bracketed with the creator actor's native BSP state change without guessing
virtual-method names, enabling an actor that was already disabled, or exposing
terminal commands after failed restoration?

## Refreshed evidence

Fresh Ghidra 12.1.2 decompilation and instruction reports were generated from
the existing hash-verified, read-only `AfEngine.dll` working copy. A fresh
automatic Rizin 0.9.1/rzpipe 0.6.2 report independently analyzed the same
SHA-256 `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E`
bytes with user configuration disabled. Raw reports, binaries, and tool
databases remain local and ignored.

Both tools retain the exact
`NfProjectile::DetectCollisions` boundary
`[0x10035EF0, 0x10036353)`. The relevant instructions are:

| Site | Observation |
|---|---|
| `0x10035EF8` | load creator UID from projectile `+0x104` |
| `0x10035F19` | resolve the actor through `NfEngine::GetActorByUID` |
| `0x10035F29` | skip the first virtual call when the actor is null |
| `0x10035F2F` | call actor vtable slot `+0x3C` and retain returned `AL` |
| `0x10035F3C` | run the first complete `PhLine` query |
| `0x10036043` | repeat the same query after a projectile-level portal |
| `0x10036081` | all terminal paths join the shared epilogue |
| `0x1003608B` / `0x10036093` | require both a retained actor and true saved byte |
| `0x10036097` | call the retained actor at vtable slot `+0x38` exactly once |

Rizin independently matches the bytes, conditions, indirect-call sites, query
back edge, and single epilogue. Ghidra supplies the MSVC type and symbol join:
the same vtable slots are `DisableBsp` at `+0x3C` and `EnableBsp` at `+0x38`.
The names are no longer hypotheses.

The exported implementations provide the state meaning:

- `AfInteractive::DisableBsp` RVA `0x00017370` returns the old enabled byte,
  disables its collision object, and clears the byte;
- `AfInteractive::EnableBsp` RVA `0x00017340` returns the old byte and enables
  the collision object when present;
- `AfVehicle::DisableBsp` RVA `0x000205A0` likewise returns whether BSP was
  enabled before disabling it; and
- `AfVehicle::EnableBsp` RVA `0x00020550` reapplies the vehicle-specific
  health gate while restoring its BSP state.

The base `NfActor` exports for both operations alias the false-returning stub at
RVA `0x00077AF0`. Consequently the saved byte is not merely call success: it
is the prior enabled state. `EnableBsp` must run only when `DisableBsp`
returned true.

## Native transaction

The reconstructed order is:

```text
creator = GetActorByUID(projectile.creatorUid)
creatorBspWasEnabled = false
if creator != null:
    creatorBspWasEnabled = creator.DisableBsp()

run complete PhLine/projectile-portal loop
run terminal actor or surface callback

if creator != null and creatorBspWasEnabled:
    creator.EnableBsp()
```

A missing creator does not reject collision work. A false `DisableBsp` result
does not reject it either and, critically, does not cause a later
`EnableBsp`. The actor pointer resolved before the query is retained through
the synchronous transaction; the native function does not resolve the creator
again in its epilogue.

## Portable contract

`LegacyProjectileCreatorBspGuard` carries two `noexcept` callbacks:

- `disableBsp(context, creatorUid)` returns `actorNotFound`, `completed`, or
  `rejected`, plus the old BSP state and an opaque stable actor handle when a
  later enable is required;
- `enableBsp(context, actorHandle)` reports whether the corresponding live
  operation completed.

`resolvePublishedLegacyMachineGunProjectileCollisionWithCreatorBspGuard`
brackets the already-implemented runtime query, every projectile-level portal
transition, and terminal state/command reduction. It preserves these paths:

| Disable result | Query/reducer | Enable call | Public status |
|---|---|---|---|
| creator UID zero | runs | none | `noCreatorUid` |
| actor not found | runs | none | `actorNotFound` |
| completed, old state false | runs | none | `alreadyDisabled` |
| completed, old state true | runs | once, same handle | `restored` |
| rejected or malformed | does not run | none | `disableRejected` |

The immutable published collision frame cannot expose mutable actor methods,
so creator BSP control remains an explicit live-actor adapter even in the
generation-matched primary-player convenience overload.

The native methods have no typed failure result. The portable boundary adds a
fail-closed policy: a rejected enable leaves the completed collision available
for diagnostics but clears its optional terminal commit and command data.
This prevents a caller from applying damage or effects after the live adapter
failed to complete the required restoration operation.

An obvious flight-state/query endpoint or room mismatch is rejected before
touching a live creator. All callbacks and the coordinator are synchronous,
allocation-free, and `noexcept`; a live adapter must keep the opaque handle
valid until the matching enable returns.

## Validation

Synthetic runtime tests cover:

- exact `DisableBsp -> actor query -> EnableBsp` ordering;
- one disable/enable pair around a two-room projectile portal loop;
- missing creator, zero UID, and already-disabled paths without enable;
- rejected and internally inconsistent disable results without tracing;
- rejected enable with terminal commit/commands suppressed;
- invalid flight/query joins before any live callback; and
- 4,096 complete guarded actor transactions with zero observed allocations.

A fresh Windows GCC 15.2 Release build and the regenerated code-intelligence
build compile all 265 steps and pass 82/82 tests. The compilation database has
169 portable entries and no Apple-only source. Clangd 22.1.8 reports zero
errors in the adapter, collision-loop, and focused test translation units.
The exact-commit Linux and GitHub Actions results are recorded in the progress
log when this slice is published.

## Remaining boundary

The creator BSP lifetime is now represented through the complete portable
query/reducer transaction. The project still needs:

- a changing primary-player producer and authenticated non-player actor
  publication/resolution;
- concrete live adapters that invoke the private actor methods and consume
  damage/surface commands;
- private projectile allocation and event `0xE2` dispatch;
- authored live muzzle transforms plus tracer/ricochet realization; and
- controlled executable traces for final runtime and numeric comparison.

## Confidence

Confidence is **3/3** for creator lookup, vtable identities, prior-state
meaning, one-pair lifetime, portal coverage, and terminal ordering. Ghidra
names the exported methods and vtable joins; Rizin independently confirms the
controlling instruction bytes and branches.

Confidence remains **2/3** for complete live behavior until private actor
adapters, changing actor publication, and controlled executable traces exist.

## Related material

- [Aircraft flight-law static contract](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
- [Projectile collision decision](EXP-20260728-034-projectile-collision-decision.md)
- [Projectile collision portal loop](EXP-20260729-042-projectile-collision-portal-loop.md)
- [Projectile runtime query adapter](EXP-20260729-043-projectile-runtime-query-adapter.md)
- [Projectile terminal collision commit](EXP-20260729-045-projectile-terminal-collision-commit.md)
