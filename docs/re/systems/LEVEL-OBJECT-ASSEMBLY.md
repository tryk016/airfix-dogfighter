# EV-20260808-001 — Level `OBJE` scene-instance assembly

**State:** static contract recovered; portable runtime assembly not implemented

**Source:** [EXP-20260808-120](../../experiments/EXP-20260808-120-level-obje-instance-contract.md)

## Confirmed native contract

The PE32 `NfMission::LoadLevel` implementation at RVA `0x0002D6F0`
(`0x1002D6F0..0x1002E988`) consumes Level `OBJE` records in physical file order.
Each record is exactly six sequential float32 values, room string, and
definition string. The order is position XYZ, axis XYZ, room, definition.

For each resolvable record, the loader loads the definition CCF with `0x2000`,
selects its `MESH` from the `0x3000` blueprint table, resolves the named room,
and calls `CcBlueprint::MakeInstance(selected, missionWorld, true)` at
`0x100244F0..0x10024617`. The load mask preserves room/resource handling and
suppresses `0x4000` placed-scene publication. No Level `OBJE` instance comes
from the definition's placed-node table.

The returned root is mutated in this exact sequence: write XYZ to `+0x94`,
`+0x98`, `+0x9C`; call `SetAxisRotation` at `0x1000E910..0x1000E92F`; store
axis XYZ through `CcAxisRot::Set` at `0x1002BAF0..0x1002BB07` into
`+0xD0..+0xD8`; notify; set the native unique-name field; and invoke virtual
`SetRoom(resolvedRoom, true)`. The external root pose overwrites prototype-root
position and rotation. It is not a transform composition with that root.

An absent selector, unresolved selected blueprint, failed clone, or unresolved
room admits no placement and continues the outer loop. This path has no
receiver-room fallback.

The room query is a `CcName` constructed from the `OBJE` room string and is
passed directly to `CcWorld::GetRoomByName` (RVA `0x000117B0`), not to mission
start selection. Native lookup checks the shared root with full name/prefix
comparison, then ordinary rooms newest-created-first with ASCII
case-insensitive name comparison only. It has no ambiguity error: the first
matching ordinary linked-list node wins. A missing match returns no room and
therefore skips the placement. In the observed object-load sequence the shared
root has a void name, so it is not an implicit fallback for a non-void room
query. The no-collision corpus result is not used to weaken this explicit static
collision rule.

## Subtree, rooms, and transforms

`MakeInstance` clones the selected root and descendants only. Mesh, null, and
light clone paths are distinct; each child is recursively made before
`CcSrtNode::SetParent` (`0x1000E0F0..0x1000E26B`) attaches it. Child traversal
retains the source physical order. New nodes start in the mission world;
recursive `SetRoom(..., true)` moves the completed subtree to the selected
room. The evidence proves admission order, not a renderer draw order.

`SetParent` materializes the child world relation, attaches it, and derives a
local relation. `GetWorldRelation` (`0x1000E440..0x1000E4FC`) composes parent
first. In the runtime's column-vector convention, selected-root authored world
transform `A0`, node authored world transform `Ai`, and placement root `P`
produce:

```text
local_i.linear      = inverse(A0.linear) * Ai.linear
local_i.translation = inverse(A0.linear) * (Ai.translation - A0.translation)
world_i             = P * local_i
```

For the selected root, `world_0 = P`. Default axis rotation is radians and
applies Z, then X, then Y. Root scalar is not universal: mesh construction
produces `1.0f`; null/light clone paths preserve `+0xCC`; axis conversion does
not reset it. F050 authored-world matrices themselves do not establish a
universal scalar application rule.

## Confidence and gates

High static confidence: function boundaries, six-field record order, `0x2000`
mask, selected-root/descendant scope, overwrite order, room skip behavior,
Z-X-Y radians, field offsets, and the column-vector translation of native
parent-relative composition.

Medium/conditional: the effect of native unique-name contents and native room
list insertion on later render order. Unknown: live x87 control/exception
state, bit-exact trigonometry/matrix behavior, and any renderer ordering beyond
the proven scene-graph traversal.

**GO:** semantic, fail-closed `LevelObjectSceneAssembly` that preserves the
stated order, placement skip rules, kind-aware scalar policy, room binding, and
subtree-relative transforms. Its authentic-content lookup must model
root-first/full-name then newest-ordinary/name-only ASCII-fold traversal; a
separate stricter importer policy for duplicate rooms must be labelled as a
port policy, not native parity.

**NO-GO:** bit-identical transform math or renderer-order compatibility until a
read-only runtime oracle captures post-`GetWorldRelation` root/descendant
matrices, locals, axes, scalar, mode, and room against the six source float32
fields. A second minimal lookup witness should compare an existing root query,
missing query, and any available case-fold duplicate ordinary-room query without
using the start resolver. The oracle must not patch memory, retain dumps, or add
private data to the repository.
