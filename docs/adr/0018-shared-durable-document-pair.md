# ADR-0018: shared durable document-pair transaction

- Status: accepted
- Date: 2026-08-01
- Deciders: project owner and implementation lead

## Context

The canonical render-presentation settings store (AFRS) and controller-input
profile store (AFIP) already used the same durable storage protocol: inspect a
bounded current document and backup, preserve valid future schemas, rotate a
valid current through a synchronized partial file, atomically publish the new
candidate, and resolve an ambiguous replacement by exact readback. The two
implementations had evolved independently and duplicated the safety-critical
directory, entry-type, cleanup, rotation, and commit-unknown logic.

The future campaign/save system also needs an atomic modern boundary. The
legacy numeric records and ordinary roster lifecycle were recovered after this
decision; [ADR-0021](0021-versioned-campaign-state-document.md) now specifies
their bounded product codec. Sharing the proven byte transaction still must
not manufacture a store policy or imply that legacy migration, profile
identity, rewards, or frontend integration is implemented.

## Decision

`airfix::io` owns one codec-neutral `DurableDocumentPair` transaction above the
existing platform-specific `DurableFile` primitives. A domain codec supplies:

- four distinct leaf names for current, backup, current partial, and backup
  partial entries;
- a positive maximum byte count; and
- a non-throwing inspection callback that classifies exact bounded bytes as
  `valid`, `futurePreserve`, or `replaceableInvalid`.

The shared layer does not parse schemas or retain semantic values. It maps
filesystem inspection into missing, oversized, wrong-type/linked, and
unavailable states, while the domain store retains its own detailed status,
schema version, defaults, decoded value, and public error type.

A commit follows this order:

1. validate the fixed configuration and require the candidate itself to be
   current-valid;
2. create or validate the exact application-private leaf directory;
3. inspect current and backup through bounded, single-link regular-file reads;
4. block before mutation if either retained entry is a future schema, unsafe
   type/link, or unavailable;
5. return unchanged only for byte-identical valid current content;
6. if current is valid, write and verify an exclusive backup partial, then
   atomically replace and exactly read back the backup;
7. never promote replaceable-invalid current content;
8. write and verify an exclusive current partial, then atomically replace and
   exactly read back current; and
9. after a replacement exception, retry file/directory durability only when
   exact readback proves that the intended bytes were published. Otherwise
   distinguish a proved pre-publication failure from `commitUnknown`.

Owned stale partial regular files may be removed and directory-synchronized.
Wrong-type, symbolic/reparse, or multiply linked partial entries fail closed
and are never removed. Errors contain only fixed path-free categories. The
domain adapters keep their prior requested-record behavior, including the fact
that rejection of an unsafe pre-existing partial happens before a candidate
transaction becomes relevant.

AFRS and AFIP now adapt their codecs and public results to this transaction.
Their file names, byte formats, load/default/backup selection, future-schema
downgrade block, readback result, and native serialized persistence ownership
do not change.

## Options considered

### Keep independent store implementations

| Dimension | Assessment |
|---|---|
| Immediate code change | Low |
| Safety drift risk | High |
| Fault-matrix reuse | Low |
| Future store readiness | Low |

Rejected because a correction to ambiguous commit handling or prepared-entry
safety could silently reach one store but not the other.

### Templated semantic store

| Dimension | Assessment |
|---|---|
| Compile-time coupling | High |
| Schema ownership clarity | Low |
| Reuse of byte transaction | High |
| Build impact | Medium |

Rejected. `airfix::io` must not depend on render or input records, codecs,
defaults, or migration policy. A small callback boundary keeps the durable
mechanism independent from domain schemas.

### Codec-neutral document-pair transaction

| Dimension | Assessment |
|---|---|
| Compile-time coupling | Low |
| Schema ownership clarity | High |
| Fault injection | High |
| Future reuse | High |

Accepted. The I/O layer proves byte publication; each domain continues to own
whether those bytes are meaningful, forward-only, or replaceable.

## Consequences

- AFRS and AFIP use one implementation for exact current/backup/partial
  publication and ambiguous-commit recovery.
- New document formats can reuse the mechanism only after defining and testing
  their own bounded codec, semantic validation, recovery/default behavior,
  native lifecycle, and serialization owner.
- A valid future document remains byte-exact and blocks downgrade writes;
  malformed or oversized regular content remains safely replaceable.
- The contract still assumes one serialized owner for an application-private
  leaf. It does not make path-based standard-library directory operations a
  security boundary against an untrusted process that can replace parent-tree
  entries concurrently.
- The transaction does not implement campaign semantics, store selection and
  defaults, legacy adapters, iOS lifecycle integration, cloud synchronization,
  or cross-process locking.

## Verification

Synthetic portable tests cover:

- missing, valid, future-preserve, replaceable-invalid, oversized, and unsafe
  entry classification;
- first commit, byte-identical no-op, exact current-to-backup rotation, and
  malformed-current replacement without promotion;
- byte-exact future current and backup preservation with downgrade blocking;
- stale owned partial cleanup and fail-closed wrong-type partial handling;
- replacement exceptions before and after publication at both backup and
  current stages;
- successful exact-readback recovery, failed durability retry and
  `commitUnknown`; and
- invalid names, missing inspector, invalid candidate, relative directory, and
  path-free errors.

The existing AFRS and AFIP store suites remain mandatory regression tests, as
do the full portable build and test suite and hosted Windows/macOS/Linux/iOS
compilation gates.

## Action items

1. [x] Add the shared codec-neutral read and document-pair commit layer.
2. [x] Route AFRS and AFIP through it without changing their public behavior.
3. [x] Add direct synthetic current/backup/partial fault-injection coverage.
4. [x] Recover the numeric campaign subset and independently specify and
       implement its bounded `AFCS` codec in ADR-0021.
5. [ ] Add the platform save-game lifecycle only after the semantic store and
       recovery policy exist.
