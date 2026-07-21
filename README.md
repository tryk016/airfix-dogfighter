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
- Resource archives use a custom `UDSP` container and are not recognized by
  7-Zip.
- The source tree and toolchain have not yet been implemented.

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
- [Input, touch, controller, and haptics specification](docs/systems/INPUT.md)
- [Initial source inventory](docs/evidence/SOURCE-INVENTORY.md)
- [Current status](docs/progress/STATUS.md)
- [ADR-0001: reconstruction strategy](docs/adr/0001-port-strategy.md)
- [ADR-0002: input architecture](docs/adr/0002-input-architecture.md)
- [ADR-0003: private single-player scope](docs/adr/0003-v1-scope.md)
