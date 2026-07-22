# FMT-GTI — `GtImage` texture container

**State:** header, chunk table, image metadata, palette placement, pixel-size
calculation, and base-level RGBA8 conversion implemented; full mip export pending

**Confidence:** 3/3 for fields below across the selected 1,985-file corpus

**Evidence:** `EV-20260721-017` through `EV-20260721-019`

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

## Portable implementation and next step

- parser and size model: `src/airfix/assets/LegacyFormats.*`;
- base-level RGBA8 converter: `decodeGtiBaseRgba`;
- bounded corpus tool: `udsp-list --inventory`;
- private diagnostic preview tool: `gti-preview` (PPM output under ignored paths);
- synthetic malformed cases: `tests/LegacyFormatsTests.cpp`.

The decoder has exact one-pixel fixtures for formats 3/4/6/7/8 and decoded the
base level of all 3,990 selected-corpus variants during inventory. A private
format-8 preview of `AcSpitfire.gti` is visually correct. Its format-4 paletted
variant differs from format 8 by only mean absolute RGB 1.70/1.56/1.69 (expected
palette quantization), corroborating palette order and channel mapping. Preview
files remain ignored because they are derived from original content.

Next, expose every mip level as canonical RGBA8 while preserving alpha and the
original quarter-area byte calculation, then define the Metal upload format and
texture cache. Font-only trailing bytes remain a separate research item.
