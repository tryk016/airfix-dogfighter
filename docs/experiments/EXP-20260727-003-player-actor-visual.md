# Player actor visual construction

**Date:** 2026-07-27
**Evidence:** `EV-20260727-004`
**Scope:** primary actor creation, vehicle skin discovery, visible blueprint
hierarchy, and actor-local transforms

## Question

Determine whether the player's aircraft is part of the mission's placed-node
scene, how the selected visual is sourced, which hierarchy is initially
visible, and how its authored transforms become actor-local transforms. No
original asset name, private configuration value, binary payload, or local
installation path may enter the public record.

## Method

Ghidra 12.1.2 Headless decompiled exact address-selected functions from
read-only working copies of the reference modules. Existing headless reports
for blueprint cloning and scene-graph parenting were cross-checked at their
recovered function boundaries. A dedicated scalar report verified the
rotation constant. Binary Ninja, the game executable, and GUI automation were
not used.

Generated decompiler reports, projects, and source binaries remain in ignored
local directories. The immutable reference hashes are recorded only in the
public source manifest.

## Result

The mission room draw path contains only placed `0x4000` scene nodes. Player
spawn separately creates a public actor of the selected type. The vehicle type
loads a model CCF, discovers named skin texture groups, and resolves three
literal blueprint roots:

```text
slot 0: thirdperson
slot 1: damaged
slot 2: destroyed
```

`AfVehicle::SetSkin` requires slot 0, deletes the previous three complete
hierarchies, and calls `CcBlueprint::MakeInstance(..., true)` for each present
slot. Every new root is parented to the actor transform, assigned local
translation zero, and rotated around legacy Y by the promoted `float` value
of pi. All three hierarchies are hidden recursively. Only slot 0 is then
unhidden, receives a dynamic BSP, and receives the actor UID recursively.

The blueprint clone preserves each node's effective authored-world transform
before rebuilding the hierarchy. Because `SetParent` preserves that world
result, descendant locals are derived relative to the selected prototype root.
`SetSkin` subsequently replaces only the cloned root's local pose. For a
mesh-bearing blueprint `i`, the portable column-vector form is:

```text
relative_i   = inverse(authored_selected_root) * authored_i
actorLocal_i = recovered_root_Y_pi * relative_i
world_i      = player_spawn_world * actorLocal_i
```

All terms are converted through the same source-to-runtime basis. The selected
root need not be an identity transform; using each authored-world transform
directly or applying the spawn transform twice is incorrect.

Runtime clones do not retain enough source identity for reconstruction.
Blueprint references are CCF-local, null clones discard their blueprint
pointer, and the actor UID is applied only to visible slot-0 mesh objects.
Portable provenance must therefore be minted while traversing the exact
authenticated CCF and must include its source identity, slot, blueprint index
and reference, physical mesh index, and final runtime instance identity.

## Confidence and remaining unknowns

Confidence is 3/3 for the separate actor hierarchy, slot literals, slot-0
initial visibility, complete descendant cloning, root zero translation,
legacy Y(pi) rotation, and recursive slot-0 object ID assignment.

Transitions to the damaged and destroyed variants, animated propeller nodes,
stickers, vapor anchors, weapons, effects, and the final portable actor-ID
lifecycle remain outside this experiment.

## Portable acceptance

The first vertical slice is deliberately limited to slot 0. The generic object
visual builder retains converted authored-world transforms. A separate actor
adapter must derive every node relative to the selected root, apply the
recovered root-local pose, retain typed provenance, and fail atomically.
Absolute spawn placement is a later, single composition step.
