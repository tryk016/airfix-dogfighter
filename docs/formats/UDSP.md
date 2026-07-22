# FMT-UDSP — `.up` archive format

**State:** metadata and compression recovered; extraction not yet exposed

**Confidence:** 3/3 for parsed fields and compression, 1/3 for two unknown
directory fields

**Evidence:** `EV-20260721-003`, `EV-20260721-006` through `EV-20260721-010`

## Scope and evidence

The layout below is supported independently by:

- the `UpPackage`, `UpFile`, and `UpHashTable` code in `UdsPack.dll`;
- byte-level comparison of all five supplied archives;
- the portable C++ parser's structural/hash checks;
- successful decompression-size validation of every compressed record.

`UdsPack.dll` was analyzed statically and was never loaded as executable code.
The original archives remain read-only and are not stored in this repository.

## Physical layout

```text
0x00000000  32-byte header
0x00000020  file payloads
dirOffset   directory records (24 bytes each)
fileOffset  file records (24 bytes each)
stringOffset NUL-terminated byte strings
EOF
```

The five observed archives place the three metadata regions contiguously at
the end of the file. Offsets and integers are unsigned little-endian values.

## Header

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0x00` | 4 | `magic` | ASCII `UDSP` |
| `0x04` | 4 | `version` | `0x00000101` |
| `0x08` | 4 | `directoryBytes` | byte size of directory table; multiple of 24 |
| `0x0C` | 4 | `directoryOffset` | absolute file offset of directory table |
| `0x10` | 4 | `stringBytes` | byte size of string table |
| `0x14` | 4 | `stringOffset` | absolute file offset of string table |
| `0x18` | 4 | `fileBytes` | byte size of file-record table; multiple of 24 |
| `0x1C` | 4 | `fileOffset` | absolute file offset of file-record table |

The version is a 32-bit integer. Earlier notes described two version bytes;
the constructor explicitly compares the complete value with `0x101`.

## Common record prefix

Both record kinds begin with:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0x00` | 4 | `nameHash` | legacy case-insensitive lookup hash |
| `0x04` | 4 | `nameOffset` | byte offset relative to the string table |

Names use a legacy single-byte encoding. Polish/Danish/Norwegian/Swedish bytes
must not be decoded as UTF-8 before hashing.

## Directory record

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0x08` | 4 | unknown | observed but not required for listing |
| `0x0C` | 4 | unknown | observed but not required for listing |
| `0x10` | 4 | `fileCount` | number of file records in this directory |
| `0x14` | 4 | `fileTableByteOffset` | relative byte offset into file table |

The referenced file-record range is sorted by `nameHash`. The complete file
table is a concatenation of such ranges and is not globally sorted. Empty
directories may point at a shared or otherwise unused position.

## File record

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0x08` | 4 | `flags` | bit 0 means compressed; no other bits observed |
| `0x0C` | 4 | `unpackedSize` | byte size visible to the caller |
| `0x10` | 4 | `storedSize` | byte size stored in the payload region |
| `0x14` | 4 | `dataOffset` | absolute file offset of stored payload |

For an uncompressed entry, `storedSize == unpackedSize`. `UpPackage::Open`
returns `[dataOffset, dataOffset + storedSize)`; `UpFile` exposes
`unpackedSize` after decompression.

## Name hash

Only the final 15 bytes take part in the weighted sum. ASCII `A`–`Z` are folded
to lowercase. Bytes `0x80`–`0xFF` contribute as negative signed-byte values,
matching the original MSVC build's direct `char` call to `tolower`.

```text
primes = [3, 5, 7, 11, 13, 17, 19, 23,
          29, 31, 37, 41, 43, 47, 53, 59]
first = max(0, length - 15)
hash = length
for i in first .. length-1:
    hash += legacy_lower_signed_byte(name[i]) * primes[length - i]
```

Arithmetic wraps modulo `2^32`. Examples:

- `Game` and `GAME` → `0x00000E5E`;
- Windows-1252 bytes for `Maskinegevær.gti` → `0x0000A7DD`.

## Compression stream

Compressed file payloads are a sequence of blocks with no separate stream
header:

| Opcode | Following bytes | Output |
|---:|---|---|
| `0x65` | `count`, then 4-byte pattern | pattern repeated `ceil(count / 4)` times |
| `0x66` | `count`, then `count` bytes | literal bytes |
| `0x67` | `count`, then `count` bytes | literal bytes; observed once per compressed file |

The original reader treats `0x66` and `0x67` identically. Decoding ends when
the `storedSize` input bytes are consumed. The output must equal
`unpackedSize`; unknown opcodes and truncated blocks are errors.

## Validation corpus

| Archive | Bytes | Directories | Files | Compressed | Stored payload bytes | Unpacked bytes |
|---|---:|---:|---:|---:|---:|---:|
| `Dansk.up` | 22,290,690 | 21 | 218 | 146 | 22,281,236 | 30,280,656 |
| `English.up` | 22,112,202 | 21 | 218 | 135 | 22,102,754 | 30,049,180 |
| `Norsk.up` | 22,365,565 | 21 | 218 | 146 | 22,356,107 | 30,283,565 |
| `Resource.up` | 170,642,453 | 478 | 2,628 | 1,338 | 170,511,460 | 191,873,346 |
| `Svenska.up` | 22,414,591 | 21 | 218 | 146 | 22,405,121 | 30,280,353 |

All 3,500 file records and all 1,911 compressed streams passed the current
metadata, hash, bounds, directory-range, opcode, and decompressed-size checks.
Across all five archives, directory ranges partition the file table exactly:
every file record is referenced once, with no gaps or overlaps.

## Portable implementation

- Library: `src/airfix/archive/UdspArchive.*`
- Read-only CLI: `udsp-list [--summary|--verify|--inventory] <archive.up>`
- Synthetic tests: `tests/UdspArchiveTests.cpp`

`Archive::open` performs bounded random-access I/O: it reads the 32-byte header,
validates offsets against the physical file length, and then loads only the
metadata tail. For `Resource.up` that is 130,961 bytes instead of 170,642,453
bytes (about 1/1,303 of the previous allocation). Diagnostic `--verify` mode
deliberately buffers the full archive so it can validate every payload stream;
that mode is tooling, not the iOS runtime path.

`Archive::openRegion` applies the same validation and bounded metadata reads to
a `[baseOffset, size)` region of a containing file. AFPACK validation therefore
opens nested `Resource.up` and localization archives directly in place without
copying their 192 MB combined payload to another buffer or temporary file.

`readFilePrefix` decodes only the requested logical prefix for signature
classification. `readFile` applies a caller-provided decoded-size limit and is
used one entry at a time by format tooling. The inventory path therefore never
holds the full archive or all decoded assets in memory.

## Security requirements

- Treat all sizes, offsets, names, terminators, hashes, and ratios as untrusted.
- Check arithmetic before adding or multiplying offsets and sizes.
- Reject data spans outside `[0x20, directoryOffset)`.
- Reject unknown flags/opcodes and inconsistent output sizes.
- Apply a configurable decompressed-output limit before allocation.
- Listing is read-only. A future extractor must reject absolute paths, drive
  prefixes, `..`, and output-root escapes.
- Keep empty, truncated, oversized, bad-hash, bad-range, traversal, and
  decompression-bomb cases in synthetic tests.

## Remaining unknowns

- Meaning of directory record fields `0x08` and `0x0C`.
- Whether opcode `0x67` was intended as an explicit final-literal marker even
  though the original decoder does not branch on that meaning.
- Whether other retail/localized builds use additional flags or encodings.
- Exact semantics of overlapping/aliased directory file ranges.
