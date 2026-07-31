# EXP-20260731-089: authenticated weapon-crosshair texture set

**Status:** implemented and locally validated

**Evidence ID:** `EV-20260731-004`

**Confidence:** high (`3`) for the recovered logical roles, exact archive
entries, selected format, dimensions and mip count; high (`3`) for the portable
authentication, validation and atomic-publication contract

## Question

Can the three recovered weapon sights be materialized from owner-provided
content without loose files, host-path lookup, scene-texture ID collisions, or
partial publication, while leaving unproved live weapon-slot mapping and GPU
submission explicit?

## Evidence

The static evidence recorded in
[EXP-20260731-088](EXP-20260731-088-legacy-weapon-crosshair-projection.md)
identifies three data-driven GTI roles under the in-game HUD texture root:
`MGsight`, `ROsight`, and `BOsight`.

A read-only owner-local inventory of the original `Resource.up` independently
confirmed that each logical entry is unique and contains both format-4 and
format-8 variants at exactly `32x32`, with one authored mip. The existing
variant policy selects format 8. No archive bytes, decoded pixels, inventory
output, local path, or derived image are repository material.

## Implemented boundary

`LegacyWeaponCrosshairTextureSet` loads all three roles through one unchanged
`VerifiedContentSession`. The fixed recovered logical names are archive keys;
they are never opened as host paths. Before pixel decoding, the loader:

1. requires a unique archive match for each role and distinct physical entries;
2. validates stored/unpacked source footprints and aggregate source limits;
3. parses bounded GTI metadata and creates a plan-only upload description;
4. requires selected format `8`, base dimensions `32x32`, and a non-empty mip
   plan;
5. checks prospective decoded, upload and resident RGBA totals;
6. materializes the GTI and requires the resulting plan to match the preflight
   plan exactly; and
7. rechecks the authenticated stream identity and content revision before
   publishing the complete set.

The three dense `TextureAssetId` values are explicitly local to this dedicated
HUD set. They are not scene-texture IDs and cannot be joined to a room texture
namespace by numeric value. `valid()` rechecks role order, logical names,
source uniqueness, request identity, format, dimensions, mip-policy shape,
per-level RGBA layout and all byte totals. `belongsTo()` additionally binds the
result to the exact authenticated stream handle, not merely an equal digest or
generation.

Any lookup, read, parse, plan, format, dimension, budget, cancellation,
callback, allocation, identity or internal failure clears the complete public
payload. A malformed single sight therefore cannot publish the other two.

## Verification

Synthetic AFPACK/UDSP/GTI tests cover:

- the exact three logical roles, dense HUD-local IDs, selected format,
  dimensions, checksum retention and RGBA channel order;
- same-revision/different-handle rejection and forged published metadata;
- missing and ambiguous archive entries with role attribution;
- malformed GTI, a supported but inauthentic format-3 variant, and wrong base
  dimensions;
- per-source, source-footprint, aggregate-source, decoded, upload and resident
  budgets;
- limits incapable of admitting the authentic `32x32` contract;
- pre-requested cancellation, callback containment, same-revision handle
  replacement, and moved-from session rejection.

The production inventory smoke uses only the owner's original archive and
performs no write. Public tests use exclusively generated synthetic content.
A second private smoke exercised the implemented loader through an existing
owner-local AFPACK and `VerifiedContentSession`: it published exactly three
valid format-8 `32x32` one-mip textures with `12,288` decoded and resident RGBA
bytes. The throwaway driver, package, and output remain outside Git.

A fresh GCC/Ninja Release build passes all `126/126` portable CTests. A fresh
MSVC 19.51/Ninja Release build of the Windows SDL3/D3D11/XAudio2 product passes
all `136/136` CTests, including both native product smokes.

## Decision

**GO** for the authenticated, bounded, all-or-nothing CPU texture set and
role-based lookup.

**NO-GO** for claiming a rendered crosshair. The native aircraft's actual
selected weapon/type mapping, live aim/collision producer, visibility policy,
GPU resource ownership, blend state, and D3D11/Metal sprite submission remain
separate evidence and implementation gates.
