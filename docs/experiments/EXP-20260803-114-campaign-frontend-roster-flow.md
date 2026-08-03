# EXP-20260803-114 — campaign, frontend, and roster flow

**Status:** statically recovered and independently reviewed

**Question:** How does the original single-player frontend move from boot and
profile selection through campaign/mission start, outcome, progression, score,
and roster persistence?

**Source build/hash:** `Dogfighter.exe`
`F48CD7D16E8D993B262F4E35731ABFB1D95288E0C688506065F168E1B4090E89`;
`AfEngine.dll`
`A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E`;
`Singleplayer.mode`
`EC821ABFE801495DF65C9879E719994F742DD0BFAACEBFD6684C4AE2478AF7DD`

**Working-copy hash:** byte-identical to each source hash above

**Environment:** isolated Windows worktree; static analysis only

**Tool versions:** Ghidra Headless 12.1.2 primary; Rizin 0.9.1 with rz-ghidra
and rzpipe 0.6.2 independent cross-check

**Offline/no-cloud confirmed:** yes

**Memory/file patching:** disabled; no game or save was executed or modified

**Safety boundary:** no x32dbg, runtime launch, real-save mutation, original
binary publication, tool database publication, or local host path in public
evidence

**Related IDs:** `EV-20260803-002`, `EV-20260721-022`,
`EV-20260724-005`-`007`, `EV-20260730-002`, `EV-20260730-005`,
`EV-20260730-006`, `EV-20260731-001`, `EV-20260731-002`

## Prediction

The falsifiable prediction was that campaign progression is stored in the same
remembered profile container that the result screen writes, and that two
independent disassemblers would agree on a closed chain:

```text
boot -> main menu -> profile -> campaign row -> mission
     -> outcome flags -> result -> progression -> old SCOR removal
     -> score -> stats -> replacement SCOR -> roster write
```

Distinguishing results were:

- a separate campaign file, database, or profile-independent maximum would
  contradict the shared-container prediction;
- score- or medal-gated mission access would contradict a pure side-maximum
  rule;
- a temp/replace or backup path would contradict direct non-atomic legacy
  writes; and
- mismatched function extents, call sites, constants, or FourCCs between
  Ghidra and Rizin would prevent publication as two-tool evidence.

## Prior-evidence review

