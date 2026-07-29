# EXP-20260729-057: transactional Metal presentation settings

- Date: 2026-07-29
- Status: implemented and hosted-CI validated
- Scope: iOS Metal render-presentation snapshot, target ownership, and resize
  recovery

## Question

Can Metal apply render scale, scene presentation, diagnostics, and visual
profile as one coherent state without publishing a setting before its complete
GPU resources exist, while retaining resources referenced by in-flight command
buffers?

## Portable transaction

`RenderPresentationTransaction` is a single-executor C++20 owner shared by
platform adapters. A prepared candidate contains:

- the complete validated `RenderPresentationSettings`;
- opaque view and device identities;
- positive output extent and surface generation;
- base and candidate revisions;
- the exact derived render-target extent; and
- no target owner at exactly 100%, otherwise one complete copyable target
  bundle with distinct color/depth identities and accounted bytes.

Preparation is read-only with respect to the active state. It reuses a complete
bundle only for a compatible device and exact target extent. Final validation
rejects stale revision, view, device, output extent, or generation. The caller
then performs an immediate no-fail move commit without callbacks or dispatch.

## Metal ownership and budget

The Metal factory first reserves the conservative BGRA8 plus Depth32 byte count
from `SnapshotGpuBudgetLedger`, allocates both private tracked textures, measures
their actual allocation sizes, and reconciles any page-rounded difference. A
partial pair or supplemental-budget failure destroys the candidate and releases
its reservation.

The opaque portable owner retains an Objective-C++ bundle containing both
textures and the exact reservation. Each draw copies the active state once.
That copy is captured by the Metal command-buffer completion handler, so an
older bundle and its budget debit remain alive until the GPU no longer
references them. Diagnostics starts from ledger-reserved bytes and therefore
does not add the scaled textures a second time.

## Exact scale behavior

Exactly `100%` renders the 3D scene directly to the native drawable/depth
attachments and owns no intermediate target. Every other accepted scale uses
the complete offscreen pair and native-output presentation pass. This decision
depends on the setting value, not on whether integer rounding happens to make
the target dimensions equal to the output.

The frame builds camera/layout values only from its copied presentation
snapshot. It rejects a drawable whose physical extent differs from the
snapshot's prepared output extent.

## Resize and retry

A positive resize increments the surface generation and attempts to prepare a
complete replacement. Failure preserves the old active snapshot, but the
renderer does not submit it to a mismatched drawable. Automatic recovery tries
once before the next eligible frame, then skips 1, 2, 4, ... up to 120 frames
between consecutive failures.

An explicit resize, successful publication, or zero extent resets the schedule.
Zero extent suspends drawing without deleting settings or the last complete
target bundle. A later compatible positive extent may reuse that bundle.

## Diagnostics and profile policy

The iOS shell no longer forces diagnostics on. The canonical settings default
is honored, and disabling diagnostics clears only best-effort overlay resources.
`Enhanced` is retained in the immutable snapshot but does not claim or activate
modern lighting, materials, or post-processing in this experiment.

## Synthetic evidence

Portable tests cover:

- 100 -> 50 -> 200 -> 100 with exact target extents and identities;
- Original 4:3, diagnostics, and Enhanced changes reusing a compatible bundle;
- consecutive late factory failures preserving the active owner and bytes;
- stale active revision, view, device, extent, and generation;
- 50% and 200% resize plus zero-extent retention/recovery;
- deterministic immediate/1/2/4/.../120-frame retry behavior;
- incomplete, aliased, under-accounted, missing-factory, invalid-setting, and
  overflow rejection; and
- active, prepared, copied, and in-flight owner lifetimes.

Local validation:

- GCC/Ninja portable build: passed;
- complete local CTest suite: 96/96 passed;
- `git diff --check`: passed apart from line-ending notices.

Hosted unsigned iPhoneOS and iPhoneSimulator builds pass on Xcode 26.6. The
portable clangd, Ubuntu, macOS ARM64, Windows, and native Windows
D3D11/XAudio2-smoke jobs pass in the same seven-check PR matrix. Two independent
reviews reached GO with no remaining P0-P2. Runtime allocation-failure injection
and physical-device acceptance remain later work because CI does not provide
deterministic Metal device-failure control.

## Limits

- This slice does not implement iOS settings persistence, its asynchronous
  prepared-token save gate, final settings UI, or launch overrides.
- The diagnostics overlay remains a separate best-effort output-resolution
  cache; it cannot invalidate a valid scene snapshot.
- No original asset, private texture, content path, checksum, or owner-derived
  image is included.

## Related

- [ADR-0014](../adr/0014-render-presentation-settings.md)
- [D3D11 transaction](EXP-20260729-055-windows-transactional-render-settings.md)
- [Native render-scale targets](EXP-20260729-050-backend-render-scale-targets.md)
