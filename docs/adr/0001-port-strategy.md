# ADR-0001: Reconstruct portable native source, then build native products

**Status:** Accepted as working baseline; local Mac consequence amended by
ADR-0004 and product targets extended by ADR-0007
**Date:** 2026-07-21  
**Deciders:** project owner and implementation lead

## Context

Airfix Dogfighter v1.01 is available as 32-bit x86 Windows binaries and custom
resource packages, but not as source code. The targets are a playable native
Windows x64 reconstruction and a private iOS/ARM64 port, with room for higher
resolution and modern lighting. Direct translation of machine code does not
recover maintainable source, while shipping an x86 emulator or Windows
compatibility layer would preserve obsolete APIs and complicate both products.

## Decision

Build a behavior-compatible reimplementation in portable C++20. Use Ghidra and
controlled runtime experiments to recover contracts and algorithms. Use offline
tools to decode assets. Keep platform and renderer interfaces narrow. Deliver a
native Windows x64 application for play, rapid debugging, and reference
comparison, and a native Metal-backed iOS application. ADR-0007 defines the
parallel product boundary.

Modern effects are added only after the relevant faithful path passes parity
tests. They remain optional so regressions can be isolated.

## Options considered

### A. Portable source reconstruction — selected

| Dimension | Assessment |
|---|---|
| Initial complexity | High |
| Long-term maintainability | High |
| iOS suitability | High |
| Fidelity potential | High with systematic tests |
| Modernization potential | High |

**Pros:** native ARM64, testable source, documented formats, controlled modern
renderer, no dependency on obsolete Windows runtime behavior.

**Cons:** substantial analysis effort; naming and type recovery are uncertain;
full feature parity must be earned subsystem by subsystem.

### B. Line-by-line decompiler cleanup

| Dimension | Assessment |
|---|---|
| Initial complexity | Medium |
| Long-term maintainability | Low |
| iOS suitability | Medium |
| Fidelity potential | Medium |
| Modernization potential | Low |

**Pros:** may produce early fragments quickly.

**Cons:** decompiler artifacts leak into architecture; reconstructed calling
conventions and undefined behavior become fragile; platform assumptions remain
entangled. Decompiled functions are still useful as evidence, but not as the
source-tree organizing principle.

### C. x86 emulation / Windows compatibility layer

| Dimension | Assessment |
|---|---|
| Initial complexity | Medium to high |
| Long-term maintainability | Low |
| iOS suitability | Low |
| Fidelity potential | Potentially high |
| Modernization potential | Very low |

**Pros:** could preserve original execution semantics.

**Cons:** x86 and DirectX 7 emulation, JIT/distribution constraints, weak touch
integration, poor renderer modernization, and dependence on the original binary.

### D. Rewrite directly in a commercial game engine

| Dimension | Assessment |
|---|---|
| Initial complexity | Medium |
| Long-term maintainability | Medium |
| iOS suitability | High |
| Fidelity potential | Medium |
| Modernization potential | High |

**Pros:** mature editors and platform export.

**Cons:** original update order, physics, content formats, and tools would need a
large semantic rewrite; engine lifecycle and licensing become new constraints.

## Trade-off analysis

The selected approach is slower before the first playable build but is the only
option that simultaneously supports reliable behavioral comparison, native
Windows and iOS execution, maintainable source, and deliberate renderer
upgrades. The playable Windows x64 product is the primary fast feedback and
comparison environment; iPhone hardware remains essential for iOS acceptance.

## Consequences

- Xcode on macOS is required, but ADR-0004 supplies it through a GitHub-hosted
  runner; no owner-operated Mac is required. Physical devices remain required.
- Static analysis databases and original-derived traces stay local; durable
  conclusions are recorded as text and tests.
- Each recovered behavior needs a confidence level and evidence link.
- Asset formats become first-class documented interfaces.
- New visual effects cannot silently alter simulation or faithful rendering.
- Public distribution remains gated on rights to the code, name, media, and
  packaged assets.

## Action items

1. [ ] Install and pin the reverse-engineering and build toolchain.
2. [ ] Create a complete hash/PE/import/export inventory.
3. [ ] Determine the `UDSP` directory and compression layout.
4. [ ] Establish a safe, recordable Windows reference runtime.
5. [ ] Build the first archive-listing tool.
6. [ ] Produce a playable Windows x64 vertical slice and keep the shared core
   usable by the iOS shell.
