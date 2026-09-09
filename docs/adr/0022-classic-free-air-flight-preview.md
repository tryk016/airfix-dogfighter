# ADR-0022: explicit Classic free-air flight policy

- Status: accepted for an opt-in Windows development preview, not gameplay parity
- Date: 2026-09-09
- Policy identifier: `classic-free-air-v1`

## Decision and scope

Proceed with a tested compatibility implementation of the recovered equations
before consumer-site x87 captures and native trajectory comparisons. This
implements the approved Classic-first priority without claiming those missing
observations exist. The default authenticated mission viewer is unchanged.
The separate C++20 `ClassicFlightPreview` and `ClassicFlightPreviewSession`
produce live motion and camera inputs only when Windows explicitly receives
`--flight-preview`. iOS compiles/tests the shared code but is not wired to this
preview. `Classic`/`Enhanced` visual settings do not select a physics policy.

This is **free-air testing, not a playable mission**. There is no aircraft
contact, floor support, wall response, portal ownership update, sleep gate,
water/ceiling response, damage, destruction, projectile, AI, mission VM or
campaign integration. The aircraft can fall through geometry. The recovered
camera retains its existing static collision behavior; that is not aircraft
collision. Missing behavior must not be disguised with a clamped height,
invented lift, an automatic launch, or fabricated health/damage.

## Reused evidence

- [AirCraft force-step contract](../re/systems/AIRCRAFT-FLIGHT-LAW.md): positive-
  health, intact-engine, off-ground branch, throttle smoothing, direct gravity,
  conditional lift-like force, thrust, pitch/bank torques, stabilization, side
  force, local-momentum damping and the engine-phase recurrence.
- [CcRigidBody contract](../re/systems/CC-RIGID-BODY.md) and
  [EXP-068](../experiments/EXP-20260730-068-rigid-body-integration-kernel.md):
  position, wxyz quaternion, linear/angular momentum, old force/torque Euler
  step, quaternion normalization, refreshed auxiliary values, and row-vector
  angular-velocity multiplication. Matrix accumulation uses `k=0,2,1`.
- [EXP-077](../experiments/EXP-20260730-077-aircraft-runtime-prerequisites.md):
  all 17 typed registration records. The constructor at AirCraft RVA `0x1EA0`
  adds blocks `(0.2,0.055,0.05)` and `(0.05,0.055,0.15)` with weight `0.375*B`
  each, then a particle at `(0,0,0.1)` with weight `0.125*B`. Total mass is
  therefore `0.875*B`, not `B`; diagonal inertia uses the recovered binary32
  `1/12` constant `0x3DAAAAAB`. Angular damping is `6.0`. No physical units
  beyond the source-coordinate/time contracts are asserted.

The implementation initializes all fields. Spawn position and rotation come
from the authenticated mission; momentum and force/torque start at zero,
throttle/engine phase start at rest. There is no spawn displacement or injected
velocity. The starting engine recurrence is advanced but its audio commands
are not yet routed to sound playback. Propeller animation is also deferred.

## Deliberate port policies, not native facts

1. **Arithmetic:** finite binary32 state stores with binary64 expressions,
   round-to-nearest environment, contraction disabled for the kernel, and
   host square root for normalization. This is not an emulation of every x87
   spill, PC/RC setting, exponent range, exception, denormal, or library call.
   Existing isolated event/throttle/engine helpers keep their documented
   policies. A foreign rounding mode, unsupported input, overflow/non-finite
   derived state, or exhausted step counter rejects a step without committing
   any state. Replays are tested within a build; cross-target bit parity and
   native trajectory tolerances remain unproven.
2. **Time:** physics uses fixed `0.012F` seconds, independently of the existing
   60 Hz input-intention recorder. Integer nanoseconds retain fractional debt.
   A host iteration executes at most eight steps; excess whole steps are
   discarded with a path-free diagnostic. Pausing drops debt. Resume waits for
   a fresh gameplay input frame and never integrates time spent paused.
3. **Input:** the latest accepted gameplay sample is held for the host's next
   physics batch. Symmetric nearest quantization maps Q15 pitch/bank to
   `[-32,32]` and throttle delta to `[-255,255]`, then uses the existing native
   reducers. There is no claim of original inter-producer order or DirectInput
   timing. Absolute throttle, mouse camera-look, weapons and recenter are not
   consumed by this preview. Keyboard/controller bindings still use the shared
   router; no new device dependency is introduced.
4. **Camera:** the same live source-world position supplies chase position and
   anchor, with initialized type health and inactive=false. This equality is
   an explicit preview assumption, not proof those native fields always agree.
   Rear view and cycle counters feed the existing camera on the physics clock.
5. **Presentation:** source units and orientation convert through the mission's
   basis exactly once. A mismatched source/runtime bootstrap is rejected.
   Texture/mission replacement creates a new session, step counter and input
   counter baseline. No old state may enter the new scene. Busy publication
   does not rewind physics; invalid state/endpoints halt the preview and pause
   the session instead of silently returning to a frozen aircraft.

## Running locally

Append `--flight-preview` to a working authenticated Windows mission launch
(`--content-root` or `--installed-content`, plus `--setup`, `--level`, and the
appropriate player object). It requires a recognized authenticated aircraft
type and cannot be combined with private reference captures, public smoke,
import, or validation-only modes. The window title and console identify the
free-air/no-aircraft-collision limitation. Default Windows bindings use arrows
for pitch/bank, W/S for throttle, C for camera, R for rear view and Esc for pause;
custom profiles remain authoritative. Restart the preview to reset the flight.

No content is copied or written by the flight kernel. Original files, private
packages, tool databases, recordings and screenshots remain outside Git and CI.

## Validation and next acceptance

Synthetic tests cover all 17 parameter sets, constructor inertia, quaternion
conversion including 180-degree cases, forces/torque direction, old-F/T phase
latency, repeated trajectories, unit quaternion maintenance, malformed inputs,
rounding-mode rejection, overflow-safe clock arithmetic, stalled frames,
pause debt, basis conversion, scene reset and live pose/chase publication.
The Windows renderer test also consumes physics-generated camera packets.

Next: add a separately tested aircraft contact/room-transition adapter, then
join coherent actor/collider/HUD/audio state. Validate takeoff, floor/wall and
inverted contacts, water/ceiling and native comparisons before closing the
[controllable-aircraft and collision gates](../design/WINDOWS-PLAYABILITY.md).
Keep the exact-native implementation NO-GO separate from this accepted port
policy. Neither this ADR nor synthetic/GPU tests close whole-game acceptance.
