# EXP-20260801-109: complete AirCraft HUD render event

**Date:** 2026-08-01

**Evidence ID:** `EV-20260801-008`

**Decision:** GO for the dormant authenticated full-stage transaction and its
native-order D3D11/Metal consumers. NO-GO for ordinary-frame publication until
one verified live producer and an accepted x87 conversion policy exist.

## Question

Can every recovered subsection of AirCraft RVA `0x00006B00` be published as
one all-or-nothing render event while preserving its shared elapsed clock and
without inventing live gameplay values?

## Evidence boundary

Ghidra 12.1.2 remains the primary source for the hash-verified
`AirCraft.type`; Rizin 0.9.1 independently confirms the exact enclosing range
`[0x10006B00,0x10007321)`, calls, branches, and instruction order. This slice
adds no new semantic label. It composes only the previously verified evidence
`EV-20260801-001` through `EV-20260801-007`:

1. analog instruments;
2. right then left instrument readouts;
3. primary/secondary panel backgrounds, then primary and secondary content;
4. armour/health gauge;
5. aircraft icon, health number, team badge, and technology number; and
6. the final one-time elapsed-clock reset.

No game executable, debugger, original image, private manifest, or private
asset is used by the implementation or tests. The unsuccessful x32dbg Phase A
capture remains `NO-GO` with `0/32` accepted hits and contributes no value,
thread, owner, control-word, or status-word conclusion.

## Transaction contract

`LegacyAircraftHudRenderEventInput` owns one gate snapshot and one logical
screen extent for all subsections. The composer begins the existing elapsed-
clock stage and requires all six consumer views to be finite, non-negative,
and bit-identical. It then builds plans and authenticated submissions in the
native structural order.

Publication occurs only after all nested packets share the same content
revision, exact verified-content transaction identity, UI scale, and expected
owners. The result contains the full event plus six availability/state pairs.
Any failure returns neither. Clock commit is last; it resets to exact positive
zero only after complete validation. Abort retains the accumulated elapsed
value and permits a later stage with a fresh token.

The boundary owns no scheduler, actor lookup, camera lookup, backend callback,
or quantization. It accepts already produced signed int32 values at the same
boundary used by the individual plans.

## Backend contract

The dormant D3D11 consumer rejects mixed full-event/individual HUD packets,
prevalidates all authenticated owners and GPU texture arrays, and issues the
fixed traversal. Its draw-call diagnostics include every nested readout digit.

The dormant Metal consumer applies the same owner, transaction, resource, and
command-count checks, encodes the same sequence, and returns success only when
the encoded count equals the complete event count. A future caller must discard
the command buffer after any mismatch. The ordinary `MTKView` callback remains
unchanged.

## Synthetic validation

Tests use one synthetic AFPACK containing only generated RGBA8 GTIs. They
verify:

- complete five-subsection publication and six equal elapsed consumers;
- exact positive-zero commit only after total success;
- native gate rejection and late-plan rollback without state publication;
- rejection of one owner from another authenticated transaction at the final
  event boundary;
- nested command-order and zero-token forgery rejection; and
- fresh tokens for successive complete events.

Clean portable GCC builds pass `142/142` CTests. Visual Studio 2026 MSVC 19.51
builds the complete Windows SDL3/D3D11/XAudio2 product and passes `154/154`
CTests, including both product smoke tests. Hosted iPhoneOS and simulator
compilation is delegated to GitHub Actions because the local host is Windows.

## Remaining gates

- Produce one coherent live snapshot for all fields and gates.
- Preserve the native zero/one/many refresh relationship when connecting the
  scheduler to the clock.
- Accept live `_ftol`/x87 policy before quantizing float-derived values.
- Validate composed output on the two target iPhones and required aspect
  ratios.
