# ADR-0021: versioned atomic campaign-state document

- Status: accepted
- Date: 2026-08-03
- Deciders: project owner and implementation lead

## Context

Static evidence now closes the ordinary numeric campaign subset of the legacy
roster: optional `THRD`, `AXMI`, `ALMI`, `SCOR`, and eight integer counters.
The original root-zero reader is unsafe on malformed extents and its direct
`wb` writer is neither atomic nor reliably durable. It cannot be the storage
contract for Windows or iOS.

The project already has the codec-neutral current/backup/partial transaction in
[ADR-0018](0018-shared-durable-document-pair.md). Reusing that transaction
requires a bounded domain codec that can distinguish current-valid, valid
future-schema, and replaceable-invalid bytes. The codec must not imply that
score arithmetic, rewards, profile identity, frontend lifecycle, or migration
of every legacy record is complete.

Profile callsigns, portrait identifiers, and medal strings introduce separate
questions about legacy character encoding, product UTF-8 normalization,
identity, duplicate names, and unknown medal semantics. Folding those values
into the first campaign-state schema would freeze policy without sufficient
evidence.

## Decision

Introduce `AFCS` (Airfix Campaign State), a small canonical little-endian
document for the numeric state changed by the recovered mission-result
transaction. Semantic schema 1 contains:

- optional typed side (`THRD`: Axis `0` or Allied `1`);
- optional signed raw Axis and Allied maxima (`AXMI`, `ALMI`);
- optional signed cumulative score (`SCOR`); and
- optional signed values for `ACKI`, `GUKI`, `WUKI`, `FOOK`, `FRIK`, `DEAT`,
  `PKIL`, and `PDEA`, retaining neutral FourCC-derived names.

Absence remains distinct from a stored zero. Any present raw maximum or
counter retains the full signed 32-bit domain; UI clamping and score/stat
production remain outside the codec. A present side must be exactly Axis or
Allied.

The envelope is canonical and bounded:

```text
offset  size  meaning
0x00    4     ASCII "AFCS"
0x04    2     envelope version 1
0x06    2     flags, exactly zero
0x08    4     semantic schema version
0x0C    4     exact total byte count
0x10    32    SHA-256 over prefix 0x00..0x0F plus fields 0x30..end
0x30    ...   ordered u16 field ID, u16 payload size, payload
```

Schema 1 requires exactly two ordered fields:

1. a 20-byte progress field: `u32` presence mask followed by four signed
   32-bit slots for side, Axis maximum, Allied maximum, and score;
2. a 36-byte counters field: `u32` presence mask followed by eight signed
   32-bit slots in the neutral order above.

Reserved mask bits must be zero. Every absent slot must contain canonical zero.
Current-schema unknown, duplicate, missing, reordered, resized, truncated, or
trailing fields fail closed. A bounded document with a higher semantic schema,
valid envelope, exact size, and valid digest is retained byte-for-byte as an
opaque future record; it is never interpreted or rewritten by an older build.

The codec itself remains storage-neutral. The separate `CampaignStateStore`
target now binds it to ADR-0018 using four fixed leaf names inside one injected
directory:

- `campaign-state.afcs`;
- `campaign-state.afcs.backup`;
- `campaign-state.afcs.partial`; and
- `campaign-state.afcs.backup.partial`.

The injected directory must be an absolute application-private leaf already
partitioned by the host for the selected profile. The store never receives or
derives a callsign, profile identifier, platform root, or legacy filename.
This keeps profile identity and UTF-8 policy outside the numeric document.

Load selection is deterministic: a valid current wins; otherwise a valid
backup wins; otherwise the canonical empty schema-1 record is returned.
Malformed and oversized regular content is replaceable and can request an
explicit repair save. A valid future current is never bypassed by an older
backup. Any retained future schema, unsafe/linked entry, or unavailable read
blocks persistence without rewriting bytes. Save rotates only a valid current,
uses the shared exact-readback ambiguity rules, and reports fixed path-free
errors while retaining the requested semantic state only when the candidate
transaction is relevant.

