# Portable machine-gun projectile runtime pool

**Date:** 2026-07-29

**Evidence:** `EV-20260728-015`, `EV-20260728-016`,
`EV-20260729-002`

**Scenario:** `SCN-WEAPON-001`

**Status:** bounded event-`0xE2` activation into generation-tagged portable
storage implemented

## Question

How can the iOS reconstruction replace the private PE32/x86 ammunition-object
allocation while preserving the native weapon-state and branch-consumption
order, rejecting stale projectile identities, and performing no heap
allocation in the simulation hot path?

## Refreshed evidence

Fresh Ghidra 12.1.2 instruction and decompilation reports were generated from
the hash-verified, read-only working copies. Rizin 0.9.1 with rzpipe 0.6.2
independently analyzed the same bytes with user configuration disabled.
Reports, binary copies, and analyzer databases remain local and ignored.

| Module | SHA-256 |
|---|---|
| `Game/Types/Projectiles.type` | `639D9F07DD954457CED364E0F1ED2732309819B493422FDAE827D00DC1D76B9F` |
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` |

The comparison covered one weapon refresh, the private ammunition factory and
constructor, and the shared event receiver:

| Function | Boundary | Size | Rizin discovery | Relevant result |
|---|---:|---:|---|---|
| `WpMGunRefresh` | `[0x1000A910, 0x1000ADA5)` | 1,173 bytes | explicit function creation from the confirmed vtable entry | cadence, barrel, and nonzero ammunition advance before the private factory call |
| `WpMGunAmmoCreateInstance` | `[0x1000AEC0, 0x1000AF00)` | 64 bytes | explicit function creation | allocates `0x198` bytes and calls the ammunition constructor |
| `WpMGunAmmoConstructor` | `[0x1000B090, 0x1000B246)` | 438 bytes | automatic | installs the four-second lifetime, acceleration, and optional tracer state |
| `AfAmmo::ProcessEvent` | `[0x100247A0, 0x100257A2)` | 4,098 bytes | automatic | event `0xE2` copies creator, target, velocity and position, resolves the room, positions an optional visual, and activates the projectile |

Ghidra remains canonical for the MSVC class, import, and pseudocode context.
Rizin independently confirms the exact boundaries, bytes, calls, and branches.
Both tools show the same material order inside `WpMGunRefresh`:

1. subtract the due cadence period;
2. load and advance the barrel;
3. load and conditionally decrement ammunition;
4. create the private ammunition instance;
5. return from the shot branch when creation fails; and
6. only after creation succeeds, update attachments and construct event
   `0xE2`.

Therefore a native allocation failure consumes the weapon transition but does
not consume muzzle or event-only inputs.

## Portable policy

`LegacyMachineGunProjectileRuntime` replaces the private type-system allocator
with caller-owned, fixed-capacity storage:

- the single simulation writer supplies a span of projectile slots;
- the first inactive slot whose 64-bit generation can advance is selected;
- successful activation writes the recovered semantic `0xE2` state and the
  technology-matched ammunition profile into that slot;
- deactivation makes a slot reusable;
- each reuse advances the generation, so a stale handle cannot resolve to a
  later projectile; and
- generation wrap is rejected rather than aliasing an old identity.

First-free selection and the pool capacity are explicit deterministic port
policies. They do not claim that the original private allocator used a pool or
the same selection order.

The operation is bounded, `noexcept`, and allocation-free. It commits the
returned fire state immediately after the recovered timing transition and
before inspecting capacity. It inspects muzzle and payload-only values only
after a usable slot exists.

## Result contract

| Result | Fire state | Payload-only inputs | Projectile storage |
|---|---|---|---|
| `invalidInput` | unchanged | not consumed | unchanged |
| `noShotDue` | committed | not consumed | not inspected |
| `capacityExhausted` | committed | not consumed | unchanged |
| `generationExhausted` | committed | not consumed | unchanged |
| `activationRejected` | committed | consumed and rejected fail-closed | unchanged |
| `activated` | committed | consumed | one slot atomically replaced and a generation handle returned |

`activationRejected` is a portable safety result for malformed or non-finite
semantic input after capacity acquisition. The native code has no equivalent
typed error result.

## Validation

Focused tests cover:

- no-shot behavior without inspecting an invalid payload or a full pool;
- complete event-`0xE2` position, velocity, room, creator, profile, technology,
  cadence, barrel, and ammunition mapping;
- full and empty capacity with already-advanced fire state;
- generation exhaustion without wrap and selection of a later usable slot;
- fail-closed invalid timing and post-capacity payload rejection;
- deactivation, reuse, mutable and const handle resolution, stale generation
  rejection, and out-of-range rejection; and
- 4,096 successful activations with zero observed heap allocations.

A fresh Windows GCC 15.2 Release build and separate code-intelligence build
each compile all 268 steps and pass 83/83 tests. The regenerated compilation
database has 171 portable entries and no Apple-only source. Clangd 22.1.8
reports zero errors in the runtime, focused test, and affected coordinator
translation units with the trusted GCC query driver.

The Ghidra, working-copy, and Rizin wrapper suites pass, as do all 12 Rizin
report-normalization tests. Synthetic public-boundary tests and the 410-file
public scan pass. The 265-row, 14-column function catalogue retains unique IDs
and module/RVA pairs, valid source paths, and only its two known missing
references. `actionlint`, the 19-file changed-scope local-path scan, and
`git diff --check` are clean. Isolated WSL and GitHub Actions remain the
publication gates for this slice.

No proprietary data, binary, analyzer project, or local path is required by
the implementation or tests.

## Remaining boundary

The next runtime join must:

- feed authored live rotated-muzzle snapshots from the changing aircraft
  producer;
- advance active generation-tagged slots through the published collision
  transaction and terminal command reducer;
- consume terminal damage and surface commands through concrete live
  actor/effect adapters;
- realize the optional tracer and ricochet visuals; and
- compare the complete path against controlled executable traces.

## Confidence

Confidence is **3/3** for the native state-before-allocation ordering, delayed
attachment/event consumption, private factory/constructor boundaries, and
event-`0xE2` activation mapping.

Confidence is **2/3** for complete runtime parity until live producer,
collision-step, effect, and controlled-trace integration exists. The fixed
pool itself is an intentional port design and is not assigned a native-parity
confidence.

## Related material

- [Primary machine-gun spawn](EXP-20260728-030-primary-machine-gun-spawn.md)
- [Machine-gun projectile contact](EXP-20260728-031-machine-gun-projectile-contact.md)
- [Machine-gun shot coordinator](EXP-20260728-032-machine-gun-shot-coordinator.md)
- [Projectile runtime query adapter](EXP-20260729-043-projectile-runtime-query-adapter.md)
- [Projectile terminal collision commit](EXP-20260729-045-projectile-terminal-collision-commit.md)
- [Projectile creator BSP guard](EXP-20260729-046-projectile-creator-bsp-guard.md)
