# Projectile runtime query adapter

**Date:** 2026-07-29

**Evidence:** `EV-20260728-018`, `EV-20260728-019`, `EV-20260728-020`

**Scenario:** `SCN-WEAPON-001`

**Status:** authenticated runtime trace/material/room adapter implemented

## Question

Can the published mission collision frame feed the implemented
`NfProjectile::DetectCollisions` loop without inventing actor state, changing
the recovered decision order, or allocating during a simulation step?

## Confirmed inputs

This integration uses only previously confirmed contracts:

- `PhLine::GetBspCollision` returns one strict-nearest result after its own
  visible type-zero portal continuation;
- the first u32 of CCF material child `0x2152` is native
  `CcMaterial + 0x5C`;
- retained static and dynamic traces already preserve that value on their
  selected polygon;
- the authenticated room catalogue maps newest-first world indices to root ID
  zero and monotonically assigned positive `CcRoom` IDs; and
- the projectile applies material `8`, client/server actor gates, actor lookup,
  projectile-level portals, and surfaces in that exact order.

No new structure offset or live-state meaning is inferred by this adapter.

## Portable mapping

`legacyProjectileQueryResultFromRuntimeTrace` converts one complete combined
portal-line result into the simulation query contract:

| Runtime result | Projectile query |
|---|---|
| no hit | typed no-hit |
| static room polygon | ownerless hit |
| dynamic polygon | owner-backed hit |
| dynamic material `0x2152` | bit-preserved signed native material |
| dynamic type-zero portal | authenticated target world index converted to `CcRoom` ID |
| dynamic nonzero object ID | actor owner populated through the live-actor contract |
| invalid/missing provenance or trace error | typed rejection |

Static material metadata is deliberately not exposed as owner-backed material:
the recovered native static polygon path has no `CcObject` owner. Dynamic hits
must carry the uniquely resolved `0x2152` value because native code
dereferences the owner material before its portal or actor decision.

The signed material conversion uses a bit-preserving C++20 `bit_cast`. This
retains all raw u32 values rather than relying on implementation-defined
unsigned-to-signed conversion; recovered constants `8` and `15` therefore
retain their exact native meanings.

## Live actor boundary

The adapter accepts a small `noexcept` actor query with three results:

- `notFound`: valid server lookup miss and later surface fallback;
- `resolved`: supplies the three confirmed actor collision/active gates; or
- `rejected`: fail closed without a projectile decision.

Native order remains explicit:

1. material `8` returns before any actor query;
2. a client projectile returns its actor gate without querying live actor
   state;
3. only a server projectile invokes the resolver;
4. resolved actor flags feed the existing deterministic decision; and
5. missing callback state is rejection, not an invented actor.

The resolver supplies transient state only. The current mission frame still
publishes the authenticated player and placed objects; other live actors need
their own future publication producer.

## Runtime composition

`resolvePublishedLegacyProjectileCollisionLoop` verifies that the
authenticated catalogue is complete and parallel to the runtime-owned arena.
Each query converts the current signed room ID to a world index, invokes the
latest published combined portal-line trace, maps its result, and lets the
bounded simulation coordinator handle any later projectile-level portal.

The adapter, trace, mapper, and coordinator allocate no memory internally.
Creator collision-guard entry/exit and terminal actor/surface callback
dispatch are not performed here because they form one higher-level live actor
transaction around the complete native loop.

## Validation

Synthetic mapping tests cover:

- no-hit and ownerless static results;
- material `8` before actor resolution;
- client actor gating without a resolver call;
- server resolved, lookup-miss, rejected, missing, and unknown actor states;
- portal target conversion and signed material bit preservation; and
- inconsistent statuses, missing provenance/material, unsupported portals,
  incomplete catalogues, and catalogue/runtime room-count mismatch.

An integration fixture publishes two retained rooms. A hidden type-zero placed
portal wins in the root room, the projectile-level loop switches to legacy
room ID `1`, and a solid placed surface terminates the second query. The result
preserves the expected positions, material, query count, and transition count.
4,096 complete two-query runtime loops observe zero allocations.

A fresh 262-step Windows GCC 15.2 Release build and all 81 CTest targets pass.
The regenerated compilation database contains 167 portable entries, and
clangd 22.1.8 reports zero errors in both the adapter and test. Twelve Rizin
normalization tests, the synthetic and 400-file public-boundary scans, the
263-row unique function catalogue, `actionlint`, 14-file changed-scope
local-path scan, and `git diff --check` also pass. Exact commit `c1b4d15`
compiles all 262 steps with GCC 13.3 and passes 81/81 tests in a new isolated
`Airfix-Dev` WSL clone. GitHub Actions remains the publication gate for this
slice.

## Remaining boundary

The project still needs:

- publication of other live actor colliders and their changing state;
- a concrete actor resolver backed by that producer;
- creator collision-guard lifetime around the complete loop;
- terminal actor/surface callback and damage/effect dispatch;
- private projectile allocation/event dispatch and live muzzle transforms; and
- controlled executable traces for final numeric/runtime comparison.

## Confidence

Confidence is **3/3** for room mapping, static/dynamic owner mapping,
`0x2152` material transport, native actor-query order, and fail-closed adapter
behavior.

Confidence remains **2/3** for complete live behavior until the actor producer,
guard/callback transaction, and controlled executable traces exist.

## Related material

- [Projectile collision decision](EXP-20260728-034-projectile-collision-decision.md)
- [Combined portal line](EXP-20260728-036-combined-line-portal-continuation.md)
- [Mission runtime ownership](EXP-20260729-041-mission-runtime-dynamic-collision-ownership.md)
- [Projectile portal loop](EXP-20260729-042-projectile-collision-portal-loop.md)
