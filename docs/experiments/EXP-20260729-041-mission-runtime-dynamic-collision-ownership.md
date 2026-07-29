# Mission-runtime dynamic-collision ownership

**Date:** 2026-07-29

**Evidence:** `EV-20260728-019`, `EV-20260728-022`

**Scenarios:** `SCN-LEVEL-001`, `SCN-WEAPON-001`, `SCN-CAMERA-001`

**Status:** mission-lifetime ownership and preallocated placed/player line
collision implemented

## Question

Can the authenticated static arena, placed/player dynamic BSP assets, and
mutable flat collision frame share one replacement-safe mission owner without
copying mesh arenas, allocating during publication or tracing, or publishing a
guessed player pose before the simulation has one?

## Ownership transfer

The non-copyable and non-moving `LegacyGameplayCameraMissionRuntime` was
already the mission-lifetime owner of the retained static arena, camera
workspaces, coordinator, and packet exchange. Its mission-load creation form
now also takes ownership of:

- the complete `MissionPlacedDynamicBspAssembly`;
- the optional complete `PlayerActorCollisionAssembly`;
- one exact-size `LegacyDynamicBspLineObject` buffer for all placed objects
  plus all player instances; and
- one exact-size room-range buffer parallel to the retained arena.

Metal preparation validates the complete `LoadedMissionWorldRoom` first, then
moves the arena and both immutable collision assemblies into this runtime.
The remaining render payload is moved into the Objective-C snapshot. No mesh,
nested BSP arena, or provenance vector is copied.

The loader already charged the immutable assets to the mission CPU ledger.
The runtime therefore adds only the two flat buffer sizes to its existing
candidate, constraint-plane, and packet-exchange accounting. Checked count,
multiplication, addition, per-object, per-room, and total-byte limits all run
before allocation. The default 65,536-object runtime ceiling matches the
combined line tracer's default per-query ceiling, so an admitted default frame
cannot immediately fail only because two defaults disagree.

The older camera-only creation form remains available for isolated portable
camera tests. It owns no dynamic assets or buffers and fails dynamic
publication closed.

## Frame publication and tracing

`tryPublishDynamicCollisionFrame` delegates to the two-pass publisher from
`EXP-20260729-040` using the runtime-owned exact buffers. On success it exposes
one producer-only view with:

```text
mesh indices: [placed immutable owner][player immutable owner]
objects:      [current-room player prefix][placed native-order ranges]
ranges:       one exact range per retained world room
```

The runtime deliberately does not publish a bootstrap player frame during
creation. A real producer must supply the current player world transform,
object ID, active gate, and room. This avoids treating the authenticated spawn
as live simulation state.

The first failed publication exposes no frame. A failed republish after a
successful one leaves the previous complete buffers and published-state flag
unchanged. `tracePublishedDynamicCollisionPortalLine` consumes only that last
complete frame and the runtime-owned static arena; a query before publication
returns typed `invalidInput`.

Both operations are `noexcept`, bounded, allocation-free, and restricted to
the single simulation producer. Returned spans remain address-stable for the
runtime lifetime, but their object/range contents are replaced by the next
successful publication and must not be retained as an immutable render lease.

## iOS boundary

The real private-room Metal preparation now uses the collision-owning creation
form. Invalid placed/player assets or room cardinality are payload failures.
Count, retained-size, and allocation failures are resource failures. The
existing 128 MiB private packed-memory gate admits the exact additional flat
buffers before the two-phase room commit.

The existing weak runtime endpoint therefore expires with mission replacement
while a temporarily locked producer owns every static and dynamic object
needed for publication and line tracing. No new Objective-C property exposes
private paths, names, geometry, or source data.

## Verification

Synthetic tests cover:

- the legacy camera-only form with zero dynamic capacity;
- exact two-object/one-room buffer ownership and byte accounting;
- a no-player mission publishing only its placed range;
- incomplete placed/player assets and placed/arena room mismatch;
- exact and one-under dynamic-object, room-range, and retained-byte limits;
- no frame and typed tracing failure before first publication;
- live-player prefix order through the runtime-owned owners;
- the player winning an exact-fraction placed-object tie;
- a failed republish retaining the prior complete frame; and
- 4,096 camera advances, frame publications, acquisitions, and portal-line
  traces with zero observed steady-state allocations.

A fresh 256-step Release build and regenerated Debug/code-intelligence build
pass 79/79 tests. Clangd 22.1.8 reports zero diagnostics in the runtime and its
test. WSL and both GitHub Actions workflows remain publication gates for this
slice.

## Remaining boundary

The runtime now owns the authenticated placed/player line-collision frame, but
the flight simulation still does not supply changing player pose, room,
object-ID, or active state. Non-player live actors, dynamic-object sphere
collision for the gameplay camera, projectile allocation/event dispatch, and
controlled executable traces remain separate work.

## Confidence

Confidence is **3/3** for ownership transfer, exact buffer accounting,
replacement safety, atomic publication, native list order, and
allocation-free tracing.

No new gameplay-parity claim is made for the absent live producer or
dynamic-sphere path.

## Related material

- [Mission dynamic-collision composition](EXP-20260729-040-mission-dynamic-collision-composition.md)
- [Gameplay-camera mission runtime](EXP-20260728-024-camera-mission-runtime.md)
- [Combined line portal continuation](EXP-20260728-036-combined-line-portal-continuation.md)
