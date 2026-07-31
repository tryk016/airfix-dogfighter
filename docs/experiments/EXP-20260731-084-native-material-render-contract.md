# EXP-20260731-084: native material render contract

**Evidence ID:** `EV-20260731-003`

**Status:** implemented data boundary; backend rendering deliberately deferred
**Confidence:** high (`3`) for CCF layout, native field offsets, blend mapping,
texture-stage operations, depth modes, and culling; medium (`2`) for the
higher-level meaning of lighting mode and `0x2151`

## Question

Which serialized CCF material values reach the original render backend, and
which minimum backend-neutral fields must the C++20 reconstruction retain
before D3D11 or Metal can reproduce that behavior?

## Method

Ghidra 12.1.2 was the primary static-analysis source. Rizin 0.9.1 independently
checked function boundaries and the relevant instructions in working copies of
`Cc.dll` and `gtDirect3D.dll`. No original binary was modified or executed.

The checked source-copy SHA-256 values were:

- `Cc.dll`:
  `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF`;
- `gtDirect3D.dll`:
  `D7C25DC5D53EE717D5092D465137F18756D8C7951EDAB868892E2AC3DC48D4F3`.

Private corpus inspection reported aggregate values only. No logical material
name, texture path, binary, disassembly database, or extracted asset enters the
repository.

## Native layout and CCF mapping

`GtMaterial` occupies the first 20 bytes of `CcMaterial`.
`GtMaterial::GtMaterial` at Cc RVA `0x71A0` initializes its two bytes, blend
word, and three texture pointers to zero. `CcMaterial::ResetMembers` at RVA
`0xBF10` additionally establishes:

| Native offset | Reset value | CCF source |
|---:|---:|---|
| `+0x00` | false | `0x2150` byte 1 |
| `+0x01` | false | `0x2151` byte 0 |
| `+0x04` | `0` | `0x2150` u32 |
| `+0x08/+0x0C/+0x10` | null | `0x2110/0x2111/0x2120` |
| `+0x3C` | `0` | `0x2150` byte 0 |
| `+0x40` | `1.0f` | final f32 in `0x2140` |
| `+0x44..+0x4C` | three `1.0f` | first `0xF030` in `0x2140` |
| `+0x50..+0x58` | three `1.0f` | second `0xF030` in `0x2140` |
| `+0x5C` | `0` | first u32 in `0x2152` |
| `+0x60/+0x64` | two `0.5f` | final two f32 in `0x2152` |

`CcRoom::LoadSceneCcf` at RVA `0x27340` performs these assignments.
`CcRoom::SaveSceneCcf` at RVA `0x262B0` independently confirms the reverse
mapping. The higher-level meaning of the two vectors, their scalar, numeric
lighting values, and `0x2151` remains deliberately unnamed.

All 10,385 private material records use authored blend mode `0` or `3`:
7,487 use `0` and 2,898 use `3`. Modes `1` and `2` are nevertheless native
runtime modes used by L3D/effect and multipass paths and therefore remain part
of the recovered contract. The corpus contains 10,306 Gouraud and 79 flat
records. These counts are compatibility observations rather than public asset
data.

## Original backend state

The concrete material vtable slot resolves to `gtDirect3D.dll` RVA `0x3890`,
range `[0x3890,0x3BE4)`.

| Material blend mode | Blend enable | source | destination |
|---:|---:|---|---|
| `0` | off | unchanged | unchanged |
| `1` | on | `INVDESTCOLOR` | `ONE` |
| `2` | on | `ZERO` | `SRCCOLOR` |
| `3` | on | `SRCALPHA` | `INVSRCALPHA` |

Material byte `+0x00` selects flat when zero and Gouraud when nonzero.
`+0x3C` gates programmatic vertex lighting. The values at `+0x40..+0x58`
participate in vertex-colour calculation. `+0x01` is copied and compared, but
this investigation did not prove its visible render effect.

Primary, secondary, and environment texture pointers occupy `+0x08`, `+0x0C`,
and `+0x10`. Active fixed-function stages modulate colour successively. Stage
zero modulates alpha; later stages retain current alpha. Hardware without the
required multitexture path flushes later textures as separate passes using
blend mode `1`.

Independent render-state checks establish:

- hardware culling is `D3DCULL_NONE`; polygon facing is rejected earlier in
  software;
- active Z uses ordinary Z buffering;
- depth modes are `GEQUAL/write`, `ALWAYS/write`, `GEQUAL/no-write`, and
  `ALWAYS/no-write`;
- minification and magnification follow the bilinear option;
- mip filtering is explicitly point, not linear;
- no alpha-test state is set by the inspected backend;
- texture address and fog setters were not found, so their effective runtime
  values are not claimed.

## Transparent-list and render-phase contract

`CcObject::AddVisiblePolygonsToRenderList` at RVA `0x1FDD0` sends a polygon to
the sorted list when material blend mode is nonzero or its separate force
argument is true. For a triangle with un-clipped camera-space depths `z0..z2`,
the native instruction order is:

