# Gameplay camera pose and modes

**Date:** 2026-07-27  
**Evidence:** `EV-20260727-011`  
**Scope:** gameplay camera ownership, mode selection, input bindings, chase
offsets, rear view, first/third-person dispatch, pose smoothing, collision
response, clipping, FOV, and the gameplay viewport

## Question

Recover the camera path used while flying an aircraft. In particular:

- separate it from editor and frontend cameras;
- identify the camera modes and their input commands;
- recover the vehicle and camera fields used by the chase pose;
- determine how position, rotation, rooms, smoothing, and collision interact;
- establish the gameplay window and centre;
- determine whether the legacy game contains a recenter operation; and
- state which facts can safely become a portable C++20 camera contract.

## Inputs and method

| Module | SHA-256 | Image base |
|---|---|---:|
| `Dogfighter.exe` | `F48CD7D16E8D993B262F4E35731ABFB1D95288E0C688506065F168E1B4090E89` | `0x00400000` |
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | `0x10000000` |
| `AirCraft.type` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` | `0x10000000` |

Ghidra 12.1.2 Headless supplied exported MSVC names, signatures,
decompilation, vtable relationships, data references, and exact instruction
boundaries. Rizin 0.9.1 independently analyzed the five representative
`AfEngine.dll` functions listed below and exported path-free normalized
reports. The installed configuration and input bindings were read as data.

All analysis used hash-verified working copies. No binary was executed,
patched, uploaded, or opened in a cloud service. Binary Ninja was not used.

## Result

The gameplay camera is the `CcCamera` owned by `NfEngine + 0x30`, not the
camera in the `Dogfighter.exe` block previously inspected at RVA `0x2B8D0`.

The flying camera has:

- three persistent chase presets selected by `togglecamera`;
- one held rear-view override selected by `+camera_rear`;
- console-selectable first/third-person state;
- a vehicle-relative horizontal/depth offset plus a world-up offset;
- nonlinear per-refresh position smoothing;
- sphere and line collision correction;
- a rotation rebuilt each pre-render so the camera looks at the vehicle; and
- a full-current-screen gameplay window.

At the original startup mode, that gameplay window is:

```text
window = (0, 0) - (640, 480)
centre = (320, 240)
```

The parity-first `640x480` logical canvas remains justified independently by
the fixed startup display mode and this full-screen gameplay setter. It does
not depend on the House Editor rectangle.

## Required correction: the `Dogfighter.exe` block is House Editor only

The following functions are not gameplay camera functions:

| `Dogfighter.exe` RVA | Behavior |
|---:|---|
| `0x0002AB80` | House Editor camera/control update |
| `0x0002B450` | House Editor camera render |
| `0x0002B8D0` | House Editor camera reset |

The containing object is independently identified by:

- `Graphics\Frontend\HouseEditor` resource paths;
- `freecam`, `cam_free`, `cam_zoom_*`, `cam_tilt_*`, and `cam_rot_*` strings;
- the text `Freecam/Fixedcam`;
- House Editor editing instructions; and
- construction of an editor test scene.

Its reset function assigns:

```text
window = (35, 35) - (555, 345)
centre = (295, 190)

