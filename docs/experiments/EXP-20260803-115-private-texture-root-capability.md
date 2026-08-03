# EXP-20260803-115: root-confined private texture file capability

**Status:** implemented and locally validated; hosted cross-platform validation
pending

**Evidence boundary:** temporary synthetic directories and binary byte strings
only; no private manifest, image, original game asset, configured package, or
local machine path was read or retained

**Decision:** GO for ADR-0019 stage 3; NO-GO for resolver, PNG preparation,
product configuration, or effective Enhanced textures

## Question

Can later replacement code read one bounded file below an owner-configured root
without receiving an unrestricted host path, following filesystem
indirections, exposing private diagnostics, or changing Classic behavior?

## Implemented contract

- a portable read-only `PrivateTextureFileStore` exposes only a generation,
  relative-file read, bounded byte result, and fixed status enum;
- the owner-side platform factory requires an absolute root, opens it once, and
  discards its path spelling after acquiring a native handle/descriptor;
- common validation rejects absolute/traversal/alternate-stream/ambiguous paths,
  unsafe components, invalid UTF-8, reserved DOS names, and configured limits;
- Windows walks one component at a time with root-relative `NtCreateFile`,
  `FILE_OPEN_REPARSE_POINT`, handle-based type/file-ID checks, read-only sharing,
  and single-link leaf enforcement;
- Apple/Linux walks with `openat`, `O_NOFOLLOW`, pinned directory descriptors,
  `fstat`, non-blocking special-file defense, and single-link leaf enforcement;
- exact reads are bounded before allocation and revalidated afterward; and
- stale generation and every failure return no bytes and no input-derived text.

No product target calls the factory. The implementation performs no manifest
read, PNG decode, replacement lookup, GTI hashing, fallback, cache operation,
setting migration, UI change, native upload, or private-data access.

## Synthetic verification matrix

The dedicated suite covers:

- valid nested reads with both accepted separator spellings;
- zero byte limit, oversize, missing file, directory leaf, hard-link alias, and
  stale generation;
- empty, absolute, drive-qualified, traversal, dot, empty-component,
  alternate-stream, wildcard, trailing-dot/space, reserved-device, malformed
  UTF-8, and configured path/component ceilings;
- configured-root file/type failure plus relative-root and embedded-NUL root
  rejection;
- symbolic-link or junction rejection for an intermediate component and for
  the configured root; and
- root-name replacement: POSIX remains pinned to the opened original root,
  while Windows may deny the rename through its retained non-delete-sharing
  handle; neither follows a replacement spelling.

The Windows junction fixture uses a temporary mount-point reparse record when
ordinary unprivileged symbolic-link creation is unavailable. All fixtures are
removed with their temporary test directory.

## Validation record

- MinGW GCC 15.2/Ninja compiles and links the dedicated Windows target.
- Visual Studio 2026 MSVC 19.51/Ninja compiles and links the same target.
- The dedicated synthetic suite passes with both Windows compilers.
- A fresh portable GCC/Ninja build completes and all 145/145 CTests pass.
- A fresh Visual Studio 2026 MSVC/Ninja product build compiles all 766 build
  edges, including SDL3, D3D11, XAudio2, the application, and data-less product
  smokes; all 159/159 CTests pass.
- `clang-tidy` with Clang analyzer and bugprone checks is clean for the common
  and Windows production implementation. The structured security review found
  and fixed an allocation-failure native-handle ownership gap, then added an
  explicit configured-root embedded-NUL rejection before commit.
- Clang formatting in warnings-as-errors mode, synthetic public-boundary tests,
  the 768-file repository scanner, changed-document links, changed-scope
  local-path review, and `git diff --check` pass.
- Linux/macOS/iOS compilation and tests remain required before publication.

## Decision

**GO** for the isolated read-only stage-3 capability. Its handle-relative walk
is a stronger boundary than constructing a full host path, and diagnostics are
path-free by type.

**NO-GO** for any runtime Enhanced claim or private package use. ADR-0019 stage
4 must first connect the accepted manifest index to actual GTI identity and
prove byte-identical per-texture fallback under every failure status.
