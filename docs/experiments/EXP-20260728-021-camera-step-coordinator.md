# Gameplay-camera step coordinator

**Date:** 2026-07-28

**Evidence:** `EV-20260728-007`

**Scenario:** `SCN-CAMERA-001`

**Status:** recovered retained-static AirCraft step composition implemented;
refresh delta later confirmed in seconds and cross-thread packet exchange
implemented; mission runtime and weak iOS endpoint implemented; live AirCraft
production and dynamic-object collision remain separate

## Question

Can every already recovered AirCraft gameplay-camera stage be composed in its
native order, including camera-mode input and final Metal packet construction,
without committing camera state, mode, or input counters after a late failure?

## Recovered order

`LegacyGameplayCameraStepCoordinator` owns the producer-side state required to
join the existing narrow contracts. One accepted step performs:

1. AirCraft-specific per-axis factor recovery;
2. persistent `camera0 -> camera1 -> camera2 -> camera0` mode cycling;
3. momentary rear-view preset selection without changing the persistent mode;
4. chase target and nonlinear smoothing;
5. retained static sphere correction and factor reduction;
6. the first portal-room update;
7. the retained current-room vehicle-to-camera line query;
8. the conditional second portal-room update;
9. look-at and unit-scale world-to-view pose construction;
10. homogeneous gameplay clip-packet construction; and
11. one generation/step/state commit through the existing single-writer
    owner.

The order matters. Factor recovery is reached through the aircraft virtual
slot before the chase update. Collision can reduce those recovered factors
again before they become the next authoritative state.

## Refresh delta in seconds

The recovered aircraft factor rule is:

```text
if vehicleHealth <= 0 or vehicleInactive:
    factor = 0
else:
    if factor < 1:
        factor += refreshDeltaSeconds * 0.25
    factor = clamp(factor, 0, 1)
```

A follow-up scheduler join confirmed that this value is the same refresh-event
delta that `AfVehicle::ProcessEvent` converts from milliseconds to seconds
before invoking AirCraft slot `+0xB0`. AirCraft's nominal 12 ms interval
therefore supplies `0.012f`; see
[EXP-20260728-023](EXP-20260728-023-camera-refresh-time-contract.md).

The coordinator still requires the caller to provide this value together with
the recovered live health and inactive state. Their exact native sources and
lifecycle transitions are confirmed in
[EXP-20260728-025](EXP-20260728-025-camera-aircraft-input-contract.md). It must
not substitute the input pump's
nominal `1/60`, because input sampling and the recovered aircraft scheduler are
separate clocks. Native iOS integration remains separate until the simulation
can provide all live AirCraft inputs.

## Transaction boundary

Initialization owns the explicit camera0 bootstrap documented in
[EXP-20260728-020](EXP-20260728-020-metal-gameplay-camera-packet.md). It records
the caller's existing cumulative camera-cycle count so a mission replacement
cannot replay old input edges.

For each later step, the coordinator computes the proposed mode and every
camera stage without mutating its persistent mode or input counter. It then
constructs the anticipated next frame using the current generation plus one,
builds the look-at pose and Metal packet, and only then asks the state owner to
commit.

The following failures expose diagnostics but no packet and preserve the
exact prior state, mode, and camera-cycle counter:

- regressed cumulative camera input;
- invalid factor-recovery inputs;
- invalid mode or chase math;
- static sphere, workspace, basis, arena, or portal failure;
- retained-static line or second portal failure;
- generation exhaustion;
- degenerate/invalid look-at pose;
- clip-packet failure; and
- stale, busy, or otherwise rejected state commit.

Camera presses skipped between accepted producer calls are applied modulo
three. Rear view selects the recovered rear tuple only for that call and never
changes the persistent mode.

## Ownership and mission runtime join

The coordinator is non-copyable and non-moving. It owns only the compact
camera state owner, mode, and last cumulative input count. No original asset or
platform type enters this API.

`LegacyGameplayCameraMissionRuntime`, documented in
[EXP-20260728-024](EXP-20260728-024-camera-mission-runtime.md), now owns the
mission BSP, runtime basis, exact-size sphere/constraint workspaces,
coordinator, and packet exchange. It exposes a weak iOS endpoint that expires
with mission replacement. Its remaining live producer must:

- source the aircraft scheduler delta in seconds and both recovery gates;
- supply the distinct chase position, world anchor, and vehicle rotation; and
- call the existing runtime without resampling the input pump.

The steady-state input keeps the recovered chase position and vehicle
world-anchor position separate. Native AirCraft code reads them from distinct
object fields; collapsing them merely because the current bootstrap has one
authenticated spawn position would erase evidence needed by later actor-state
integration.

## Validation

Synthetic tests cover:

- camera0 initialization and cycle-counter provenance;
- a stationary complete step binding step, generation, state, pose, and clip
  packet;
- distinct chase-position and vehicle-anchor inputs remaining distinct through
  the composed step;
- factor clearing before chase and later `seconds * 0.25` recovery before the
  next chase;
- one-press mode selection, held rear view, and batched modulo-three cycling;
- uninitialized, regressed-counter, invalid-recovery, and invalid-arena
  failures;
- a late degenerate-pose failure before commit;
- a stale state commit after all preceding stages;
- exact preservation of authoritative state, mode, and counter on every
  failure; and
- 4,096 allocation-counted successful composed steps.

No proprietary content is required. A fresh Release Ninja/GCC 15.2 build and
the regenerated code-intelligence build each pass all 66 portable tests.
Focused clangd 22 checks report zero errors in both new translation units.
The 332-file public-source boundary, three reverse-engineering wrapper suites,
12 Rizin normalization tests, 224-entry function-catalogue uniqueness check,
`actionlint`, and `git diff --check` also pass.

## Result and next boundary

All recovered retained-static AirCraft camera stages now have one explicit
producer-side transaction. This closes the ordering and rollback problem;
the later scheduler join also closes the refresh unit without inventing
dynamic-object data.

The SPSC exchange, preallocated mission runtime, and weak producer endpoint are
now implemented. The endpoint is intentionally inactive until the simulation
publishes the required aircraft inputs. Controlled native traces remain
necessary for end-to-end parity, not for the refresh argument's unit.

## Confidence

Confidence is **3/3** for the composition order, persistent mode and rear-view
semantics, precommit pose/packet validation, transactional state/input
behavior, and allocation-free portable implementation.

Confidence remains **2/3** for complete gameplay-camera parity because the
moving-object BSP, transparent collision portals, live producer inputs, and
controlled runtime traces are still missing.

## Related material

- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Static state integration](EXP-20260728-017-camera-static-state-integration.md)
- [State owner](EXP-20260728-018-camera-state-owner-snapshot.md)
- [Retained-static pose snapshot](EXP-20260728-019-camera-retained-static-pose-snapshot.md)
- [Metal gameplay-camera packet](EXP-20260728-020-metal-gameplay-camera-packet.md)
- [Camera refresh time contract](EXP-20260728-023-camera-refresh-time-contract.md)
- [Gameplay-camera mission runtime](EXP-20260728-024-camera-mission-runtime.md)
- [AirCraft input contract](EXP-20260728-025-camera-aircraft-input-contract.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