free-editor branch:  near = 0.1, far = 500, FOV = 90
fixed-editor branch: near = 8.0, far = 500, FOV = 50
```

These values must not be used as gameplay constants. The `520x310` rectangle
is a House Editor widget area and is outside the version-1 port scope.

## Gameplay function map

All RVAs in this section are relative to `AfEngine.dll`.

| RVA | Symbol | Camera role |
|---:|---|---|
| `0x0001A950` | `AfVehicle::AfVehicle` | initializes chase offset, speed, and axis factors |
| `0x0001B6C0` | `AfVehicle::ProcessEvent` | refresh smoothing, camera collision, look-at rotation |
| `0x0001FE60` | `AfVehicle::SetFirstPerson(CcCamera*)` | forwards through the vehicle's camera-attach virtual slot |
| `0x0001FE70` | `AfVehicle::SetThirdPerson()` | detaches the camera |
| `0x0001FEC0` | `AfVehicle::SetThirdPerson(CcCamera*)` | attaches and initially places the camera |
| `0x0001FF70` | `AfVehicle::SetCamera` | stores the camera pointer only |
| `0x000213B0` | `AfVehicle::SetCamSpeed` | writes the smoothing multiplier |
| `0x000213C0` | `AfVehicle::GetCamSpeed` | reads the smoothing multiplier |
| `0x000213D0` | `AfVehicle::SetCamOffset` | writes the three offset components |
| `0x000213F0` | `AfVehicle::GetCamOffset` | reads the three offset components |
| `0x0003F8E0` | `NfEngine::NfEngine` | creates/configures the gameplay camera and registers commands |
| `0x0003FDC0` | `NfEngine::ResetCamera` | calls `CcCamera::Reset` |
| `0x0003FDD0` | `NfEngine::GetCameraMode` | returns the persistent mode integer |
| `0x00040170` | `NfEngine::GetNearClipping` | returns the engine near plane |
| `0x00040180` | `NfEngine::GetFarClipping` | returns the engine far plane |
| `0x00040190` | `NfEngine::SetFirstPersonUID` | changes the tracked actor and dispatches first-person attach |
| `0x000401F0` | `NfEngine::SetThirdPersonUID` | changes the tracked actor and dispatches third-person attach |
| `0x00040250` | `NfEngine::ProcessEvent` | screen setup and first/third-person event handling |
| `0x000422A0` | `NfEngine::ConsoleCommand` | mode, rear, FOV, and clipping commands |
| `0x0004A860` | `NfEngine::GetCamera` | returns `NfEngine + 0x30` |
| `0x0005A420` | `NfPlayer::SetFirstPerson` | forwards camera attach to the primary actor |

## Engine camera state

The constructor establishes:

| `NfEngine` field | Initial value | Meaning |
|---:|---:|---|
| `+0x30` | allocated `CcCamera*` | gameplay camera |
| `+0xFC` | `1` | engine first/third-person state flag; `1` after first-person event |
| `+0x100` | `0` | tracked actor/UID |
| `+0x104` | `0.25` | near clipping plane |
| `+0x108` | `200.0` | far clipping plane |
| `+0x124` | `0` | persistent chase preset index |

The camera constructor path performs:

```text
camera.SetClippingDist(0.25, 200.0)
camera.SetMatrixRotation()
camera.SetFov(90.0)
```

The FOV is horizontal, as established by the projection audit. Console
command `fov` accepts one nonzero floating-point value and sends it directly
to `CcCamera::SetFov`.

Console commands `zclip_near` and `zclip_far` update both the stored engine
values and the camera clipping pair. Confirmed validation is:

```text
near >= 0.15
near < far
far <= 16384.0
far > near
```

`zclip_apply` reapplies the stored pair to the camera and screen.

## Persistent chase presets and held rear view

`NfEngine::NfEngine` creates four console variables:

| Variable | Speed | Offset `(x, y, z)` | Confirmed structural role |
|---|---:|---:|---|
| `camera0` | `0.7` | `(0.0, 0.1, -0.75)` | chase preset 0 |
| `camera1` | `0.07` | `(0.0, 0.2, -0.85)` | chase preset 1 |
| `camera2` | `0.7` | `(0.0, 1.8, -0.75)` | chase preset 2 |
| `camera_rear` | `0.7` | `(0.0, 0.1, +1.0)` | held rear-view override |

The recovered coordinate convention is `+X` right, `+Y` up, and `+Z`
forward. Negative `z` therefore places the camera behind the vehicle.
Positive `z` places the rear-view camera in front of it; the later look-at
step points it back toward the vehicle.

The original semantic names of presets 0, 1, and 2 were not found. Describing
them as normal, slow/tight, and elevated is a useful geometric
interpretation, not a recovered UI label.

`togglecamera` increments `NfEngine + 0x124`, finds the corresponding
`cameraN` variable, and applies it through the `camera speed x y z` path. If
the next variable does not exist, the logic wraps to mode zero. With the
three constructor-defined variables, the cycle is:

```text
0 -> 1 -> 2 -> 0
```

The mode integer is not changed by rear view. `camera_rear true` applies the
rear tuple; `camera_rear false` reapplies the tuple named by the persistent
mode integer.

The installed default input data confirms the intended control behavior:

```text
F1       -> togglecamera
LShift   -> +camera_rear
button 3 -> +camera_rear
button 4 -> togglecamera
```

The `+camera_rear` binding supplies press/release behavior, making rear view a
momentary override rather than a fourth persistent preset.

## Vehicle fields

The relevant `AfVehicle` fields are:

| Offset | Initial value | Role |
|---:|---:|---|
| `+0x22C` | null | attached `CcCamera*` |
| `+0x278..+0x280` | dynamic | vehicle position used by chase target |
| `+0x284..+0x290` | dynamic | four-component vehicle rotation |
| `+0x41C` | `0.0` | camera offset `x` |
| `+0x420` | `0.1` | camera offset `y`, applied as world-up lift |
| `+0x424` | `-0.75` | camera offset `z` |
| `+0x428` | `0.7` | per-refresh smoothing speed |
| `+0x42C` | `1.0` | X movement factor modified by collision response |
| `+0x430` | `1.0` | Y movement factor modified by collision response |
| `+0x434` | `1.0` | Z movement factor modified by collision response |

The world-relation position at `AfVehicle + 0x1A4..+0x1AC` is used for the
initial attach and pre-render look-at target. This report keeps it distinct
from the dynamic position at `+0x278..+0x280` because the binary does.

## Attach, detach, and first/third-person state

`AfVehicle::SetThirdPerson(CcCamera*)`:

1. stores the camera at `vehicle + 0x22C`;
2. sets a vehicle byte at `+0x9C` to one;
3. copies the vehicle node's room to the camera;
4. unparents the camera;
5. copies vehicle world position `+0x1A4..+0x1AC` to camera position
   `+0x94..+0x9C`;
6. adds exactly `4.0` to camera Y; and
7. calls `CcSrtNode::NotifyChange`.

The initial `+4.0` placement is transient. Subsequent refresh events drive the
camera toward the selected chase target.

`AfVehicle::SetThirdPerson()` clears `+0x22C` and the byte at `+0x9C`.
Both attach and detach also notify three optional vehicle-owned nodes through
the same virtual operation. Its exact semantic name is not established.

`NfEngine` registers `firstperson` and `thirdperson`. They create events
`0x57` and `0x58`, respectively. `NfEngine::ProcessEvent` dispatches those
events to the tracked UID using adjacent virtual slots and writes:

```text
event 0x57 -> engine +0xFC = 1
event 0x58 -> engine +0xFC = 0
```

For the examined `AirCraft.type`, the aircraft vtable imports the base
`AfVehicle` camera methods rather than defining a distinct aircraft camera
implementation. Base `AfVehicle::SetFirstPerson(CcCamera*)` forwards through
the adjacent virtual camera-attach slot, which resolves to the imported base
`SetThirdPerson(CcCamera*)` on this aircraft path.

Therefore no separate first-person position or offset was found for the
representative aircraft. The engine still records first/third-person state
separately, so it would be unsafe to erase that state from the reconstruction
or to claim that all possible actor plugins behave identically.

## Chase target and smoothing

The chase update is in the shared `AfVehicle::ProcessEvent` path for events
`0x76`, `0x77`, and `0x78`. It runs only when a camera is attached and the
camera node is active.

Let:

```text
o = configured offset (ox, oy, oz)
R = rotation matrix derived from vehicle rotation
p = vehicle position at +0x278..+0x280
c = current camera position
```

The binary does not rotate `oy` with the vehicle. It rotates only the
horizontal/depth components and adds Y in world space:

```text
worldOffset = R * (ox, 0, oz) + (0, oy, 0)
target      = p + worldOffset
error       = (p - c) + worldOffset
```

This preserves an upright world-space lift while the behind/front component
follows vehicle rotation. `target - c` is algebraically equivalent, but the
second form above is the recovered instruction order and is retained by the
portable implementation so it does not introduce a different float-rounding
boundary.

The recovered nonlinear error adjustment is:

```text
if lengthSquared(error) < lengthSquared(o):
    scale = (sqrt(lengthSquared(error)) + 0.001) * 0.99
    error = error * scale
