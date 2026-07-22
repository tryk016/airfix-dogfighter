# Backend-neutral render data and first-model diagnostic

**State:** mesh payload and private CPU diagnostic implemented; Metal backend pending

**Evidence:** `EV-20260721-030` through `EV-20260721-032`

## Texture lookup

CCF material texture strings are base names, not archive paths. Objects provide
the search root in `TEXU`. `GtTextureGroup::AddTexture` at `Cc.dll` RVA
`0x00046C30` formats ordinary image candidates as `%s\\%s.gti`; the alternate
boolean path selects `.tga`. CCF loading calls the ordinary path.

`resolveObjectTextureEntries` therefore joins `TEXU`, one separator, the exact
material source text, and `.gti`, then uses bounded UDSP normalization and
legacy lookup. It never searches alternate roots or treats an already-present
extension specially. Missing roots, unsafe paths, missing entries, ambiguity,
and configured limits are typed failures. All 210 selected-corpus texture edges
resolve uniquely under this recovered rule.

## Draw mesh payload

CCF positions use shared indices, while UVs and recovered flat normals belong
to triangle corners. Sending the CCF index buffer directly to a modern GPU
would merge seams. `buildDrawMesh` emits a deterministic backend-neutral
payload whose split key is:

```text
source vertex index + UV presence/bits + normal bits
```

Signed zero is canonicalized. Material is deliberately not part of the key.
Materials retain first-use order; contiguous draw ranges split on material or
UV-mode changes without globally regrouping triangles. Thus source order such
as `A, B, A` remains three ranges, preserving a safe basis for later blending
research. Indices remain 32-bit and bounds cover emitted local positions.

The builder rejects non-finite values, invalid indices, missing/duplicate
material bindings, integer overflow, and configured vertex/index/material/range
or aggregate-byte limits. The payload keeps primary, secondary, and environment
texture roles even though the first diagnostic samples only primary.

## CPU diagnostic

`rasterizeDiagnostic` is a deterministic validation backend, not a replacement
for Metal. It applies the recovered model linear transform and translation,
auto-fits an orthographic three-quarter view, rasterizes both windings with a
z-buffer, and samples primary RGBA8 textures with nearest filtering. Missing
primary textures/UVs receive a stable diagnostic color. V flip is an explicit
option and remains off by default.

The first private mesh-backed object produced a complete, correctly assembled
and textured silhouette: 111 triangles became 291 seam-safe vertices, six draw
ranges, three materials, and two primary textures. Both raw-V and flipped-V
images were generated for comparison; neither public code nor documentation
claims a final V-origin decision from this one object. Private PPM/PNG outputs
remain below ignored `artifacts/private-model-preview/` and are not committed.

The aircraft object selected for the next visual milestone resolves to a null
group rather than one mesh. Rendering the complete aircraft therefore depends
on the placed-node hierarchy/transform contract; selecting an arbitrary child
mesh would not be a faithful shortcut.

## Metal handoff

The Metal backend can upload `DrawMeshPayload` without repeating asset-format
logic. Remaining backend decisions are buffer ownership/cache handles, shader
layout, projection/depth convention, final front-face/culling state,
alpha/blending behavior, sampler choice, and authored/generated mip upload.
The first Metal pass stays unlit/reference-oriented and keeps culling disabled
until parity diagnostics establish final viewport parity.
