# EXP-20260803-111: explicit texture sample-space contract

**Status:** implemented and locally validated

**Evidence boundary:** architecture/implementation evidence only; no original
texture is semantically classified

**Decision:** GO for the dormant shared contract and native format mappings;
NO-GO for changing current GTI views or enabling linear-light rendering

## Question

Can portable texture preparation preserve an explicit sample-space decision
through D3D11 and Metal without globally assuming that legacy GTI or the later
private HD package is sRGB?

## Contract

Every RGBA8 upload plan carries exactly one `TextureSampleSpace`:

- `encodedUnclassified` means the encoded channel values have no proven colour
  semantics and must retain the conservative UNORM path;
- `srgbColor` permits an sRGB sampling view only after an upstream classifier
  has explicitly identified colour data; and
- `linearData` identifies already-linear or technical channel data and selects
  a non-sRGB UNORM view.

Unknown enum values map to an explicit invalid encoding. They are rejected by
the portable validator and native upload preflight rather than falling back to
one of the valid interpretations.

## Existing-content boundary

`describeGtiUpload` and `prepareGtiUpload` always publish
`encodedUnclassified`. GTI metadata does not prove whether a texture is colour,
a mask, a lookup image, or another technical payload. This change therefore
does not alter existing pixels, mip policy, filtering, material behaviour,
cache identity, or simulation.

The label is retained by room-plan equality checks and validated by every
authenticated HUD texture owner. No manifest, private texture, logical path,
checksum, or original asset is added to the repository or test suite.

## Backend mapping

Windows D3D11 selects:

- `DXGI_FORMAT_R8G8B8A8_UNORM` for unclassified and linear data; and
- `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` for explicitly classified sRGB colour.

iOS Metal selects:

- `MTLPixelFormatRGBA8Unorm` for unclassified and linear data; and
- `MTLPixelFormatRGBA8Unorm_sRGB` for explicitly classified sRGB colour.

Both mappings are part of the existing immutable texture/heap preparation
transaction. The sRGB branches remain dormant for current content until a
separate classifier and end-to-end output-colour stage are accepted.

## Validation

- Portable selector tests cover all three valid labels and a forged enum.
- Existing exact GTI-chain tests confirm the default remains
  `encodedUnclassified`.
- A fresh GCC 15.2/Ninja build succeeds and all 143/143 portable CTests pass.
- Visual Studio 2026 MSVC 19.51/Ninja compiles and links the complete
  SDL3/D3D11/XAudio2 product with the new DXGI mapping.
- All 157/157 Windows CTests pass, including the D3D11 renderer and complete
  product smoke tests.
- Public-boundary tests pass and the scanner accepts all 755 public files.
- Hosted macOS/Linux and iOS builds remain publication gates.

## Decision

**GO** for retaining the explicit contract as a prerequisite for correct
colour handling, private HD replacements, and future compressed derivatives.

**NO-GO** for assigning sRGB to any current GTI or private texture by filename,
alpha mode, visual appearance, or global policy. Linear-light shading, sRGB
output encoding, MSAA, and final-image anti-aliasing remain later ADR-0013
stages.
