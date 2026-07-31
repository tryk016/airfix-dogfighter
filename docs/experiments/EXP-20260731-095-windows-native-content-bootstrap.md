# EXP-20260731-095: Windows native private-content bootstrap

**Date:** 2026-07-31

**Evidence composed:** ADR-0005, the AFPACK installer/recovery contracts, and
the existing iOS private-content lifecycle

**Status:** native pre-game Windows picker/progress/retry/rollback manager
implemented; physical interactive acceptance remains pending

## Question

Can the Windows product expose owner-private AFPACK installation and verified
rollback through native operating-system UI without introducing another
parser, store, path-bearing diagnostic surface, or live mission reload?

## Product contract

`AirfixDogfighter.exe --manage-installed-content` resolves only the existing
undisclosed SDL-private content root. The modal coordinator:

1. inspects the authenticated active record and current/previous packages;
2. offers import, retry, close, and rollback only when the inspected state
   admits that action; a known-good previous generation must be restored before
   replacement import, while an indeterminate store permits only retry/close;
3. selects an owner package through `IFileOpenDialog` with an `.afpack` filter;
4. maps portable install/recovery progress to checking, copying,
   authenticating, activating, restoring, and complete;
5. forwards cancellation from `IProgressDialog` through `std::stop_token`;
6. serializes import and rollback with the existing per-user writer mutex;
7. re-inspects the durable store after every ordinary attempted mutation,
   while an ambiguous commit terminates the manager; and
8. displays only fixed status/action text.

Outside the owner-controlled OS picker, the UI never echoes or logs the
selected source path, private store path, logical archive path, checksum,
generation identifier, or backend exception.
An ordinary failure retains the previously known active generation. An
ambiguous post-commit state is labelled separately, terminates that manager
run, and requires restart plus a fresh check before another mutation. No action
reloads a running mission.

This action gate protects AFAC history: importing while `rollbackAvailable`
would rotate the invalid current generation into `previous` and discard the
only verified rollback reference. Import is therefore admitted only from a
resolved `noContent`, `ready`, or `unusable` state; malformed active records
still fail closed inside the portable installer.

Common Controls version 6 is selected through an embedded public application
manifest. `TaskDialogIndirect` is resolved only after startup so a missing
native UI prerequisite fails inside the explicit manager path rather than
blocking the data-less D3D11 product smoke.

## Synthetic verification

The coordinator tests cover ready close, picker cancellation, successful
import followed by reinspection, authenticated rollback followed by
reinspection, rejection of an unavailable rollback action, all typed redacted
failure categories, blocked replacement while recovery/availability is
uncertain, a bounded retry loop, and incomplete callback rejection.
The native prerequisite test creates the picker and progress COM classes and
resolves the TaskDialog entry point without displaying a window or reading
private content. Command-line tests cover parsing, duplication, and mutual
exclusion with render, capture, mission/content, smoke, and one-shot import
modes.

The clean MSVC 19.51 HostX64/Ninja build produces the complete Windows product
and passes 139/139 CTests, including both D3D11 product smokes. The public test
suite uses no proprietary content.

## Independent review

An independent read-only review found and drove fail-closed corrections:

- `rollbackAvailable` is not active-ready content;
- the verified previous generation must be restored before replacement import,
  and an indeterminate store cannot import;
- an ambiguous commit must terminate the manager; and
- native status-dialog failure is a product failure rather than a simulated
  user close.

The reviewer rechecked the corrected diff, COM lifetimes, Common Controls v6
manifest, redacted product boundary, coordinator, and focused tests and
reported no remaining P0-P3 findings.

## Result

**GO** for the implemented pre-game owner-private content manager and continued
data-less CI. **NO-GO** for hot reload, displaying package internals, or
claiming physical interactive acceptance from synthetic tests alone.
