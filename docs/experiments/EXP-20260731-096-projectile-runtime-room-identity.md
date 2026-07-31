# EXP-20260731-096: runtime-owned projectile room identity

**Status:** implemented and locally validated
**Confidence:** high (`3`) for the immutable legacy-ID mapping and removal of
the build-catalogue lifetime dependency

## Question

Can the published projectile collision path translate legacy signed `CcRoom`
IDs after `MissionWorldRoomCatalog` and parsed CCF metadata have been destroyed,
without retaining names, contributors, source metadata, or native pointers?

## Result

Yes. The recovered mapping is determined entirely by the immutable runtime
room count:

- root world index `0` maps to legacy ID `0`;
- non-root world index `i` maps to `roomCount - i`; and
- positive legacy ID `r` maps back to `roomCount - r`.

The compact overloads in `MissionWorldRooms` validate nonzero count,
`int32_t` representability, and both index/ID bounds. Existing catalogue
overloads remain as build-time validators and delegate to the same mapping.

`LegacyProjectileRuntimeQueryAdapter` no longer accepts a caller-retained
catalogue. Its portal loop, terminal transaction, creator-BSP guard, and active
slot advance read `worldRoomCount()` from the same
`LegacyGameplayCameraMissionRuntime` that owns the static arena, dynamic
collision frame, and trace implementation. Room identity and geometry can no
longer be supplied from different mission snapshots through this API.

## Safety and determinism

- No name, source path, contributor list, CCF metadata, or private content is
  retained.
- No allocation, sorting, lookup table, or mutable cache is added to a query.
- Zero and unrepresentable room counts fail closed.
- Negative, out-of-range, or stale room IDs fail before tracing.
- Portal target conversion uses the runtime-owned count from the same
  synchronous query context.
- Existing collision order, material bits, actor gates, creator-BSP bracketing,
  slot order, and terminal commands are unchanged.

## Validation

Synthetic room tests prove catalogue/count equivalence for root and all
non-root IDs, both directions, plus zero, overflow, negative, and out-of-range
rejection. Projectile adapter tests cover direct trace mapping, one-room actor
queries, a two-room portal transition, creator-BSP transactions, invalid input,
and 4,096-iteration allocation checks without constructing or retaining a
room catalogue.

A fresh GCC 15.2/Ninja build completed all 408 build steps and passes 127/127
CTests. A fresh MSVC 19.51 HostX64/Ninja Windows-product build completed all
702 build steps; the final post-format rebuild relinked the affected graph and
passes 139/139 CTests, including the D3D11 product and scaled-product smokes.

Public-boundary synthetic tests and the 671-file repository scan pass. The 263
entry portable compilation database contains neither Windows-product nor
Apple-only sources. All 12 Rizin export-normalization tests, the changed-
addition local-path scan, and `git diff --check` also pass. Hosted Windows,
Linux, macOS, and unsigned-iOS Actions remain the publication gate.

## Decision

**GO** for using the mission runtime as the sole projectile room-identity
owner.

**NO-GO** remains for live projectile production, muzzle transforms, mutable
actor callbacks/effects, or scheduling; this bridge only removes a data
lifetime mismatch.
