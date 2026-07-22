# Airfix Dogfighter reconstruction and private iOS port

This repository contains an evidence-driven, native reimplementation of
**Airfix Dogfighter v1.01 (2000)** and the foundations of a private iOS port.
The goal is to preserve the original single-player experience on modern Apple
hardware, then add optional improvements such as higher rendering resolution
and modern lighting without changing faithful-mode gameplay.

The project is under active development and is **not yet a playable port**.
It currently provides the portable archive/asset pipeline, deterministic input
core, private content-package infrastructure, diagnostic rendering, and a
data-less UIKit/Metal application shell.

## Project scope

| Area | Version 1 target |
|---|---|
| Distribution | Private installation on the owner's registered devices |
| Platform | Native ARM64 iOS application; no x86 emulation or JIT |
| Minimum OS | iOS 16.4 deployment target |
| Gameplay | Single-player campaign and required menus |
| Input | Complete touch controls and Bluetooth/USB extended controllers |
| Rendering | Faithful Metal path first; optional enhanced mode later |
| Content | Imported locally from the owner's original v1.01 files |
| Out of scope | App Store release, public asset distribution, multiplayer, House Editor, Paint Room |
| Music | Original CD audio is unavailable and remains optional/absent |

The initial device-validation matrix is:

| Device | Runtime OS | Role |
|---|---:|---|
| iPhone 17 Pro Max | iOS 26.6 | Large-screen, high-performance target |
| iPhone SE (3rd generation) | iOS 26.3 | Compact-screen and performance floor |

These are target test devices, not a claim that the unfinished game has already
passed device acceptance. GitHub Actions currently verifies unsigned
`iphoneos` and `iphonesimulator` builds with deployment target 16.4.

## Legal and content boundary

This is a compatibility and preservation project. It assumes that each user:

- owns a lawfully acquired copy of Airfix Dogfighter v1.01;
- is permitted by the laws applicable to them to make and use a private
  compatibility copy;
- keeps all original and converted game content private; and
- does not redistribute original executables, archives, media, converted
  `.afpack` files, or app packages containing that content.

Original game binaries and assets are deliberately excluded from Git. The
repository contains independently written source code, format documentation,
synthetic test fixtures, and aggregate observations only. Conversion reads an
owner-supplied installation without modifying it and produces a private,
validated runtime package outside the repository.

This is not legal advice, does not grant rights to the original game, and is
not affiliated with or endorsed by the game's developers, publishers, or
trademark owners. Do not obtain missing music or assets from unofficial
sources.

## Current status

| Component | State |
|---|---|
| Reverse engineering | Ongoing, with functions and claims tied to reproducible evidence |
| `UDSP` archives | Bounded metadata parser, lookup, decompression, verification, and CLI implemented |
| Legacy assets | GTI textures, CCF scenes/meshes/materials/blueprints/placed nodes, and major FourCC definitions parsed |
| Dependency resolution | Object-to-scene, blueprint subtree, material, and texture paths implemented |
| Private packaging | AFPACK writer, validation, transactional install/recovery, and native iOS import/rollback UI implemented |
| Rendering | Deterministic CPU diagnostics and a synthetic iOS Metal smoke renderer implemented |
| Input core | Deterministic semantic router for touch/controller/test sources implemented |
| Native controls | UIKit touch overlay and Apple Game Controller adapters pending |
| Game simulation | Reconstruction in progress; no complete playable loop yet |
| Continuous integration | Portable C++ tests plus unsigned device/simulator iOS builds |

Detailed, frequently updated progress lives in
[docs/progress/STATUS.md](docs/progress/STATUS.md) and
[docs/progress/LOG.md](docs/progress/LOG.md).

## Architecture

The port is a native reimplementation, not a recompilation of x86 assembly for
iOS. Decompilation and static analysis are used to understand observable file
formats and behavior; maintainable C++20 and Objective-C++ code is then written
for ARM64 and Metal.

```mermaid
flowchart LR
    O["Owner's original v1.01 files"] --> C["Offline inspection and conversion"]
    C --> P["Private AFPACK"]
    P --> V["Validation and atomic installation"]
    V --> A["Portable archive and asset layer"]
    A --> S["Deterministic simulation core"]
    S --> R["Metal renderer"]
    S --> U["UIKit menus and HUD"]
    T["Touch controls"] --> I["Semantic input router"]
    G["Apple Game Controller"] --> I
    I --> S
```

The main boundaries are:

- **Reverse-engineering evidence:** function catalogues, format notes, and
  reproducible experiments; generated decompiler databases stay local.
- **Portable core:** bounded parsers, deterministic input, reconstructed game
  rules, content identity, and platform-neutral render payloads.
