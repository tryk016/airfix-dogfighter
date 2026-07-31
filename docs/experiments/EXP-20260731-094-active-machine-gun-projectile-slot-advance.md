# EXP-20260731-094: active machine-gun projectile slot advance

**Date:** 2026-07-31

**Evidence composed:** `EV-20260728-016`, `EV-20260728-018`,
`EV-20260728-019`, `EV-20260728-020`, `EV-20260729-001`, and
`EV-20260729-002`

**Status:** bounded active-slot flight/collision/terminal-state composition
implemented; live producer, scheduler, callback dispatch, and effects remain
disconnected

## Question

Can each active generation-tagged `WpMGunAmmo` slot advance through the
already-recovered ballistic/lifetime step and the authenticated published
mission collision transaction without inventing a scheduler, actor, effect,
or renderer runtime?

## Reused evidence

This experiment introduces no new native-behavior claim. It composes the
previously cross-checked functions:

- `NfProjectile::ProcessEvent` at
  `[0x10035BD0,0x10035E77)` for age, strict lifetime, ballistic position,
  collision call, and velocity update;
- `NfProjectile::DetectCollisions` at
  `[0x10035EF0,0x10036353)` for creator-BSP bracketing, room tracing,
  projectile-level portals, actor gates, and surface dispatch;
- `WpMGunAmmo::ActorHit` at
  `[0x1000B2C0,0x1000B325)` for damage and deactivation; and
- `WpMGunAmmo::SurfaceContact` at
  `[0x1000B330,0x1000B5C4)` for creator suppression, material-15
  interpolation, ricochet parameters, and deactivation.

The underlying flight reducer, published room/material/portal adapter,
generation-matched primary-player resolver, terminal state/command reducer,
and creator `DisableBsp`/conditional `EnableBsp` transaction were already
implemented and tested separately. The missing boundary was their application
to every active caller-owned projectile slot.

## Portable contract

`advancePublishedLegacyMachineGunProjectileSlots` accepts one complete
caller-owned slot span, an exactly parallel output span, one delta in seconds,
the authenticated room catalogue and mission runtime, actor-query callbacks,
the creator-BSP guard, and bounded collision options.

Before touching output or slot state, it validates:

- exact slot/result shape;
- finite, non-negative delta;
- a complete room catalogue; and
- catalogue/runtime room-count identity.

It then visits slots in stable supplied-index order. This order and the fixed
pool are explicit deterministic port policy, not a claim about the native
private allocator or time-dependant heap order.

For each slot:

1. inactive state is reported without inspecting its profile;
2. an active zero-generation or malformed flight/profile is rejected without
   mutation;
3. strict lifetime expiry commits deactivation without invoking any collision
   or live-actor callback;
4. a live flight result is kept local while the complete published collision,
   portal, actor, terminal, and creator-BSP transaction runs;
5. only a successful transaction commits position, velocity, age, room,
   activity, and water state; and
6. damage or surface/ricochet data is exposed only after that state commit as a
   bounded caller-dispatch request.

A rejected slot remains bit-identical and does not block later independent
slots. Creator-BSP **restore** failure is different: the live actor's collision
state is no longer trustworthy, so the batch marks the failing generation,
publishes no terminal command, and stops before visiting later slots. Earlier
successful slot commits are retained; the eventual live owner must treat this
status as a fatal mission-runtime condition rather than continuing simulation.

The per-slot record carries the pre-step generation handle, terminal outcome,
query/portal counts, BSP-guard result, and optional damage or surface request.
A handle intentionally stops resolving when that same step deactivates its
slot; it identifies the command-producing generation rather than promising
post-step liveness.

## Synthetic verification

The authenticated runtime adapter tests cover:

- inactive profile poison proving the slot is not inspected;
- no-hit ballistic advancement against a published collision frame;
- strict lifetime deactivation without creator callbacks;
- primary-player actor contact, exact generation identity, damage request, and
  immediate stale-handle rejection after deactivation;
- one rejected creator-BSP entry leaving its slot unchanged while a later slot
  still advances;
- failed `EnableBsp` aborting all later slots after the exact
  disable/query/enable sequence;
- invalid delta, output-size mismatch, catalogue/runtime mismatch, and active
  zero generation;
- no stale output on a valid call; and
- 4,096 complete published no-hit slot advances with zero heap allocations.

The focused target passes. The complete portable and code-intelligence builds
each pass 127/127 CTests. Native MSVC builds the SDL3/D3D11 product and passes
137/137 CTests. The compilation database contains 263 entries and no Apple-only
sources. Synthetic public-boundary tests, the 662-file repository scan, all
12 Rizin exporter tests, the 363-row/14-column unique function catalogue, and
`git diff --check` pass. Hosted platform builds remain the publication gate for
this branch.

## Decision

**GO** for deterministic advancement of already-active machine-gun projectile
slots through the current published mission collision generation and for
returning committed terminal command data to a later live dispatcher.

**NO-GO** for ordinary weapon firing integration. Authored changing muzzle
transforms, primary-attack scheduling, the native time-dependant order,
non-player live actors, concrete creator-BSP callbacks, damage/effect dispatch,
tracers, ricochets, audio, render publication, and controlled executable traces
remain separate requirements.
