# EXP-20260801-099: authenticated health-gauge textures

**Status:** cross-archive loader and native GPU resource staging implemented;
ordinary drawing intentionally disabled

**Evidence ID:** `EV-20260801-001`
**Decision:** GO for atomic authenticated ownership and D3D11/Metal staging;
NO-GO for visible gauge submission until the screen-call contract is complete

## Question

Can the two recovered AirCraft health-gauge texture roles be loaded and owned
by both native backends without host-path reopen, archive confusion, dense-ID
collision, partial publication, or an unsupported draw-state guess?

## Provenance contract

The recovered type loader and private archive inventory establish different
origins for the two roles:

| Role | Exact logical path | Required nested archive | Selected contract |
|---|---|---|---|
| meter background | `Graphics\Ingame\HUD\armour_meter.gti` | `Resource.up` | format 8, `128x128` |
| localized foreground | `Graphics\Ingame\HUD\armour.gti` | manifest-selected localization archive | format 8, `128x128` |

All supported localization archives contain the foreground role, but runtime
selection comes exclusively from the authenticated AFPACK manifest. No locale
or host filesystem path is guessed by the loader.

## Session and recovery change

`ActiveContentLease` and `VerifiedContentSession` now retain both nested UDSP
archive views and their exact AFPACK entry indices. Direct opening parses the
source entry and the single strict-manifest localization entry from the same
already-authenticated seekable handle. Recovery opens both before publishing a
ready lease. Adoption checks path, kind, offset, size, manifest identity, pack
metadata, and the unchanged stream token before moving either view.

Source and localization reads share the session's serialized handle. The
diagnostic label is never opened as a path, and moving the session invalidates
the old transaction identity exactly as before.

## Atomic loader

`LegacyAircraftHealthGaugeTextureSet` treats archive kind as part of each role
key. This matters because the independent UDSP tables may assign the same file
index to both textures. The dedicated texture IDs `0` and `1` are local to this
set and cannot be merged by value into mission or crosshair namespaces.

The loader performs, in order:

1. unchanged session/revision and limit validation;
2. exact archive-specific lookup of both logical paths;
3. per-source and aggregate stored/unpacked footprint preflight;
4. bounded GTI parse and upload-plan construction;
5. exact format/dimension and aggregate decoded/upload/resident checks;
6. bounded pixel materialization and byte-for-byte plan agreement;
7. final session identity check and one all-or-nothing publication.

Cancellation, callback failure, missing/ambiguous entries, wrong archive,
malformed GTI, unexpected format/dimensions, overflow, allocation failure, or
any budget overrun leaves the result without a texture set.

## Native resource staging

Windows makes the gauge set mandatory for a private mission, validates it
against the mission revision, creates both D3D11 shader-resource views and
required mip levels before moving collision/runtime ownership, and publishes
them in the same no-fail mission swap. The renderer's GPU-memory estimate now
includes both resources.

iOS moves the set through the private Objective-C snapshot, validates it in
the worker-side Metal preflight, includes both descriptors in logical and heap
budgets, creates/uploads every texture before publication, generates mips when
requested, and retains the authenticated C++ set beside the room and
crosshairs. A single failure destroys the unpublished candidate and preserves
the active snapshot.

Both products still issue no gauge draw call. Static evidence establishes the
ordered plan, but the generic native screen calls used by this HUD stage do not
yet justify guessing a blend/depth state or bypassing the modern UI transform.

## Validation

Synthetic fixtures build separate source and localization UDSP archives inside
one valid AFPACK. Tests cover a successful equal-file-index cross-archive pair,
missing localized content, a foreground placed in the wrong archive, malformed
dimensions, valid-but-disallowed format, invalid limits, cancellation,
throwing progress callbacks, and moved-session rejection. No original asset or
private path enters the test suite.

The complete portable Ninja build passes 130/130 CTests. A real MSVC 19.51
HostX64/Ninja build compiles the complete Windows application and passes
142/142 CTests, including D3D11 renderer and both product smoke tests. Hosted
iPhoneOS and iPhoneSimulator builds are the final Objective-C++ publication
gate.

## Decision

**GO** for the authenticated cross-archive texture set and atomic native GPU
resource staging.

**NO-GO** for ordinary-frame rendering until the renderer-neutral gauge plan,
screen-call state, native-output UI mapping, and live aircraft health producer
are joined without semantic guesses.
