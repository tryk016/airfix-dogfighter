# Backend-neutral render data and first-model diagnostic

**State:** mesh/model payload, conservative explicit-room assembly, stable
runtime texture plans, atomic RGBA8 upload preparation, fail-closed draw
submission, private CPU diagnostic, public synthetic Metal bootstrap, and
two-phase private asset-backed Metal room publication implemented; physical
device visual acceptance pending

**Evidence:** `EV-20260721-030` through `EV-20260721-034`,
`EV-20260727-002`

The representative static cross-check in
[EXP-20260727-001](../../experiments/EXP-20260727-001-static-tool-crosscheck.md)
confirms that Ghidra and Rizin independently select the exact
`FN-CC-0001FDD0` instruction range and direct calls. Ghidra remains canonical
for its MSVC `__thiscall` signature, named globals, and recovered types.

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

## Source-aware mission-room boundary

The mission-level seam does not concatenate explicit physical-room assemblies.
`resolveMissionWorldRoomDrawPlan` scans each ordered `LoadSceneCcf` source once
and preserves physical placed-node order inside it. A transient source-local
room-reference table models `CcLoadedRoom` rebinding: when multiple physical
records resolve to one runtime room, only the last wrapper remains addressable.
Unresolved references select the receiving root once during that source scan.
This prevents both contributor-group reordering and fallback duplication.

`MissionCcfRoomLoadSource::placedSceneEnabled` records the observed `0x2000`
mask behavior. Definition CCF loads still contribute room records but their
placed tables publish no instances. Mesh, material, node, and room references
never cross a `sourceIndex`. Equal local values in two sources remain distinct,
and aggregate preflight covers all rooms and meshes scanned even when `0x20`
suppresses room publication but leaves placed resolution active.
while one physical mesh reused by several selected nodes in the same source
receives one global first-use mesh slot.

`buildMissionWorldRoomTextureBindings` re-resolves the canonical room plan and
requires every texture-source CCF identity to match the exact load-source list.
It authenticates each supplied logical CCF path against its UDSP file index,
resolves every source with its own `TEXU`, and then remaps the existing
single-source binding plans into one dense global `TextureAssetId` namespace.
IDs follow canonical first use; an identical archive file index from the
authenticated session is deduplicated across sources, materials, and roles.
Equal local material references in different CCFs remain separate. This is a
metadata-only identity check: the caller must parse each CCF and bind its
logical path/file index in the same immutable load transaction. The binder
does not prove the provenance of independently constructed `CcfMetadata`.
It reads no payload and any late identity, resolution, binding, overflow, or
limit failure clears all renderer-facing output atomically.

`MissionLoadManifest` establishes the authenticated metadata input that will
precede this renderer boundary. Its builder consumes one
`VerifiedContentSession` and publishes a move-only, private-constructor proof
carrying the exact `ContentRevision`. Move transfers its valid state and
invalidates the source; `belongsTo()` requires validity and an equal revision.
For the construction transaction, the builder also pins the opaque identity of
the exact authenticated handle. That token is not retained: `belongsTo()`
intentionally accepts a separately authenticated session for the same content
revision. The caller supplies `setupLogicalPath` and the Level logical path
independently. The setup lookup is exact and has no Level-name inference,
extension search, sibling search, or path fallback.

The setup AFS, Level, World, and unique physical Level `OBJE` definition reads
use the one guarded handle. A bounded, non-executing parser recognizes only the
exact `AddStartPos` form and enforces the legacy 16-entry capacity. The proof
owns the canonical setup path/index, checked setup-source footprint, and parsed
start records; raw AFS bytes and all parser views are released. Exact unique
lookup then proceeds through Level to World, main World `CCFF`, optional first
`BCKD`, and each `OBJE` definition to its `CCFF`; an `OBJE` whose parsed
definition root is `MODL` is rejected.

