# Level object scene assembly API

`buildLevelObjectSceneAssembly` is the renderer-facing, fail-atomic boundary
for appending authenticated Level `OBJE` visuals to an already-built room
model. It consumes sources in physical placement order, delegates selected
blueprint geometry and material construction to
`buildObjectVisualDrawAssembly`, derives every drawable node relative to the
selected authored root, and composes the result once with the external
position plus Z-X-Y-radian pose.

The input deliberately carries pointers to the already-authenticated
placement, object definition, and CCF metadata together with their manifest
indices and resolved target world-room identity. The API performs no path,
room-name, archive, or content lookup. A missing pointer, selector, room,
visual, finite transform, or budget invalidates the complete candidate; there
is no per-placement skip mode.

The output model retains the supplied room model as an exact prefix. Each
placement contributes its own contiguous mesh and instance ranges, including
when multiple placements use the same object definition. Object-only
provenance records the placement, load-source and unique-definition indices,
target room, blueprint and physical mesh identity, and final absolute mesh and
instance indices. No cross-placement sorting or mesh deduplication occurs.

The root scalar policy follows
[EV-20260808-001](../re/systems/LEVEL-OBJECT-ASSEMBLY.md): mesh roots use exact
`1.0f`, while null/light roots retain their authored scalar in the external
axis-rotation relation. This is semantic reconstruction, not a claim of
bit-identical x87 transform arithmetic or renderer draw-order parity. The
builder does not consume CCF `0x4000` placed-scene records and does not apply
the separate player `Y(pi)` pose.