```

Then each axis advances independently:

```text
candidate.x = c.x + factorX * speed * error.x
candidate.y = c.y + factorY * speed * error.y
candidate.z = c.z + factorZ * speed * error.z
```

The candidate is passed to:

```text
CcPosition::SetPosAndTracePortals(candidate, 0.0)
```

using a temporary `CcPosition`. The prior camera room is restored with
`CcCamera::SetRoom`; the resolved coordinates are copied directly into
`CcCamera + 0x94..+0x9C`; and `NotifyChange` publishes the change.

There is no elapsed-time multiplier in this camera recurrence. Although the
same event carries timing data for vehicle simulation, the camera step uses
the mode speed directly per refresh. Porting it as a naïve units-per-second
velocity would change the feel.

## Collision response and look-at rotation

The event-5 pre-render path performs a second, collision-oriented camera
stage:

1. Copy the current camera position and room.
2. Build a collision sphere at that position.
3. Set its radius to:

   ```text
   nearClipping * 1.1
   ```

   With constructor defaults, the radius is `0.275`.
4. Resolve sphere contacts against BSP geometry.
5. Subtract twice the absolute collision correction from the corresponding
   axis factor at `+0x42C`, `+0x430`, or `+0x434`, clamping each to zero.
6. Trace the corrected temporary position through portals with third
   argument `0.2`, then copy it back to the camera.
7. Trace a line between the vehicle world anchor and camera. If it hits,
   move the camera to the hit fraction and trace that position through
   portals with third argument `0.1`.
8. Form:

   ```text
   direction = vehicleWorldPosition - cameraPosition
   ```

9. Convert the direction with `CcAxisRot::FromDirection`.
10. Reset the camera rotation matrix to identity, call
    `CcMatrixRot::RotateByAxisRot`, and publish with `NotifyChange`.

This makes the final camera face the vehicle for chase and rear-view tuples.

The audited code initializes the three axis factors to one and contains the
listed collision-driven reductions. No recovery/increase write was found in
the examined constructor and event function. Their long-term behavior should
therefore be preserved cautiously and verified dynamically before assigning
a higher-level name such as collision spring.

## SRT and pose mutation

The gameplay chase path does not use a simple public
`camera.SetPos()/camera.SetRot()` pair.

Confirmed operations are:

- `CcCamera::SetRoom` and `CcSrtNode::SetParent` during attach;
- direct writes to camera position fields `+0x94..+0x9C`;
- `CcPosition::SetPosAndTracePortals` on temporary positions;
- `CcSrtNode::NotifyChange` after direct position writes;
- direct identity initialization of the camera rotation matrix;
- `CcMatrixRot::RotateByAxisRot` for the final look-at orientation; and
- `CcSrtNode::SetMatrixRotation` during engine camera construction.

The portable implementation may expose higher-level setters, but it must
retain the ordering of room/portal resolution, position publication, and
look-at rotation.

## Gameplay viewport

In `NfEngine::ProcessEvent`, event case 5 configures the engine-owned camera:

```text
camera.SetScreen(screen)
camera.SetWindow(0, 0, screen.width, screen.height)
camera.SetCentre(screen.width * 0.5, screen.height * 0.5)
```

This is the actual gameplay window path. At the recovered startup display
mode it evaluates to:

```text
(0,0)-(640,480), centre (320,240)
```

The setters read the current screen dimensions, so a console-selected display
mode changes this full-screen window. No automatic modern widescreen policy
was found.

## Recenter result

`NfEngine::ResetCamera` exists and calls `CcCamera::Reset`, but it is not
registered among the camera console commands in the audited constructor and
is not bound in the installed default input data. The normal chase/rear
pipeline continuously rebuilds orientation to look at the vehicle; it does
not expose a player-triggered orbit-recenter action.

Consequently:

- no legacy recenter offset, duration, or damping constant was recovered;
- a touch/controller `recenter camera` action remains a port addition; and
- that addition should reset any new free-look/orbit state to the selected
  legacy tuple rather than inventing a change to the recovered chase
  recurrence.

## Call graph

```text
NfEngine::NfEngine
  create CcCamera
  set near/far/FOV
  define camera0, camera1, camera2, camera_rear
  register togglecamera, camera, camera_rear, firstperson, thirdperson

