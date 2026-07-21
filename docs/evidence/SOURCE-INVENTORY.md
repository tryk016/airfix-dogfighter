# Initial source inventory

**Evidence set:** `EV-20260721-001` through `EV-20260721-005`  
**Source:** `E:\roms\Airfix Dogfighter`  
**Method:** read-only directory listing, SHA-256, header inspection, and 7-Zip
listing attempts; no game binary was executed.

## Summary

| Property | Value |
|---|---|
| Version claimed by readme | v1.01 |
| Files | 33 |
| Total bytes | 356,442,590 |
| Native code modules | 16 PE32/x86 files |
| Main resource package | `Resource.up`, 170,642,453 bytes |
| Language packages | English, Dansk, Norsk, Svenska, about 22 MB each |
| Intro media | `airfix.avi`, `companies.avi` |
| Original graphics APIs | Direct3D/DirectX 7-era path and optional Glide |

## Code-bearing files

All rows were parsed as PE32/x86. PE timestamps are metadata, not proof of source
history.

| File | Bytes | PE kind | Preliminary role |
|---|---:|---|---|
| `Dogfighter.exe` | 290,816 | EXE | launcher/bootstrap |
| `Dogfighter.icd` | 282,669 | EXE | older high-entropy protected/compressed executable artifact |
| `AfEngine.dll` | 696,320 | DLL | probable engine core |
| `Cc.dll` | 380,928 | DLL | unknown shared subsystem |
| `UdsPack.dll` | 24,576 | DLL | probable `UDSP` archive API |
| `gtDirect3D.dll` | 36,864 | DLL | Direct3D renderer adapter |
| `gt3DFX.dll` | 36,864 | DLL | Glide renderer adapter |
| `Game/Modes/Dogfight.mode` | 36,864 | DLL | dogfight mode |
| `Game/Modes/Singleplayer.mode` | 20,480 | DLL | single-player mode |
| `Game/Types/AfFX.type` | 90,112 | DLL | effects actor family |
| `Game/Types/AirCraft.type` | 77,824 | DLL | aircraft actor family |
| `Game/Types/GroundUnit.type` | 81,920 | DLL | ground-unit actor family |
| `Game/Types/Interactive.type` | 40,960 | DLL | interactive actor family |
| `Game/Types/Pickups.type` | 40,960 | DLL | pickup actor family |
| `Game/Types/Projectiles.type` | 110,592 | DLL | projectile/weapon actor family |
| `Game/Types/WaterUnit.type` | 49,152 | DLL | water-unit actor family |

The `.mode` and `.type` extension scheme is therefore a plugin/module mechanism,
not a plain data format.

## Resource evidence

- `packlist.ini` loads `Resource.up` followed by `@language`.
- `Resource.up` and `English.up` begin with bytes `55 44 53 50 01 01`, or
  `UDSP` plus version-like bytes `01 01`.
- 7-Zip 26.00 reports that `.up` files are not recognized archives.
- `English.loc` is plain text mapping numeric localization IDs to quoted strings.
- `Game/*.mode` and `Game/*.type` are PE DLLs, so their apparent directories do
  not represent unpacked gameplay data.

## Configuration evidence

- `system.ini` selects `Direct3D` and contains a 1920x1200 setting for the local
  NVIDIA device. This proves only that this configuration was written, not that
  correct widescreen rendering has been verified.
- `user.ini` exposes commands and options for flight, camera, weapons, effects,
  shadows, lightmaps, volumetric light, vapor trails, view distance, portals,
  multiplayer limits, joystick, mouse, and player type.
- The readme states that music is played from the original CD. No standalone
  music files were found in this installation, so disc/audio-track inventory is
  an early dependency.

## Mutable files

`system.ini`, `user.ini`, the `User` tree, and the `cache` tree may be runtime or
user-generated. Their hashes describe this local snapshot and should not be used
to identify the retail build. Executables, DLLs, `.mode`, `.type`, `.up`, `.loc`,
readme, and video files are provisionally treated as immutable until disc
comparison proves otherwise.

## Source manifest

See `source-manifest.sha256`. Re-run `tools/Inspect-Source.ps1` before relying on
addresses or binary-specific conclusions.
