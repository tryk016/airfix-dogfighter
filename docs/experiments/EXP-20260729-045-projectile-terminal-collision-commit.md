# Projectile terminal collision commit

**Date:** 2026-07-29

**Evidence:** `EV-20260728-016`, `EV-20260728-018`,
`EV-20260728-019`, `EV-20260728-020`

**Scenario:** `SCN-WEAPON-001`

**Status:** portable terminal state and command reduction implemented

## Question

Can a completed shared projectile collision query be committed into the
portable `WpMGunAmmo` state and its already-recovered actor/surface callbacks
without allocating, dispatching private events, or inventing missing material
or actor identity?

## Confirmed native boundary

The shared `NfProjectile::DetectCollisions` path has already selected one
terminal outcome and prepared its position, previous position, room, fraction,
normal, material, resolved actor identity, and material-15 water flag.
`WpMGunAmmo` then applies one of two subclass callbacks:

- actor contact emits damage event `0x7D` and deactivates only when creator UID
  is nonzero and differs from the hit actor UID;
- surface contact ignores an owner that resolves to the creator, otherwise
  deactivates, interpolates material `15`, and may create `FxRicochet` with the
  five recovered ordered parameters.

The creator collision guard brackets the complete shared query. Its paired
entry/exit calls are confirmed, but their base-class semantic names are not.
This slice deliberately does not guess those names or simulate the guard.

## Portable commit

`commitLegacyMachineGunProjectileCollision` accepts an active finite
projectile, a valid technology profile, and a completed collision-loop result.
It:

1. rejects an incomplete query, remaining portal transition, malformed
   terminal payload, inactive state, or invalid profile without changing the
   returned state;
2. commits the terminal position and signed room ID;
3. preserves active flight for no-hit, material-8, and actor-gate outcomes;
4. emits bounded damage command data and applies the creator/self deactivation
   rule for actor contact;
5. sets the persistent water flag before material-15 surface handling;
6. applies the existing creator-surface gate, interpolation, and deactivation;
   and
7. returns an optional bounded ricochet request rather than creating or
   dispatching a private effect.

Resolved actor identity is accepted on actor contact and on the server
surface fallback only when the earlier actor lookup actually resolved it. A
raw nonzero owner-object ID is not promoted to an actor UID after lookup
failure.

## Missing material policy

Static retained-room polygons are ownerless in the recovered shared decision
order and therefore do not expose the owner-backed `CcMaterial + 0x5C` value.
The terminal commit still updates position/room and deactivates on that surface,
but suppresses the optional ricochet request instead of inventing a material
integer. Dynamic owner-backed contacts retain their authenticated signed
`0x2152` value and can produce the complete request.

## Published runtime composition

`resolvePublishedLegacyMachineGunProjectileCollision` joins the existing
published mission query and the terminal reducer. The current state must be the
already-integrated flight state at the query endpoint and initial room;
mismatch rejects before the runtime trace. Both the explicit live-actor
callback overload and the generation-matched primary-player overload remain
available.

The result retains query/portal counts and the exact loop failure when no
terminal commit is possible. Successful results carry the new projectile state
plus at most one damage or surface result. No private actor event, effect
allocation, sound, tracer, or renderer action occurs.

## Validation

Synthetic reducer tests cover:

- no-hit state retention;
- actor damage/deactivation and creator self-hit behavior;
- ordinary and material-15 surface commits;
- persistent water marking and callback interpolation;
- resolved creator-surface suppression;
- ownerless material suppression without skipping deactivation;
- inactive, incomplete, portal, inconsistent-water, and invalid-profile
  rejection with unchanged state; and
- 4,096 actor commits with zero observed allocations.

Authenticated runtime integration covers a published primary-player
actor-contact/damage commit, a flight/query mismatch rejected before tracing,
actor-gate pass-through, a two-room portal-to-surface commit with ricochet
request, and 4,096 complete actor-gate and portal/surface transactions without
observed allocation.

A fresh Windows GCC 15.2 Release build compiles 265 steps; it and the
regenerated code-intelligence build pass 82/82 tests. The compilation database
contains 169 portable entries and no Apple-only source. Clangd 22.1.8 reports
zero errors in all five changed production/test translation units with the
trusted GCC query driver. The RE wrapper suites, 12 Rizin normalization tests,
synthetic/public boundary tests and 405-file scan, 263-row function catalogue,
`actionlint`, 19-file local-path scan, and `git diff --check` pass. Exact commit
`b355415` compiles all 265 steps with GCC 13.3 and passes 82/82 tests in
44.80 seconds in a new isolated `Airfix-Dev` WSL clone. GitHub Actions remains
the publication gate.

## Remaining boundary

The project still needs:

- creator collision-guard entry/exit around the complete live query;
- changing primary-player and authenticated non-player producers;
- live consumption of damage and surface/effect command data;
- private projectile allocation and event `0xE2` dispatch;
- authored live muzzle transforms and tracer/effect realization; and
- controlled executable traces for final runtime comparison.

## Confidence

Confidence is **3/3** for the reducer order, actor/self deactivation, surface
interpolation, material-15 water update, and bounded command values because
they compose already cross-checked Ghidra/Rizin contracts.

Confidence remains **2/3** for complete live collision behavior until the
creator guard, changing actors, private dispatch, and controlled traces exist.

## Related material

- [Machine-gun projectile contact](EXP-20260728-031-machine-gun-projectile-contact.md)
- [Projectile collision decision](EXP-20260728-034-projectile-collision-decision.md)
- [Projectile collision portal loop](EXP-20260729-042-projectile-collision-portal-loop.md)
- [Projectile runtime query adapter](EXP-20260729-043-projectile-runtime-query-adapter.md)
- [Projectile primary-player resolver](EXP-20260729-044-projectile-player-actor-resolver.md)
