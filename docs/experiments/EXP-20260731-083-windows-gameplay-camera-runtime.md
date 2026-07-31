# EXP-20260731-083: Windows gameplay-camera runtime parity

- Date: 2026-07-31
- Status: implemented and validated locally and in the hosted platform matrix
- Scope: Windows D3D11 mission ownership and render-frame consumption

## Question

Can the Windows renderer own and consume the same bounded, replacement-safe
mission camera runtime as Metal without guessing the missing AirCraft producer,
attaching it to the 60 Hz input pump, or weakening transactional mission
installation?

## Implemented boundary

The D3D11 mission snapshot replaces its immutable bootstrap camera packet with
one strong `LegacyGameplayCameraMissionRuntime` owner. Construction moves the
authenticated spatial arena, placed dynamic collision, and optional player
collision into that runtime only after publication validation, draw-plan
validation, pose planning, and GPU-resource preparation have succeeded.

The runtime bootstrap must publish simulation step `0` and camera generation
`1`. Windows exposes only a weak producer endpoint. Replacing the installed
mission or destroying the renderer therefore expires the old endpoint rather
than allowing a producer to mutate detached scene state.

## Render-frame consistency

Each gameplay frame attempts one camera-packet acquisition before building the
native render layout. The resulting lease remains alive through the complete
draw pass, so the camera canvas, projection, and every per-draw constant buffer
come from one simulation step and one publication generation. A missed or
invalid acquisition drops the gameplay frame; it cannot silently fall back to
the old frozen bootstrap packet.

Settings and resize transactions cannot borrow the single-consumer packet
exchange outside a rendered frame. The mission snapshot therefore retains only
the validated bootstrap canvas and horizontal FOV as immutable layout-planning
metadata. Actual gameplay rendering always uses the leased current packet.

## Scheduling boundary

The Windows product shell retains the weak endpoint with the authenticated
mission transaction and includes its lifetime in resume readiness. It
deliberately does not call `tryAdvance()` from the 60 Hz semantic-input loop.
The recovered camera requires complete live AirCraft positions, factor inputs,
world-room state, and scheduler delta. That producer remains gated on the
separate 12 ms/x87 evidence and numeric-policy work.

## Validation

- A fresh portable GCC 15.2/Ninja build completed 383 steps and passed all
  120 tests, including the camera mission-runtime and packet-exchange suites.
- The current MSVC 19.51/Ninja Windows product build completed all pending
  targets and passed 130/130 tests, including the D3D11 renderer test and both
  native product smoke modes.
- The D3D11 test installs a wholly synthetic, texture-free mission. It verifies
  step `0`/generation `1`, directly publishes step `1`/generation `2`, renders
  the advanced packet, rejects a malformed replacement without losing the
  active endpoint, and proves that a valid replacement expires the old weak
  endpoint and restarts at step `0`/generation `1`.
- Independent review found no functional, ownership, transaction, SPSC, or
  scheduling defects.
- Synthetic public-boundary tests, the 622-file repository scan, changed-range
  formatting, local-path review, and `git diff --check` passed.

GitHub Actions passed all seven publication jobs for commit `66ba042`:
Ubuntu 24.04, macOS 26, Windows 2025, the dedicated Windows x64 D3D11/XAudio2
product smoke, clangd, iPhoneOS, and iPhoneSimulator.

## Result

GO for equal Windows/Metal ownership, endpoint lifetime, and coherent
frame-consumer semantics.

NO-GO for claiming a moving camera or playable flight. No platform loop may
advance this runtime until the trace-driven AirCraft producer and its distinct
scheduler contract are implemented and validated.
