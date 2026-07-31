# FMT-GTI — `GtImage` texture container

**State:** header, chunk table, image metadata, palette placement, pixel-size
calculation, bounded full-chain RGBA8 conversion, backend-neutral upload
metadata, and atomic owned upload preparation implemented

**Confidence:** 3/3 for fields below across the selected 1,985-file corpus

**Evidence:** `EV-20260721-017` through `EV-20260721-019`, `EV-20260721-031`

## Evidence

The layout is corroborated by `GtImage::IsValid`, `GtImage::LoadAs`,
`GtImage::LoadChecksum`, `GtImage::GetPixelSize`, `GtImage::GetImageSize`, and
`GtImage::ConvertToARGB8888` in `Cc.dll`, plus bounded parsing of every GTI in
`Resource.up` and `English.up`.

## File header

All integers are little-endian.

| Offset | Size | Field | Value |
|---:|---:|---|---|
| `0x00` | 4 | magic | ASCII `GtIm` |
| `0x04` | 4 | version | `4` |
| `0x08` | ... | chunks | repeated 8-byte-header chunks |

## Top-level chunk

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `+0x00` | 4 | id | FourCC |
| `+0x04` | 4 | payloadSize | excludes this 8-byte header |
| `+0x08` | `payloadSize` | payload | chunk-specific bytes |

Observed IDs are `CRC0` and `Imag`. `CRC0` contains one 32-bit checksum. It is
optional; files can contain repeated checksum chunks, and the original loader
keeps the last value. If none exists, it can calculate a checksum after loading.

## `Imag` payload

| Offset | Size | Field |
|---:|---:|---|
| `+0x00` | 4 | `format` (`GT_FMT`) |
| `+0x04` | 4 | width |
| `+0x08` | 4 | height |
| `+0x0C` | 4 | palette entry count |
| `+0x10` | 4 | mipmap level count |
| `+0x14` | `paletteCount * 4` | 32-bit palette entries |
| following | computed | concatenated mip levels |

The original image-byte calculation is:

```text
baseBytes = width * height * bitsPerPixel(format) / 8
total = sum(baseBytes >> (2 * level), level = 0 .. mipmapLevels-1)
```

This reflects quarter-area mip levels. It uses format-derived sizes rather than
trusting the enclosing `Imag` size.

## Formats present in v1 content

| Value | Bits | Palette | Alpha | Selected-corpus variants | Meaning from converter code |
|---:|---:|---:|---|---:|---|
| `3` | 8 | 256 | no | 986 | palette index (`P8`) |
| `4` | 16 | 256 | yes | 1,007 | palette index + 8-bit alpha |
| `6` | 16 | 0 | yes | 3 | packed ARGB 4:4:4:4 |
| `7` | 24 | 0 | no | 986 | RGB 8:8:8 |
| `8` | 32 | 0 | yes | 1,008 | ARGB 8:8:8:8 |

There are 3,990 variants in 1,985 files. Normal textures usually contain a
compact paletted variant (`3` or `4`) paired with a true-color fallback (`7` or
`8`). `GtImage::LoadAs` selects the requested format; when allowed, it falls back
to formats 8 then 7 and converts.

## Shipped terminal-size quirk

Twelve supplied files declare a final `Imag` extent beyond physical EOF. The
original loader seeks to that declared end, terminating its scan, but reads
selected mip bytes using the computed size above. All twelve contain enough
physical bytes for every computed mip level.

The portable parser accepts this only for the terminal `Imag` chunk, rejects it
for metadata/other chunks, and still requires complete format-derived pixel
data. This is a documented compatibility rule, not a general truncation bypass.
Some font variants also contain trailing bytes after the computed mip data; the
runtime converter must ignore/preserve them deliberately after their meaning is
resolved.

## Portable mip-chain implementation

- parser and size model: `src/airfix/assets/LegacyFormats.*`;
- per-level layout: `describeGtiMipLevels`;
- bounded per-level decoder: `decodeGtiMipLevelRgba`;
- strict owned chain: `decodeGtiMipChainRgba`;
- compatibility entry point: `decodeGtiBaseRgba` delegates to level zero;
- bounded runtime descriptor: `render/TextureRuntimePlan` selects the variant,
  upload policy, levels, rows, and decoded/upload/resident byte budgets;