Before new exports, all existing reports concerning AFS, mission result,
mission loading, settings/durable documents, menu-adjacent input, and control
were reviewed. The exact reuse/exclusion ledger is in
[CAMPAIGN-FLOW.md](../re/systems/CAMPAIGN-FLOW.md#earlier-evidence-reused).

The experiment reused boot/module loading from EXP-066, outcome state from
EXP-070, result/progression from EXP-075, AFS ordering/call shape from EXP-079
and EXP-080, generic AFCHUNK framing from `EV-20260721-022`, and mission
loading/ownership from `EV-20260724-005`-`007`. It did not re-run their corpus
scans or re-derive confirmed AFS bytecode.

The modern settings and persistence experiments EXP-055, 057-059, 064-065,
071-074, 076, and 081-082 were reviewed to identify product boundaries only.
They do not describe the original menu, campaign, or roster writer. ADR-0018
was specifically excluded as evidence of legacy atomicity or schema.

## Procedure

1. Fetch all remote references, verify that `EXP-20260803-114` and
   `EV-20260803-002` are absent from all local and remote history, and create an
   isolated branch from the then-current `origin/main`.
2. Re-hash immutable source binaries and the authenticated roster sample
   against `docs/evidence/source-manifest.sha256`. Work only on byte-identical
   local analysis copies.
3. Use the repository Ghidra wrapper with the existing offline Ghidra 12.1.2
   project to export selected decompilation, exact instructions, xrefs,
   strings, vtable targets, and import-call sites for application, frontend,
   campaign, result, profile, `AfChunkContainer`, and `RegisterStats` paths.
4. Independently invoke the Rizin 0.9.1 wrapper on explicit function RVAs. Do
   not seed it with Ghidra names. Normalize boundaries, calls, data references,
   instructions, and rz-ghidra pseudocode to one JSON report per function.
5. Compare end-exclusive extents, call sites, FourCC integers, field offsets,
   command literals, constants, branch predicates, and ownership. Resolve any
   discrepancy by inspecting complete instruction bytes, not by choosing the
   preferred decompiler output.
6. Parse the authenticated 62-byte roster sample offline as bounded bytes only;
   do not write it, pass it to the game, or publish the file.
7. Record confirmed facts, conditional inferences, hypotheses, and unknowns
   separately. Keep UI drawing outside the scope.
8. Update the system/format notes, function/module ledgers, parity matrix, and
   progress logs. Run repository wrappers, report uniqueness/path checks,
   documentation links, public-boundary checks, and `git diff --check`.
9. Submit the complete documentation diff to an independent reviewer before
   commit; resolve every P0-P3 finding.

## Raw observations

### Tool agreement

Ghidra is the primary semantic source. Rizin normalized 24 explicitly selected
functions and independently matched the following central bodies and their
call/data references. The final column prevents a Ghidra-only recovery from
being mistaken for two-tool evidence.

| Function | Byte range `[VA,VA)` | Key observation | Cross-check |
|---|---:|---|---|
| campaign constructor | `0x00401000-0x004015C7` | `THRD`, `AXMI`, `ALMI`; default side; `[0,9]` clamps | Ghidra + normalized Rizin |
| campaign input | `0x004016F0-0x00401DFC` | selection bounds and exact mission-launch command data | Ghidra + normalized Rizin |
| result constructor | `0x00405A90-0x0040690A` | success predicate, progression, old-`SCOR` removal, score/stats/write order | Ghidra + normalized Rizin |
| result action | `0x00406CE0-0x00406E14` | shutdown/briefing/creds/restart/menu routes | Ghidra + normalized Rizin |
| main-menu constructor | `0x00408E00-0x00409444` | first login/profile construction | Ghidra + normalized Rizin |
| main-menu selection | `0x004099F0-0x00409B67` | exact profile/briefing/multiplayer/roster/options/paint/editor/credits cases | Ghidra + normalized Rizin |
| profile/login constructor | `0x0040A4B0-0x0040AA31` | pre-write and roster catalogue | Ghidra + normalized Rizin |
| profile selection callback | `0x0040AAB0-0x0040ADB6` | sender `+0x18` writes/reads then opens pilot roster; sender `+0x1C` reads directly then closes login; both check read | Ghidra + normalized Rizin |
| new-profile callback | `0x0040B1D0-0x0040B3B3` | root 0, NAME/PICT/MEDA, ignored write result | Ghidra + normalized Rizin |
| options constructor | `0x0040CEC0-0x0040D08D` | shared main-menu/pause options entry; rendering excluded | Ghidra + normalized Rizin |
| application update | `0x00411080-0x00411453` | loading completion creates main menu | Ghidra + normalized Rizin |
| input dispatcher | `0x00411BB0-0x0041221A` | campaign handler call at `0x00411F1D` | Ghidra + normalized Rizin |
| central frontend handler | `0x00412EA0-0x004143C1` | dispatch case 6 result/pause and case `0x2B` campaign | Ghidra + normalized Rizin |
| roster read | `0x10038CD0-0x10038E4B` | non-transactional replacement and insufficient child bounds | Ghidra + normalized Rizin |
| roster write | `0x10038E50-0x10038EC9` | direct `wb`, flawed short-write success, no atomic replace | Ghidra + normalized Rizin |
| stat registration | `0x10058220-0x100584A2` | cumulative chunks and exact `FRIK` gate/value anomaly | Ghidra + normalized Rizin |

Rizin had to create missing functions at `0x00406CE0`, `0x004099F0`,
`0x0040AAB0`, `0x0040B1D0`, `0x004100D0`, and `0x00412EA0`; automatic
discovery covered the supplemental `0x0040CEC0` and `0x10058220` bodies. The
resulting instruction extents and call/data references agree with Ghidra. The
only initial boundary discrepancy was a report convention that ended roster
functions inside the three-byte `ret 4`. Complete bytes prove final ends
`0x10038E4B` and `0x10038EC9`.

### Confirmed values

- Application allocation: `0xF8`; global owner pointer `0x00445958`;
  `NfMain*` at wrapper `+0x48`; main menu/loading/campaign at `+0x4C`,
  `+0x50`, `+0x5C`.
- `WindowsApplicationMain` resolves application-open through vtable slot `+0x04`
  at `0x004161A9`, poll/input/time through slot `+0x14` at `0x004161D0`,
  and a separate active-frame event through slot `+0x18` at `0x004161DB`.
  The poll body calls input dispatch at `0x00410FA7` and clock advance at
  `0x00410FDC`; clock advance conditionally calls application update at
  `0x00410F8D`. The active-frame event does not call application update.
- Campaign allocation: `0x5E8`; result allocation: `0x190`; pause allocation:
  `0x9C`.
- Application update calls `NfMain::LoadUser`; false invokes the default-profile
  initializer at `0x004125D0` before both paths construct the main menu.
- Side: Axis `0`, Allied `1`; missing `THRD` defaults the campaign constructor
  to Allied. Missing `AXMI`/`ALMI` defaults to zero.
- The constructor accepts any signed `THRD`: values other than zero/one choose
  Axis text but apply neither side maximum. Ordinary producers emit only
  zero/one; malformed-value import behavior therefore requires an explicit
  policy.
- Catalogue: ten rows per side, selected as zero-based `0..9`; constructed
  mission/briefing number is `selected + 1`.
- Valid-side construction and every side change initialize selection to that
  side's clamped maximum, so the highest unlocked row is shown first. An
  unsupported `THRD` leaves the constructor's initial zero selection unchanged.
- A special campaign callback `[0x00404C10,0x00404D83)` can set clamped maxima
  to nine and raw maxima to ten. It is a cheat/special path, not the normal
  unlock producer.
- Progress: success replaces `THRD` and raises only the selected side maximum
  with signed `mission_number + 1`; failure does not update progression.
- Successful Continue executes `briefing`, but the newly constructed Campaign
  writes `+0xD8 = 0` at `0x004015B0`; it returns to campaign selection with the
  latest unlocked row preselected, not directly to briefing.
- Score constants: float32 `10.0` at `0x00438A4C`, float32 `100.0` at
  `0x00438A50`; integer weights are recorded in CAMPAIGN-FLOW.
- Health and fuel score terms operate on the result of an unnamed virtual slot
  `+0x94` reached from `GetPrimaryActor`; either a null actor result or a null
  slot result omits both terms. The slot's semantics, exceptional division, and
  x87 conversion behavior remain outside the numeric-parity claim.
- Result reads/defaults and destroys old `SCOR` before the local-player gate.
  With a non-null player it calls `RegisterStats` at `0x00406650`, adds the
  replacement `SCOR` at `0x00406683`, and calls ignored `Write(nullptr)` at
  `0x00406695`; with a null player the old `SCOR` remains absent in memory.
- Container fields prove a minimum extent of `0x18`; child size is `0x20`;
  embedded profile owner is `NfMain+0x5F8`, with remembered name at
  `container+0x14`.
- Profile callback sender `+0x18` calls ignored write at `0x0040AC3A` then
  checked read at `0x0040AC4F` and constructs pilot roster/statistics at
  `0x0040F370`; sender `+0x1C` calls checked read at `0x0040ACF1` without a
  callback-local pre-write, then updates the name and destroys login. Callback
  `0x0040AAB0` directly constructs the new-profile owner `0x0040ADC0` on its
  create branch; callback `0x0040B1D0` is reached through the new owner's
  vtable.
- Roster framing is little-endian root `<id,payloadBytes>` plus ordered child
  `<id,payloadBytes,payload>` records. No version/magic/checksum was found.
- The 62-byte sample has root 0/payload `0x36`, `NAME` size 5, `PICT` size 8,
  and `MEDA` size 17.

The complete observations, state diagram, call graph, structure maps, score
formula, lifecycle, and evidence labels are published as
[`EV-20260803-002`](../re/systems/CAMPAIGN-FLOW.md) and
[FMT-ROSTER](../formats/ROSTER.md).

### Conditional inferences, hypotheses, and unknowns

- **Conditional inference:** for normal launcher values, success on row `n`
  unlocks row `n+1`; row nine writes raw maximum ten and the selector clamps it
  to nine.
- **Hypothesis only:** raw ten may feed campaign-thread completion, but the
  producer of the result's `threadend` value was not joined.
- **Unknown:** persistent difficulty, complete frontend enum/modal precedence,
  ordinary medal/equipment rules, `MEDA` trailing integer, exact malformed-file
  effects, and bit-identical output across a real roster corpus.

## Interpretation

The shared-container prediction is supported. Profile, campaign, cumulative
score/stat records, and medals are all accessed through the embedded
`NfMain+0x5F8` container. The result screen performs progression and removes
old `SCOR` before the local-player gate, then performs score/stats/replacement
`SCOR`/write only for a non-null local player.

The pure side-maximum mission rule is supported for campaign rows. No score or
medal gate occurs in the recovered selection handler. This does not prove
ordinary reward rules, which remain unknown.

The atomic-write prediction is contradicted: the original writer truncates the
target directly and has an incorrect short-write success comparison. Modern
durable storage must be a new product contract, not a claim of original parity.

Static evidence supports a bounded valid-roster importer, but not malformed
behavior parity or a bit-identical exporter. No runtime launch was used to
raise confidence beyond 2/3.

## Reproduction artifacts

All raw reports and tool databases remain ignored and local. Selected logical
Ghidra report names and SHA-256 values are:

| Logical report | SHA-256 |
|---|---|
| `Dogfighter.exe.campaign-flow-ghidra-app-core-decomp.txt` | `65B10F9A250DD4CE8CD78B2A4314D9143D9087E496877BED1F79F3A9335C14BC` |
| `Dogfighter.exe.campaign-flow-ghidra-campaign-core-instructions.txt` | `4BD331714A1A25307E7AFA02ECE964D86781A3BCFFA14741DBC47AC50C45788D` |
| `Dogfighter.exe.campaign-flow-ghidra-frontend-save-decomp.txt` | `F0AB7C947825CD50B7DB927EA286D9DA10E7F508160F76903D11BFE0295E1068` |
| `Dogfighter.exe.campaign-flow-ghidra-pause-result-instructions.txt` | `CDF6E041DBE2F47E50357615F6EBFF56F9EBE6F8673F698902021E8F54AB0BB8` |
| `AfEngine.dll.campaign-flow-ghidra-save-core-instructions.txt` | `2628EA3357801144ED73DBAAC36EFBF30139E708086A8E653B1A53FD68816862` |
| `AfEngine.dll.campaign-flow-ghidra-registerstats-named.txt` | `16588F267505777FEABDB007B67C64677B9BCD215B5F0D537CE4970847B8F5A7` |

The independent Rizin run produced 24 normalized JSON reports with 24 unique
function IDs. Three supplemental review reports closed the main-menu selection,
options, and stat-registration cross-checks:

| Function ID | SHA-256 |
|---|---|
| `FN-DOGFIGHTER-000099F0` | `BC6070BBC21E00B21964876A86CF30336D13AD6DB9C41AA123E5A7CF2FB56004` |
| `FN-DOGFIGHTER-0000CEC0` | `FD7837AE5FA8ADD8A1057D9E1371A9B5D4B73545C14B0EE5D868755E88603549` |
| `FN-AFENGINE-00058220` | `E8BDF2C4A8C3009D19AB5B5F9F4617D3FE36EFB6A923BF35C081C6CA0CFB022E` |

The 24 reports remain only as ignored local artifacts. Their IDs, source
hashes, and path cleanliness were validated during review; the three selected
supplemental hashes above were computed directly from those artifacts. No
separate hash ledger, binary, roster, decompiler database, or host path is
committed.

## Integrity checks

- Source and copy hashes match the public manifest: **pass**.
- Ghidra wrapper tests and working-copy/source-hash gates: **pass**; **29**
  local reports, zero unresolved-address/decompilation-failure markers, and all
  six published report hashes match.
- Rizin wrapper tests and all **12** normalized-export unit tests: **pass**;
  **24/24** reports have unique function IDs and valid schemas.
- Ghidra/Rizin end-exclusive boundary comparison: **19/19 exact** for the
  selected campaign/frontend/save bodies. Selected direct/import-resolved call
  sites: **26/26 exact**.
- Changed-document scan: **26/26** relative links resolve across eight Markdown
  files; **0** host-path hits across all nine changed documentation files.
- Function catalogue: **392/392** unique IDs and **14** columns; no new dangling
  relation was introduced (the two previously recorded historical CC
  `called_by` references remain).
- Public-boundary tests and full scan: **pass**, **771** files checked.
- Original files and real saves were not modified: **pass**.
- Reserved-ID uniqueness across refreshed refs: **pass**, zero prior hits for
  `EXP-20260803-114` and `EV-20260803-002`.
- Final staged `git diff --check`: **pass**.
- Independent review: **GO**, zero unresolved P0/P1/P2/P3 findings.

## Decision

| Area | Decision |
|---|---|
| frontend state machine | **NO-GO** for complete parity; bounded **GO** for the published boot/profile/campaign/mission/pause/result skeleton |
| campaign model | ordinary producer values **GO**; malformed-value behavioral parity **NO-GO** |
| sequential mission unlocks | **GO**; rewards remain **NO-GO** |
| scoring | numeric/bitwise parity **NO-GO**; bounded **GO** only for symbolic formula transcription and `SCOR` ordering; reward parity **NO-GO** |
| roster parser | **GO** for a new bounded valid-file importer; corrupt-file behavior parity **NO-GO** |
| bit-identical legacy writer | **NO-GO** |

## Follow-up

- Specify a versioned portable profile/campaign schema over ADR-0018's durable
  transaction, without treating it as a legacy format.
- Build synthetic valid/malformed roster fixtures and obtain a larger
  authenticated roster corpus before revisiting legacy export.
- Recover difficulty, normal medal/equipment producers, and the `threadend`
  producer in separate evidence IDs.
- Implement no runtime behavior from this documentation-only experiment.
