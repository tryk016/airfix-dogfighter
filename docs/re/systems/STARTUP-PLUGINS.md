# Startup, package chain, and plugin registration

**Owner modules:** `Dogfighter.exe`, `AfEngine.dll`, `Cc.dll`

**Status:** Windows behavior cross-checked statically; portable registry not
yet implemented

**Confidence:** 3/3 for loader ownership, discovery names, version checks,
startup phase order, and graphics/mode handle lifetime; 2/3 for formal factory
parameter types and shared type-module teardown coordination

**Evidence IDs:** `EV-20260721-011`, `EV-20260721-013`, `EV-20260721-014`,
`EV-20260730-002`

**Scenario IDs:** `SCN-BOOT-001`, `SCN-STARTUP-001`, `SCN-PACKAGE-001`,
`SCN-PLUGIN-001`

## Responsibilities and boundaries

- `Dogfighter.exe` owns the Windows application/window/message-loop shell.
- `NfMain` owns package loading, console/configuration, engine/database/server,
  graphics and sound device setup, and high-level startup ordering.
- `NfDatabase` discovers actor-type and mission-mode plugins.
- Graphics-adapter discovery and creation use a separate three-export ABI.
- `UdsPack.dll` owns archive access but receives the package chain from
  `NfMain::LoadPackages`/`CcSetPackageList`.

The iOS reconstruction preserves these semantic boundaries without loading
Windows DLLs or exposing the historical ABI.

## Windows bootstrap

`Dogfighter.exe` function `0x00416090` is the effective WinMain-style shell:

1. Seed CRT random state from `timeGetTime`.
2. Set `FX_GLIDE_NO_SPLASH`.
3. Register `AirfixClass`, create the main window and startup dialog.
4. Construct the application object.
5. Call virtual open/init.
6. While its virtual continue predicate is true, call two virtual per-frame
   methods in order.
7. Destroy the object/window and unregister the class.

The wrapper vtable at VA `0x004393FC` identifies the used slots:

| Slot | RVA | Working name | Behavior |
|---:|---:|---|---|
| `+0x04` | `0x00010A10` | `ApplicationOpen` | closes prior state and calls `NfMain::Open` |
| `+0x08` | `0x00010AD0` | `ApplicationClose` | releases wrapper render objects and calls `NfMain::Close` |
| `+0x0C` | `0x00010C00` | `ApplicationWindowMessage` | activation, character input, CD notification, and device state |
| `+0x10` | `0x00010FF0` | `PumpMessagesAndContinue` | drains messages and stops after `WM_QUIT` |
| `+0x14` | `0x00010FA0` | `PollInputAndAdvanceTime` | input/console work followed by game and real-time advancement |
| `+0x18` | `0x00011460` | `DispatchActiveFrameEvent` | resets the timer and saves/processes the active-frame event |

The exact loop is message pump/continue, input/time, then active-frame event.
The event is constructed with `0xFF000000` and `4`; its semantic enum name
remains unclaimed.

## Package-chain initialization

`NfMain::NfMain` calls `LoadPackages("Packlist.ini")` before creating the
console. `NfMain::LoadPackages`:

1. Destroys the old linked package chain.
2. Opens the requested list through `UpFile`.
3. Removes spaces/tabs/newlines and ignores blank or `;` comment lines.
4. Treats an `@name` line as a console-variable substitution.
5. Appends `.up` if a selected package name has no `.up` suffix.
6. Constructs each `UpPackage` in list order, linking it to the previous head.
7. If the list cannot be opened, enumerates `*.up` as a fallback.
8. Publishes the head through `CcSetPackageList`.

The initial pass skips `@language` because the console does not exist yet.
After configuration sets/changes `language`, `NfMain::ConsoleCommand` calls
`LoadPackages("Packlist.ini")` again, at which point the localized package is
resolved. This explains the supplied `Resource.up` followed by `@language`.

## Actor-type plugin contract

Default directory and search pattern:

```text
game\types\*.type
```

The console variable `typepath` can replace the default directory. For each
candidate, `NfDatabase::AddTypes`:

1. loads the DLL;
2. resolves `nfVersion` and `typeCreate`;
3. requires both exports and `*(uint32_t*)nfVersion == 0x6D`;
4. calls the factory with `(NfDatabase*, HMODULE, modulePath)`;
5. unloads the DLL on any failure;
6. retains successful type modules through registered `NfType` ownership so
   their code remains valid.

Recovered semantic signature (some concrete C++ types remain provisional):

```text
bool typeCreate(NfDatabase*, ModuleHandle, const char* modulePath)
```