```text
q   = _ftol((z2 + z1 + z0) * cameraFarInv * bitcast_f32(0x4CAAAAAB))
key = 0x80000000u - uint32(q)
```

The constant represents the centroid-depth scale conceptually equivalent to
`2^28 / 3`, but parity must retain the displayed x87 sum and multiply order.
Sprites and custom objects enter the same per-`RenderRoom` list with
`cameraZ * cameraFarInv * bitcast_f32(0x4D800000)`. Thus the queue cannot be
split by item kind.

`CcLinkSort::Sort` at RVA `[0x19FA0,0x1A0AC)` is a four-pass LSD radix sort
whose net order is unsigned-key ascending. At ordinary positive camera Z this
is back-to-front. It is stable relative to its input chain, but every producer
prepends, so equal-key items retain reverse discovery order. Opaque material
grouping is bypassed for this list; an `A/B/A` sequence must remain `A/B/A`.

`CcCamera::RenderRoom` drains opaque phases first. It then selects native depth
mode three (`GREATER_EQUAL`, depth write disabled), renders a pre-sorted layer,
sorts the shared triangle/sprite/custom queue, calls `PostSort` to drain that
queue, and drains the following layer. `PostSort` is the sorted-item drain, not
a separate hook after an earlier drain. Sorting is scoped to each room/portal
render call, not globally to a frame.

The formula, unsigned direction, stability, prepend tie behavior, cross-kind
queue, and depth phase are high-confidence static results. Exact `_ftol`
behavior at half boundaries, overflow, NaN, and infinity remains conditioned
on the live x87 control/exception policy.

This proves that simply enabling blend factors in the current backend is
insufficient. A later implementation needs triangle-level sorted payloads,
runtime camera-space keys, cross-kind ordering, and an explicit numeric policy.
Therefore this experiment does not change D3D11 or Metal output.

## Implemented boundary

The portable parser now exposes typed optional records for `0x2140`, `0x2150`,
and `0x2151`, and retains all three `0x2152` values. `DrawMaterialState`
contains the renderer-relevant recovered values with the exact native reset
defaults. World-room, aggregate mission-room, and player-actor binding paths
convert source-local physical materials in the same order as their references.

`buildTextureBindingPlan` requires a one-to-one state/reference mapping when a
state span is supplied and fails atomically on mismatch. The state then travels
with `DrawMaterial` into each `DrawSubmissionCommand`, alongside all three
texture roles. Existing generic callers that do not own CCF metadata receive
the native reset defaults.

`LegacySortedRenderQueue` implements the recovered `CcLinkSort::Sort` ordering
for already-keyed, caller-owned triangle/sprite/custom references. It accepts
one native input-chain order, rejects invalid kinds and explicit item/working-
memory limit violations before allocation, and performs four stable unsigned
LSD byte passes. Equal keys retain input-chain order; native reverse-discovery
ties remain a producer-side consequence of prepending. The helper that maps an
already-quantized signed depth to a key uses exact unsigned arithmetic but does
not perform or emulate `_ftol`.

Neither native backend consumes the new state or queue yet, so this boundary
cannot cause a partial or incorrectly ordered transparency implementation.

## Synthetic verification

Tests cover:

- exact distinct f32 vectors/scalars and all `0x2150/0x2151` fields;
- optional absence without parser synthesis;
- both trailing `0x2152` scalars;
- native reset defaults;
- exact CCF-to-render-state conversion;
- state/reference cardinality rejection including an explicitly empty state
  span for a non-empty material list;
- texture-binding and draw-command propagation;
- fail-closed rejection of non-finite material scalars/vectors with material
  context;
- unsigned ordering across all four key bytes, stable mixed-kind ties,
  caller-owned identity, empty/single inputs, and exact item/working-memory
  limits for the portable sorted queue.

A fresh Windows GCC/Ninja build completes and all 121 portable CTests pass. An
isolated native MSVC 19.51/Ninja build links the Windows product and passes all
131 CTests, including both D3D11 product smokes. Exact commit `88235e8` also
builds all 376 steps in a clean exported source tree under the dedicated
`Airfix-Dev` WSL2 GCC 13.3/Ninja environment and passes all 121 CTests.
Synthetic public-boundary tests, the 626-file repository scan, the
340-row/14-column unique function catalogue, and `git diff --check` pass. Two
independent reviews report no code finding; their sole P2 identified stale
validation provenance in this expanded report, which is corrected by this
paragraph. Hosted platform builds remain the publication gate for the queue
addition.

## Decision

**GO** for retaining the recovered material state through the public C++20
parser, binding, mesh, and draw-command boundary.

**GO** for the implemented per-room unsigned sorted-item queue over already
prepared native-chain items and already-quantized keys.

**NO-GO** for enabling native-backend blend or multitexture behavior in this
slice. The current range-level draw plan cannot reproduce triangle-level
ordering, and bit parity for `_ftol` remains numeric-policy-conditioned. PBR
names, universal sRGB classification, alpha testing, address modes, and
semantic names for unresolved fields must not be inferred from this evidence.
