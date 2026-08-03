# FMT-ROSTER — player profile and campaign roster

**State:** valid-file framing and known records statically recovered; bounded
portable importer implemented; legacy writer not implemented

**Evidence IDs:** `EV-20260721-022`, `EV-20260803-002`

**Sample SHA-256:**
`10FE1F21599755498A62E1307C0B7E3328D8FFEF06643C2B9C1DFAFADC46ED4F`

**Endian:** little-endian

**Version:** no version field found

## Role and detection

A roster is the original single-player profile/campaign document. The
frontend catalogues the logical role `user\rosters\*.roster`. One file carries
profile identity, selected portrait, campaign side and maxima, cumulative
score/stat records, and repeated medals.

There is no dedicated magic value. The authenticated shipped sample uses root
ID `0`, and new-profile code explicitly constructs root ID `0`. The generic
root ID remains a four-byte field; root zero alone is not a safe file-type
detector.

The file is a specialization of [FMT-AFCHUNK](AFCHUNK.md). This note records
roster-specific records and lifecycle without changing the generic format.

## Disk layout

```text
offset  size  type       meaning
0x00    4     u32 LE     root/container ID (new roster: 0)
0x04    4     u32 LE     serialized child bytes (physical size - 8)
0x08    ...   records    ordered, unpadded child records

record:
+0x00   4     FourCC     record ID, encoded as u32 LE
+0x04   4     u32 LE     payload byte count, excluding this header
+0x08   n     bytes      payload; no alignment or padding
```

The header has no version, record count, checksum, compression marker, or
atomic-write generation. Zero-length and duplicate generic chunks are
supported by `AfChunkContainer`; `MEDA` is known to repeat in rosters.

### Known roster records

| FourCC | Numeric value | Payload | Default / producer | Confidence |
|---|---:|---|---|---:|
| `NAME` | `0x454D414E` | NUL-terminated profile callsign | required by new-profile producer | 2 |
| `PICT` | `0x54434950` | NUL-terminated portrait identifier | required by new-profile producer | 2 |
| `MEDA` | `0x4144454D` | NUL-terminated medal identifier followed by observed LE `int32 1` | new profile adds `medal_rookie`; repeatable | 2 |
| `THRD` | `0x44524854` | signed `int32` | absent defaults campaign UI to Allied (`1`); result writes Axis `0` or Allied `1` | 2 |
| `AXMI` | `0x494D5841` | signed `int32` raw maximum selectable zero-based Axis row | absent reads as `0` in campaign UI; success updater adds its candidate directly | 2 |
| `ALMI` | `0x494D4C41` | signed `int32` raw maximum selectable zero-based Allied row | absent reads as `0` in campaign UI; success updater adds its candidate directly | 2 |
| `SCOR` | `0x524F4353` | signed `int32` cumulative score | absent means `0` | 2 |
| `ACKI` | `0x494B4341` | signed `int32` cumulative statistic | absent means `0` when updated | 2 |
| `GUKI` | `0x494B5547` | signed `int32` cumulative statistic | absent means `0` when updated | 2 |
| `WUKI` | `0x494B5557` | signed `int32` cumulative statistic | absent means `0` when updated | 2 |
| `FOOK` | `0x4B4F4F46` | signed `int32` cumulative statistic | absent means `0` when updated | 2 |
| `FRIK` | `0x4B495246` | signed `int32` cumulative statistic | legacy update gate/value anomaly; see below | 2 |
| `DEAT` | `0x54414544` | signed `int32` cumulative statistic | absent means `0` when updated | 2 |
| `PKIL` | `0x4C494B50` | profile UI reads an integer | producer and exact role unknown | 1 |
| `PDEA` | `0x41454450` | profile UI reads an integer | producer and exact role unknown | 1 |

The public names of `ACKI` through `DEAT` are preserved as FourCCs. The
semantic names of their source counters are not guessed. The original
`RegisterStats` path tests mission field `+0x4C` before updating `FRIK`, but
adds mission field `+0x48`; both disassemblers confirm that mismatch.

