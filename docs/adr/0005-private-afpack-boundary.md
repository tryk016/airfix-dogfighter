# ADR-0005: Use a deterministic private AFPACK content boundary

**Status:** Accepted

**Date:** 2026-07-21

**Deciders:** project owner and implementation lead

## Context

The repository and GitHub Actions build are public, while the owner has a local
original Airfix Dogfighter installation. The data-less iOS application needs a
safe way to receive approximately 190 MB of required source archives without
uploading them to GitHub or embedding them in the IPA. The recovered UDSP format
already compresses individual payloads and must remain accessible through
bounded random reads.

## Decision

Define AFPACK version 1 as a small project-owned container with fixed tables,
normalized UTF-8 logical paths, 64-bit offsets, SHA-256 per entry, and no outer
compression. A deterministic Windows converter runs only on the owner's
machine. It initially wraps `Resource.up` and one selected localization archive
without extracting them, then can add documented converted-asset entries later.

The public iOS application and CI remain data-less. The iOS importer validates
and streams the selected local pack into private Application Support storage,
activating it atomically only after complete verification. Original Windows
executables and plugins are denied by the converter and importer.

The normative byte layout, manifest schema, allowlist, and import transaction
are specified in `docs/formats/AFPACK.md`.

## Options considered

### A. Custom table container with opaque UDSP entries — selected

Preserves random access, avoids a second compression layer and ZIP dependency,
allows streaming integrity checks, and lets reconstruction proceed before every
asset format is converted.

### B. ZIP archive

Rejected for the first implementation. Deflated whole entries add no useful
compression over UDSP and make nested random access more complex. ZIP64,
filename normalization, and library behavior also enlarge the trusted surface.

### C. Embed original resources in the IPA or GitHub artifacts

Rejected because it would expose copyrighted private data through a public
repository/build system and couple every code build to a large content upload.

### D. Loose files copied directly into the app

Rejected because partial transfer and replacement are harder to make atomic,
and a single audited manifest/container is easier to validate and support.

## Consequences

- Code CI can use only generated synthetic packs.
- `.afpack` remains globally ignored by Git and must never be uploaded as a
  public Actions artifact.
- The writer and importer need streaming SHA-256 and checked 64-bit I/O.
- Version 1 deliberately carries already-compressed UDSP without outer
  compression; this spends a small amount of padding for simpler bounded I/O.
- The runtime needs a bounded subfile reader so UDSP can operate on an entry
  without copying a 170 MB archive.
- Manifest integrity detects corruption but is not origin authentication.
- No original PE module crosses the content boundary; plugin registration is
  reconstructed in native code.
- Missing CD music is represented explicitly and remains non-fatal.
- AFPACK v1 remains content-only. An explicit owner-private AFMS companion
  selects a setup/Level/start without changing, repacking, or trusting the
  authenticated container; filled AFMS documents also remain outside Git/CI.

## Revisit conditions

- Converted assets replace all source UDSP archives and need content-addressed
  deduplication or chunking.
- Device transfer requires resumable multi-part transport.
- A real threat model requires signed manifests/authenticated content.
- Measurements show outer compression gives material size savings without
  harming device import time or random access.

## Action items

1. [x] Specify AFPACK version 1 and manifest schema.
2. [x] Implement a portable bounded metadata parser and malformed-input tests.
3. [x] Implement the deterministic, allowlisted, streaming local writer.
4. [x] Add bounded entry regions and nested UDSP validation.
5. [x] Implement the atomic iOS verifier/import transaction.
6. [ ] Exercise cancellation, low-storage, corruption, and rollback on device.
7. [x] Add the bounded AFMS mission-selection companion and durable iOS import.
