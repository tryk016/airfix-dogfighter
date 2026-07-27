# Camera and projection evidence

**Status:** partial static evidence; original camera contract not recovered

This document separates confirmed legacy behavior from reconstruction
decisions. It does not define an Airfix-compatible camera yet.

## Confirmed functions

| Component | Module / RVA | Confidence | Confirmed behavior |
|---|---|---:|---|
| `CcPolyVertex::Project` | `Cc.dll` / `0x0000CCE0` | 3/3 | View space faces positive Z. Projection divides X and Y by positive Z, adds X around the screen center, and subtracts Y from the screen center. |
| `CcObject::AddVisiblePolygonsToRenderList` | `Cc.dll` / `0x0001FDD0` | 3/3 | `thiscall` method receiving `CcCamera*`, `CcRenderList*`, and a boolean. It transforms vertices, walks polygons, applies the recovered reverse-cross facing test, evaluates optional light/flare work, and queues render records. |
| `Cc3Matrix::SetRow`, `SetCol`, `operator*` | `Cc.dll` / `0x0002B5C0`, `0x0002B5E0`, `0x0002B670` | 3/3 | Confirms legacy row-vector storage and the need for the existing explicit runtime column-vector adapter. These functions do not establish a view matrix. |

Ghidra and Rizin agree on the boundaries and raw calls for
`CcObject::AddVisiblePolygonsToRenderList`. Ghidra is authoritative for its
`thiscall` ABI; Rizin's proposed `cdecl` signature is contradicted by ECX
receiver use and stack cleanup.

The saved direct-call list for that function does not contain
`CcPolyVertex::Project`. Projection may therefore occur later while draining
`CcRenderList`, but that remains a hypothesis.

## Confirmed spatial build chain

```text
NfMission::LoadLevel                   0x0002D6F0
  -> CcRoom::LoadScene                 0x00027270
  -> CcRoom::LoadSceneCcf              0x00027340
  -> CcWorld::FreezeAll                0x00011CF0
       -> CcRoom::Freeze               0x000218E0
            -> CcRoom::CreateStaticBsp 0x00021720

CcWorld::CreateAllPortalBsp            0x00011C90
  -> CcRoom::CreatePortalBsp           0x00021820
```

The portable asset layer already retains flat BSP nodes with root/child
indices, split normal, and point-on-plane data. Polygon ownership resolves
through object, mesh, and triangle records, and portal bindings retain their
target room index.

This proves how spatial data is built. It does not prove camera-side BSP
classification, child order, portal clipping, recursion, cycle prevention, or
current-room transitions.

## Missing call chain

Only the endpoints below are currently justified:

```text
[unknown world/camera traversal]
  -> CcObject::AddVisiblePolygonsToRenderList(
         CcCamera*, CcRenderList*, bool)
  -> CcRenderList
  -> [unknown projection/list drain/device calls]
  -> GtDevice
  -> gtDirect3D.dll or gt3DFX.dll
```

The graphics bootstrap is known through driver probing, device factory
selection, `dllCreate(ModuleHandle, 0)`, and the returned `GtDevice*`.
The device vtable and render-list submission path are not yet mapped.

## What is not yet known

- `CcCamera` construction, layout, update, or view transform;
- camera-to-player relation and camera mode switching;
- FOV, focal scale, near/far values, or the use of configured view distance;
- viewport resize, aspect ratio, 4:3 framing, or widescreen policy;
- clip-depth and viewport-Y mapping at the graphics backend;
- the caller graph for RVAs `0x0000CCE0` and `0x0001FDD0`;
- current-room selection and camera-driven BSP/portal traversal;
- `CcRenderList` drain and `GtDevice` submission slots;
- fog/view-distance interaction.

No parity claim should assign values or behavior to these fields before the
missing evidence is recovered.

## Current reconstructed renderer

The current Metal path is a structural diagnostic, not a recovered camera.
It applies aspect correction directly to each model transform:

```text
clip = aspectCorrection * modelFromLocal * position
```

World coordinates can therefore fall outside clip space. The diagnostic
rasterizer's auto-fit orthographic three-quarter view is also not evidence of
the original camera.

The future renderer join is:

```text
clipFromView * viewFromWorld * modelFromLocal * position
```

## Safe interim contract

Before the original builders are known, the portable layer may introduce only
a validated matrix carrier:

```cpp
struct CameraMatrices {
    Mat4 viewFromWorld;
    Mat4 clipFromView;
};

struct CameraViewport {
    std::uint32_t width;
    std::uint32_t height;
};

struct CameraRenderFrame {
    std::uint64_t simulationStep;
    CameraMatrices matrices;
    CameraViewport viewport;
};
```

Such a carrier must:

- use the runtime column-vector convention;
- define `clipFromView * viewFromWorld * modelFromLocal` explicitly;
- validate finite values, nonzero viewport, and an invertible affine view;
- compute the combined view-projection once per frame;
- label any temporary projection as `DiagnosticCamera`;
- keep draw-all selected-room rendering as the fallback;
- avoid BSP and portal culling until traversal semantics are recovered.

It must not invent an Airfix FOV, near/far pair, aspect policy, or camera mode.

## Next static-analysis targets

1. Enumerate all exports and symbols containing `CcCamera`.
2. Recover callers of `CcPolyVertex::Project` and
   `CcObject::AddVisiblePolygonsToRenderList`.
3. Identify current-room selection and the entry to BSP traversal.
4. Recover the BSP side test and child visitation order.
5. Recover portal clipping, recursion, visited-state, and room transition.
6. Recover `CcRenderList` drain and the relevant `GtDevice` vtable calls.
7. Trace globals used as screen center, focal scale, FOV, near/far, and view
   distance.
8. Trace resize/aspect behavior and camera-to-player/rear-view mode changes.

Portal traversal additionally requires a retained, budgeted CCF/catalogue/BSP
arena. The currently published static room intentionally does not retain that
complete runtime world.

## Related evidence

- [Function catalogue](../FUNCTION-CATALOG.csv)
- [Coordinate contract](COORDINATE-CONTRACT.md)
- [Render data contract](RENDER-DATA-CONTRACT.md)
- [Startup and plugin chain](STARTUP-PLUGINS.md)
- [Static tool cross-check](../../experiments/EXP-20260727-001-static-tool-crosscheck.md)