`THRD` is not range-validated by the original campaign constructor. Ordinary
result producers write Axis `0` or Allied `1`. For any other signed value, the
constructor selects Axis text (`value != 1`) but selects neither side maximum
(`value != 0 && value != 1`). A bounded importer must reject, preserve as an
unsupported value, or explicitly normalize this case; treating every nonzero
value as Allied would not reproduce the recovered instructions.

## Authenticated sample

The only authenticated roster sample is 62 bytes:

| Record | Payload bytes | Parsed value |
|---|---:|---|
| root | `0x36` (54) | ID `0`, physical size `8 + 54` |
| `NAME` | `5` | four-character callsign plus NUL |
| `PICT` | `8` | `pilot09` plus NUL |
| `MEDA` | `17` | `medal_rookie` plus NUL and LE `int32 1` |

The callsign is intentionally not promoted to a schema rule. One sample proves
the record shape, not uniqueness, ordering of every future record, versioning,
or all valid values.

New-profile code emits `NAME`, `PICT`, then `MEDA`. Its physical size is:

```text
51 + strlen(callsign) + strlen(portrait)
```

because both strings include their NUL terminators and the fixed medal payload
is 17 bytes.

## In-memory layout and ownership

`AfChunkContainer` occupies at least `0x18` bytes:

| Offset | Field | Ownership |
|---:|---|---|
| `+0x00` | vptr | class dispatch |
| `+0x04` | first child | owns linked child chunks |
| `+0x08` | root ID | value |
| `+0x0C` | serialized child-byte total | value |
| `+0x10` | serialized cache | owns allocation |
| `+0x14` | remembered filename | owns `strdup` allocation or null |

Each child occupies `0x20` bytes:

| Offset | Field | Ownership |
|---:|---|---|
| `+0x00` | vptr | class dispatch |
| `+0x04` | payload | owned allocation |
| `+0x08` | FourCC ID | value |
| `+0x0C` | payload size | value |
| `+0x10` | read cursor | mutable value |
| `+0x14` | parent container | non-owning back pointer |
| `+0x18` / `+0x1C` | previous / next | intrusive list links |

`NfMain` embeds the authoritative profile container at `+0x5F8`; it is not a
separate heap pointer. Container constructor/destructor are
`[0x10038B30,0x10038B42)` and `[0x10038B50,0x10038B90)`. `GetBuffer` is
`[0x10038ED0,0x10038F54)` and reset helper is
`[0x10038F60,0x10038F72)`.

## Read lifecycle and malformed files

`AfChunkContainer::Read` is `[0x10038CD0,0x10038E4B)`.

### Null filename

`Read(nullptr)` frees the remembered filename, sets it to null, and returns
false. It does not clear children or the serialized cache. The profile-delete
callback uses it after requesting file deletion; this unbinds persistence but
does not reset the in-memory profile.

### Non-null filename

The reader duplicates and remembers the supplied logical name before opening
it. It removes the prior serialized buffer and children before the new file is
fully validated. It then reads the whole file and checks the root payload size
against `fileSize - 8`.

Confirmed false returns include missing/open failure, seek/tell failure,
incomplete whole-file read, and root-size mismatch. The whole-file allocation
at `0x10038D95` is not null-checked before the read, so allocation-failure
behavior is unknown rather than a confirmed false return. The operation is not
transactional: old state is not restored and the new remembered name can
remain bound.

The legacy reader does not establish safe per-child remaining-byte checks,
overflow-safe arithmetic, a minimum eight-byte file check before header use,
or bounded NUL search in `ReadString`. A root-size mismatch path also performs
an invalid interior-pointer free in the recovered instructions. Exact effects
of malformed inputs are intentionally **unknown** because no corrupt file was
executed.

A portable importer must not copy these faults. It must check the complete
root, each eight-byte record header, every payload extent, aggregate/count
limits, integer overflow, exact NUL termination, duplicate policy, and trailing
extent before publishing any state.