local player spawn
  NfEngine::GetCameraMode
  execute "camera N"
    NfEngine::ConsoleCommand
      AfVehicle::SetCamSpeed
      AfVehicle::SetCamOffset

firstperson / thirdperson
  NfEngine::ConsoleCommand
    save/process event 0x57 / 0x58
      NfEngine::ProcessEvent
        tracked UID virtual attach
          NfPlayer::SetFirstPerson
            AfVehicle::SetFirstPerson
              virtual attach -> AirCraft uses AfVehicle::SetThirdPerson(camera)

refresh events 0x76 / 0x77 / 0x78
  AfVehicle::ProcessEvent
    derive chase target from vehicle pose and selected tuple
    nonlinear position smoothing
    CcPosition::SetPosAndTracePortals
    copy camera position + NotifyChange

pre-render event 5
  AfVehicle::ProcessEvent
    sphere correction
    vehicle-to-camera line correction
    look-at axis rotation + NotifyChange

engine event 5
  NfEngine::ProcessEvent
    full-screen SetWindow and midpoint SetCentre
```

## Representative pseudocode

```text
applyCameraTuple(vehicle, tuple):
    vehicle.camSpeed  = tuple.speed
    vehicle.camOffset = tuple.offset
```

```text
toggleCamera(engine):
    engine.mode += 1
    if variable("camera" + engine.mode) exists:
        execute(variable)
    else:
        engine.mode = 0
        execute(camera0)
