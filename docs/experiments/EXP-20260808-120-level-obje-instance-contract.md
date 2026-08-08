# EXP-20260808-120: Level `OBJE` instance contract

**Status:** statically recovered; publication is documentation-only

**Question:** What exact sequence turns one physical Level `OBJE` placement
into a scene subtree, including its external pose, room membership, and the
transform relationship between the selected blueprint root and its descendants?

**Source build:** the versioned PE32/i386 reference recorded by the public
source manifest

**Environment:** isolated worktree; Ghidra Headless 12.1.2 primary and Rizin
0.9.1/rz-ghidra independent cross-check; static analysis only

**Safety boundary:** no game binary, original asset, decompiler database,
local path, or runtime C++20 change is included. No game, debugger, or GUI was
run.

**Evidence:** [EV-20260808-001](../re/systems/LEVEL-OBJECT-ASSEMBLY.md)

## Prediction and prior evidence

The prediction was that a Level `OBJE` record selects one `0x3000` blueprint,
clones that root and only its descendants, then overwrites the clone root with
the record's external position and axis rotation before re-homing the complete
subtree in the named room. A placement must not be represented by a generic
`0x4000` placed-scene record or by applying the same external transform to
every authored node independently.

This work reuses, without repeating, the bounded Level layout in
[AFCHUNK](../formats/AFCHUNK.md#world-level-briefing-and-path-semantics), CCF
blueprint graph and texture/material rules in
[CCF](../formats/CCF.md#blueprint-hierarchy-and-object-instancing), room
publication from `EV-20260724-006`, and authored-world/parent-local formulas
from `EV-20260721-033`. It narrows their joining contract at the native
`NfMission::LoadLevel` call sites.

## Method and agreement

The selected paths were exported from a fresh headless analysis and independently
checked with normalized Rizin boundaries, direct calls, xrefs, field accesses,
and the relevant instruction sequences. Both tools agree on the following
end-exclusive boundaries:

| Function | RVA | VA range |
|---|---:|---|
| `NfMission::LoadLevel` | `0x0002D6F0` | `[0x1002D6F0, 0x1002E988)` |
| `CcBlueprint::MakeInstance` | `0x000244F0` | `[0x100244F0, 0x10024617)` |
| `CcSrtNode::SetParent` | `0x0000E0F0` | `[0x1000E0F0, 0x1000E26B)` |
| `CcSrtNode::GetWorldRelation` | `0x0000E440` | `[0x1000E440, 0x1000E4FC)` |
| `CcSrtNode::SetAxisRotation` | `0x0000E910` | `[0x1000E910, 0x1000E92F)` |
| `CcAxisRot::Set` | `0x0002BAF0` | `[0x1002BAF0, 0x1002BB07)` |
| `CcObject::SetRoom` | `0x00015260` | `[0x10015260, 0x100152AC)` |

## Recovered physical sequence

`NfMission::LoadLevel` iterates physical `OBJE` records in file order. For each
record, it reads six consecutive float32 values as `position.x`, `position.y`,
`position.z`, `axis.x`, `axis.y`, `axis.z`, followed by the room string and the
object-definition string. It resolves the definition's `CCFF`, loads it with
`0x2000`, obtains its `MESH` selector, resolves that selector through the
definition-local `0x3000` blueprint index, resolves the record's room through
`CcWorld::GetRoomByName`, and calls:

```text
root = CcBlueprint::MakeInstance(selectedBlueprint, missionWorld, true)
```

The loader then executes this order, not an inferred equivalent:

```text
root.position = record.position                 // +0x94, +0x98, +0x9C
root.SetAxisRotation()                          // +0x90 = axis mode
root.axis.Set(record.axis.x, record.axis.y, record.axis.z) // +0xD0..+0xD8
root.NotifyChange()
root.SetUniqueName(...)
root.SetRoom(resolvedRoom, true)
```

The external pose therefore overwrites the selected root's prototype pose; it
does not compose with it. `SetAxisRotation` selects the axis representation,
the following `CcAxisRot::Set` stores the three raw float32 values, and the
separate subsequent `NotifyChange` invalidates the relation cache. For the
default axis path, effective rotation is radians in Z-X-Y application order:
`Rz(rz)`, then `Rx(rx)`, then `Ry(ry)`.

The object CCF load keeps room/resource admission active but suppresses its
`0x4000` placed-scene table. The only instance source here is the selected
blueprint subtree. A missing selector, unresolved blueprint, failed instance,
or unresolved room skips this `OBJE` instance and continues the outer physical
loop; no receiver/root-room fallback is established for the placement.

### Room lookup is a native lookup, not a start resolver

The room string is wrapped as a `CcName` and passed to
`CcWorld::GetRoomByName` at RVA `0x000117B0`; no start-position selector or
portable resolver participates. Static control flow first compares the shared
root room by full `CcName` identity (name and prefix), then walks ordinary rooms
newest-created-first and compares their name component using ASCII
case-insensitive matching. The ordinary-room branch does not perform a
prefix fallback.

The native lookup has no ambiguity rejection: if multiple ordinary rooms match
under that fold, the first linked-list candidate wins, which is the newest
created matching room. A missing query returns no room and therefore takes the
already-observed placement-skip path. The shared mission root is void during
the observed object-load sequence, so it does not substitute for an unresolved
non-void placement string. The selected corpus has no matching ordinary-name
collisions; the collision rule is established by static traversal rather than a
corpus witness. This statement is deliberately independent of the distinct
mission-start resolver.

## Instance and room semantics

`CcBlueprint::MakeInstance` allocates the selected root according to its kind,
then recursively visits its child list in retained physical order. It selects
only that root and descendants: ancestors are excluded. A mesh root receives a
mesh object; null and light roots retain their respective clone paths. Every
new node initially belongs to `missionWorld`; `root.SetRoom(target, true)` then
rebinds the root and recursively rebinds children in child-list order.

`CcSrtNode::SetParent` first materializes each child's previous world relation,
then derives its local relation against its newly attached parent. In the port's
column-vector notation, for an authored selected root `A0`, authored selected
node `Ai`, and placement root `P`:

```text
relative_i.linear      = inverse(A0.linear) * Ai.linear
relative_i.translation = inverse(A0.linear) * (Ai.translation - A0.translation)
final_i                = P * relative_i
```

Thus the selected root is exactly `P`; each descendant retains its authored
relationship to that root. The source implementation uses row-vector algebra;
the formula above is its already-established runtime column-vector equivalent.

The raw scalar at `+0xCC` needs kind-sensitive handling. Mesh construction
sets the clone root scalar to exact `1.0f`; null/light clone paths retain their
source scalar, and `SetAxisRotation` does not reset it. The observed F050
authored-world matrix does not itself apply that scalar, but an axis-rotated
null/light placement can. No portable adapter may silently assign `1.0` to all
root kinds.

## Portability gate

**GO (semantic `LevelObjectSceneAssembly`):** a pure C++20 assembly boundary
may preserve physical `OBJE` order and source provenance; load only the
definition `0x3000` selection/subtree; derive descendant locals from authored
world transforms; apply one external Z-X-Y-radian root pose; retain the
mesh-versus-null/light scalar distinction; use the native root-then-newest
ordinary ASCII-fold room lookup for valid input; bind the resulting room; and
skip unresolved placements without aborting the mission. It should reuse the
existing geometry/material/texture assembly rather than duplicate it.

**NO-GO (bit parity):** exact native floating-point parity is not established.
Portable trigonometry and matrix arithmetic have not been compared under the
reference process's live x87 precision, rounding, exceptions, or denormal
policy. Neither native linked-list insertion order nor GPU draw order follows
from this evidence alone.

## Minimal later dynamic oracle

With a valid mission loaded in an isolated, read-only debug session, capture one
root and each descendant after `GetWorldRelation`: effective matrix and position
(`+0x58..+0x8C`), local position (`+0x94..+0x9C`), mode (`+0x90`), raw scalar
(`+0xCC`), axes (`+0xD0..+0xDC`), and current room (`+0xF4`). Pair those values
only with the six input float32 fields and selected-blueprint identity. Compare
the port's root and descendant transforms, room admission, and source order.
Include a null root with scalar other than `1.0` if one is available. Do not
patch memory, save a dump, or publish binaries or asset-derived data. A second
minimal lookup witness should compare root, missing, and available case-fold
duplicate ordinary-room queries without using the start resolver.

## Result

The prediction is supported at high static confidence for sequence, field
offsets, selected-subtree scope, room admission, axis order, and transform
composition. The runtime implementation remains intentionally unchanged by this
experiment.
