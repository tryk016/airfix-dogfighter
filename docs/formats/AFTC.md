# AFTC private touch-control preferences

`touch-controls.aftc` is the reconstruction's private, platform-owned settings
document for the iOS touch overlay. It is not an original Airfix Dogfighter
format and never belongs in an AFPACK or original-resource corpus.

## V2 semantic record

| Field | Encoding | V2 values |
|---|---:|---|
| Handedness | `uint8` | `0` right-handed, `1` left-handed |
| Density | `uint8` | `0` automatic, `1` forced compact |
| Resting opacity | `uint8` percent | inclusive `50-100` |
| Visibility | `uint8` | `0` auto-hide with a connected controller, `1` always visible during active gameplay |

The opacity percentage scales resting control backgrounds only. Text, semantic
accent borders, active feedback, capture rectangles, and the 44-point minimum
touch targets are not multiplied by this value.

## Canonical envelope

All integers are little-endian. Current-schema V2 is exactly 68 bytes:

| Offset | Size | Meaning |
|---:|---:|---|
| `0x00` | 4 | ASCII magic `AFTC` |
| `0x04` | 2 | envelope version `1` |
| `0x06` | 2 | flags, currently `0` |
| `0x08` | 4 | semantic schema version `2` |
| `0x0C` | 4 | exact document size |
| `0x10` | 32 | SHA-256 over the prefix and all fields, excluding this slot |
| `0x30` | 5 | field `1`, size `1`, handedness byte |
| `0x35` | 5 | field `2`, size `1`, density byte |
| `0x3A` | 5 | field `3`, size `1`, resting-opacity byte |
| `0x3F` | 5 | field `4`, size `1`, visibility byte |

Current fields are required, unique, strictly ordered, and exactly one byte.
Unknown, duplicate, missing, reordered, truncated, trailing, semantically
invalid, or larger-than-4-KiB current documents fail closed. A checksum-valid
future schema is retained byte-for-byte and blocks an older build from
downgrading it.

The decoder accepts the exact 63-byte V1 record and migrates it in memory to
V2 with visibility value `0`. A loaded V1 current document remains valid but
is marked for repair. The next explicit Save and apply transaction publishes
canonical V2 while rotating the intact V1 bytes to the backup. No automatic
background write occurs during loading.

## Storage and recovery

The private store uses `touch-controls.aftc` plus
`touch-controls.aftc.backup` under an injected absolute application-private
settings leaf. It follows ADR-0018: `current -> backup -> canonical defaults`,
exclusive prepared files, durable atomic replacement, exact readback, and
explicit commit-unknown handling. Linked/non-regular entries, unsafe
directories, unavailable I/O, and intact future schemas block writes. A
malformed or oversized current document may be replaced after safe recovery.

No local path, document bytes, checksum, device identity, controller name, or
Bluetooth metadata crosses the native store boundary or appears in UI/logs.
The visibility preference stores only the user's policy; connection state is
ephemeral and is never written to AFTC.
