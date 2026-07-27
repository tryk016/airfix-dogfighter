# Private content installation and recovery

**State:** portable installer, startup inspection, rollback transaction, native
iOS document-picker/progress/recovery UI, and authenticated runtime lease
adoption implemented

**Scope:** private sideload build, AFPACK v1, one serialized importer, no saves
inside the content transaction

## Boundary and invariants

The application bundle is data-less. A package selected by the owner is copied
from an untrusted external location into an application-private staging
directory before any parser accepts it. The external file is never opened as an
executable, loaded as a plugin, or used directly by runtime asset readers.

The installer maintains these invariants:

1. `active.afac` refers only to a fully authenticated, nested-archive-validated
   pack whose filename is derived from its complete SHA-256 digest.
2. A failure before the AFAC replacement cannot change the active generation.
3. Saves, settings, and screenshots live outside the content root and are never
   removed by import, rollback, or content cleanup.
4. No external filename, path component, manifest path, or archive path becomes
   a filesystem destination. Only a canonical UUID transaction ID and a digest
   are admitted into internally constructed filenames.
5. The content root and both child directories are app-private and controlled
   exclusively by one serialized installer/recovery service. Path-based rename
   primitives are not a security boundary for a world-writable directory.
6. Cancellation is honored during copying and validation. The short AFAC commit
   begins only after a final cancellation check and is non-cancellable.
7. Original or converted payload bytes never enter Git, GitHub Actions, the app
   bundle, diagnostics, crash metadata, or public test fixtures.
8. Runtime adopts the exact authenticated active-content handle and metadata;
   it does not reopen a diagnostic label or digest-derived pathname to begin a
   room load.

## Private directory layout

```text
Application Support/AirfixDogfighter/
  content/
    active.afac
    active-<canonical UUID>.afac.partial  # crash-only temporary
    packs/
      pack-<64 lowercase SHA-256 hex>.afpack
    staging/
      import-<canonical UUID>.afpack.partial
  saves/
  settings/
  diagnostics/
```

The native iOS adapter supplies the resolved Application Support URL, enforces
private directory permissions, copies a security-scoped document into an
exclusive `0600` private file, and releases external access before validation.
The portable core receives `content/` as its trusted root and creates only the
two known child directories. It rejects an empty root, wrong-type components,
and non-canonical transaction IDs. Startup cleanup scans only the three trusted
direct directories and deletes regular non-link files matching the exact
lowercase canonical-UUID temporary patterns; it never scans `packs/`.

## Transaction states

| State | Durable filesystem fact | Cancellation | Recovery consequence |
|---|---|---|---|
| idle | zero or one valid `active.afac` | allowed | normal startup |
| copying | owned `.afpack.partial` only | allowed | delete exact owned partial |
| validating | closed/synced partial, no pointer change | allowed | delete exact owned partial |
| published | digest-named final may be orphaned | allowed before commit | keep or later collect orphan |
| preparing commit | durable temporary AFAC exists | no new cancellation | delete temp if pointer unchanged |
| committed | `active.afac` atomically names new generation | no | new current is authoritative |
| recovering | no concurrent installer | not applicable | never infer active pack from filenames |

An orphan digest-named pack is safe because it is unreachable from
`active.afac`. Publication intentionally precedes pointer replacement; the
reverse order could expose a missing or partially written current pack.

## Install algorithm

1. Validate limits, trusted root, canonical lowercase UUID, and serialized
   ownership of the transaction.
2. Create/validate `content`, `packs`, and `staging` without following an
   untrusted component.
3. Inspect the external source as a nonempty regular file and reject its size
   before allocating the bounded copy buffer.
4. Exclusively reserve the exact staging name. Stream the source into it while
   enforcing the byte ceiling, detecting initial-size shrink/growth, updating a
   full-package SHA-256, reporting progress, and polling cancellation.
5. Flush and close the staged file and synchronize its directory entry.
6. Run the stable-source AFPACK validation gate. It authenticates each entry,
   strictly validates the manifest, parses both required UDSP metadata regions
   through the same handle, and performs a second complete authentication pass.
