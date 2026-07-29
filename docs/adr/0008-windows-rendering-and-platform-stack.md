# ADR-0008: Use D3D11/DXGI and SDL3 for the Windows product

**Status:** Accepted

**Date:** 2026-07-29

**Deciders:** project owner and implementation lead

## Context

ADR-0007 promotes Windows x64 to a playable product and fixes the boundary
between shared C++20 systems and product-specific adapters. The Windows window,
rendering, and physical-input technologies now need a concrete implementation
choice.

The original used DirectX 7-era rendering. Recreating that API is neither
necessary for behavioral parity nor a useful base for modern resolution,
lighting, shadows, post-processing, and diagnostics. The reconstruction needs
a predictable renderer with broad Windows support and a substantially smaller
implementation/debugging burden than Direct3D 12 or Vulkan.

The project also needs maintained window, event, keyboard, mouse, and controller
handling without making that library the owner of rendering. iOS already has a
native Metal backend and its unsigned simulator build is part of every public
CI change.

## Decision

Use this Windows x64 stack:

| Responsibility | Selected technology |
|---|---|
| Window and operating-system events | SDL3 |
| Keyboard, mouse, and game controllers | SDL3 |
| Renderer | Direct3D 11 |
| Swap chain, presentation, display modes, and adapter enumeration | DXGI |
| Windows shaders | HLSL |
| iOS renderer | Native Metal; unchanged |

SDL3 does not issue game draw calls and is not a renderer abstraction. The
Windows renderer owns D3D11/DXGI resources, pipeline state, command submission,
presentation, resize, and device-loss handling. The Windows audio backend
remains a separate platform decision.

The shared renderer is portable C++20 and owns:

- canonical geometry, index, texture, material, and resource identities;
- camera, world/view/projection, lighting, fog, and presentation data;
- backend-neutral depth, blend, cull, sampler, and pass descriptions;
- validated draw, upload, lifetime, and diagnostic commands; and
- faithful/reference versus enhanced feature policy.

It must not expose SDL windows, Win32 handles, COM interfaces, D3D11/DXGI
objects, HLSL bytecode, Metal objects, or Apple types. Thin backends translate
the same validated commands:

```text
Portable C++20 renderer front end
    +-- Metal backend -- iOS
    `-- D3D11/DXGI backend -- Windows x64
```

HLSL is the Windows shader implementation. Metal uses its native shader path.
The two backends share named shader inputs, material semantics, render-state
contracts, test scenes, and expected outputs; they are not required to share
one shader source language.

Do not reproduce the DirectX 7 API or fixed-function interfaces. Recover the
observable render states and ordering as evidence, represent them in the modern
shared command model, and implement their faithful result in D3D11 and Metal.

SDL GPU is not the primary renderer abstraction. Its modern cross-platform API
targets D3D12, Vulkan, and Metal and requires backend-appropriate shader
formats. On this decision date, its official system requirements also mark the
iOS Simulator as unsupported, which conflicts with the current public Actions
matrix. Keep it as a future revisit option if those constraints and the
project's diagnostic needs materially change.

## Rationale

- D3D11 is sufficient for the faithful renderer, higher resolutions, dynamic
  lights, shadow maps, post-processing, MSAA, and planned effects.
- Its device/context model and debugging workflow are simpler for this project
  than explicitly managed D3D12 or Vulkan command and synchronization systems.
- DXGI provides the native Windows presentation and adapter boundary directly.
- HLSL is the native shader language/toolchain for the selected renderer.
- SDL3 removes routine window, event, keyboard, mouse, and controller plumbing
  while leaving rendering ownership explicit.
- Native Metal remains the most direct iOS path and preserves simulator
  compilation in GitHub Actions.

## Dependency and licensing policy

- Pin the selected SDL3 source release, source URL, archive hash, and zlib
  license in the toolchain lock before linking it.
- Fetch/build SDL3 reproducibly through the public dependency workflow; do not
  commit vendor build outputs or machine-local paths.
- Use Windows SDK D3D11, DXGI, HLSL/compiler, and debug-layer interfaces through
  the selected MSVC/clang-cl toolchain.
- Public CI and smoke scenes remain data-less and use synthetic assets.

## Consequences

- Windows gains a concrete product shell path: SDL3 events feed portable input,
  and D3D11/DXGI consumes portable render commands.
- Renderer parity is tested at both the shared command boundary and the final
  backend output. A backend cannot silently alter game state or simulation.
- Reference mode uses explicit modern pipeline states that reproduce observed
  output; modern features remain separately switchable.
- Shader-interface changes require validation in both HLSL and the native Metal
  path.
- D3D11 debug-layer diagnostics, graphics capture, device-removal tests, resize,
  fullscreen/windowed presentation, and frame pacing become Windows acceptance
  work.
- The exact minimum Windows version, D3D feature-level floor, Windows audio API,
  and private packaging procedure remain separate implementation decisions.

## Official references

- [Microsoft: Getting started with Direct3D](https://learn.microsoft.com/en-us/windows/win32/getting-started-with-direct3d)
- [Microsoft: Direct3D 11 Graphics](https://learn.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11)
- [Microsoft: High-level shader language](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl)
- [SDL official repository and zlib license](https://github.com/libsdl-org/SDL)
- [SDL3 GPU documentation and system requirements](https://wiki.libsdl.org/SDL3/CategoryGPU)
