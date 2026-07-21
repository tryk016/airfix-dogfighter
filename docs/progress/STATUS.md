# Project status

**Updated:** 2026-07-21  
**Stage:** Phase 0 — initial baseline complete; Phase 1 not started

## Now

- Planning and documentation system established.
- Original installation inventoried without executing binaries.
- Port strategy recorded in ADR-0001.
- Complete iOS port checklist and input/control/haptics system specified;
  semantic input architecture recorded in ADR-0002.
- Local Git repository initialized on branch `main`; planning baseline committed
  as `59828ed`.

## Confirmed

- Reference readme identifies v1.01.
- 33 files total 356,442,590 bytes.
- All 16 executable modules inspected are PE32/x86.
- `.mode` and `.type` files are DLLs despite custom extensions.
- `Resource.up` and language `.up` files begin with `UDSP 01 01` and are not
  standard archives recognized by 7-Zip.
- Original rendering uses Direct3D/DirectX 7-era interfaces and optional Glide.
- Current workstation has Git and 7-Zip, but Ghidra, Java, Python, CMake, Ninja,
  and PE command-line tools were not found on `PATH`.
- The iOS control baseline requires full touch-only play plus controller-only
  play, hot-plug recovery, remapping, deterministic input frames, safe-area
  layouts, and optional haptics.
- Accepted v1.0 scope: private sideload only, iOS 16.4 minimum deployment target,
  iPhone 17 Pro Max primary device, Apple Developer account available,
  single-player only, no editors/multiplayer, and no original CD music.

## Next

1. Install/pin the Phase 1 toolchain.
2. Generate full imports/exports/sections reports.
3. Analyze `UdsPack.dll` and document the `UDSP` directory structure.
4. Add a tested no-music asset configuration because the original CD/audio is
   unavailable.

## Open questions

- Which Mac/Xcode host will be available for signing, simulator, and device
  deployment?
- Is physical testing on an older iOS-16-compatible device important, or is
  deployment-target plus simulator validation sufficient for this private build?
- Should faithful 4:3 framing or expanded widescreen be the default?

These questions do not block static analysis or the archive work.

## Blockers

- None for Phase 1 static analysis.
- A Mac with Xcode and a physical iOS device will be required before Phase 9.
- No public distribution is planned; private signed/converted artifacts must not
  be shared.
