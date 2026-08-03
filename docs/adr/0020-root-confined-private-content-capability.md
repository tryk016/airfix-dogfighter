# ADR-0020: root-confined private-content file capability

- Status: accepted
- Date: 2026-08-03
- Deciders: project owner and implementation lead

## Context

ADR-0019 requires optional owner-private texture replacements to remain outside
Git, CMake resources, application bundles, and public CI. Its stage-2 manifest
index validates relative names but intentionally performs no filesystem I/O.
The next boundary must let later portable code read a bounded file without
giving that code an unrestricted host path or allowing a manifest entry to
escape the configured private root.

The existing `readBoundedRegularFile` pins and validates one final file. It is
appropriate for trusted application-owned document paths, but it does not pin
a root directory and then resolve every untrusted relative component from that
handle. Pre-validating and concatenating a host path would leave intermediate
symbolic-link/junction traversal and time-of-check/time-of-use substitution as
part of the security boundary.

The threat model includes malformed manifest strings, absolute/traversal paths,
symbolic links, Windows junctions and other reparse points, special files,
hard-link aliases, root-name replacement, oversized files, and completion from
an obsolete package generation. It does not assume that the private package is
cryptographically trusted merely because its owner selected a directory.

## Requirements

- Classic GTI loading must remain independent and unchanged.
- The platform owner opens an absolute, non-empty, embedded-NUL-free configured
  root once. Portable code sees only an opaque read-only capability and a
  monotonic non-zero generation.
- Every path component is resolved relative to an already opened directory;
  unrestricted path concatenation is forbidden.
- Absolute, drive-qualified, device/UNC, alternate-stream, empty, dot, dot-dot,
  malformed UTF-8, overlong, ambiguous Windows, and reserved DOS names fail
  before filesystem access.
- Symbolic links, junctions, reparse points, directories at file positions,
  devices, FIFOs, sockets, and multiply linked regular files are rejected.
- Reads are bounded before allocation and revalidate identity, type, size, and
  link count after the exact read. A stale generation performs no read.
- Failure exposes one fixed enum and no input-derived text, root, logical path,
  checksum, operating-system message, or partial bytes.
- Public tests create only temporary synthetic directories and binary bytes.

## Decision

Introduce two public boundaries:

```text
platform owner/configuration
        |
        | absolute owner-local root + generation
        v
openPrivateTextureFileStoreLocalRoot(...)
        |
        | opaque pinned capability
        v
PrivateTextureFileStore::readFile(relative, maximumBytes, expectedGeneration)
        |
        +-- complete byte vector
        `-- fixed redacted failure status + no bytes