- atomic runtime materializer: `render/TextureRuntimeData` parses, plans, and
  decodes a supplied GTI byte span, then publishes the plan and owned upload
  levels together only after their shapes and byte totals agree;
- authenticated crosshair consumer: `content/LegacyWeaponCrosshairTextureSet`
  preflights the recovered MG/RO/BO archive entries, exact format/dimensions and
  aggregate budgets before decoding, then publishes all three only against one
  unchanged verified content transaction;
- bounded corpus tool: `udsp-list --inventory`;
- private diagnostic preview tool: `gti-preview` (PPM output under ignored paths);
- synthetic format and materialization cases: `tests/LegacyFormatsTests.cpp`
  and `tests/TextureRuntimeDataTests.cpp`.

The decoder has exact fixtures for formats 3/4/6/7/8, multiple levels, palette
and alpha behavior, declared and physical bounds, aggregate output limits, and
trailing data. It preserves the original quarter-area source offsets while
also reporting the nominal clamped dimensions required by a modern texture
API. A private format-8 aircraft texture preview is visually correct. Its
format-4 paletted variant differs from format 8 by only mean absolute RGB
1.70/1.56/1.69 (expected palette quantization), corroborating palette order and
channel mapping. Preview files remain ignored because they are derived from
original content.

## Corpus upload policy

The 3,990 selected variants declare 9,617 levels. For 3,974 variants, every
declared source level has exactly the texel count required by its nominal Metal
dimensions; those 9,523 levels are decoded and uploaded as the authored chain.
Sixteen variants contain a legacy quarter-area/clamped-dimension mismatch. For
those, only the valid base level is decoded and the remaining natural chain is
generated by Metal. The aggregate inventory therefore decodes 9,539 levels and
selects no undefined or zero-byte legacy level.

Canonical pixels are owned RGBA8 bytes with straight alpha and stored row
order. The first Metal backend uses `MTLPixelFormatRGBA8Unorm`, not the sRGB
variant: the source color space is not yet proven, so automatic transfer
conversion would be an unsupported semantic guess. It does not premultiply
alpha or flip rows/V coordinates.

For an exact chain, allocate the declared level count and copy each level to
its matching `mipmapLevel` with `bytesPerRow = width * 4`. For an anomalous
chain, allocate the natural full level count, copy level zero, and call
`generateMipmaps(for:)`. This follows Apple's documented
[`MTLTexture` mip-level upload](https://developer.apple.com/documentation/metal/copying-data-into-or-out-of-mipmaps)
and [`MTLBlitCommandEncoder` generation](https://developer.apple.com/documentation/metal/mtlblitcommandencoder/generatemipmaps%28for%3A%29)
contracts. A future texture cache keys the logical asset, selected GTI variant,
content checksum, pixel format, and authored/generated policy; cache
implementation and font-only trailing-byte semantics remain separate work.

`prepareGtiUpload` implements the portable half of this handoff. It enforces
the source-byte limit before parsing and forwards variant/metadata budgets into
the parser, where they are checked before retaining each record. It then
decodes all authored levels or the anomalous base only and verifies identity,
selected variant/format/checksum, mip-policy shape, per-level dimensions and
row/image bytes, plus decoded/upload/resident totals. Success returns one
`GtiUploadPreparation` containing both the canonical plan and owned RGBA8
levels. Parse, plan, decode, mismatch, limit, or overflow failures clear both,
so even corruption in a later mip cannot leak a partial chain. The eventual
Metal layer must still create/cache resources and generate lower mips when the
plan requests `generateFromBase`; color-space, alpha, row-orientation, and
blending policy remain separate evidence-gated decisions.

The weapon-crosshair consumer adds a stricter role-specific contract on top:
all three unique HUD entries must select format 8 at exactly `32x32`. Its
plan-only pass rejects aggregate RGBA overruns before pixel materialization;
the subsequent owned decode must reproduce the same plan byte-for-byte. Dense
IDs are local to the HUD set rather than aliases into a mission's scene texture
namespace. See
[EXP-20260731-089](../experiments/EXP-20260731-089-authenticated-crosshair-texture-set.md).
