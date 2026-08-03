# GitHub Actions execution and compiler-cache policy

**Status:** accepted and implemented for public, data-less CI

## Goals

- Return documentation-only pull-request results without allocating Windows,
  macOS, or iOS build runners.
- Retain the complete portable, Windows product, macOS, iPhoneOS, and
  iPhoneSimulator matrix whenever a change can affect a build or test.
- Reuse public C/C++ object files where the active CMake generator supports a
  compiler launcher.
- Run a regular complete matrix with no compiler launcher or object cache.
- Keep original game data, owner configuration, signing material, local paths,
  and private texture content outside every cache and workflow input.

## Measured cold baseline

Profiling uses the immutable job and step timestamps already published by
GitHub Actions. Pull-request run `30814762857` at commit
`6cbfae9228e0d437e1b5bed3957e448ce14bedc7` established the baseline before
this policy:

| Job | Total | Dominant build step |
|---|---:|---:|
| clangd preset / Ubuntu | 2m 03s | 1m 44s |
| Ubuntu portable | 3m 10s | 2m 56s |
| Windows portable | 8m 10s | 7m 16s |
| Windows D3D11/XAudio2 product | 9m 13s | 8m 06s |
| macOS portable | 20m 28s | 19m 59s |
| iPhoneOS | 2m 10s | reported by the unsigned iOS workflow |
| iPhoneSimulator | 2m 26s | reported by the unsigned iOS workflow |

The immediately preceding main run also measured a 24m 07s macOS compile.
macOS object reuse and avoiding native runners for documentation-only reviews
therefore have the highest expected return. These timestamps are the baseline;
later changes must compare warm and cold runs before removing more coverage.

## Conservative change classification

Both public workflows always start on pull requests. They do not use a
workflow-level `paths-ignore` rule: GitHub documents that a workflow skipped by
path filtering can leave a required check pending and block the pull request.
Instead, a small Ubuntu policy job checks the three-dot base-to-head diff and
publishes one `full_build` decision.

The fast path accepts only:

- Markdown, CSV, SHA-256 ledger, text, and Mermaid files below `docs/`;
- the root README, licence, contribution, security, and code-of-conduct files;
- Markdown templates below `.github/`.

An empty range, malformed path, failed Git comparison, JSON/schema change,
workflow change, CMake change, source, application, shader, test, tool, or any
other unclassified path fails closed to the complete matrix. Non-PR events also
always select the complete matrix. Synthetic tests cover the allowlist and the
fail-closed cases.

The documentation path still runs:

- synthetic and repository-wide public-boundary checks;
- internal Markdown-link validation;
- unique experiment IDs and the 14-column function-catalog invariants;
- existing deterministic Rizin-export and runtime-capture validators;
- revision-scoped `git diff --check`.

Only after those gates pass may the native jobs be reported as intentionally
skipped.

## Object cache

The portable Ubuntu and macOS jobs and the Ninja clangd job use Mozilla's
`sccache` GitHub Actions backend. The action is pinned to commit
`fc920bf0ec8de6ee65d409111f7ec508035751ba` (release `v0.0.11`) and installs
an explicit `sccache v0.17.0`. CMake receives the resolved executable through
`CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER`; the source root
is normalized through `SCCACHE_BASEDIRS`. Server/cache I/O failure degrades to
the real compiler instead of failing the build.

The Visual Studio generator used by the Windows product and the Xcode
generator used by the iOS product do not consume CMake's compiler-launcher
property. They remain uncached. Changing those generators merely to increase
cache hits is not authorized; their native product coverage is more important
than cache uniformity. Windows/Ninja or Xcode-specific caching can be evaluated
later with separate parity evidence.

The action's post step publishes hit/miss and compilation statistics in every
cached job. No build directory is committed. GitHub cache contents contain
only products of the public, data-less checkout; private content roots and
owner inputs are never configured in public CI.

## Cold and manual validation

At 04:17 UTC every Monday, `Portable C++ CI` runs the complete matrix with no
`sccache` action and no CMake compiler launcher. A manual dispatch defaults to
the same cold behavior; an operator must explicitly clear `cold_build` to
request cache reuse. Pull requests and ordinary `main` pushes may use the
cache, while all iOS and Windows Visual Studio product builds remain cold by
construction.

The scheduled run is not a replacement for pull-request coverage. It detects
undeclared dependencies and stale-cache assumptions that a warm compile could
otherwise hide.

## Upstream references

- [GitHub path filters and pending required checks](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow#using-filters-to-target-specific-paths-for-pull-request-or-push-events)
- [GitHub Actions contexts and job conditions](https://docs.github.com/en/actions/reference/workflows-and-actions/contexts)
- [Mozilla sccache](https://github.com/mozilla/sccache)
- [Mozilla sccache action](https://github.com/Mozilla-Actions/sccache-action)
- [CMake compiler-launcher property](https://cmake.org/cmake/help/latest/prop_tgt/LANG_COMPILER_LAUNCHER.html)
