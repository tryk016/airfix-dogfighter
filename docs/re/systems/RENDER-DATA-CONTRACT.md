# Backend-neutral render data and first-model diagnostic

**State:** mesh/model payload, conservative explicit-room assembly, stable
runtime texture plans, atomic RGBA8 upload preparation, fail-closed draw
submission, private CPU diagnostic, and public synthetic Metal smoke path
implemented; private asset-backed Metal path pending

**Evidence:** `EV-20260721-030` through `EV-20260721-034`

## Texture lookup

CCF material texture strings are base names, not archive paths. Objects provide
the search root in `TEXU`. `GtTextureGroup::AddTexture` at `Cc.dll` RVA
`0x00046C30` formats ordinary image candidates as `%s\\%s.gti`; the alternate
boolean path selects `.tga`. CCF loading calls the ordinary path.

`resolveTextureEntries` therefore joins `TEXU`, one separator, the exact
material source text, and `.gti`, then uses bounded UDSP normalization and
legacy lookup. It never searches alternate roots or treats an already-present
extension specially. Missing roots, unsafe paths, missing entries, ambiguity,
and configured limits are typed failures. Object wrappers retain the same
behavior. Complete selected object subtrees resolve all 2,959 texture edges
uniquely under this rule.

## Runtime texture and upload plans

World texture resolution first verifies that the world's `CCFF` path maps to
the supplied CCF source-entry index, then internally derives the canonical
explicit-CCF-room dependency plan. This comparison is metadata-only: the
loader must create the parsed CCF and its source index in one immutable
transaction. The resolver does not claim that an ordinal alone authenticates
payload provenance.

`buildTextureBindingPlan` accepts the expected dependencies and the complete
texture-entry resolution. Any upstream issue, missing/extra entry, or mismatch
in role, material reference/index, or source text fails closed. Successful
entries receive dense `TextureAssetId` values in first-use order, deduplicated
by archive entry across all materials and primary, secondary, and environment
roles. ID zero is valid; absence remains `std::optional`.

`describeGtiUpload` performs no I/O and makes no sRGB, premultiplication, or
V-origin decision. It selects formats in recovered preference order
`8, 7, 4, 3, 6`. Exact legacy mip layouts upload every authored level;
dimension-anomalous quarter-area chains upload only level zero and allocate the
natural clamped chain for backend generation. Dimensions and declared mip
counts retain the parser's hard 32,768/16 bounds even if configurable budgets
are raised. All RGBA8 row, decoded, upload, resident, and metadata arithmetic
is checked before a plan is published.

`prepareGtiUpload` is the owned-data boundary above that descriptor. It checks
the source-byte ceiling before parsing and passes variant/metadata budgets into
the parser, which enforces them before retaining records. It parses the supplied
GTI byte span, derives the canonical `GtiUploadPlan`, and decodes either every
selected authored mip or only level zero for `generateFromBase`. Before
publishing, it checks request identity, variant index/format, checksum,
mip-policy shape, every level number/dimension/row byte/image byte count, and
the aggregate decoded, upload, and resident RGBA8 totals against the plan.

The returned `GtiUploadPreparation` is atomic: a successful result contains the
plan and all uploadable owned `RgbaImage` levels together. A parse, planning,
decode, mismatch, limit, or overflow issue clears both fields and reports a
typed issue with plan, variant, and level context where available. A corrupt
later authored mip therefore cannot expose an earlier partial chain. Generated
lower mips are deliberately absent from the payload and remain work for the
render backend. This boundary still makes no sRGB, premultiplication, vertical
orientation, blending, archive-I/O, or Metal resource decision.

Across all 215 object plans, 2,959 edges become 614 per-object unique imports:
612 authored-chain and two generated-chain imports, with 2,355 uploaded and
2,368 allocated levels. All 29 world-to-CCF bindings and all 135 physical CCF
rooms also validate. Explicit selection assigns all 4,911 objects exactly once
to ordinary rooms; receiver rooms contain no selected object.

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

## Fail-closed draw submission

