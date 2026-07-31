# EXP-20260731-090: weapon-type crosshair binding

**Date:** 2026-07-31

**Evidence ID:** `EV-20260731-005`

**Status:** implemented portable type-to-sight binding; native sprite submission pending
**Confidence:** high (`3`) for the nine registered type names, type-loader sight
selection, `WpParaMine` no-sight result, and secondary-slot transition; medium
(`2`) for any later claim about simultaneous primary/secondary composition or
draw order because no live render trace was taken

## Question

Which of the three authenticated `MGsight`, `ROsight`, and `BOsight` textures
belongs to each concrete native weapon type, how does the aircraft change its
selected secondary weapon, and can that knowledge be represented without
inventing a global HUD mode or weakening content provenance?

## Sources and method

The verified `AirCraft.type` working copy has SHA-256
`9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E`.
The verified `Projectiles.type` working copy has SHA-256
`639D9F07DD954457CED364E0F1ED2732309819B493422FDAE827D00DC1D76B9F`.
No binary, decoded texture, local path, or generated analysis database is
repository material.

Ghidra 12.1.2 supplied the canonical MSVC class/vtable interpretation and
typed pseudocode. A new reusable headless exporter locates indirect calls by
one or more exact vtable displacements. Rizin 0.9.1 plus `rzpipe` 0.6.2 then
opened the same hash-verified copies independently and emitted normalized
instruction reports. Function creation was requested only after Ghidra had
independently fixed each entry address; it was in-memory and read-only.

Rizin agrees with these relevant end-exclusive ranges:

| Function | VA range | Bytes |
|---|---:|---:|
| secondary weapon transition | `[0x10007600, 0x1000763E)` | 62 |
| shared bomb/atomic-bomb sight load | `[0x10006A80, 0x10006AE8)` | 104 |
| machine-gun sight load | `[0x1000A620, 0x1000A69A)` | 122 |
| para-mine load | `[0x1000CC70, 0x1000CC93)` | 35 |
| particle-beam sight/effect load | `[0x1000E4B0, 0x1000E607)` | 343 |
| shared rocket/cannon/missile sight load | `[0x1000F680, 0x1000F6E8)` | 104 |
| Tesla-coil sight/effect load | `[0x10010CD0, 0x10010E27)` | 343 |

## Recovered mapping

`Projectiles.type::typeCreate` registers exactly these weapon names in this
order. The concrete type vtables join each name to its `Load` implementation;
the loader writes its returned texture reference to inherited type field
`+0x4C`.

| Registered type | Recovered sight | Portable role |
|---|---|---|
| `WpMGun` | `MGsight` | `machineGun` |
| `WpRocket` | `ROsight` | `rocket` |
| `WpBomb` | `BOsight` | `bomb` |
| `WpCannon` | `ROsight` | `rocket` |
| `WpTeslacoil` | `ROsight` | `rocket` |
| `WpParticleBeam` | `ROsight` | `rocket` |
| `WpMissile` | `ROsight` | `rocket` |
| `WpParaMine` | none | `noSight` |
| `WpABomb` | `BOsight` | `bomb` |

The `WpParaMine` constructor installs a type vtable whose `Load` slot points to
RVA `0x0000CC70`. That function only delegates to `AfWeaponType::Load` and sets
the loaded byte; unlike all sight-bearing loaders, it does not add the HUD
search path, request a sight name, or write type field `+0x4C`. Its missing
sight is therefore an affirmative result, not a failed string search.

The previously recovered `AfWeapon::RenderCrosshair` reads the texture already
attached to its concrete instance at `+0x100`; it does not select a global
MG/RO/BO mode. The representative `WpMGun` constructor copies the type-level
reference from `+0x4C` into that instance field. The portable boundary retains
the same per-type decision but does not reproduce native pointer ownership.

## Aircraft secondary selection

The function at AirCraft RVA `0x00007600` accepts a non-null candidate. If it
differs from selected-secondary field `AirCraft+0x494`, it performs this exact
transition:

1. when an old secondary exists, call its fire slot `+0x34` with false;
2. call old weapon slot `+0x24`, which resolves to `AfWeapon::Deactivate`;
3. store the candidate at `AirCraft+0x494`; and
4. call candidate slot `+0x28`, which resolves to `AfWeapon::Activate`.

The primary weapon remains a separate pointer at `AirCraft+0x490`. The
evidence therefore rejects a single global selected-sight variable: future
HUD composition must bind primary and selected-secondary instances
independently. Static evidence alone does not yet prove whether both successful
plans are drawn in one frame, their final order, or a suppression policy.

## Portable boundary

`LegacyWeaponTypeCatalog` publishes the exact nine case-sensitive registration
identities and an optional sight role. It allocates nothing, performs no path
normalization, and returns null for invalid enums or identifiers.

`bindLegacyWeaponCrosshairTexture` accepts one concrete weapon type, the
already authenticated three-texture set, and the exact
`VerifiedContentSession`. A successful result carries:

- weapon type and recovered sight role;
- the dedicated HUD-local `TextureAssetId`;
- content revision; and
- the opaque identity of the exact authenticated stream transaction.

`WpParaMine` returns the valid `noSight` outcome without depending on private
texture availability. Invalid type values, forged texture-set metadata, and a
same-revision texture set from another stream handle publish no binding. The
caller invokes the binder independently for primary and selected-secondary
slots; this slice does not invent a live weapon-instance owner or render order.

## Verification

Synthetic AFPACK/UDSP/GTI tests cover all nine exact names and mappings,
case-sensitive/invalid lookup, every successful role and HUD-local ID,
`WpParaMine` no-sight behavior, same-revision/different-stream rejection,
forged texture-set rejection, retained transaction identity, and `noexcept`
binding. The production code reads no additional private data.

Ghidra's new displacement exporter was exercised against the complete
`AirCraft.type` program and recovered the `+0x24`, `+0x28`, and `+0x34` calls
inside the selection function. The seven selected Rizin reports match the
instruction bytes and relevant boundaries; the para-mine report independently
contains no sight-reference write.

A fresh GCC 15.2/Ninja Release build completed 403/403 build steps and passed
126/126 portable CTests. A separate fresh MSVC 19.51/Ninja Release build
completed 689/689 build steps and passed 136/136 CTests, including the SDL3,
D3D11, and XAudio2 Windows product plus both native product smokes. The public
repository-boundary, formatting, catalogue, and diff checks are publication
gates for the same change.

## Decision

**GO** for the exact type catalogue and authenticated per-weapon sight binding.

**NO-GO** for claiming a visible or parity-complete crosshair. Live primary and
secondary instances, changing aim/collision input, visibility/composition
policy, GPU resource ownership, alpha blending, and D3D11/Metal sprite
submission remain separate gates.
