# Airfix Dogfighter — reconstruction and iOS port

This repository is the engineering notebook and future source tree for a
behavior-compatible reconstruction of **Airfix Dogfighter v1.01** and a native,
privately sideloaded iOS port.

The original installation at `E:\roms\Airfix Dogfighter` is treated as a
read-only reference. Original executables and assets are not copied into this
repository. Version 1.0 is for private signed installation only; App Store and
other public distribution are explicitly out of scope.

## Current status

- Initial, non-executing inventory completed on 2026-07-21.
- 33 files / 356,442,590 bytes identified.
- 16 native modules identified as 32-bit x86 PE files, including modules whose
  extensions are `.mode` and `.type`.
- The custom `UDSP` metadata, name hashing, directory ranges, and compression
  stream have been recovered and documented.
- A portable C++20 `UDSP` library, read-only listing/verifier CLI, and synthetic
  tests are implemented. All five supplied archives pass structural and payload
  validation without being copied into the repository.
- Bounded GTI/CCF/FourCC parsers recover complete texture mip chains, mesh and
  material metadata, blueprints, and exact object-to-texture archive entries.
  A seam-safe draw payload and deterministic private CPU preview validate the
  first textured model while corpus diagnostics emit aggregate facts only.
- A deterministic portable input router now defines stable semantic actions,
  touch/controller/test bindings, multi-source arbitration, context/lifecycle
  releases, and controller reconnect neutral gating. Native iOS adapters and the
  visual touch overlay are the next platform layer.
- Portable CI builds/tests on Ubuntu 24.04, Windows Server 2025, and native
  ARM64 macOS 26 runners.
- iOS builds/signing will run on GitHub Actions; runtime tests target iPhone 17
  Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3 with deployment target iOS 16.4.

## Working principles

1. Preserve and hash originals; never edit them.
2. Every reconstructed behavior must point to static evidence, a dynamic
   observation, or a reproducible experiment.
3. Establish faithful gameplay before adding visual improvements.
4. Keep simulation, platform services, rendering, and asset conversion
   separate.
5. Do not commit original game assets, memory dumps, or decompiler databases.
6. Use native ARM64 code on iOS; do not depend on x86 emulation or JIT.

## Start here

- [Detailed project plan](docs/PROJECT-PLAN.md)
- [Target architecture](docs/ARCHITECTURE.md)
- [Reverse-engineering workflow](docs/RE-WORKFLOW.md)
- [Complete iOS port requirements](docs/design/IOS-PORT-REQUIREMENTS.md)
- [Accepted version 1.0 scope](docs/design/V1-SCOPE.md)
- [GitHub Actions iOS build/signing design](docs/ci/GITHUB-ACTIONS-IOS.md)
- [MacBook blocker/20% acceleration gate](docs/process/MACBOOK-GATE.md)
- [Input, touch, controller, and haptics specification](docs/systems/INPUT.md)
- [Initial source inventory](docs/evidence/SOURCE-INVENTORY.md)
- [Current status](docs/progress/STATUS.md)
- [ADR-0001: reconstruction strategy](docs/adr/0001-port-strategy.md)
- [ADR-0002: input architecture](docs/adr/0002-input-architecture.md)
- [ADR-0003: private single-player scope](docs/adr/0003-v1-scope.md)
- [ADR-0004: GitHub Actions and private data package](docs/adr/0004-github-actions-ios-builds.md)

## Portable build

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAIRFIX_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

The local read-only inspector accepts
`udsp-list [--summary|--verify|--inventory|--resolve-objects] <archive.up>`.
The private diagnostic tool accepts
`model-preview <archive.up> <logical\\path.object> <output.ppm> [--flip-v]`;
its outputs belong only under ignored `artifacts/` paths.
Never add original archives or generated asset packages to this repository.
