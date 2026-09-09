# Windows Classic playability gates

This is the execution order for the Classic-first priority in
[the project plan](../PROJECT-PLAN.md#critical-path), not a new renderer or a
claim that the current application is a playable game. The acceptance contract
remains [ADR-0007](../adr/0007-windows-x64-parallel-target.md#acceptance-boundary).
Original content and executable-reference captures stay owner-local.

## Ordered path to a complete Windows game

| Gate | Present boundary | Acceptance needed to close it |
|---|---|---|
| 1. Build, content, presentation | SDL3/D3D11/XAudio2 shell, authenticated content import, room/model rendering, input/settings, diagnostic paths exist | Clean x64 build/tests; private mission load/reload without stale resources; keyboard/mouse/controller and lifecycle checks |
| 2. Controllable aircraft | Opt-in Windows free-air producer now connects input, recovered equations, live pose and camera under ADR-0022; default viewer stays frozen | Validate contacts/takeoff and native trajectories; coherent changing position/orientation/health per simulation step; no claim of full gameplay or bit parity from free-air tests |
| 3. Camera and collisions | Recovered projection, camera, pose publication, and collision boundaries exist separately | Chase/rear/cycle use live state; takeoff, walls, ground, inverted/water branches and pause/resume pass reference comparisons without camera or collider desynchronization |
| 4. Combat and actors | Weapon/projectile/damage helpers exist separately | Live muzzle and target transforms, ammo, primary/secondary fire, collision, damage, destruction, HUD and audio agree within the same simulation generation |
| 5. AI | Task/controls/dispatcher research and isolated control helpers exist | Recovered tasks, target transitions, event ordering and force timing drive live actors; remaining wrappers and numerical assumptions are resolved or explicitly policy-labelled |
| 6. Mission scripts | Declaration order and outcome reducers exist; all 70 opcodes now have bounded framing and structural CFG inspection | Bounded source parser/compiler, typed values/handles, stack/call validation, instruction/call/process budgets, local scheduler lifecycle and proven mission integration; real success/failure conditions |
| 7. Campaign and persistence | Numeric campaign model, versioned store, and import boundaries exist separately | Mission result updates the intended profile exactly once; unlock/next/retry/save/reload paths work; missing/corrupt/future saves remain safe; rewards require their own evidence |
| 8. Whole-product acceptance | Automated tests and data-less smoke are not end-to-end gameplay acceptance | Complete both single-player campaign paths in the required scope, clean restart/save recovery, usable controls/menus/audio, no critical crashes, measured frame/memory budgets and owner-local reference comparisons |

The first delivery checkpoint is **one complete playable mission**, then the
same shared systems must cover the remaining campaign content. New lighting,
shadows, reflections, HD expansion, and post-processing are not dependencies
of any gate; ray tracing remains excluded. Existing native resolution, Hor+,
sharp UI, working options, and private-content safety are maintained.

Do not turn a static reconstruction into a live producer by assuming that the
60 Hz input tick is the recovered 12 ms physics cadence. Unobserved x87 control,
rounding, exception state, producer order and driver timing remain unknowns.
An alternative compatibility policy must be explicit and tested, not reported
as native bit parity. A device-only unknown blocks its dependent acceptance,
not unrelated Windows code or static research.

## Current flight checkpoint

[ADR-0022](../adr/0022-classic-free-air-flight-preview.md) defines the approved
compatibility policy and opt-in `--flight-preview`. This is not a completed
mission: aircraft collisions, ground support, damage, audio and mission logic
are still disconnected. No artificial launch or floor clamp hides that gap.
Next connect bounded contact/room transitions while keeping pose and camera
on the independent 12 ms clock.

## Independent implementation track: AFS

1. **Implemented:** all 70 opcode widths/operands, bounded inline text, local
   and process indices, absolute instruction-boundary targets, labelled CFG,
   atomic rejection, and unresolved execution requirement flags. See
   [the structural inspection contract](../formats/AFS.md#portable-structural-inspection).
2. Add a checked typed model and stack/CFG validation before executing any
   instruction. Native descriptor words require typed linkage, never casts to
   pointers. Null-dependent stack effects and variable native-call signatures
   need explicit representations, not fixed guessed stack deltas.
3. Specify and implement the bounded source grammar/compiler using recovered
   evidence and synthetic fixtures; resolve declarations transactionally.
4. Add budgeted execution and local process scheduling with allocation,
   re-entrancy, mutation/destruction, delay and pause tests. Keep numerical
   compatibility claims separate from a selected safe execution policy.
5. Connect verified mission handlers/outcomes only after lifecycle and global
   order are established; then close the campaign result/store transaction.

These stages may advance while gate 2's runtime evidence remains incomplete.
Structural validation alone does not unlock the interpreter or mission start.

## Validation discipline

Every code stage gets focused synthetic tests, the full Windows product
build/test suite, and public-boundary/document checks. Shared-core changes also
retain Linux/macOS builds and unsigned iOS/simulator CI. Native desktop smoke
is run without proprietary content; private play sessions are separate and
must not change the original installation.

Use deterministic scenarios and explicit pass/fail criteria rather than a
percentage inferred from file counts or helper-test counts. Record which gate
was actually closed and which adapters remain disconnected. Manual Mac/iPhone
acceptance is grouped into a later stable gameplay milestone, without claiming
Windows or CI results prove device behavior or performance.