```

```text
setRearView(enabled):
    if enabled:
        apply(camera_rear)
    else:
        apply(camera[engine.mode])
```

```text
refreshChase(vehicle, camera):
    planarDepthOffset =
        rotation(vehicle.q) * (offset.x, 0, offset.z)
    target =
        vehicle.position + planarDepthOffset + (0, offset.y, 0)
    error =
        (vehicle.position - camera.position) +
        planarDepthOffset + (0, offset.y, 0)

    if dot(error, error) < dot(offset, offset):
        error *= (length(error) + 0.001) * 0.99

    candidate =
        camera.position +
        axisFactors * camSpeed * error

    resolved = tracePortals(candidate, 0.0)
    camera.room = previousRoom
    camera.position = resolved.position
    camera.notifyChange()
```

```text
prepareCameraForRender(vehicle, camera):
    radius = engine.nearClip * 1.1
    camera.position = resolveSphere(camera.position, radius)
    reduceAxisFactorsByCollisionCorrection()
    camera.position = resolveVehicleToCameraLine(camera.position)
    camera.rotation = lookAt(vehicle.worldPosition - camera.position)
    camera.notifyChange()
```

## Ghidra/Rizin cross-check

Rizin independently found the same five function boundaries and instruction
counts as Ghidra:

| Function | VA range, end-exclusive | Instructions | Cross-check |
|---|---:|---:|---|
| `AfVehicle::ProcessEvent` | `[0x1001B6C0, 0x1001FA6C)` | `4454` | exact agreement |
| `AfVehicle::SetThirdPerson(CcCamera*)` | `[0x1001FEC0, 0x1001FF68)` | `51` | exact agreement |
| `NfEngine::NfEngine` | `[0x1003F8E0, 0x1003FCA2)` | `292` | exact agreement |
| `NfEngine::ProcessEvent` | `[0x10040250, 0x1004203B)` | `1697` | exact agreement |
| `NfEngine::ConsoleCommand` | `[0x100422A0, 0x10042DB0)` | `926` | exact agreement |

| Question | Ghidra | Rizin | Result |
|---|---|---|---|
| function boundary | exported symbols and instruction body | automatic PE32/x86 discovery | exact agreement for all five |
| camera/vehicle field roles | strongest: typed MSVC symbols and decompilation | confirmed raw field offsets and constants | agreement |
| mode strings and constants | resolved constructor strings and command flow | confirmed string bytes and immediate values | agreement |
| attach position | recovered room/SRT APIs and `+4.0` Y constant | confirmed instruction order and data address | agreement |
| smoothing formula | clearest structured expression | confirmed square root, `0.001`, `0.99`, and axis multiplies | agreement |
| calling convention and argument count | authoritative exported `__thiscall` signatures | reported `cdecl` and over-inferred arguments for large member functions | Ghidra preferred |

Rizin's incorrect `cdecl` labels and excessive inferred argument counts in
large C++ member functions are not used as evidence. Its boundary,
instruction, callsite, and constant results remain useful independent checks.

## Confidence

Confidence is **3/3** for:

- ownership of the gameplay camera by `NfEngine + 0x30`;
- the House Editor correction for `Dogfighter.exe` RVAs `0x2AB80`,
  `0x2B450`, and `0x2B8D0`;
- the full-current-screen gameplay window and midpoint centre;
- constructor near `0.25`, far `200`, and horizontal FOV `90`;
- all four speed/offset tuples;
- the `0 -> 1 -> 2 -> 0` persistent mode cycle;
- held rear-view restore behavior;
- relevant `AfVehicle` field offsets and initial values;
- initial camera Y lift `4.0`;
- the nonlinear smoothing constants `0.001` and `0.99`;
- collision-sphere multiplier `1.1`;
- direct camera position/matrix writes and notification order; and
- exact boundaries of the five cross-checked functions.

Confidence is **2/3** for:

- descriptive labels attached to the three unnamed chase presets;
- the higher-level semantic meaning of the three collision axis factors; and
- equivalence of first/third-person camera pose beyond the examined
  `AirCraft.type` implementation.

No legacy player-triggered recenter behavior was found. That negative result
is confidence **2/3** because it covers the engine camera command
registration, installed default bindings, and the recovered chase path, but
not every possible script or actor plugin.

## Reconstruction consequence

A parity-first portable camera should initially preserve:

1. one engine-owned gameplay camera;
2. the three recovered persistent tuples and held rear tuple;
3. world-up treatment of offset Y;
4. the recovered nonlinear per-refresh position step;
5. portal-aware position mutation before pose publication;
6. the near-plane-scaled sphere and vehicle-to-camera line correction;
7. a final look-at rotation toward the vehicle;
8. the `0.25/200/90` constructor projection defaults; and
9. a logical full-screen `640x480` gameplay viewport, aspect-fitted by the
   separate presentation layer.

`LegacyGameplayCameraPreset` implements the exact four tuples, fail-closed raw
mode validation, persistent `0 -> 1 -> 2 -> 0` cycling, held rear selection,
and the `0.25/200/90` projection defaults.

`LegacyGameplayCameraChase` now implements the confirmed stateless portion of
the refresh recurrence: runtime-column rotation of offset X/Z, world-up offset
Y, the recovered `(vehicle - camera) + worldOffset` operation order, strict
nonlinear threshold, exact binary32 constants, per-axis factors, and the
candidate position without `dt`. The allocation-free `noexcept` boundary
rejects non-finite input or derived output atomically. It accepts the finite
matrix supplied by the caller without silently normalizing it; the later
quaternion adapter must preserve the original `CcMatrixRot::FromQuat`
behavior.

The implementation deliberately does not claim bit-identical x87 extended
precision on every modern target. Algebra and instruction grouping are
confidence **3/3**; cross-platform bit identity remains confidence **2/3**.
Portal tracing, collision correction, look-at rotation, and final pose
publication remain separate unimplemented stages.

Time-normalized smoothing, free look, touch drag, controller right-stick
orbit, recenter, and Hor+ widescreen are useful port features, but they must
be optional layers around this recovered baseline. They are not binary facts.

See
[EXP-20260727-005](EXP-20260727-005-camera-world-to-view.md) for the recovered
world-to-camera transform, and
[EXP-20260727-007](EXP-20260727-007-viewport-aspect.md) for projection,
startup display mode, and the parity-first presentation decision.
