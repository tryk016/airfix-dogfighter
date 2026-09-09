# Airfix Ghidra Knowledge Bridge

The Airfix Ghidra Knowledge Bridge turns reviewed reverse-engineering evidence
into repeatable, path-free input for a fresh Ghidra project. It also produces
one deterministic function report that replaces several separate exporter
runs. The bridge is headless-first and has no plugin, cloud, or GUI dependency.

Ghidra remains the canonical static-analysis database. Rizin remains an
independent scripted cross-check. A bridge record is not evidence by itself: it
must point to an existing `EV-YYYYMMDD-NNN` record and retain the confidence
limits of that evidence.

## Public contracts

Two versioned JSON schemas define the exchange boundary:

- `schemas/airfix-ghidra-knowledge-v1.schema.json` describes curated names,
  signatures, structures, `this` fields, vtables, event IDs, notes, hypotheses,
  evidence, and confidence;
- `schemas/airfix-ghidra-function-v1.schema.json` describes one aggregate
  Ghidra function report.

Knowledge is selected by the lowercase SHA-256 of the complete module. Every
function, vtable, field, and event uses a canonical eight-digit PE32 RVA. No
record depends on a source filename or a local filesystem path.

Every knowledge item has this assessment:

```json
{
  "status": "confirmed",
  "confidence": 2,
  "evidence": [
    {
      "id": "EV-YYYYMMDD-NNN",
      "summary": "Concise statement supported by the evidence record."
    }
  ],
  "apply_eligible": true
}
```

`apply_eligible` is deliberately separate from confidence. Automatic mutation
is allowed only when all of these conditions hold:

- status is `confirmed`;
- confidence is at least 2;
- at least one canonical evidence ID is present;
- the module SHA-256 exactly matches the open Ghidra program.

Conditional facts, hypotheses, unknowns, and unreviewed exports remain useful
in the public document but are skipped by the importer.

## One-command function report

Incoming relations are module-relative instruction/data sites. Ghidra's
synthetic external-entry annotations on PE exports have no source RVA and are
not emitted as callers/xrefs; they are not executable callers. Real in-module
call sites and data references remain present, including calls to external
targets. The normalizer still rejects unaddressed, kind-only relations.

For a program already present in the local Ghidra project:

```powershell
./tools/Invoke-AirfixGhidraBridge.ps1 `
  -Mode ExportFunction `
  -ProgramName 'AirCraft.type' `
  -FunctionId 'FN-AIRCRAFT-00003F40' `
  -Rva '0x00003F40'
```

The ignored JSON report contains:

- exact end-exclusive body boundary, disjoint ranges, instructions, bytes,
  basic blocks, and CFG edges;
- Ghidra signature and pseudocode;
- callers, callees, incoming and outgoing xrefs, and global reads/writes;
- direct `ECX`/`RCX` plus displacement candidates for `this+offset`;
- exact immediate integer bits plus `f32`/`f64` bits only for decoded x87
  floating loads from memory;
- recovered switch tables and indirect-call displacement candidates;
- normalized per-instruction P-code;
- x87 loads, spills, retained-stack values, environment operations, and
  resolved `_ftol`-family calls.

The exporter redacts host-style absolute paths before publication. The Python
normalizer independently rejects any path that remains, malformed or unknown
fields, oversized data, noncanonical addresses, inconsistent IDs, and unsafe
text. Reports stay under the ignored `artifacts/ghidra` boundary.

The `this+offset` and numeric interpretations are candidates, not promoted
semantic names. Direct-register matching does not yet replace full data-flow
recovery, and the x87 section cannot determine the live process control word.

## Exporting and curating knowledge

Export current names and signatures as non-applicable observations:

```powershell
./tools/Invoke-AirfixGhidraBridge.ps1 `
  -Mode ExportKnowledge `
  -ProgramName 'AirCraft.type' `
  -Rvas @('0x00003F40', '0x00006F90')
```

The result always starts as `unknown`, confidence 0, with
`apply_eligible=false`. A researcher may promote an item only after linking the
specific evidence, retaining unresolved alternatives in `notes.hypotheses`,
and independently reviewing the change. Never mark a whole module confirmed
because one function was verified.

Normalize a knowledge document without launching Ghidra:

```powershell
python tools/re/airfix_ghidra_bridge.py normalize-knowledge `
  --input <knowledge.json> `
  --output <normalized.json>
