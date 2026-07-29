# Machine-gun shot preparation coordinator

**Date:** 2026-07-28

**Evidence:** `EV-20260728-015`, `EV-20260728-016`

**Scenario:** `SCN-WEAPON-001`

**Status:** recovered timing, attachment selection, ammunition profile, and
complete projectile payload composed into one portable transaction

## Question

Can the recovered `WpMGun` refresh and `NfEventProjectile` contracts be joined
without creating original private types, importing mission objects, or
pretending that the incomplete dynamic collision adapter is ready?

## Evidence join

The existing Ghidra and Rizin reports establish this order in RVA
`0x0000A910`:

1. advance the accumulated refresh time;
2. test parent, trigger, cadence, barrel, and ammunition state;
3. subtract one cadence period;
4. advance/wrap the barrel and decrement only nonzero ammunition;
5. attempt `WpMGunAmmoTechN` private allocation;
6. only after allocation succeeds, update the vehicle attachments, assemble
   event `0xE2`, and dispatch it.

This means an allocation failure does not roll the cadence, barrel, or
ammunition state back. The earlier portable pieces separately represented the
state transition and the complete payload but did not give a runtime caller one
transactional handoff with that ordering.

## Reconstruction decision

`LegacyMachineGunShotCoordinator`:

- caps a requested technology above four exactly at the weapon boundary;
- advances `LegacyMachineGunFireState`;
- reads no muzzle/payload-only input when no shot is due;
- when a shot is due, requires one coherent rotated-muzzle snapshot matching
  the attachment barrel count;
- selects the emitted barrel, joins the matching technology damage/lifetime
  profile, and constructs the complete semantic event `0xE2` payload; and
- returns the already-advanced fire state next to the prepared request.

The runtime ordering is explicit: commit the returned fire state, attempt
capacity acquisition, and activate the payload only if acquisition succeeds.
The coordinator remains an eager pure-preparation helper. The follow-up
`LegacyMachineGunProjectileRuntime` directly composes the timing primitive,
fixed-capacity acquisition, and payload construction so muzzle/event-only
input is not consumed before capacity succeeds. It preserves the native
branch order without creating an original private type; see
[EXP-20260729-047](EXP-20260729-047-portable-projectile-runtime-pool.md).

Invalid timing, attachment, or payload inputs fail closed. Payload-only values
are intentionally not inspected on a refresh that emits no shot, preserving
the native branch-consumption boundary.

## Follow-up runtime join

Room-ID mapping, retained static/dynamic collision, projectile-level portals,
the authenticated primary-player resolver, terminal state/command reduction,
and creator BSP bracketing are now represented by later slices. The remaining
join is to advance active generation-tagged slots through that transaction
using live muzzle and actor producers, then consume terminal commands through
concrete actor/effect adapters. The fixed-capacity activation boundary is
recorded in
[EXP-20260729-047](EXP-20260729-047-portable-projectile-runtime-pool.md).

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all 244
steps and pass 75/75 portable tests. Clangd 22.1.8 reports zero errors in the
production and test translation units. The Ghidra/working-copy/Rizin wrapper
suites, 12 Rizin normalization tests, public-boundary tests and 371-file scan,
251-row catalogue validation, `actionlint`, local-path scan, and
`git diff --check` pass. GitHub Actions remains the publication gate.

No proprietary data or local path is required by the implementation or tests.

## Confidence

Confidence is **3/3** for the native state-before-allocation ordering,
technology cap, barrel selection, payload composition, and no-shot
branch-consumption rule.

Confidence remains **2/3** for portable floating-point agreement until
controlled x87-versus-ARM traces establish numeric tolerances.
