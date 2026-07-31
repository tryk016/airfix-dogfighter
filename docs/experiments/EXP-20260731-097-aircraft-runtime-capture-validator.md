# EXP-20260731-097: AirCraft runtime capture validator

**Status:** implemented with synthetic validation; no real game capture run
**Decision:** GO for collecting and validating a private, controlled runtime
observation; NO-GO remains for a portable rigid-body kernel or live flight
integration

## Question

Can the final branch-time x87, thread, and 12 ms ordering evidence be collected
in a bounded form without committing original binaries, debugger databases,
paths, arbitrary memory, or private trace data?

## Result

Yes. `tools/re/validate_aircraft_runtime_capture.py` implements a strict JSONL
contract for the exact reference `Dogfighter.exe`, `AfEngine.dll`, and
`Cc.dll` module hashes and 14 recovered module/RVA sites. It accepts no local
path field and emits no captured payload, state, hash, or filename.

Every sample binds an exact site ID, module, RVA, thread ID, x87 CW/SW, and
MXCSR. Event samples add only axis, signed payload, and the observed field
store. Rigid-body samples add exactly the 13 recovered state DWORDs and,
optionally, one V1-V6 result object. Ordering records model local input,
`PollRemote`, time-dependent dispatch, zero or more AirCraft refreshes, and
the pre-render boundary.

The validator uses exact rational arithmetic to reproduce the native signed
`int32 * binary32(0x3D3020C5)` x87 multiply and binary32 store under PC24,
PC53, or PC64 and every RC mode. The public PC53/nearest-even 11-vector table
therefore remains executable evidence rather than decimal floating-point test
data. V1-V6 use the exact public bit patterns from `EXP-20260730-068`; V5
distinguishes PC24 from the native PC53/PC64 spill asymmetry, while V6 supports
all documented PC/RC outcomes.

## Two profiles

An `observation` may be incomplete. It validates every present record and
returns a deliberate `NO-GO`, allowing consumer sites to be gathered in
phases without interpreting absence as success.

An `acceptance` fails closed unless it has:

- all exact sites and authenticated modules;
- startup-before/startup-after as the first two samples;
- one stable thread, x87 control word, and MXCSR control policy after startup;
- PC53/nearest-even and masked exceptions, with sticky x87/MXCSR status flags
  recorded but not treated as policy changes;
- zero-, one-, and multi-refresh outer-loop examples with exact 12 ms due-time
  spacing;
- all 11 event payload/store vectors; and
- all six rigid-body oracle vectors at their correct function anchors.

Unknown or category-incompatible fields, duplicate JSON keys, sequence gaps,
wrong addresses, thread/policy changes, invalid word widths, symlinks,
non-regular inputs, invalid UTF-8, embedded NUL, oversized files/lines, and
oracle mismatches are rejected.

## Safety

The later dynamic procedure uses ordinary-user x32dbg against a disposable
working copy, with hardware execution breakpoints changed between phases in
one process. It prohibits memory/file patching, software breakpoint byte
replacement, administrator execution, cloud analysis, and broad dumps.

Raw `.dd32`, `.trace32`, normalized `*.aircraft-capture.jsonl`, backups, and
local trace/dump directories remain ignored. The public-boundary scanner has
a dedicated ending check and synthetic regression for the normalized capture
format.

## Validation boundary

The public tests construct only synthetic JSONL. They cover the complete GO
profile, partial NO-GO observations, all 11 event vectors, V5/V6 policy
discriminators, wrong RVA, thread and policy changes, event/state/vector
mismatches, scheduler order, missing evidence, unknown fields, duplicate
keys, and CLI redaction.

Full portable/Windows builds, CTest, boundary scans, and hosted platform
validation remain publication gates for this branch.

## Final decision

**GO** for the controlled private capture described in
`docs/toolchain/AIRCRAFT-RUNTIME-CAPTURE.md`.

**NO-GO** remains for `CcRigidBody` runtime code, an assumed 12 ms producer,
live input/Q15 wiring, force-law or PRNG choices, and product aircraft motion
until an acceptance capture exists and is reviewed.

No original asset, executable, debugger database, trace, local path, or real
runtime value was added to the repository.
