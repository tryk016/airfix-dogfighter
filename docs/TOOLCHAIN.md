# Toolchain plan

**State on 2026-07-21:** the Phase 1 Windows toolchain is installed and verified.
Exact packages, source artifacts, licenses, and hashes are recorded in
`docs/toolchain/LOCK.md`. `dumpbin` is not required because LLVM supplies the PE
reporting tools.

## Reverse engineering

| Capability | Proposed tool | Purpose |
|---|---|---|
| Static analysis/decompiler | Ghidra 12.1 | PE import, types, disassembly, decompilation, headless reports |
| Ghidra runtime | 64-bit JDK 21 | Required by the current official Ghidra guide |
| Ghidra scripting/debug bridge | Python 3.14 | Within the supported PyGhidra range for the locked release |
| PE command-line reports | LLVM tools | Reproducible sections/imports/exports/disassembly |
| Dynamic x86 analysis | debugger chosen in Phase 1 | Breakpoints, module load, memory/object traces in isolated VM |
| Filesystem/process observation | Windows tracing tools in VM | Runtime access and module behavior |

Official Ghidra requirements and security advisories are checked before
installation. Versions and download hashes are pinned in
`docs/toolchain/LOCK.md` rather than relying on whichever executable happens to
be on `PATH`.

## Reconstruction build

| Capability | Proposed tool |
|---|---|
| Language | C++20 |
| Build | CMake plus Ninja |
| Editor/LSP | clangd with a generated CMake compilation database |
| Windows compiler | MSVC or clang-cl, selected after warning/sanitizer spike |
| Tests | lightweight C++ test runner selected with the skeleton |
| iOS platform/input/controller | UIKit and Game Controller behind Objective-C++ adapters |
| Optional desktop common layer | SDL3 technical spike when a desktop shell needs it |
| iOS graphics | Metal with MetalKit/Objective-C++ bridge as needed |
| iOS build/signing | Pinned Xcode on explicit GitHub-hosted macOS runner |

The native iOS adapters are the first implementation path. SDL3 remains a later
desktop/common-layer option; neither Apple nor SDL types may leak into the
portable game core.

The shared `code-intelligence` CMake preset and editor setup are documented in
[toolchain/CODE-INTELLIGENCE.md](toolchain/CODE-INTELLIGENCE.md). Generated
compilation databases and machine-specific compiler paths remain local.

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
export, so no owner-operated Mac/Xcode installation is required by default. A
MacBook is introduced only at a hard blocker or documented >=20% acceleration
under `docs/process/MACBOOK-GATE.md`.

Use an explicit runner label such as `macos-26`, select an explicit installed
stable Xcode, and record runner/Xcode/SDK versions. Do not rely on the moving
`macos-latest` alias. The accepted deployment target is iOS 16.4; runtime device
tests are iPhone 17 Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3.

Signing uses a protected Actions environment, temporary keychain, certificate,
and provisioning profile covering both devices. See
`docs/ci/GITHUB-ACTIONS-IOS.md`.
