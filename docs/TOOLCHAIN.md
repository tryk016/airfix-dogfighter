# Toolchain plan

**State on 2026-07-21:** Git and 7-Zip are available on `PATH`; Ghidra, Java,
Python, CMake, Ninja, `dumpbin`, and LLVM PE tools were not found.

## Reverse engineering

| Capability | Proposed tool | Purpose |
|---|---|---|
| Static analysis/decompiler | Ghidra 12.1 | PE import, types, disassembly, decompilation, headless reports |
| Ghidra runtime | 64-bit JDK 21 | Required by the current official Ghidra guide |
| Ghidra scripting/debug bridge | Python 3.12 initially | Within current supported PyGhidra/debugger range |
| PE command-line reports | LLVM tools | Reproducible sections/imports/exports/disassembly |
| Dynamic x86 analysis | debugger chosen in Phase 1 | Breakpoints, module load, memory/object traces in isolated VM |
| Filesystem/process observation | Windows tracing tools in VM | Runtime access and module behavior |

Official Ghidra requirements and security advisories are checked before
installation. Versions and download hashes will be pinned in a lock file rather
than relying on whichever executable happens to be on `PATH`.

## Reconstruction build

| Capability | Proposed tool |
|---|---|
| Language | C++20 |
| Build | CMake plus Ninja |
| Windows compiler | MSVC or clang-cl, selected after warning/sanitizer spike |
| Tests | lightweight C++ test runner selected with the skeleton |
| Platform/input/controller/audio | SDL3 technical spike |
| iOS graphics | Metal with MetalKit/Objective-C++ bridge as needed |
| iOS build/signing | Pinned Xcode on explicit GitHub-hosted macOS runner |

SDL3's official iOS guide recommends its xcframework distribution. This is a
starting choice, not permission to let SDL types leak into the portable game
core.

## Installation policy

- Download only from official project/vendor sources.
- Verify published hashes or release signatures where available.
- Record exact version, source URL, local hash, install method, and license.
- Do not install analysis extensions before their source and permissions are
  reviewed.
- Treat old game binaries and extracted content as untrusted input.
- Perform dynamic execution in an isolated, snapshot-capable environment.

## Apple and CI dependency

The Windows workstation hosts static analysis, local conversion of private game
data, the portable core, and desktop reference build. GitHub Actions supplies an
ephemeral macOS runner with Xcode for simulator compilation and signed IPA
export, so no owner-operated Mac/Xcode installation is required.

Use an explicit runner label such as `macos-26`, select an explicit installed
stable Xcode, and record runner/Xcode/SDK versions. Do not rely on the moving
`macos-latest` alias. The accepted deployment target is iOS 16.4; runtime device
tests are iPhone 17 Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3.

Signing uses a protected Actions environment, temporary keychain, certificate,
and provisioning profile covering both devices. See
`docs/ci/GITHUB-ACTIONS-IOS.md`.
