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
  iPhone 17 Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3 runtime devices, Apple
  Developer account available, single-player only, no editors/multiplayer, and
  no original CD music.
- GitHub Actions hosted macOS/Xcode is the accepted iOS build/signing host; no
  local Mac is required. CI produces a signed data-less IPA and private original
  data remains local in an imported `.afpack`.

## Next

1. Install/pin the Phase 1 toolchain.
2. Generate full imports/exports/sections reports.
3. Analyze `UdsPack.dll` and document the `UDSP` directory structure.
4. Add a tested no-music asset configuration because the original CD/audio is
   unavailable.
5. Connect a private GitHub remote and add CI after the portable build skeleton
   exists.

## Open questions

- Which Windows-compatible method will install the signed Actions IPA on both
  registered phones?
- Should faithful 4:3 framing or expanded widescreen be the default?

These questions do not block static analysis or the archive work.

## Blockers

- None for Phase 1 static analysis.
- A private GitHub remote, signing certificate/profile for both device UDIDs,
  protected Actions secrets, and Windows IPA installation path are required
  before the first signed device spike.
- No public distribution is planned; private signed/converted artifacts must not
  be shared.
