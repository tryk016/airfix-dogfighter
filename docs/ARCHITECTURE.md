# Target architecture

**Status:** proposed baseline  
**Last updated:** 2026-07-21

## Constraints

- The reference build is Windows PE32/x86 and uses DirectX 7-era APIs plus an
  optional Glide renderer.
- iOS requires a native ARM64 application and an Apple build/signing stage in
  Xcode on macOS.
- The original source code is unavailable. Decompiled output is evidence, not
  production-ready source.
- Original assets are in custom `UDSP` packages and must be understood before a
  complete vertical slice can load.
- Fidelity and modernization are separate acceptance dimensions.

## Architecture

```mermaid
flowchart TD
    A["Original read-only installation"] --> B["Inventory and hashes"]
    A --> C["Static and dynamic analysis"]
    A --> D["UDSP extractor and asset decoders"]
    C --> E["Behavior specifications"]
    D --> F["Converted development assets"]
    E --> G["Portable C++20 game core"]
    F --> H["Runtime asset layer"]
    G --> I["Platform services interface"]
    G --> J["Renderer command interface"]
    H --> G
    I --> K["Windows reference harness"]
    I --> L["iOS app shell"]
    J --> M["Windows development renderer"]
    J --> N["Metal renderer"]
    L --> N
    G --> O["Deterministic and parity tests"]
```

## Component boundaries

### `engine/core`

Portable C++20 only. Owns math conventions, object identity, update order,
timers, serialization primitives, event dispatch, and deterministic random
number generation. No Windows, Apple, graphics, or filesystem headers.

### `game`

Reconstructed actors and rules: aircraft, ground and water units, projectiles,
pickups, effects, interaction, AI, missions, dogfight, and single-player flow.
Subsystems initially mirror the original `.type` and `.mode` module boundaries;
they may be refactored only after parity tests protect behavior.

### `assets`

Two layers:

- Offline tools parse `UDSP` and convert original formats into documented,
  versioned intermediate data.
- Runtime loaders consume the intermediate representation and validate schema,
  bounds, and checksums.

The first extractor must also support a listing-only mode so research can
progress without copying full assets.

### `render`

The game emits API-neutral draw commands and lighting data. A Windows backend
supports rapid desktop development; the shipping iOS backend uses Metal.
The faithful pipeline is implemented first. Modern lighting, shadows,
post-processing, higher-resolution textures, and upscaling are optional feature
layers with independent toggles and performance budgets.

### `platform`

Narrow interfaces for input, game controllers, touch, audio, timing, files,
localization, lifecycle, and video. SDL3 is the proposed portability layer for
window/event/controller/audio plumbing. A small Objective-C++ bridge handles
iOS-specific lifecycle, storage, safe-area, audio-session, and Metal integration.

Input is a distinct subsystem: platform adapters produce normalized physical
events, a context/binding router resolves semantic actions, and the simulation
receives immutable per-tick `InputFrame` values. Touch, controllers, desktop test
input, and optional motion never enter the game core as platform key/button
codes. See `docs/systems/INPUT.md` and ADR-0002.

### `apps`

- `reference-win`: desktop executable used during reconstruction and comparison.
- `airfix-ios`: iOS application target.
- Optional diagnostic viewers for archives, models, levels, and effects.

## Dependency direction

Dependencies point inward: app shells and backends depend on interfaces; game
logic depends on the portable core; the portable core never depends on a
platform. Asset conversion tools are build-time programs and are not linked into
the shipping game unless a small, audited runtime reader is necessary.

## Proposed source layout

```text
apps/
  reference-win/
  airfix-ios/
src/
  core/
  game/
  assets/
  render/
  platform/
tools/
  inventory/
  udsp/
  viewers/
tests/
  unit/
  integration/
  parity/
docs/
analysis/                 # local heavy data; ignored by Git
private-fixtures/         # original-derived fixtures; ignored by Git
```

## Decisions deliberately deferred

- Exact desktop graphics API: decide after a small draw-command spike.
- Runtime intermediate model/level formats: decide after `UDSP` contents are
  inventoried.
- Multiplayer scope: preserve protocol findings, but schedule implementation
  only after single-player parity.
- Minimum supported iOS/device generation: set after the first Metal vertical
  slice and memory measurements.
- Whether `GCVirtualController` is useful for the first device spike before the
  final custom touch overlay is ready.
