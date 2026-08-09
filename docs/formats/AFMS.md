# AFMS private mission-selection document

AFMS is a small owner-private companion to AFPACK v1. It names one explicit
setup/Level pair, an optional player-aircraft object, and a start ordinal. It
contains no game asset bytes and grants no trust to the referenced content:
the application resolves every path only inside an independently
authenticated, currently active AFPACK revision.

The public repository, GitHub Actions, and unsigned IPA never contain a filled
AFMS document. An owner creates it locally with `afmission-create`, transfers
it privately, and imports it after the AFPACK. The iOS application persists a
canonical current document and one validated backup in its private Application
Support directory. Invalid input cannot replace a future schema or an unsafe
file-system entry.

## Version 1 envelope

All integers are unsigned little-endian. The complete document is limited to
16 KiB.

| Offset | Size | Meaning |
|---:|---:|---|
| `0x00` | 4 | ASCII magic `AFMS` |
| `0x04` | 2 | envelope version, exactly `1` |
| `0x06` | 2 | flags, exactly zero |
| `0x08` | 4 | semantic schema version, exactly `1` |
| `0x0c` | 4 | exact total document size |
| `0x10` | 32 | SHA-256 of bytes `0x00..0x0f` followed by bytes `0x30..EOF` |
| `0x30` | variable | four canonical TLV fields |

Each TLV has a two-byte field identifier, a two-byte payload size, and the
exact payload. Fields occur once each and in ascending identifier order.

| Field | Payload |
|---:|---|
| 1 | required setup logical path |
| 2 | required Level logical path |
| 3 | optional player-object logical path; zero bytes means absent |
| 4 | exact four-byte requested start ordinal |

Logical paths are non-empty printable ASCII, at most 4096 bytes, and use the
same canonical backslash-separated UDSP spelling as the authenticated archive.
Absolute paths, `.`/`..`, forward slashes, duplicate fields, unknown fields,
trailing bytes, truncation, noncanonical spelling, and mismatched hashes are
rejected.

The SHA-256 detects corruption and makes the encoding deterministic. It is not
a signature and does not authenticate the selected mission. AFPACK validation,
the active content revision, exact archive lookup, and the existing atomic
mission publication gate remain authoritative.

## Owner-local creation

```powershell
afmission-create.exe `
  --setup <setup-logical-path> `
  --level <level-logical-path> `
  [--player-object <player-object-logical-path>] `
  [--start-index <uint32>] `
  --output <private-file.afmission>
```

The tool refuses an existing output, noncanonical or unsafe paths, an invalid
ordinal, duplicate arguments, and output names without the `.afmission`
extension. Its status output never echoes logical paths.

## iOS lifecycle

1. The owner imports and authenticates AFPACK through the existing picker.
2. `Import Mission Selection` coordinates and copies a selected `.afmission`
   into a bounded private temporary file.
3. The codec validates it before a durable current/backup transaction.
4. The app re-inspects active content and resolves the selection only against
   the pinned AFPACK handle.
5. A successful selection survives relaunch. A corrupt current may recover the
   validated backup; a linked/wrong-type entry or future schema blocks writes.

The UI reports only fixed status text. It never displays or logs the logical
paths, local source URL, checksum, or installed private path.