CCF load descriptors preserve legacy order: main World, optional backdrop, then
one descriptor for every `OBJE` in physical placement order. Repeated
descriptors are not deduplicated, although repeated object-definition archive
indices reuse one parsed cache entry. Level `MODL` paths are resolved only to
owned metadata identities; their payloads are not read and they do not enter
the CCF load-source list. Main/backdrop descriptors retain flags `0`; object
descriptors retain `0x2000`, enabled room sections, and disabled placed-scene
publication. The transaction preflights compressed definition source peaks,
aggregate unique definition bytes, each planned CCF source, aggregate planned
CCF bytes charged per descriptor including repeats, descriptor count, and
semantic published CPU ownership. Cancellation, callback failure, or any late
lookup/read/parse/type/limit failure returns no manifest. Resolution is
cancellable between each World/`OBJE`/`MODL` item, and every progress callback
is surrounded by exact handle-identity and revision checks. The same guard runs
before publication, so replacement or move-out returns
`sessionIdentityChanged` even if the visible revision is unchanged.

The manifest intentionally reads no CCF or GTI payload. It does not by itself
authenticate independently constructed renderer metadata.

`MissionWorldRoomLoader` now supplies the provenance-preserving composition
boundary. It accepts no caller-built CCF metadata, catalogue, load source, or
texture source. Instead it revalidates each manifest descriptor against the
same authenticated revision, pins the exact loader-session marker, reads and
parses unique CCF entries through that handle, and constructs all pointer-bearing
source lists locally after cache addresses are stable. Repeated descriptor
loads remain repeated catalogue and draw sources even when their physical CCF
payload is cached once.

The loader selects a room through `resolveMissionStartsInWorld` and
`selectMissionWorldStart` using only the manifest-owned start records. Its
public request has no caller-provided start span. A successfully authenticated
setup with no start records takes the root-room fallback. The loader builds the
global selected-room bindings, prepares all GTIs, and passes those bindings into
`buildMissionWorldRoomDrawAssembly`. The owned model, numeric provenance,
texture upload data, and validated `DrawSubmissionPlan` are published together
under one `ContentRevision`; the result also carries the canonical setup
identity and setup-source footprint. Raw AFS, working CCF metadata, and views
are destroyed. Every callback and payload read is surrounded by session
identity/revision checks. Source-admission, RGBA, assembly, submission, and
semantic published-CPU limits fail closed. Retained CCF metadata is measured
by a separate post-parse logical counter; it does not claim to cap peak
allocation or process RSS beyond the parser's existing hard caps.

`buildMissionWorldRoomDrawAssembly` consumes those per-source bindings and
produces one `DrawModelPayload` plus parallel numeric mesh/instance provenance.
It fails atomically on a late source, transform, binding, limit, or
forged-catalog error, re-resolves the plan from the canonical catalog and exact
load sources, and requires every draw-source CCF identity to match that list
before allocation. The existing `DrawSubmissionPlan` accepts the combined
model directly. `MissionWorldRoomLoader` now connects the authenticated
manifest to this complete portable CCF/texture/binding/draw path. Native
mission publication, retained scene data for later room changes, and applying
the selected start room/pose to the reconstructed player remain unimplemented.

## Metal handoff

The data-less iOS shell now exercises a bounded multi-mesh/multi-instance
adapter with public synthetic data. `AirfixMetalRenderer` first obtains a
complete `DrawSubmissionPlan`; it does not independently reinterpret model
ranges or accept a partial plan. The fixture contains two reusable meshes and
three instances in deliberate mesh-slot order `1 -> 0 -> 1`. Each command
uses its exact `instanceIndex` transform and rebinds the corresponding
per-mesh vertex and index resources before drawing.

Vertices and column-vector transforms are explicitly repacked into the
documented CPU/GPU ABI. Preparation is two-pass: all mesh upload counts,
vertex/index byte lengths, command offsets and range ends, instance/mesh/range
relationships, and `NSUInteger` conversions are checked before vertex
repacking or Metal-buffer creation. Each buffer must fit
`device.maxBufferLength`; total logical GPU resources and peak packed CPU
vertices have separate 64 MiB ceilings. Empty/no-draw meshes are represented
by strongly retained per-mesh wrappers with nullable buffers, so no zero-length
Metal allocation is attempted and no command can reference a missing resource.