## Write lifecycle and durability

`AfChunkContainer::Write` is `[0x10038E50,0x10038EC9)`.

- An explicit name is used for that call but does not replace the remembered
  name. A null argument uses the remembered name; no available name returns
  false.
- The writer serializes the root and children, opens the destination directly
  with `fopen(..., "wb")`, truncates it, issues one `fwrite`, then calls
  `fclose`.
- There is no temporary file, atomic replace, backup, generation, checksum,
  flush-to-disk, directory sync, rollback, or fallback read.
- The write requests `payloadBytes + 8` bytes but compares the returned byte
  count with `payloadBytes`. A file missing up to its final eight bytes can be
  reported as a successful write. With zero payload bytes, even a zero-byte
  `fwrite` result satisfies the comparison.
- The `fclose` result is ignored.

The profile/login constructor, new-profile creation, mission result, and
`NfMain` shutdown ignore the returned write boolean. In the profile selection
callback, sender `+0x18` also ignores `Write(nullptr)` before a checked
`Read(selected)` and then constructs the pilot roster/statistics screen at
`0x0040F370`, while sender `+0x1C` performs the checked read directly, updates
the player name, and destroys login without a callback-local pre-write.
Consequently the original UI can continue after a failed or falsely successful
write wherever a write is attempted.

The mission-result constructor reads/defaults and destroys old `SCOR` before
testing whether a local player exists. A null player therefore leaves `SCOR`
absent in memory and skips mission score, stat updates, replacement `SCOR`, and
the roster write; a non-null player performs those later steps.

The writer is neither atomic nor reliably durable. ADR-0018's modern
current/backup/partial storage should be reused for a new schema, but it is not
evidence that the original roster had those properties.

## Parser and exporter decision

**GO** for implementing a bounded, fail-closed importer for valid files with
the known records above. Unknown and duplicate records need an explicit
preservation policy if migration fidelity is required.

The portable implementation is documented in
[Campaign and legacy-roster core](../systems/CAMPAIGN.md). It accepts root zero,
rejects duplicate known singleton records, permits repeated `MEDA`, and retains
unknown records only as ID/size descriptors. This is an explicit safe product
policy, not a claim about every legacy duplicate or migration case.

**NO-GO** for reproducing legacy corrupt-file behavior. The recovered behavior
is memory-unsafe and not completely defined without prohibited execution.

**NO-GO** for a bit-identical legacy exporter. One authenticated sample is
insufficient to close ordering, duplicate/update behavior, all record payloads,
integer conversion, and golden round trips. The flawed direct writer must not
be used as the durability model for either Windows or iOS.

## Cross-references

| Operation | Module / location |
|---|---|
| generic read | `AfEngine.dll` VA `0x10038CD0`, RVA `0x00038CD0` |
| generic write | `AfEngine.dll` VA `0x10038E50`, RVA `0x00038E50` |
| generic serialization | `AfEngine.dll` VA `0x10038ED0`, RVA `0x00038ED0` |
| embedded owner construction | `NfMain` call site `0x10043A73` |
| shutdown write | `NfMain` call site `0x1004471B` |
| embedded owner destruction | `NfMain` call site `0x100448B2` |
| campaign/result updates | `Dogfighter.exe` `[0x00405A90,0x0040690A)` |
| stat registration | `AfEngine.dll` `[0x10058220,0x100584A2)` |
| profile/login lifecycle | `Dogfighter.exe` `[0x0040A4B0,0x0040B3B3)` plus delete callback `[0x0040A0D0,0x0040A1B1)` |

## Unknowns

- Meaning of the trailing observed `MEDA` integer.
- Producers and complete semantics of `PKIL` and `PDEA`.
- Complete valid record set and ordering/duplicate rules across real profiles.
- Whether any original release introduced a root ID or implicit schema change.
- Exact legacy effect of each malformed/truncated file shape.
- Bitwise output across a representative roster corpus.