- **Private content pipeline:** offline conversion, cryptographic validation,
  content-addressed installation, activation, and rollback.
- **iOS platform layer:** UIKit lifecycle, Metal presentation, Game Controller,
  touch UI, audio, haptics, and sandbox storage.
- **CI/release layer:** cross-platform tests and unsigned iOS builds today;
  protected private signing later.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design and
[docs/adr/0001-port-strategy.md](docs/adr/0001-port-strategy.md) for the chosen
reconstruction strategy.

## Control system

The game must remain fully usable with either touch or a supported extended
controller.

The planned touch layout includes a landscape virtual flight stick, throttle,
primary and secondary fire, weapon selection, camera/rear view, mission status,
and pause. It is designed around multi-touch, safe areas, left-handed and
large-control presets, adjustable opacity/size/position, and reliable release
of held actions when iOS interrupts or backgrounds the app.

Bluetooth pairing remains managed by iOS. The native adapter will use Apple's
Game Controller framework for supported Xbox, PlayStation, and MFi-style
extended controllers, including hot-plug, disconnect recovery, neutral-state
gating, calibration, remapping, controller-only menus, and optional rumble.
Touch stays available when a controller is connected.

Both platform adapters feed the implemented deterministic semantic input
router. The simulation sees immutable per-tick actions rather than UIKit or
controller objects, which keeps gameplay testable and independent of frame
rate. The complete contract is documented in
[docs/systems/INPUT.md](docs/systems/INPUT.md).

## Build and test the portable code

Prerequisites:

- CMake 3.25 or newer;
- a C++20 compiler;
- Ninja or another CMake-supported build tool.

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DAIRFIX_BUILD_TESTS=ON \
  -DAIRFIX_BUILD_TOOLS=ON
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Tests use synthetic fixtures and do not require proprietary game files. The
read-only archive inspector can be run against a privately supplied archive:

```text
udsp-list [--summary|--verify|--inventory|--resolve-objects] <archive.up>
```

Diagnostic preview outputs and all private/generated content belong in ignored
local directories and must never be committed.

## iOS builds

GitHub Actions is the current Apple build host, so portable development and
compile validation do not require a local Mac. The workflow builds a data-less
UIKit/Metal shell for both ARM64 devices and the simulator and verifies that no
private content enters the app bundle.

Device signing and installation are a later protected workflow using the
owner's Apple Developer credentials and provisioning profile. Signed IPAs,
certificates, profiles, device identifiers, original assets, and `.afpack`
content must not be published as public artifacts. A MacBook is introduced only
if an on-device task cannot be completed through the accepted workflow or it
provides a documented material acceleration.

See [docs/ci/GITHUB-ACTIONS-IOS.md](docs/ci/GITHUB-ACTIONS-IOS.md) and
[docs/process/MACBOOK-GATE.md](docs/process/MACBOOK-GATE.md).

## Repository guide

```text
apps/airfix-ios/       UIKit, Metal, and Objective-C++ platform shell
src/airfix/archive/    UDSP archive parsing and bounded entry reads
src/airfix/assets/     Legacy formats, dependency graphs, render-ready data
src/airfix/input/      Deterministic semantic input router
src/airfix/io/         Cross-platform durable file primitives
src/airfix/package/    Private AFPACK format, validation, and installation
src/airfix/render/     Portable geometry conversion and diagnostics
src/airfix/runtime/    Platform-neutral application/session state
tests/                 Synthetic unit and malformed-input tests
tools/                 Read-only inspectors, converters, and CI checks
docs/                  Plans, ADRs, format notes, evidence, and progress logs
```

Recommended starting points:

- [Project plan](docs/PROJECT-PLAN.md)
- [Version 1 scope](docs/design/V1-SCOPE.md)
- [iOS requirements](docs/design/IOS-PORT-REQUIREMENTS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Reverse-engineering workflow](docs/RE-WORKFLOW.md)
- [Private content format](docs/formats/AFPACK.md)
- [Input and controller design](docs/systems/INPUT.md)
- [Current status](docs/progress/STATUS.md)

## Contributing safely

- Never commit or attach original game files, converted assets, memory dumps,
  decompiler projects, signed IPAs, certificates, profiles, or device IDs.
- Use synthetic fixtures for tests and report corpus findings only as bounded
  aggregate facts.
- Keep parsers defensive: validate sizes and limits before allocation or reads.
- Separate proven behavior from hypotheses and link implementation claims to
  evidence or a reproducible experiment.
- Preserve faithful behavior before proposing enhanced graphics or controls.
- Run the portable tests and public-boundary check before publishing changes.

No source-code license has been selected yet. Publication of this repository
does not grant rights to the original Airfix Dogfighter software or content;
third-party tools and references retain their own licenses.
