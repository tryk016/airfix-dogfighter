# Toolchain plan

**State on 2026-07-27:** the Windows static-analysis and portable C++ toolchain
is installed and verified. The isolated `Airfix-Dev` WSL2 build environment is
also operational. Exact packages, source artifacts, licenses, hashes, and
installation states are recorded in `docs/toolchain/LOCK.md`. `dumpbin` is not
required because LLVM supplies the PE reporting tools.

## Reverse engineering

| Capability | Proposed tool | Purpose |
|---|---|---|
| Primary static analysis/decompiler | Ghidra 12.1 | Canonical PE import, types, disassembly, decompilation, and repeatable headless reports |
| Scriptable independent static analysis | Rizin 0.9 | PE32/x86 metadata, function discovery, references, signatures, search, diff, and `rzpipe` reports |
| Rizin graphical client | Cutter 2.5 | Manual graph, disassembly, and bundled Ghidra-decompiler views; not independent evidence from Rizin |
| Manual independent decompiler | Binary Ninja Free 5.3 | Optional difficult-function cross-check in an operator-scheduled GUI window; local Desktop use only, with network features disabled and outbound traffic blocked |
| Ghidra runtime | 64-bit JDK 21 | Required by the current official Ghidra guide |
| Ghidra scripting/debug bridge | Python 3.14 | Within the supported PyGhidra range for the locked release |
| PE command-line reports | LLVM tools | Reproducible sections/imports/exports/disassembly |
| Dynamic x86 analysis | x32dbg 2026.05.27 | Later read-only-first breakpoints, module load, and memory/object traces in a controlled Windows environment |
| Filesystem/process observation | Windows tracing tools in VM | Runtime access and module behavior |

Ghidra remains the canonical and repeatable static-analysis source. Rizin is the
second scripted implementation, Cutter is its GUI, and Binary Ninja Free is a
manual independent check rather than an automation dependency. Original files
must never be uploaded to Binary Ninja Cloud or any other internet service.
See [toolchain/RE-WORKBENCH.md](toolchain/RE-WORKBENCH.md) for the complete
offline workflow.

## Reconstruction build

| Capability | Proposed tool |
|---|---|
| Language | C++20 |
| Build | CMake plus Ninja |
| Editor/LSP | clangd with a generated CMake compilation database |
| Windows x64 product compiler | MSVC or clang-cl, selected after warning/sanitizer spike |
| Windows window/input | Pinned SDL3 for window, events, keyboard, mouse, and controllers; not game rendering |
| Windows graphics | Direct3D 11 plus DXGI with HLSL |
| Windows audio | Separate native adapter selected by a focused spike |
| Tests | lightweight C++ test runner selected with the skeleton |
| iOS platform/input/controller | UIKit and Game Controller behind Objective-C++ adapters |
| iOS graphics | Metal with MetalKit/Objective-C++ bridge as needed |
| iOS build/signing | Pinned Xcode on explicit GitHub-hosted macOS runner |

Windows x64 and iOS are parallel products over the same portable core. Windows
is the primary rapid-debug and reference-comparison environment; iOS retains
native Apple adapters. SDL3 owns Windows window/input plumbing but does not
render the game; D3D11/DXGI/HLSL is the Windows renderer. Windows, Apple, SDL,
COM, D3D, and Metal types may not leak into the portable game core.

The shared `code-intelligence` CMake preset and editor setup are documented in
[toolchain/CODE-INTELLIGENCE.md](toolchain/CODE-INTELLIGENCE.md). Generated
compilation databases and machine-specific compiler paths remain local.

## Installation policy

- Download only from official project/vendor sources.
- Verify published hashes or release signatures where available.
- Record exact version, source URL, local hash, install method, and license.
- Keep portable tools, vendor databases, working copies, reports, and debugger
  workspaces in ignored local directories.
- Do not install analysis extensions before their source and permissions are
  reviewed.
- Treat old game binaries and extracted content as untrusted input.
- Perform dynamic execution in an isolated, snapshot-capable environment.
- Do not run the old game, analysis tools, or third-party installers elevated.
- Do not use a cloud decompiler, PDB downloader, or remote symbol service for
  private inputs.

## Apple and CI dependency

The Windows workstation hosts static analysis, local conversion of private game
data, the portable core, and the native Windows x64 product/debug build. GitHub
Actions supplies an ephemeral macOS runner with Xcode for simulator compilation
and signed IPA
export, so no owner-operated Mac/Xcode installation is required for portable
development and compile validation. Interactive device debugging and profiling
later use local Xcode and Apple developer tools.

Use an explicit runner label such as `macos-26`, select an explicit installed
stable Xcode, and record runner/Xcode/SDK versions. Do not rely on the moving
`macos-latest` alias. The accepted deployment target is iOS 16.4; runtime device
tests are iPhone 17 Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3.

Signing uses a protected Actions environment, temporary keychain, certificate,
and provisioning profile covering both devices. See
`docs/ci/GITHUB-ACTIONS-IOS.md`.

## Local Linux validation

The dedicated `Airfix-Dev` WSL2 distribution provides a fast, local
CMake/Ninja build and CTest pass without changing the default distribution,
global WSL settings, or environments used by other projects. It contains no
original game data and is not a sandbox for running the legacy game. See
[toolchain/WSL2.md](toolchain/WSL2.md).
