# Project status

**Updated:** 2026-07-21  
**Stage:** Phase 0 — initial baseline complete; Phase 1 not started

## Now

- Planning and documentation system established.
- Original installation inventoried without executing binaries.
- Port strategy recorded in ADR-0001.

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

## Next

1. Initialize version control and record this planning baseline.
2. Install/pin the Phase 1 toolchain.
3. Generate full imports/exports/sections reports.
4. Analyze `UdsPack.dll` and document the `UDSP` directory structure.
5. Ask the project owner to locate the original disc/image and CD audio tracks.

## Open questions

- Is the original CD or disc image available, including audio tracks?
- Is the intended first deliverable private sideloading or public App Store
  distribution?
- Which Mac, iPhone/iPad models, and Apple developer account will be available?
- Are multiplayer, House Editor, and Paint Room required for version 1.0?

These questions do not block static analysis or the archive work.

## Blockers

- None for Phase 1 static analysis.
- A Mac with Xcode and a physical iOS device will be required before Phase 9.
- Rights documentation will be required before distributing original content.

