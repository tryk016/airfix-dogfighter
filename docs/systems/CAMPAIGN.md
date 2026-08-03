# Campaign and legacy-roster core

The portable core now has two bounded building blocks for the original
single-player campaign. They are intentionally not connected to a product UI,
filesystem, or save writer yet.

The recovered behavior and its confidence boundary remain authoritative in
[CAMPAIGN-FLOW](../re/systems/CAMPAIGN-FLOW.md) and
[FMT-ROSTER](../formats/ROSTER.md). This document describes only the public
C++20 implementation contract.

## Components

```text
caller-owned legacy bytes
        |
        v
LegacyRosterImporter (assets)
        |
        v
LegacyRoster -- future explicit adapter --> LegacyCampaignSeed
                                           |
mission outcome --> LegacyMissionOutcomeConsumer
        |                                  |
        +--> ordered progress directive ---+
                                           v
                                  LegacyCampaignModel
                                           |
                                           v
                              future versioned save codec
                                           |
                                           v
                                DurableDocumentPair
```

### `LegacyRosterImporter`

The importer accepts one complete caller-owned byte span and publishes a value
only after every extent and known record validates. It reuses the generic
AFCHUNK framing parser and adds roster-specific rules:

- the supported roster root is exactly zero;
- input bytes, record count, string length, medals, and unknown-record
  descriptors have configurable bounds;
- `NAME` and `PICT` must be exactly NUL-terminated;
- recovered integer records must contain exactly one little-endian signed
  32-bit value;
- `MEDA` must contain one exact NUL-terminated identifier followed by one
  little-endian signed 32-bit value and may repeat;
- all other known records are singletons; duplicates fail closed because the
  native winner rule is not proven;
- unknown records are retained only as ID/size descriptors. Payloads are not
  copied, and this slice cannot export or migrate them losslessly;
- every data error returns an empty roster and a non-success status.

The importer performs no file I/O and never logs filenames, profile strings,
record contents, or host paths. It does not emulate the original reader's
destructive failure paths.

### `LegacyCampaignModel`

The model owns only typed campaign state and deterministic transitions:

- two sides, Axis `0` and Allied `1`;
- ten rows per side;
- missing `THRD` displays Allied but remains absent from stored state;
- missing maxima display row zero;
- signed raw maxima remain available while selectable maxima are independently
  clamped to `[0, 9]`;
- construction and side changes select the highest currently unlocked row;
- mission selection rejects locked and out-of-catalogue rows;
- failure directives preserve campaign progress;
- valid success directives materialize `THRD` and add, replace, or retain the
  active side maximum exactly as decided by `LegacyMissionOutcomeConsumer`;
- mission-ten completion retains raw maximum `10` while the UI stays clamped
  to row `9`;
- catalogue identifiers are emitted only for a valid selectable state.

Unsupported `THRD` values are rejected. Progress outside the proven ordinary
range (`nextMissionNumber` 1 through 10), forged side values, and inconsistent
add/replace/keep directives also fail without mutating the input state. This is
the portable product policy; it does not claim parity with malformed native
state.

## Deliberate non-goals

This slice does not implement:

- frontend windows, menu dispatch, difficulty, credits, or modal precedence;
- score arithmetic, x87 conversion policy, cumulative-stat updates, medals,
  equipment, or other reward rules;
- a legacy roster writer or bit-identical round trip;
- the versioned portable profile/campaign schema;
- filesystem locations, migration, recovery UI, or lifecycle integration;
- automatic conversion from `LegacyRoster` to `LegacyCampaignSeed`;
- Windows/iOS runtime wiring.

The future portable schema must use
[ADR-0018](../adr/0018-shared-durable-document-pair.md) for its byte
transaction. It must not copy the original direct `wb` writer.

## Validation

Public tests use synthetic byte vectors only:

- `LegacyRosterImporterTests` covers all recovered record shapes, signed
  values, repeated medals, unknown descriptors, truncation, root mismatch,
  duplicate singletons, malformed strings/integers/medals, and every configured
  limit;
- `LegacyCampaignModelTests` covers defaults, raw/clamped maxima, both sides,
  locked rows, exact catalogue identifiers, consumer-to-model progression,
  mission-ten clamping, failure preservation, and forged directives.

No original roster, profile name, portrait, medal corpus, local path, or game
binary is required by the build or tests.
