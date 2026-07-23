# Backend-neutral render data and first-model diagnostic

**State:** mesh/model payload, conservative first-room assembly, private CPU
diagnostic, and public synthetic Metal smoke path implemented; private
asset-backed Metal path pending

**Evidence:** `EV-20260721-030` through `EV-20260721-034`

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

## First-room draw-all boundary

`resolveFirstRoomDrawPlan` selects placed objects from the first receiver/root
room in physical order and never uses BSP as a visibility list. The plan maps
mesh slots and material/texture dependencies in first-use order.
`buildFirstRoomDrawAssembly` converts each unique mesh once and emits one world
instance per selected placed object. Stored F050 placed transforms are authored
world relations and are applied exactly once; prototype transforms and parent
links are not recomposed.

The builder accepts externally assigned `TextureAssetId` bindings so archive
lookup/upload ownership remains outside render conversion. It enforces both
per-mesh and aggregate mesh, instance, vertex, index, material, range, and byte
limits. Any plan, binding, transform, geometry, overflow, or limit issue leaves
the public `DrawModelPayload` empty. The F040 placed orientation is explicitly
unsupported until evidence establishes its meaning.

All 286 first rooms build with no issue, producing aggregate metadata-only
counts of 2,084 instances, 46,981 triangles, and 6,435 ordered ranges. These
payloads are validated in memory only; no private geometry is exported.

## Metal handoff

The data-less iOS shell now proves the first complete handoff with public
synthetic data. `AirfixMetalRenderer` consumes the exported `DrawModelPayload`,
explicitly repacks vertices and column-vector transforms into a documented
CPU/GPU ABI, uploads one shared mesh and RGBA8 texture, then draws two instances
and their ranges in source order. The offline `.metal` source is compiled into
the application `default.metallib`; bundle verification requires it to exist.

This smoke pipeline uses BGRA8 color, Depth32Float with less/write, nearest
clamp sampling, no blending, and no culling. It does not yet load AFPACK or
private textures. Remaining backend work is resource ownership/cache handles,
the authored/generated mip upload policy, final projection/front-face and
blending decisions, then the asset-backed room path. The reference-oriented
pass keeps culling disabled until parity diagnostics establish viewport parity.