This store adds no platform root lookup, profile catalogue, legacy adapter,
automatic repair, migration, UI, save scheduling, or runtime wiring. The host
must serialize access to one injected leaf.

The separate `LegacyCampaignStateImport` adapter accepts only a successful,
bounded root-zero roster import. It copies optional `THRD`, `AXMI`, `ALMI`,
`SCOR`, and the eight neutral counters into schema 1 without clamping or
inventing absent values. `THRD` values other than exact Axis `0` or Allied `1`
fail closed and publish no partial numeric state. Name, portrait, medals, and
unknown records remain outside AFCS; path-free remainder flags require the
future host to retain the legacy source whenever such data exists. The adapter
does not read, write, delete, or declare a complete profile migration.

## Options considered

### Reuse and rewrite the legacy root-zero roster

| Dimension | Assessment |
|---|---|
| Compatibility appearance | High |
| Malformed-input safety | Low |
| Durability | Low |
| Future evolution | Low |

Rejected. The original writer has a confirmed short-write success defect and
direct truncation. Duplicate ordering, all payloads, and bit-identical output
are not closed.

### One complete portable profile document now

| Dimension | Assessment |
|---|---|
| Single-file profile lifecycle | High |
| Numeric campaign readiness | High |
| Identity/UTF-8 readiness | Low |
| Reward/medal readiness | Low |

Deferred. Callsign/portrait encoding, profile identifiers, medal semantics,
and lossless handling of unknown legacy records require explicit decisions.
They should not delay or contaminate the proven numeric transaction.

### Separate canonical numeric campaign-state document

| Dimension | Assessment |
|---|---|
| Scope matches evidence | High |
| Atomic mission-result storage | High |
| Forward compatibility | High |
| Complete profile lifecycle | Low until later work |

Accepted. All numeric values that a future recovered result transaction must
commit together fit one small document, while unproven identity and reward
policy remain outside schema 1.

## Trade-off analysis

Splitting numeric campaign state from future profile metadata creates a later
multi-document profile lifecycle problem. It avoids a more damaging early
commitment: guessing how private legacy strings map to cross-platform UTF-8 or
claiming that medals and unknown records can be migrated losslessly. Mission
results mutate only `AFCS`, so their progress/score/counter update can still be
one atomic document-pair transaction.

SHA-256 is not an authenticity mechanism here; private store confinement and
native no-follow file handling remain separate. The digest detects corruption
and makes future-schema preservation/ambiguous readback byte-exact, matching
the established AFRS/AFIP model.

An injected per-profile directory avoids freezing a profile identifier, but
it deliberately moves partitioning responsibility to the future host adapter.
Until that adapter exists, the store is a tested dormant boundary and cannot
select or create a player's save location by itself.

## Consequences

- Windows and iOS can share one deterministic numeric campaign-state codec.
- Windows and iOS can share one current/backup/default recovery and atomic
  commit policy once their hosts inject the correct private profile leaf.
- Missing legacy chunks can remain missing through import instead of being
  silently rewritten as stored zero.
- Future semantic schemas can block downgrade writes without data loss.
- Score and counters may be imported and preserved, but no producer may claim
  parity until its separate numeric and lifecycle evidence is closed.
- A full player-profile schema, UTF-8/import policy, medals/rewards, and unknown
  legacy payload migration still need separate decisions.
- `AFCS` does not authorize connecting the isolated mission-outcome consumer,
  AFS VM, frontend, profile catalogue, or platform lifecycle.

## Action items

1. [x] Add the portable schema-1 semantic record and bounded canonical codec.
2. [x] Cover exact round trips, signed extrema, presence, corruption, field
       canonicality, limits, and opaque future-schema preservation with
       synthetic tests.
3. [x] Define the legacy numeric adapter and explicit incomplete/unsupported
       import policy.
4. [x] Define AFCS current/backup names, inspection, recovery/default behavior,
       and implement the isolated ADR-0018 store adapter.
5. [ ] Define a separate profile identity/UTF-8 and medal/reward decision.
6. [ ] Connect persistence only after the bounded VM, outcome lifecycle,
       ordering, and save-error UI are ready.