7. Derive the final filename from the complete staged-file digest. Publish with
   a no-replace operation. If that name already exists, reuse it only after its
   physical size, full digest, AFPACK semantics, and nested UDSP structures all
   validate. A mismatching collision is never overwritten or deleted.
8. Read `active.afac` with the exact 64/104-byte ceiling. Missing means the first
   generation; malformed means fail closed without replacing it.
9. If current digest and size already equal the imported pack, return
   idempotently without increasing the generation.
10. Make the next AFAC record, moving only the old current into `previous`.
    Perform the last cancellation check.
11. Exclusively write and durably flush the transaction-specific temporary AFAC
    file, then atomically replace `active.afac`. From this point the commit is
    deliberately non-cancellable.
12. Reopen the active record and require an exact match with the requested
    generation before reporting success. The final pack has already passed full
    digest, semantic, nested-archive, and post-validation confirmation before
    publication. Remove only the exact owned staging/temp names. Unreferenced
    final packs are left to recovery/garbage collection.

If atomic replacement throws after the rename, the installer rereads the AFAC
record. An exact requested generation is accepted only after retrying both file
and content-directory synchronization. A failed/unreadable retry produces
`InstallCommitUnknown`, which carries the requested AFAC value so startup
recovery can distinguish the intended generation without guessing.

## Progress and lifecycle

Portable phases are `preparing`, `copying`, `validating`, `publishing`,
`committing`, and `complete`. Each event carries phase-local completed/total
bytes plus validation-entry counts where relevant. Counts are monotonic within
a phase; a phase transition may reset its local byte counter.

The Objective-C++ adapter maps these events to UI without exposing paths or
payload values. Document-picker access remains open only for the external copy.
If the app resigns active during copy/validation, it requests cancellation and
waits for bounded I/O to unwind. A future background-task wrapper may protect
the small publish/commit tail, but must not broaden the non-cancellable region
to hashing hundreds of megabytes.

## Failure behavior

| Failure | Pointer result | Files allowed to remain |
|---|---|---|
| permission/source open | unchanged | none |
| source shrinks/grows | unchanged | no owned partial after cleanup |
| copy I/O or disk full | unchanged | no owned partial after cleanup |
| cancellation | unchanged | no owned partial after cleanup |
| AFPACK/manifest/UDSP rejection | unchanged | no owned partial after cleanup |
| final-name collision mismatch | unchanged | pre-existing final only |
| no-replace succeeds, later step fails | unchanged | authenticated orphan final |
| malformed old AFAC | unchanged | authenticated orphan final |
| AFAC temp write fails | unchanged | authenticated orphan final; no owned temp |
| atomic pointer replace/readback/resync is ambiguous | unknown (`InstallCommitUnknown`) | recovery compares exact requested AFAC and pack validation |

Errors are typed for UI mapping but diagnostic text is not a stable API. Logs
may include phases, sizes, generation numbers, error classes, and shortened
project-owned transaction IDs. They must not include external source paths,
archive logical paths, complete digests, content bytes, or user document names.

## Startup recovery

The portable recovery service runs before runtime content opens and while the
installer lock is held:

1. Parse `active.afac` exactly. Never choose a pack merely because its filename
   sorts newest or is the only file present.
2. Validate that current filename, size, full digest, AFPACK semantics, and
   nested archives agree. If so, it remains active.
3. If current is invalid and the AFAC previous reference is valid, expose a
   typed `rollbackAvailable` result. Rollback commits only that already verified
   previous pack through a new atomic AFAC generation, never by editing the old
   record in place. Whether this is automatic or user-confirmed is an iOS UX
   decision.
4. If neither reference validates, enter the no-content/error state; do not run
   the game against partially trusted data.
5. Remove a staging/temp entry only when its name is an exact recognized
   canonical pattern, it is a regular non-reparse file inside the trusted
   staging directory, and no transaction owns it.
6. A separate garbage-collection pass may remove digest-named packs not
   referenced by current or previous. It first verifies the filename grammar
   and regular-file type and never traverses outside `packs/`.

Missing `active.afac` is a valid first-launch/no-content state. A malformed
record is not silently treated as missing because doing so would hide a failed
or corrupted commit.

