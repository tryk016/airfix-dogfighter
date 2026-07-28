# Combined static/dynamic line portal continuation

**Date:** 2026-07-28

**Evidence:** `EV-20260728-020`

**Scenarios:** `SCN-WEAPON-001`, `SCN-CAMERA-001`

**Status:** bounded allocation-free automatic continuation implemented

## Question

How can the already implemented non-portal static/dynamic nearest query preserve
the recursive tail of `PhLine::GetBspCollision` without native pointers,
unbounded recursion, hidden allocation, or an invented live actor publisher?

## Evidence and safety

The implementation reuses the hash-verified read-only `Cc.dll` evidence from
`EV-20260728-018` and `EV-20260728-019`:

```text
SHA-256 18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF
```

Ghidra 12.1.2 remains canonical for the typed
`PhLine::GetBspCollision` function at RVA `0x00038BD0`. The existing independent
Rizin 0.9.1 report confirms its automatic boundary
`[0x10038BD0, 0x10039067)`, 1,175-byte size, and the recursive call at virtual
address `0x10038FFF`. No binary, local report, or analysis database is added to
Git.

## Native contract

Each invocation:

1. traces room-static trees;
2. traces active room objects in list order through the same strict-nearest
   fraction;
3. returns immediately for no hit, disabled automatic continuation, a missing
   owner, a nonzero portal type, or an invisible owner;
4. otherwise constructs a new line from the exact hit point to the old endpoint
   in the owner's portal room; and
5. recursively replaces the polygon and normal when a later hit exists.

The final fraction is:

```text
outer + (1 - outer) * inner
```

A transparent portal with no later collision makes the complete native query
return false. The recursive line preserves the dynamic-object and two-sided
flags and re-enables automatic continuation.

This automatic `PhLine` behavior is distinct from the later
`NfProjectile::DetectCollisions` type-zero decision. The projectile loop can
still request another room query for a type-zero owner which was not consumed
by the automatic visibility gate.

## Portable boundary

`traceMissionWorldRuntimeCombinedPortalLine` consumes:

- the immutable retained static arena;
- the runtime/source basis;
- immutable per-mesh dynamic BSPs;
- one flat native-order object span; and
- one object range per world room.

The winning dynamic hit now carries the owner's portal type, target world-room
index, and visibility. Ordinary objects use type `-1`; types `0` and `1`
require an in-range target room. Object indices are translated back from a
room-local subspan to absolute published indices.

The adapter is iterative, `noexcept`, read-only, and allocation-free. It
requires every local hit fraction to remain within `[0, 1]`, rejects malformed
room ranges and portal metadata, and caps transitions at the retained-world
hard maximum of 256. A zero budget is valid only when no transparent portal
wins. Self-portals and cycles fail with a typed
`portalTransitionLimitExceeded` result instead of hanging a frame.

Completed transition count is diagnostic only. A no-later-hit result contains
no stale portal hit, while failures never present themselves as valid
collisions. Final hit position, normal, material, actor identity, and source
provenance come from the later collision; only its fraction is composed back
onto the original segment.

Binary64 staging is used for the short fraction expression before the final
binary32 store. Exact binary32 inputs fit that deterministic calculation, but
bitwise equivalence to the original unspilled x87 expression remains subject
to controlled runtime traces.

## Validation

Synthetic public-data tests cover:

- one and two visible type-zero portal transitions;
- exact whole-segment fraction composition;
- recursion-unwind rounding order across a two-portal chain;
- absolute object provenance after room subspan selection;
- a portal with no later hit;
- invisible type-zero and visible type-one blocking gates;
- a nearer static hit blocking a farther portal;
- malformed object ranges, portal targets, and portal types;
- a later epsilon-admitted hit outside the requested subsegment;
- zero/excessive configured budgets and a self-portal cycle; and
- 4,096 portal-continuing traces with zero observed heap allocations.

The complete cross-platform build, tests, static checks, and GitHub Actions
results are recorded in the progress log when this slice is published.

## Confidence

Confidence is **3/3** for the gate, recursive replacement, no-later-hit result,
and algebraic fraction contract because Ghidra and Rizin agree at the exact
function boundary and call sites.

Confidence is **2/3** for bitwise fraction parity and live behavior until x87
runtime traces and authenticated room-object publication are available.
