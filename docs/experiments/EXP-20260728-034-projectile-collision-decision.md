# Shared projectile collision decision

**Date:** 2026-07-28

**Evidence:** `EV-20260728-018`

**Scenario:** `SCN-WEAPON-001`

**Status:** deterministic `NfProjectile::DetectCollisions` decision layer
implemented; bounded repeated-query coordination completed separately

## Question

Which part of the native projectile collision loop can be reconstructed
without inventing moving-actor geometry or treating the camera's retained
static BSP query as a complete `PhLine::GetBspCollision` replacement?

## Sources and safety

Two hash-verified, read-only working copies were used:

| Source | SHA-256 | Role |
|---|---|---|
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | shared projectile state, collision loop, actor and surface dispatch |
| `Cc.dll` | `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF` | `PhLine` construction, static/dynamic nearest-hit query, portal recursion |

The original installation was not executed or modified. Ghidra 12.1.2 reused
the existing local projects. Rizin 0.9.1 independently analyzed the verified
copies with user configuration disabled. Binaries, working copies, reports,
and tool projects remain outside Git.

## `PhLine` query contract

The default `PhLine` constructor enables automatic portal continuation at
`+0x34`, dynamic-object tracing at `+0x35`, and room static-BSP tracing at
`+0x36`. It selects one-sided polygons by clearing `+0x37`.

`PhLine::GetBspCollision` RVA `0x00038BD0` rejects a null room, derives the
segment direction, resets the nearest fraction to `FLT_MAX`, and clears the
current owner state. It then:

1. traverses every static tree in the room list at `CcRoom +0x40`;
2. visits enabled dynamic objects from the room object list;
3. midpoint/radius-culls each dynamic object;
4. transforms the segment into object-local space;
5. invokes the same `TraceBspNode` while retaining the shared nearest
   fraction; and
6. restores the source segment after each object query.

Consequently a static and dynamic hit are not two independent results: they
compete through one strict nearest-fraction state, and exact ties retain the
first visited polygon.

When the final nearest polygon is a visible type-zero portal and automatic
continuation is enabled, `GetBspCollision` recursively traces from the hit
point to the prior endpoint in the target room. A recursive hit replaces the
polygon and normal and is converted back to a whole-segment fraction:

```text
combined = outerFraction + (1 - outerFraction) * innerFraction
```

A portal with no later hit makes the complete query report no collision.
The native recursion is unbounded; any portable runtime adapter requires an
explicit ceiling.

## Shared projectile decision order

`NfProjectile::DetectCollisions` RVA `0x00035EF0` temporarily enters the
creator actor's collision guard, queries `PhLine`, and applies this order to
each returned polygon:

1. ownerless polygon: surface contact;
2. owner-backed material `8`: stop querying and commit the full endpoint;
3. owner with nonzero actor object ID:
   - non-server projectile: commit the endpoint;
   - server lookup failure: surface contact;
   - disabled projectile/actor gates: commit the endpoint;
   - otherwise normalize the normal, interpolate the hit position, call the
     actor at virtual slot `+0x70`, then call the projectile actor callback at
     slot `+0x44`;
4. owner with zero actor object ID:
   - portal type `-1` or `1`: surface contact;
   - portal type `0`: interpolate the next start, switch to the portal room,
     and repeat the `PhLine` query;
5. no later hit: commit the full endpoint.

The native portal-type domain is `{-1, 0, 1}`: `CcObject::GetPortalType`
returns `-1` for a mesh not classified as a portal, otherwise it returns the
placed object's raw type.

Actor and surface contacts normalize the collision normal only when its
length-squared differs exactly from `1.0f`. Material `15` is special: the
shared layer swaps the full endpoint into the current position, sets the
projectile water flag, and lets the subclass surface callback perform the
fraction interpolation. Other surfaces interpolate before the callback.

The creator collision guard is exited on every terminal path. Its virtual
method meanings remain unnamed because the call pairing is known but the
base-class semantic names are not.

## Tool comparison

| Function | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| `NfProjectile::DetectCollisions` `0x35EF0` | typed `[0x10035EF0, 0x10036353)`, high-level owner/material/portal/actor pseudocode | automatic same boundary, 305 instructions and exact branches/calls | agree |
| `PhLine::GetBspCollision` `0x38BD0` | typed `[0x10038BD0, 0x10039067)`, static list, object-local dynamic trace, portal recursion | automatic same boundary, 1175 bytes, exact calls at `0x38C58`, `0x38EC3`, and recursive `0x38FFF` | agree |
| `PhLine::TraceBspNode` `0x391D0` | typed near-first traversal and strict nearest replacement | independent instruction report from the earlier retained-BSP pass | agree |

Ghidra remains canonical for MSVC types and virtual-call context. Rizin
independently confirms the boundaries, flag offsets, list order, shared
nearest state, portal recursion, material immediates, actor gates, swaps, and
normalization branches used by this contract.

## Portable boundary

`legacyProjectileCollisionDecision` is an allocation-free, `noexcept` C++20
transition for one already-selected nearest hit. It returns one of:

- no-hit endpoint advance;
- material-8 endpoint advance;
- actor-gated endpoint advance;
- portal continuation;
- actor contact; or
- surface contact.

The result preserves the semantic `NfProjectile +0xC0/+0xCC` positions,
current room, fraction, normalized normal, material, resolved actor identity,
and material-15 water flag. Normal length uses the recovered actor/surface
addition order with binary64 products as a deterministic proxy for the native
unspilled x87 sum. It composes directly with the existing machine-gun
actor/surface callbacks.

The generic coordinator in
[EXP-20260729-042](EXP-20260729-042-projectile-collision-portal-loop.md)
now repeats the decision after projectile-level portal outcomes with a bounded
budget. Its callback must still:

- merge retained static BSP with live dynamic-object BSP through one
  strict-nearest query;
- supply actor object identity, lookup state, and all three native actor
  gates; and
- let the live adapter dispatch actor/projectile callbacks and effects while
  bracketing the complete loop with the creator collision guard.

The portable transition rejects non-finite segments, non-finite or
out-of-segment fractions, zero/non-finite contact normals, zero actor object
IDs, unsupported portal types, missing portal targets, and owner-backed hits
without material metadata. Those are deliberate fail-closed policies instead
of native unsafe dereferences or unbounded recursion.

The retained mission static line tracer is not silently substituted for the
missing combined iterator: doing so could report a farther wall when a moving
actor is actually the nearest hit.

## Validation

Synthetic tests cover every terminal outcome, material-8 precedence over the
actor path, portal continuation state, client/server and actor gates, resolved
and unresolved actors, normal normalization, ownerless surfaces, material-15
callback composition, and malformed input. Full build, portability, tooling,
public-boundary, and CI results are recorded in the progress log when the
slice is published.

No proprietary data or local path is required by the implementation or tests.

## Confidence

Confidence is **3/3** for the native decision order, constants, actor gates,
position swaps, and the pure outcome transition.

Confidence is **2/3** for bitwise interpolation/normalization and complete
`PhLine` runtime parity until x87 comparisons, moving-object BSP state, and
controlled executable traces are available.
