# EXP-20260729-052: portable player-pose runtime preparation

- Date: 2026-07-29
- Status: implemented; hosted gates and physical-device acceptance pending
- Scope: backend-neutral actor-pose runtime planning, ownership, and frame
  leases

## Question

Can Metal and D3D11 share one fail-closed contract for preparing and consuming
the authenticated player-pose runtime without inventing a flight law or
coupling the renderer to private content?

## Previous boundary

The portable pose runtime and its bounded SPSC exchange already published one
complete actor-only frame for each accepted simulation step. Metal had a local
planner that derived runtime limits, verified the authored step-zero pose, and
owned the resulting runtime. D3D11 rendered only authored instance transforms
and Windows therefore used the presentation coordinator's headless path.

That split risked backend drift: identical authenticated mission data could be
admitted under different memory or initial-pose rules.

## Portable contract

`PlayerActorPoseRuntimePreparation` now provides two explicit operations:

1. Plan from the validated player binding, actor provenance, and authored
   instances. The plan records exact dynamic-pose limits and retained CPU bytes.
2. Prepare only from an unchanged plan and the same inputs. Preparation creates
   the bounded runtime and verifies every step-zero override index and transform
   bit-for-bit against the authored instance.

The status domain distinguishes:

- a valid mission with no authenticated player;
- a ready player runtime;
- malformed or internally inconsistent authenticated input;
- a valid request that exceeds a platform or allocation limit.

Preparation rejects a modified plan rather than trusting caller-provided
limits. Its retained-byte calculation includes immutable authored source data,
scratch storage, and both exchange slots. The API accepts only portable render
types and has no content-loader or native-backend dependency.

## Backend integration

Metal removed its duplicate planner and delegates preparation to the portable
contract while retaining its separate aggregate renderer admission budget.

D3D11 prepares the same runtime before publishing a candidate mission snapshot.
The snapshot strongly owns the runtime, while producer code receives only a
weak endpoint. Each gameplay frame acquires at most one immutable pose lease,
uses it for every dynamic instance lookup in that draw pass, and releases it
after the pass. Authored transforms remain the fallback when a command is not
part of the actor override range.

Windows now passes the weak endpoint to the shared
`PlayerAircraftPresentationCoordinator` whenever the installed mission owns an
authenticated player runtime. Replacing the mission destroys the old strong
owner and makes stale producer endpoints expire without accessing backend
objects.

## Atomicity and ownership

- A failed plan or preparation never mutates the published renderer snapshot.
- GPU resource creation begins only after portable runtime preparation succeeds.
- The renderer owns no simulation clock and performs no pose interpolation.
- A render lease observes one complete generation for the entire gameplay draw
  pass.
- Producer publication remains allocation-free after mission preparation.
- Missions without a player need no runtime and keep the explicit headless
  consumer path.

## Validation

Synthetic portable tests cover:

- valid no-player admission and orphan-provenance rejection;
- exact limits, retained-byte accounting, and bit-identical step zero;
- later publication of a changing world transform;
- modified-plan rejection;
- authored-instance, binding-count, and binding-range mismatches;
- platform override ceilings.

The complete native MSVC 19.51/Ninja Windows build compiles the D3D11 ownership,
endpoint, and lease path and passes 97/97 tests, including both public D3D11
product smoke modes. Hosted Windows, portable, iPhoneOS, and iPhoneSimulator
gates remain the publication acceptance boundary.

All tests use synthetic instances. No original data, converted assets, private
logical names, checksums, or local content paths are required or logged.

## Remaining boundary

This experiment intentionally does not change the visible aircraft transform.
The presentation coordinator still publishes the authenticated spawn pose. A
changing producer requires controlled evidence for the distinct 12 ms AirCraft
clock, Q15-to-native event timing and payload conversion, scheduler ordering,
numeric tolerance, and branch/PRNG behavior. The renderer must not infer those
rules from input intentions.

The optional private HD-texture resolver is also deferred. This pose-runtime
work stabilizes backend snapshot ownership but does not pre-empt the later
texture/cache contracts or introduce any private package dependency.

D3D11 currently enforces the runtime's exact per-scene ceilings and checked
retained-byte calculation, but does not yet have Metal's aggregate CPU snapshot
admission ledger. That broader Windows policy should budget geometry, texture
staging, collision, pose, and future streaming state together rather than
adding a pose-only total that implies complete mission accounting.

## Confidence

- Portable preparation and ownership contract: high.
- Cross-backend compilation and Windows renderer consumption: high.
- Hosted Apple compilation: pending publication gates.
- Flight behavior parity or changing-pose correctness: not claimed.

## Related

- [Architecture](../ARCHITECTURE.md)
- [Project status](../progress/STATUS.md)
- [Parity matrix](../verification/PARITY-MATRIX.md)
- [Deterministic Windows input consumer](../progress/LOG.md)
