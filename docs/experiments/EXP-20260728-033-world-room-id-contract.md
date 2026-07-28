# World-room ID contract

**Date:** 2026-07-28

**Evidence:** `EV-20260728-017`

**Scenarios:** `SCN-LEVEL-001`, `SCN-WEAPON-001`

**Status:** native `CcRoom` IDs mapped bidirectionally to the retained
mission-world room catalogue

## Question

Can the signed room ID carried by projectile event `0xE2` be translated to the
pointer-free `MissionWorldRoomCatalog::rooms` index without retaining a
`CcWorld`, guessing from room names, or importing original mission data?

## Sources and safety

The pass used the hash-verified, read-only `Cc.dll` working copy:

| Source | SHA-256 | Role |
|---|---|---|
| `Cc.dll` | `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF` | room construction, ID assignment, linked-list order, and ID lookup |

The original installation was not executed or modified. LLVM inspected the PE
exports. Ghidra 12.1.2 reused the existing local project, and Rizin 0.9.1
independently analyzed the verified copy with user configuration disabled.
All binaries, working copies, reports, and tool projects remain outside Git.

## Recovered identity law

`CcWorld` is also its root `CcRoom`. Its constructor:

- initializes root field `CcRoom +0x48` to ID `0`;
- stores the root as its own world; and
- initializes the next-room counter at `CcWorld +0x8C` to `1`.

Each ordinary `CcRoom(CcWorld*)`:

1. prepends itself to the linked list at `CcWorld +0x78`;
2. copies the current counter to `CcRoom +0x48`; and
3. increments the counter.

`CcRoom::GetRoomId` returns the signed integer at `+0x48`.
`CcWorld::GetRoomById` first checks the root, then scans the ordinary-room list
newest-first and returns the first room whose `+0x48` matches.

The existing catalogue independently reconstructs the same successful
mission-load snapshot: root at index zero and ordinary rooms newest-created
first. Name deduplication adds contributors to an existing room and does not
consume a new native ID. For a complete catalogue with `roomCount` entries:

```text
worldRoomIndex == 0  <=>  legacyRoomId == 0

worldRoomIndex > 0:
legacyRoomId = roomCount - worldRoomIndex
worldRoomIndex = roomCount - legacyRoomId
```

For example, four runtime rooms are represented as IDs `{0, 3, 2, 1}` at
portable indices `{0, 1, 2, 3}`.

## Tool comparison

| Function | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| `CcRoom(CcWorld*)` RVA `0x112E0` | typed prepend, counter copy, increment | automatic boundary and exact `+0x48/+0x8C` accesses | agree |
| `CcRoom::GetRoomId` RVA `0x11580` | direct signed field return | automatic three-instruction getter | agree |
| `CcWorld::CcWorld` RVA `0x11590` | root ID and next-ID initialization | automatic boundary and exact immediates | agree |
| `CcWorld::GetRoomById` RVA `0x11810` | root-first, newest-first scan | automatic 33-byte loop with `+0x48/+0x78/+0x24` accesses | agree |

Ghidra remains canonical for the MSVC class and export meaning. Rizin
independently confirms every field offset, comparison, branch, and boundary
used by the mapping.

## Reconstruction decision

`legacyCcRoomIdForWorldRoomIndex` and
`worldRoomIndexForLegacyCcRoomId` are allocation-free C++20 helpers over a
complete `MissionWorldRoomCatalog`. They:

- preserve signed 32-bit native IDs;
- accept root-only catalogues;
- reject incomplete catalogues, negative IDs, out-of-range indices/IDs, and
  counts that cannot fit the native signed domain; and
- round-trip every valid room identity without consulting names or original
  resources.

The contract covers the immutable successful mission-load snapshot represented
by the catalogue. It deliberately does not claim parity for later arbitrary
room deletion or creation, neither of which is represented by the retained
mission-world runtime.

This closes the room-identity input for projectile event `0xE2`. It does not
yet implement the combined `NfProjectile::DetectCollisions` iterator, dynamic
actor ownership, material-8 continuation, or private event dispatch.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all 244
steps and pass 75/75 portable tests. Clangd 22.1.8 reports zero errors in the
changed production and test ranges with the trusted MinGW query driver. The
Ghidra/working-copy/Rizin wrapper suites, 12 Rizin normalization tests,
public-boundary tests and 372-file scan, 253-row catalogue validation,
`actionlint`, nine-file local-path scan, and `git diff --check` pass. GitHub
Actions remains the publication gate for this slice.

No proprietary data or local path is required by the implementation or tests.

## Confidence

Confidence is **3/3** for root ID zero, monotonic ordinary-room IDs,
newest-first lookup, the catalogue-order formula, and both portable
translations.

Confidence is **2/3** that no future stateful runtime will require a separate
generation-aware identity table after deliberate room unload/reload support is
introduced.