Texture asset ID zero remains a valid real asset and binds the generated
2-by-2 RGBA8 texture. Missing primary textures and
`TexcoordMode::none` bind a separate explicit one-pixel RGBA8 fallback; those
states are not rewritten in `DrawModelPayload` or `DrawSubmissionPlan`.
Pipeline, depth, sampler, mesh buffers, both textures, the immutable resource
snapshot, payload, plan, and checked offsets are constructed before renderer
state is published. C++ allocation/conversion failures are contained at the
Objective-C initializer boundary and reported as `NSError`.

The smoke policy remains BGRA8 color, Depth32Float with less/write, UInt32
indices, nearest/clamp sampling, no blending, and no culling. It makes no new
sRGB, premultiplication, V-origin, projection, front-face, blending, or BSP
visibility decision. The offline `.metal` source is still compiled into
`default.metallib`, which bundle verification requires.

The portable content layer supplies an authenticated, move-only, single-handle
AFPACK/`Resource.up` session tagged with the exact
`{generation, size, digest}` revision. The native coordinator adopts the ready
`ActiveContentLease` into that session on its serialized worker; the package
handle is never transferred to the main or render threads. The first native
smoke request selects the logical asset `Game/Worlds/axis_1.world` and explicit
physical CCF room index 1.

`WorldRoomLoader` composes exact World/CCF dependencies, that explicit physical
room, every GTI preparation, geometry, and draw submission into one immutable
result carrying the session revision. A monotonic request serial and the full
content revision gate both the worker callback and final publication. Replaced
requests, lifecycle invalidation, reinspection, and any generation, size, or
digest mismatch fail closed. The Objective-C CPU snapshot owns the complete
loaded result and allows it to be moved into renderer preparation only once.

Private Metal replacement is two-phase. `prepareLoadedRoom` must run off-main
and leaves the published snapshot untouched while it validates and creates
every mesh buffer, RGBA8 texture, authored mip upload, generated mip chain,
checked index offset, payload, and draw-submission owner. Generated mip work is
committed and required to complete before the candidate is returned. Main then
revalidates the publication ticket and `publishPreparedRoom` performs a single
atomic strong-pointer assignment. A candidate is tied to its renderer and
device, can be published once, and cannot partially replace the old room.

Private preparation has a 128 MiB packed-CPU ceiling, a 256 MiB logical-GPU
preflight ceiling, a separate 256 MiB descriptor heap-plan ceiling, and
per-buffer `device.maxBufferLength` checks. Logical byte preflight is a
best-effort early rejection. Preparation then queries
`heapBufferSizeAndAlign` and `heapTextureSizeAndAlign` with the exact lengths,
options, and descriptors that allocation will use. It builds checked
sequential placement plans, including every reported resource-alignment gap
and final resource alignment, for separate shared, tracked,
automatic-placement buffer and texture heaps. The combined descriptor plan is
admitted by the shared 384 MiB aggregate heap-admission ledger before
`newHeap` is called. Buffers and textures are allocated only from those heaps;
shared buffer contents are populated with `memcpy`. The snapshot retains the
heaps together with their suballocated resources.

The descriptor plan is not claimed as a hard physical ceiling. Apple's Metal
contract permits `MTLHeapDescriptor.size` to be rounded to a page boundary, but
exposes no documented pre-creation upper bound for that rounding on the iOS
16.4 baseline. Aligning a plan to the maximum resource alignment therefore
does not establish a documented heap-page bound. Immediately after all heap
resources have been created, preparation reads each retained heap's
`currentAllocatedSize`. If the measured total is below the admitted plan, the
move-only token reconciles downward. If it is above the plan, the difference
must obtain a separate reservation from the same aggregate ledger and is then
absorbed into the candidate's token without changing the aggregate. A
candidate whose supplement is unavailable is never returned or published.