`inspectActiveContent` captures one stable absolute content root, reads only the
digest-derived current/previous paths, requires regular non-link files and exact
sizes, then streams each complete pack through SHA-256. After that full-file
authentication it opens bounded AFPACK metadata, strictly validates the
manifest, and opens both required nested UDSP regions. A valid candidate is
returned as a move-only `ActiveContentLease` that retains the same authenticated
handle together with the parsed pack, manifest, and exact source-archive
metadata. `VerifiedContentSession::adopt` consumes this proof without a pathname
reopen, second full-file hash, or repeated metadata parse. Typed results distinguish
`noContent`, `ready`, `rollbackAvailable`, `unusable`, `malformedActive`, and
transient `unavailable`; a temporary I/O failure never offers rollback.

`commitRollback` accepts only a `rollbackAvailable` inspection. It verifies the
previous pack again, compares the exact source AFAC before writing and again
immediately before commit, rotates current/previous through `makeNextActive`,
and makes only the final pointer replacement non-cancellable. Exact readback and
file/directory resynchronization are required after ambiguous post-rename
errors; otherwise the caller receives the same explicit unknown-commit outcome
used by installation. Progress phases are monotonic and end at `complete`.

## Authenticated runtime handoff

A ready inspection owns a move-only `ActiveContentLease`. Its rvalue-only
`takeReadyLease` can be called once, and `VerifiedContentSession::adopt`
transfers the already authenticated AFPACK handle, active generation/reference,
parsed pack and manifest, and exact `source/Resource.up` archive without
reopening, rehashing, or reparsing them.

The native coordinator stores both inspection and adopted session only on its
serialized content worker. The main and render threads receive no package
handle or host path. Before install or rollback begins, the worker destroys
both reader objects; after the writer transaction, a fresh inspection must
establish the next authenticated revision.

Main-thread publication state contains only the exact
`{generation, size, digest}` content revision and a monotonic room-request
serial. Each worker load checks that its session matches the ticket before and
after `WorldRoomLoader`, and the completed room must carry the same revision.
The main-thread gate then rejects callbacks whose serial was replaced or whose
revision was cleared by lifecycle, import, rollback, or reinspection. The
accepted CPU room snapshot is move-only in effect: its complete payload can be
consumed by native rendering once. If a stale snapshot loses its last UI-side
owner before consumption, its storage is detached in constant time and
destroyed on a dedicated serial queue rather than on the main thread.

## Required verification matrix

Synthetic tests contain no game assets and cover:

- first install, two-generation replacement, previous rollback reference, and
  idempotent reimport;
- source shrink/growth, byte ceilings, tiny buffers, cancellation in copy and
  every validation phase, and progress invariants;
- malformed AFPACK, strict manifest disagreement, invalid nested UDSP, and
  same-size mutation during validation;
- existing valid final reuse and existing corrupt final collision;
- missing/malformed/overflowing AFAC, active-temp collision, and pointer replace
  failure with old-current preservation;
- no-content/ready/rollback/unusable/malformed/unavailable inspection, current
  and previous size/digest/type/link failures, stale AFAC checks, monotonic
  recovery progress, re-verification before rollback, and ambiguous commit;
- canonical UUID/path rejection, relative/absolute aliases, hard links,
  source-identity substitution, and wrong-type filesystem entries;
- crash checkpoints after staging sync, final publication, AFAC temp sync, and
  pointer rename, followed by startup recovery;
- proof that a sentinel under `saves/` remains byte-identical through every
  success, failure, rollback, and cleanup scenario;
- current/stale request serials, exact generation/size/digest revision matching,
  explicit invalidation and active-revision clearing, and fail-closed serial
  exhaustion for room publication.

Linux, Windows, ARM64 macOS, `iphoneos`, and `iphonesimulator` targets compile
the portable installer and recovery service because AFPACK is linked into the
iOS application target. The repository-wide validation result is recorded in
`docs/progress/LOG.md` rather than duplicated here.
Physical-device tests later add document-provider permissions,
background/foreground interruption, force termination at checkpoints,
insufficient-space behavior, file protection while locked, large-package
duration/thermal measurements, and private-room visual acceptance.
