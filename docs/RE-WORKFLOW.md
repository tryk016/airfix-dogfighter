# Reverse-engineering documentation workflow

The purpose of this workflow is to ensure discoveries survive tool upgrades,
renaming, long pauses, and incorrect early hypotheses. Ghidra is a working tool;
the repository is the durable knowledge base.

## Stable identifiers

| Item | Format | Example |
|---|---|---|
| Function | `FN-<MODULE>-<RVA>` | `FN-AFENGINE-0012A430` |
| Global | `GL-<MODULE>-<RVA>` | `GL-UDSPACK-00006120` |
| Vtable/type | `TY-<MODULE>-<RVA>` | `TY-AIRCRAFT-00011020` |
| Evidence | `EV-YYYYMMDD-NNN` | `EV-20260721-001` |
| Experiment | `EXP-YYYYMMDD-NNN` | `EXP-20260721-001` |
| Format | `FMT-<NAME>` | `FMT-UDSP` |
| Scenario/test | `SCN-<AREA>-NNN` | `SCN-FLIGHT-001` |
| Decision | `ADR-NNNN` | `ADR-0001` |

RVA means image-relative virtual address, not a runtime absolute address. The
stable ID never changes after a better name is found.

## Function states

`unseen -> located -> decompiled -> understood -> specified -> implemented -> verified`

A function may move backward if contradictory evidence appears. “Decompiler
looks plausible” is not the same as “understood.”

## Confidence

| Level | Meaning |
|---|---|
| 0 — unknown | Placeholder or untested guess |
| 1 — low | One weak clue or decompiler-only interpretation |
| 2 — medium | Multiple static clues or one controlled runtime observation |
| 3 — high | Static and dynamic evidence agree, or a parity test verifies it |

Confidence applies to individual claims. Avoid assigning one blanket confidence
to an entire function when only its name is understood.

## Evidence rules

Every nontrivial claim records:

- stable ID and exact module SHA-256/build;
- tool and version;
- address/RVA, file offset, archive entry, or scenario;
- observation separated from interpretation;
- confidence and alternative hypotheses;
- link to a function, format, system, experiment, or test;
- date and author/agent.

Do not paste large decompiler listings into Markdown. Store concise pseudocode,
contracts, data layouts, constants, and cross-references. The original binary and
local Ghidra database remain the source for raw disassembly.

## Standard work cycle

### Start of a session

1. Read `docs/progress/STATUS.md`, the latest log entry, and the current system
   note.
2. Select one question with a falsifiable result.
3. Record the expected evidence and exit condition.
4. Verify the source hash before using an address or import database.

### During analysis

1. Rename symbols conservatively; uncertain names end in `_maybe` or stay
   descriptive.
2. Apply recovered types only when they improve multiple call sites.
3. Add evidence IDs immediately, not from memory at session end.
4. When static analysis is ambiguous, design the smallest controlled runtime or
   file experiment.
5. Never edit, patch, unpack into, or launch directly from the original folder.

### Before implementation

Write a behavioral contract containing inputs, outputs, state read/written,
ordering, numeric units/precision, error behavior, ownership/lifetime, and known
unknowns. Link the contract from the function catalog.

### End of a session

1. Update changed function/format/system notes.
2. Add tests or explicitly record why a test is not yet possible.
3. Append a dated log entry with evidence IDs and contradictions.
4. Update `STATUS.md`: now, next, blocked, and decisions needed.
5. Ensure no original asset, dump, trace, or Ghidra database is staged.

## Documents and ownership

| Location | Purpose | Update trigger |
|---|---|---|
| `docs/progress/STATUS.md` | Single current project state | End of each work session |
| `docs/progress/LOG.md` | Append-only chronological record | Every material experiment/session |
| `docs/re/MODULE-MAP.md` | Modules and dependency hypotheses | Import/export/call-edge finding |
| `docs/re/FUNCTION-CATALOG.csv` | Stable function index | Any function state/name change |
| `docs/re/systems/*.md` | Behavioral contracts by subsystem | Understanding or implementation change |
| `docs/formats/*.md` | Binary/archive layouts | Any format evidence change |
| `docs/experiments/*.md` | Reproducible observations | Controlled runtime/file experiment |
| `docs/verification/PARITY-MATRIX.md` | Feature/scenario completion | Test result or scope change |
| `docs/adr/*.md` | Architectural decisions and trade-offs | Material irreversible decision |

## Function catalog schema

The CSV columns are:

- `function_id`, `module`, `rva`, `original_symbol`, `working_name`;
- `system`, `state`, `confidence`;
- `calls`, `called_by` using stable IDs where relevant;
- `evidence_ids`, `scenario_ids`, `source_path`;
- `notes`.

One function per row. Values containing lists use semicolons. RVAs are uppercase,
zero-padded hexadecimal. Do not delete old stable IDs; mark thunks/duplicates in
notes.

## Contradictions and corrections

When new evidence disproves a claim:

1. Do not silently rewrite history in the log.
2. Update the active system/format note and lower/raise confidence as needed.
3. Append a correction that cites the old and new evidence.
4. Rename working symbols, preserving stable IDs.
5. Re-run linked tests and record which results changed.

## Implementation traceability

Production code that reconstructs a non-obvious behavior should reference a
system contract or scenario ID in a nearby concise comment, not raw disassembly.
Tests should name the scenario ID. Commit messages should state the affected
system/format IDs and whether parity changed.

## Definition of done for one recovered behavior

- Observation and interpretation are separated.
- Function/system/format IDs are assigned.
- Inputs, outputs, units, ordering, and failure behavior are documented.
- At least one independent evidence source or controlled scenario supports it.
- Portable implementation has no accidental platform dependency.
- Automated test exists, or the missing test is recorded as a blocker.
- Reference comparison is within its stated tolerance.
- Catalog, status, parity matrix, and log are consistent.