Accepted snapshots charge the measured `currentAllocatedSize` of each retained
heap exactly once. Individual resource `allocatedSize` values are
validation-only and are not added again. Between `newHeap` and the immediate
measurement/supplement decision, page rounding can transiently make the Metal
allocation exceed the admitted descriptor plan. That exposure is bounded in
lifetime to scoped preparation and in ownership to the unpublished
candidate's fixed heap set, but no undocumented byte bound is claimed. Every
failure drains the scoped autorelease pool and destroys staging resources and
heaps before the plan reservation is consumed. The public bootstrap follows
the same sequence with a 64 MiB logical and descriptor-plan policy. Its
fallback texture has its own heap, and the final measured debit is split
without changing the aggregate so that heap and the bootstrap snapshot have
independent lifetime tokens.

Each frame reads one immutable snapshot. Its command buffer completion handler
retains that snapshot and the budgeted persistent fallback owner until the GPU
has finished using the referenced buffers and textures. When the final owner
releases a retired snapshot, destruction of its large Objective-C arrays and
C++ payload is dispatched to a serial off-main release queue. Suballocated
resources are released before their retained heaps, and the ledger debit is
released only after both are destroyed and the surrounding autorelease pool
has drained. Prepared candidates, the published snapshot, retired snapshots,
and snapshots retained by in-flight command buffers therefore all remain
charged.
Discarding a stale prepared candidate follows the same teardown path. The
snapshot's reservation token retains the ledger state, so deferred teardown
remains safe after its renderer is gone. Publication transfers the already
reserved candidate with one atomic strong-pointer assignment; it is
budget-neutral and constant-time. The closed flag and reserved-byte count
occupy one atomic CAS word, so close, reserve, reconciliation, and token
consumption share one linearizable synchronization domain. Reservation tokens
are move-only and single-consumption; moved-from, repeated, and zero-byte
consumption are idempotent. Direct upward reconciliation leaves its debit
charged and permanently closes the ledger to new snapshots; the documented
post-creation page-rounding path instead obtains and absorbs a separately
admitted token.

## Dynamic instance-pose boundary

`airfix::render::ScenePoseExchange` is the portable transport seam for future
dynamic actors. It does not mutate `LoadedWorldRoom`, rebuild a Metal resource
snapshot, or identify any instance as the player. One published frame contains
only a simulation step and a sorted, unique, bounded list of absolute
runtime-world `Mat3`/`Vec3` overrides keyed by the exact instance index of one
scene.

Every exchange mints an opaque handle for one scene generation. A private
shared control block is the identity: an equivalent content revision or a new
allocator object at the same address cannot revive a stale handle. Beginning a
new scene invalidates the earlier generation. Destroying the exchange closes
its handles, while an already acquired lease keeps its slot storage alive and
readable until the lease releases it.

Two slots are allocated to their configured maximum at construction. Publish
and acquire perform no allocation, explicit mutex operation, or wait. A
generation-tagged front plus a per-slot exclusive writer-claim bit prevents an
older reader from registering against a slot while it is being reused. Reader
counts saturate rather than wrap. If the required back slot is leased, publish
returns `busy`; simulation continues and the renderer retains the last
complete frame. It never consumes a partial transform list.

Before claiming a slot, publish validates the scene identity, strictly
increasing simulation step, override and byte ceilings, in-range indices,
strict index order, and finite matrix/translation values. Any rejection leaves
the previous front and step domain unchanged. A read lease resolves an exact
override by binary search or returns the caller's authored transform
byte-for-byte. Any `std::span` borrowed from a lease is valid only while that
same lease remains active.

This primitive intentionally stops before native integration. Static evidence
now identifies the player's primary actor and the selected skin's up to three
complete blueprint hierarchies; weapons and auxiliary effects in the broader
owned-UID graph remain separate. The current room payload still represents
static placed objects and does not publish those dynamic hierarchies as
instance indices. No code may select a player instance by first index, name
similarity, `sourceNodeReference`, or the entire owned-UID heap. The remaining
join requires an explicit dynamic actor-to-instance publisher.

An external private-content smoke run completed the writer, installation,
inspection, same-handle lease adoption, and full room load. The immutable
result contained 62 meshes, 62 instances, 23 dense textures, and 167 validated
draw commands. These are aggregate observations only; no private content,
converted package, geometry, texture, or generated artifact was committed.
Physical-device rendering and visual acceptance remain pending.