```

Normalization is deterministic: keys, module records, RVAs, evidence, notes,
fields, and slots receive a canonical representation and order. Publication is
an atomic replace through a unique temporary file.

## Applying reviewed knowledge

Apply a normalized document to an existing local project:

```powershell
./tools/Invoke-AirfixGhidraBridge.ps1 `
  -Mode ApplyKnowledge `
  -ProgramName 'AirCraft.type' `
  -KnowledgeFile <knowledge.json>
```

For an immutable first import, replace `-ProgramName` with
`-InputFile <verified-private-copy>` and select an isolated project directory.
The private input is used locally and is never copied into the report.

The importer:

- selects exactly one module by executable SHA-256;
- rejects duplicate JSON keys, non-finite numbers, trailing data, excessive
  nesting, oversized records, and host paths before starting a transaction;
- skips every item that is not explicitly eligible;
- creates a missing confirmed function only at its exact RVA, only in an
  executable memory block, and never inside another function;
- applies names, namespaces, calling conventions, return types, parameters,
  repeatable evidence comments, structures, fields, event enums, and verified
  vtable slot references;
- verifies any requested parameter storage after Ghidra assigns the ABI layout
  and rolls back rather than silently substituting a different storage;
- checks that a vtable slot's stored pointer bytes match the claimed target;
- limits declared structures to a nonzero size below 16 MiB and rejects field
  arithmetic or overlap outside that boundary;
- performs all Ghidra mutations in one transaction and rolls everything back
  if any record or result write fails.

Reapplying the same document is supported. A failed or mismatched document must
not leave a partially modified program.

## Ghidra–Rizin difference report

Generate the normal independent Rizin report for the same module SHA and RVA,
then run:

```powershell
./tools/Invoke-AirfixGhidraBridge.ps1 `
  -Mode Compare `
  -GhidraReport <ghidra-function.json> `
  -RizinReport <rizin-function.json> `
  -OutputFile <difference.json>
```

The result contains only comparable mismatches in:

- function boundary;
- basic-block count;
- callees;
- global/data references;
- constants;
- indirect calls.

The current public Rizin v1 exporter does not emit constants or a normalized
indirect-call collection. Those categories appear under `unavailable` with
`field-not-exported`, never as false agreement or a fabricated difference.
Global references retain `read`, `write`, or `unknown`; an unknown Rizin access
mode cannot silently agree with a directional Ghidra observation.

The command rejects different module hashes or function RVAs. An empty
`differences` array means the compared projections agree; it does not prove the
signature, semantics, or confidence 3. `rz-ghidra` remains a Rizin-family
result and is not independent Ghidra evidence.

## Safety boundary

Commit only schemas, scripts, synthetic tests, and deliberately curated
knowledge. Keep all of the following outside Git:

- original game modules and resources;
- Ghidra `.gpr/.rep`, Rizin `.rzdb`, and Binary Ninja `.bndb` databases;
- raw aggregate reports, pseudocode dumps, local manifests, and memory data;
- installation paths, usernames, project directories, and tool caches.

The bridge does not access a network. It uses the existing locked local Ghidra
and Python standard library. Reports are bounded in size and are written
atomically. The repository public-boundary scanner remains the final staging
gate.

## Delivery stages

Version 1 establishes the closed knowledge loop, aggregate exporter,
first-pass `this`/indirect/vtable evidence, x87 risk inventory, and automatic
Ghidra–Rizin differences. Later compatible stages are intentionally separate:

1. improve `this` data-flow grouping and produce class candidates across many
   functions without promoting names automatically;
2. add relocation-tolerant function fingerprints and a reviewed shared-engine
   signature index across `.type` modules;
3. use those fingerprints to propose, never silently apply, cross-module
   matches;
4. add an optional thin GUI only if the headless workflow shows a concrete
   usability gap.

The current format reserves no claim that a candidate class or common-code
match is confirmed. Those stages need synthetic collision tests and independent
review before they become apply-eligible.

## Validation

Portable validation does not need private files or a Ghidra installation:

```powershell
python tests/AirfixGhidraKnowledgeBridgeTests.py
./tests/Invoke-GhidraAnalysisTests.ps1
./tests/Invoke-AirfixGhidraBridgeTests.ps1
```

Release validation additionally compiles all Java scripts in the locked Ghidra
version, exports a representative real function, and applies synthetic
confirmed knowledge to a fresh disposable project made from a verified working
copy. That project and every generated report remain ignored and are deleted
after the test.
