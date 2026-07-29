# Projectile collision portal loop

**Date:** 2026-07-29

**Evidence:** `EV-20260728-018`, `EV-20260728-020`

**Scenario:** `SCN-WEAPON-001`

**Status:** bounded allocation-free repeated-query coordinator implemented

## Question

Can the remaining repeated-query portion of
`NfProjectile::DetectCollisions` be reconstructed without coupling the
simulation library to the renderer, inventing live actor state, or retaining
the native unbounded portal loop?

## Refreshed evidence

Fresh Ghidra 12.1.2 decompilation and instruction reports were generated from
the existing hash-verified read-only `AfEngine.dll` working copy. A fresh
automatic Rizin 0.9.1/rzpipe 0.6.2 report independently analyzed the same
verified bytes with user configuration disabled.

| Property | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| Function | typed `NfProjectile::DetectCollisions` | automatic function at requested RVA | agree |
| Boundary | `[0x10035EF0, 0x10036353)` | `[0x10035EF0, 0x10036353)`, 1,123 bytes | agree |
| First query | `PhLine::GetBspCollision(this + 0xA8)` | indirect call at `0x10035F3C` | agree |
| Portal repeat | update `+0xC0` from fraction, replace `+0xD8` room, query again | back edge through the same indirect query call | agree |
| No-later-hit exit | swap `+0xC0` and `+0xCC`, then release creator guard | shared terminal block and virtual guard release | agree |

Ghidra remains canonical for the MSVC receiver, field types, virtual calls, and
room/object meaning. Rizin independently confirms the exact boundary,
instructions, back edge, and terminal blocks. Raw reports, binaries, and tool
databases remain local and ignored.

## Native repeated-query state

The native function begins with the projectile's current position at `+0xC0`
and full step endpoint at `+0xCC`. One complete `PhLine` query already includes
its own automatic visible-portal continuation. The later projectile decision
can still encounter an owner-backed type-zero portal. In that case it:

1. interpolates `+0xC0` to the local hit fraction;
2. keeps `+0xCC` unchanged as the full endpoint;
3. replaces current room `+0xD8` with the portal target; and
4. invokes the same complete line query again.

Every no-hit, material-8, client/actor-gated, actor-contact, or surface-contact
outcome is terminal. When a later query has no hit, the final swap advances the
projectile to the original endpoint and retains the last portal contact as the
previous position. The creator actor's collision guard brackets the complete
loop, not each query, and is released through the shared terminal path.

The native loop has no cycle or transition limit.

## Portable boundary

`resolveLegacyProjectileCollisionLoop` accepts a small `noexcept` function
pointer instead of depending on the renderer. The callback receives only:

- current segment start;
- unchanged full endpoint; and
- current signed legacy room ID.

It returns a typed no-hit, hit, or rejected state. A hit carries the existing
single-decision contract, including material, portal target, actor identity,
and actor gates. The coordinator invokes
`legacyProjectileCollisionDecision`, returns every non-portal decision
unchanged, or feeds a `followPortal` decision into the next query.

This split keeps four responsibilities explicit:

- the mission runtime owns and executes the combined static/dynamic `PhLine`
  equivalent;
- a future live adapter maps world-room indices, bound material values, actor
  identity, and actor gates into the callback result;
- this coordinator owns only the repeated projectile portal state; and
- the live adapter owns the creator guard plus terminal actor/surface callback
  dispatch.

The operation is allocation-free and `noexcept`. It defaults to 64 completed
projectile-level transitions and rejects a configured limit above the
retained-world hard maximum of 256. Zero allows a terminal first query but
rejects the first requested portal transition.

## Atomic failure policy

Malformed callback combinations, callback rejection, invalid hit metadata,
non-finite input, and an excessive/cyclic portal chain return distinct typed
statuses. Failures include query and completed-transition counts for
diagnostics but never expose an optional terminal decision.

This is intentionally stricter than the native pointer loop. It prevents an
invalid query result from masquerading as a no-hit endpoint advance and
prevents a self-portal from hanging the simulation thread.

## Validation

Synthetic tests cover:

- terminal no-hit with a zero portal budget;
- two projectile portals followed by a surface;
- preservation of the full endpoint and per-hop room/start inputs;
- material-8 and actor-contact terminals after a portal;
- rejected, internally inconsistent, unknown, and missing-hit query states;
- malformed hit rejection at the single-decision boundary;
- zero and one-transition budgets plus an explicit self-portal cycle; and
- 4,096 two-query portal/surface loops with zero observed allocations.

A fresh 259-step Windows GCC 15.2 Release build and all 80 CTest targets pass.
The regenerated compilation database contains 165 portable entries, and
clangd 22.1.8 reports zero errors in both the coordinator and its test. Twelve
Rizin normalization tests, the synthetic and 396-file public-boundary scans,
the 263-row unique function catalogue, `actionlint`, changed-scope local-path
scan, and `git diff --check` also pass. Exact commit `ba5b2ec` compiles all
259 steps with GCC 13.3 and passes 80/80 tests in a new isolated `Airfix-Dev`
WSL clone. GitHub Actions remains the publication gate for this slice.

## Remaining boundary

The generic loop deliberately does not yet bind the runtime collision hit to
legacy material values or live actor gates. The current collision frame
contains only the authenticated player and placed objects, and the native
creator collision guard/callback dispatch still requires a stateful actor
runtime. Private projectile allocation/event dispatch, effects, and controlled
executable traces also remain separate.

## Confidence

Confidence is **3/3** for query repetition, room/start/end state, terminal
swaps, creator-guard lifetime, and the bounded coordinator structure.

Confidence remains **2/3** for bitwise x87 interpolation and complete live
behavior until controlled traces and the actor/material adapter are available.

## Related material

- [Shared projectile collision decision](EXP-20260728-034-projectile-collision-decision.md)
- [Combined line portal continuation](EXP-20260728-036-combined-line-portal-continuation.md)
- [Mission-runtime dynamic collision](EXP-20260729-041-mission-runtime-dynamic-collision-ownership.md)