`buildDrawSubmissionPlan` is the validation boundary between an assembled
`DrawModelPayload` and backend upload/draw work. Before allocating output, it
preflights mesh, instance, aggregate vertex/index/material/range, command, and
source-byte ceilings with checked arithmetic. It then validates finite vertex
attributes and transforms; finite, ordered bounds that contain every vertex;
the required zero bounds for an empty mesh; in-range indices, instance mesh
slots, material slots, and texture IDs; known texture-coordinate modes; and a
contiguous, complete partition of the index buffer into non-empty triangle
ranges.

Only complete success publishes a `DrawSubmissionPlan`. Mesh-upload metadata
retains model mesh order, while indexed commands retain instance order and then
range order. Each command preserves primary, secondary, and environment
bindings independently; `TextureAssetId{0}` is a valid binding whenever at
least one texture asset is available, while absence remains `std::optional`.
An empty model and a well-formed empty mesh produce successful empty work.
Every typed issue suppresses the whole plan and carries the narrowest available
mesh, instance, vertex, index, range, material, texture-role, and texture-ID
context. No partial upload list or draw-command prefix is exposed.

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
group rather than one mesh. The bounded blueprint resolver now follows the
exact selected-node-and-descendants `MakeInstance` contract. A multi-instance
`DrawModelPayload` applies each authored world transform once and shares one
auto-fit projection and z-buffer across all meshes.

The resulting private aircraft diagnostic contains 46 nodes, 25 mesh
instances, 663 triangles, 42 materials, and seven primary textures. It renders
as one coherent assembly and repeats byte-identically. Its markings also
distinguish the UV policies: preserved raw V is correct for this path, while
the explicit flipped-V comparison is visibly wrong. The private outputs and
hashes remain ignored and uncommitted.

## Explicit-room draw-all boundary

`resolveRoomDrawPlan` selects placed objects from one caller-supplied physical
index into `CcfMetadata::rooms`, in physical placed-node order, and never uses
BSP as a visibility list. The index is not an index or ID from
`WorldDefinition::rooms`, a CCF reference, or a name match. The receiver
fallback belongs only to physical CCF room zero. The plan maps mesh slots and
material/texture dependencies in first-use order.
`buildRoomDrawAssembly` converts each unique mesh once and emits one world
instance per selected placed object. Stored F050 placed transforms are authored
world relations and are applied exactly once; prototype transforms and parent
links are not recomposed. The original first-room functions remain thin room-
zero compatibility wrappers.

The builder accepts externally assigned `TextureAssetId` bindings so archive
lookup/upload ownership remains outside render conversion. It enforces both
per-mesh and aggregate mesh, instance, vertex, index, material, range, and byte
limits. Any plan, binding, transform, geometry, overflow, or limit issue leaves
the public `DrawModelPayload` empty. The F040 placed orientation is explicitly
unsupported until evidence establishes its meaning.

All 392 rooms across 286 CCFs build with no issue. A per-node coverage bitmap
proves that all 6,995 placed objects appear in exactly one plan. The aggregate
metadata-only result contains 6,995 instances, 170,039 triangles, and 20,211
ordered ranges. These payloads are validated in memory only; no private
geometry is exported.

## Metal handoff

The data-less iOS shell now proves the first complete handoff with public
synthetic data. `AirfixMetalRenderer` consumes the exported `DrawModelPayload`,
explicitly repacks vertices and column-vector transforms into a documented
CPU/GPU ABI, uploads one shared mesh and RGBA8 texture, then draws two instances
and their ranges in source order. The offline `.metal` source is compiled into
the application `default.metallib`; bundle verification requires it to exist.

This smoke pipeline uses BGRA8 color, Depth32Float with less/write, nearest
clamp sampling, no blending, and no culling. It does not yet load AFPACK or
private textures. The portable layer now provides validated draw commands and
owned upload levels, but the Metal shell does not consume those boundaries yet.
Remaining backend work is resource ownership/cache handles, wiring authored
uploads and generated mip requests, final projection/front-face and blending
decisions, then the asset-backed room path. The reference-oriented pass keeps
culling disabled until parity diagnostics establish viewport parity.
