# Projectile primary-player actor resolver

**Date:** 2026-07-29

**Evidence:** `EV-20260728-016`, `EV-20260728-018`,
`EV-20260728-019`, `EV-20260728-020`

**Scenario:** `SCN-WEAPON-001`

**Status:** generation-matched primary-player resolver implemented

## Question

Can the authenticated player collider supply the recovered live actor gates to
the projectile query without a second unsynchronized callback state, hidden
allocation, or invented non-player actors?

## Confirmed boundary

The native server path resolves a nonzero owner object ID and then checks three
independent values:

- projectile actor collisions are enabled;
- the actor accepts projectile collisions; and
- the actor is active.

The collision publication already receives the primary player's current
object ID and active flag from its future live producer. This slice adds the
two remaining confirmed gates as explicit producer inputs. It does not infer
their values from geometry, actor type, health, visibility, or activity.

## Atomic runtime publication

`LegacyGameplayCameraMissionPlayerActorState` carries the object ID, active
flag, and both projectile-collision gates. A successful
`tryPublishDynamicCollisionFrame` commits that value beside the exact player
geometry generation. A failed geometry validation leaves both the previous
flat collision frame and its actor state unchanged.

When the authenticated mission has no player collision assembly, successful
placed-only publication exposes no player state even if the caller supplied
placeholder inputs. Before the first complete dynamic-frame publication no
actor state is observable.

This is a logical producer transaction. It does not add cross-thread access:
the dynamic collision frame, player state, projectile query, and camera
producer diagnostics remain single-producer runtime interfaces.

## Concrete lookup

`queryPublishedLegacyProjectilePlayerActor` has three typed outcomes:

| Condition | Result |
|---|---|
| zero query ID or no complete frame | `rejected` |
| complete frame without the requested primary-player ID | `notFound` |
| exact current player ID | `resolved` with all three gates |

The primary-player overload of
`resolvePublishedLegacyProjectileCollisionLoop` installs that lookup for the
duration of the synchronous bounded query. It therefore cannot observe actor
gates from a different collision generation. The existing explicit callback
overload remains the future extension seam for other authenticated live actor
producers.

## Native order preserved

The resolver does not change the established projectile decision order:

1. material `8` still exits before actor lookup;
2. client projectiles still do not read server actor state;
3. server lookup miss still falls back to surface behavior;
4. any false resolved gate still advances through the actor contact; and
5. three true gates produce the existing actor-contact decision.

Inactive player geometry remains excluded by the combined line tracer before
lookup, matching the existing dynamic-object publication contract.

## Validation

The integration fixture owns a complete one-room runtime with an empty placed
scene and one authenticated player collision mesh. A server projectile crosses
the published player plane, resolves object ID `91`, material `4`, and all
three true gates, and produces one actor contact at the expected point.
Republishing the same geometry with the actor-acceptance gate disabled produces
the native pass-through actor-gate outcome. The client path keeps the same
outcome without server-state lookup.

Focused runtime and projectile-adapter tests also cover:

- unpublished, zero-ID, mismatched-ID, and no-player lookup states;
- successful player state publication;
- retention of the last actor state after a rejected geometry republish; and
- 4,096 complete concrete-resolver queries with zero observed allocations.

A fresh Windows GCC 15.2 Release build compiles all 262 steps, and both it and
the regenerated code-intelligence build pass all 81 tests. The compilation
database contains 167 portable entries and no Apple-only source. Clangd 22.1.8
reports zero errors in both changed production units and both changed tests.
Twelve Rizin normalization tests, synthetic public-boundary tests, the
401-file public scan, 263-row unique function catalogue with only its two known
missing references, `actionlint`, 16-file changed-scope local-path scan, and
`git diff --check` also pass. Exact commit `0796d98` compiles all 262 steps
with GCC 13.3 and passes 81/81 tests in a new isolated `Airfix-Dev` WSL clone.
GitHub Actions remains the publication gate for this slice.

## Remaining boundary

The project still needs:

- the changing primary-player producer that supplies pose, room, identity,
  activity, and both gates each accepted simulation generation;
- authenticated geometry/state publication and resolvers for other actors;
- concrete creator BSP and actor/effect adapters for the implemented guard and
  terminal damage/surface command contracts;
- private projectile allocation/event dispatch and live muzzle transforms; and
- controlled executable traces for final runtime comparison.

## Confidence

Confidence is **3/3** for transaction retention, lookup outcomes, native actor
gate order, and allocation-free primary-player resolution.

Confidence remains **2/3** for complete live weapon behavior until the
changing producer, other actors, private live dispatch, and controlled runtime
traces exist.

## Related material

- [Projectile collision decision](EXP-20260728-034-projectile-collision-decision.md)
- [Authenticated player collision publication](EXP-20260728-037-authenticated-player-collision-publication.md)
- [Mission runtime ownership](EXP-20260729-041-mission-runtime-dynamic-collision-ownership.md)
- [Projectile runtime query adapter](EXP-20260729-043-projectile-runtime-query-adapter.md)
- [Projectile terminal collision commit](EXP-20260729-045-projectile-terminal-collision-commit.md)
- [Projectile creator BSP guard](EXP-20260729-046-projectile-creator-bsp-guard.md)