```

`PrivateTextureFileStore.hpp` contains the portable interface and fixed result
types. It has no filesystem path API. `PrivateTextureFileStorePlatform.hpp` is
the thin owner-side factory and is the only public surface that accepts the
configured host root. Implementations discard its spelling after acquiring a
native root handle or descriptor.

The generation is a lifecycle token, not a content checksum. The owner creates
a new non-zero generation for each validated package configuration. A caller
must supply the generation it planned against on every read; mismatch returns
`staleGeneration` before relative-path parsing or filesystem access. A pinned
old capability can never silently stand in for a newly published package.

### Common relative-path policy

The common C++20 parser accepts `/` and `\` as input separators, divides the
name into components, and passes only individual UTF-8 components to native
code. It rejects control bytes, colon, Win32 wildcard/namespace punctuation,
empty/dot/dot-dot components, leading/trailing separators, trailing dot/space,
reserved DOS device stems, invalid or non-scalar UTF-8, and configured byte or
component ceilings. It performs no locale-dependent normalization and does not
rewrite case.

The manifest parser remains responsible for its schema-specific path limits;
the capability validates again because callers and future derivative manifests
are not trusted merely for possessing a parsed string.

### Windows adapter

The root is opened as a directory with `FILE_FLAG_OPEN_REPARSE_POINT`, sharing
read access only. The adapter verifies that the resulting disk handle names a
non-reparse directory and then retains the handle, not its path.

Each child is opened one component at a time with the documented user-mode
`NtCreateFile` API. `OBJECT_ATTRIBUTES.RootDirectory` is the already pinned
parent handle, and `FILE_OPEN_REPARSE_POINT` disables ordinary reparse
processing for the child being opened. Microsoft explicitly documents both
relative-to-directory-handle naming and this no-reparse behavior in
[NtCreateFile](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile).
The adapter rejects the reparse attribute after every open and retains the
current directory handle while opening its child. It therefore does not need
to reconstruct or compare a potentially private final path string.

The final handle shares read access only, preventing a compatible new writer or
delete/rename handle from being introduced while it is owned. File type,
reparse tag, size, link count, volume serial, and 128-bit file ID are queried
through handle-based information; Microsoft documents these information
classes for
[GetFileInformationByHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex).
A final file with more than one hard link is rejected so a name inside the root
cannot alias a second directory entry outside it.

### Apple/POSIX adapter

The Apple/Linux implementation opens the absolute root with `O_DIRECTORY`,
`O_NOFOLLOW`, `O_CLOEXEC`, and non-blocking read semantics. It walks every
intermediate component with `openat` relative to the current descriptor and
the same no-follow directory policy, then opens the leaf with `O_NOFOLLOW` and
`O_NONBLOCK`. Builds fail rather than silently weakening the contract when
these required flags are unavailable.

`fstat` validates the opened object instead of trusting a prior path-based
inspection. This follows Apple's secure-file guidance to use `O_NOFOLLOW`,
retain the opened descriptor, and validate type and metadata through `fstat`
([Race Conditions and Secure File Operations](https://developer.apple.com/library/archive/documentation/Security/Conceptual/SecureCodingGuide/Articles/RaceConditions.html)).
The adapter requires one-link regular files, bounds size before allocation,
uses positioned reads, and compares device, inode, type, size, link count,
modification time, and change time after the read.

### Failure and privacy contract

The capability is non-throwing at its public boundary. Allocation, conversion,
native-open, inspection, and read failures collapse to fixed statuses; every
failure clears the byte vector. Platform error messages and paths never enter a
result. A later resolver may aggregate counts by status but must not add the
relative name, configured root, digest, manifest method, or private metadata.

## Scale and reliability

The first version reads one bounded complete file at a time. It does not scan a
directory, decode PNG, cache bytes, or start asynchronous work. A read owns at
most one handle per current directory plus the final file; previously traversed
parents can be released because each child handle is already pinned. The store
is immutable and concurrent reads do not share a file offset.

Later resolver/cache work may issue concurrent reads through the same root
capability, but each request must carry manifest, package, and mission
generations and remain within separate encoded/staging budgets. Directory
enumeration for proving an exact mip set is a separate extension and must use
the same component-walk boundary; it is not implied by this file-read API.

## Alternatives rejected

### Concatenate root and manifest path, then canonicalize

Rejected. Lexical validation and canonical-path prefix checks do not pin every
intermediate object and create avoidable substitution windows. They also risk
retaining or logging private host paths.

### Reuse `readBoundedRegularFile` directly

Rejected for this boundary. It safely pins the leaf it opens, but it accepts a
complete host path and does not establish a root-relative component walk.

### Allow links that resolve below the root

Rejected in the first implementation. Proving link targets and stable ancestry
adds platform-specific complexity with no requirement from the reviewed
package. All link and reparse forms fail closed.

### Copy private content into an application-managed public cache first

Rejected. That would broaden the private-data lifecycle and does not remove the
need to secure the source import boundary.

## Consequences and remaining gates

Stage 3 receives **GO** for a read-only root-confined capability and synthetic
tests on Windows, Linux, macOS, and iOS builds. The root spelling remains solely
an owner-side input and no product currently calls the factory.

The following remain **NO-GO** until their own reviewed stages:

- parsing a real private manifest from a configured root;
- `TextureReplacementResolver`, actual-GTI SHA confirmation, and per-texture
  fallback;
- PNG/IHDR/mip validation and any native GPU upload;
- product arguments, settings, availability UI, or effective Enhanced mode;
- directory enumeration, streaming, caches, and compressed derivatives; and
- claims of cryptographic authentication for non-zero mips absent from the
  current manifest schema.

The POSIX metadata comparison detects common in-place changes but is not a
content digest or mandatory kernel lease against a process that already owns a
writable descriptor. Later resolver stages must verify every available digest;
owner-private untrusted concurrent writers remain outside the accepted package
operating model. Any failure still falls back per texture only after the
resolver and unchanged GTI path are implemented.