## Mission-mode plugin contract

Default directory and search pattern:

```text
game\modes\*.mode
```

The console variable `missionpath` can replace the default directory.
`NfDatabase::AddMissions` temporarily loads every mode and reads:

- required `nfVersion == 0x6D`;
- required `missionName` string;
- optional `bMultiplayer` byte, defaulting to false.

It stores the display name, basename, full module path, and multiplayer flag,
then unloads the module. `NfMissionInfo::CreateMission` reloads it on demand,
resolves `missionCreate`, and calls:

```text
NfMission* missionCreate(NfMain*, NfMissionInfo*)
```

The handle remains associated with the mission-info object and is released by
its destructor. `Dogfight.mode` additionally exports `setupServer` and
`showInfo`; these multiplayer hooks are outside v1 scope.

## Graphics-adapter plugin contract

There are three distinct graphics paths.

`AfSetup::Open` first probes each `gt*.dll` candidate with
`LoadLibraryEx(..., DONT_RESOLVE_DLL_REFERENCES)`, requires
`dllVer == 0x3B`, reads `dllName`, and unloads it. It skips `_debug` and
`_profile` candidates. Setup-time card/mode enumeration then loads the accepted
adapter normally, resolves `dllCreate`, creates a temporary device, enumerates
it, and releases it.

The actual runtime device is opened later by `NfMain::ChangeDevice` through
exported `Cc.dll::GtOpenDevice`. That function requires `dllVer`, `dllName`,
and `dllCreate`, then calls approximately:

```text
GtDevice* dllCreate(ModuleHandle, WindowHandle)
```

It opens the selected card through the returned device's virtual interface.
The successful device retains the module handle until `GtDevice::Release`
destroys the device and unloads the DLL. `Cc.dll::GtEnumDevices` is a separate
public callback-based enumeration API; the recovered `AfSetup` path performs
its own enumeration rather than calling it.

Both `gtDirect3D.dll` and `gt3DFX.dll` implement the same three-export
boundary.

## Startup ordering inside `NfMain::Open`

The recovered high-level order is:

1. close any prior runtime;
2. open/configure `system.ini` and select the graphics device;
3. create font/console and optional sound device;
4. create database, engine, and server objects;
5. call `NfDatabase::AddTypes`, then `NfDatabase::AddMissions`;
6. execute remaining startup console commands and mark the runtime open.

Drive/volume queries and historical CD handling are interleaved with this code.
They are platform artifacts, not dependencies of the portable core.

## Error and edge behavior

- Invalid type modules are unloaded and skipped.
- Mode discovery requires version/name but tolerates missing `bMultiplayer`.
- Missing `missionCreate` unloads the mode and returns no mission.
- Missing/invalid graphics metadata or creation exports produce setup errors and
  unload the module.
- Type and mode scans use unsorted `FindFirstFileA`/`FindNextFileA` order.
  Portable registry order must be explicit rather than inheriting filesystem
  enumeration order.
- A missing `Packlist.ini` falls back to every `*.up` file in the working
  directory; the iOS importer will not use this nondeterministic fallback.
- Starting/changing a mission issues `cd play` for a random legacy CD track.
  The no-disc v1 port must make that command a non-fatal no-op unless legal
  replacement music is supplied by the owner.

## Reconstructed contract

The portable runtime will use a compile-time registry:

```text
TypeRegistry: ordered registration callbacks
ModeRegistry: stable id, display metadata, multiplayer flag, factory callback
RenderBackendRegistry: Metal backend metadata and factory
```

Registration order must remain deterministic. The registry will expose version
constants for traceability, but no Windows module handle, `LoadLibrary`, or
`GetProcAddress` crosses into portable code. The excluded editors and
multiplayer mode are not registered in the v1 product build.

## Implementation mapping

- FMT-UDSP package metadata: `src/airfix/archive/UdspArchive.*`
- Static plugin registry: not yet implemented
- iOS application lifecycle/Metal backend: not yet implemented
- Private `.afpack` manifest will replace runtime wildcard enumeration and bind
  explicitly ordered package/content sources.

## Known unknowns

- Concrete C++ parameter/ownership types behind the three factory calls.
- The formal source-level name of the frame event constructed with
  `(0xFF000000, 4)`.
- Exact coordination of `NfType::Release` when several registered types share
  one plugin handle.
- All commands executed from obfuscated startup data in `NfMain::Open`.

The full cross-tool comparison and exact function boundaries are recorded in
[`EXP-20260730-066`](../../experiments/EXP-20260730-066-bootstrap-module-loading.md).
