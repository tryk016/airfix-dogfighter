# EXP-20260731-086: Enhanced scene-texture sampling

**Status:** implemented and locally validated

**Confidence:** high (`3`) for the portable policy, D3D11 binding, sampler
separation, and Windows comparison; medium (`2`) for Metal runtime behavior
until physical-device inspection

## Question

Can the persisted Classic/Enhanced profile become a real, bounded visual
choice without changing simulation state, texture content, UI sharpness, or
the final render-scale presentation policy?

## Design

The portable renderer now maps each validated visual profile to one explicit
scene-texture sampling policy:

- Classic uses nearest minification, magnification, and mip selection with an
  anisotropy count of one. This preserves the reconstructed renderer's
  established output and still consumes complete authored GTI mip chains.
- Enhanced uses linear minification and magnification, linear mip blending,
  and a fixed maximum anisotropy of eight.

The policy is backend-neutral and rejects forged modes, invalid non-anisotropic
counts, and anisotropy outside the portable 2-16 range. D3D11 and Metal create
both immutable scene samplers during renderer initialization and select one
from the already-published presentation snapshot for each frame. Diagnostic
and product UI overlays retain a dedicated point sampler; scaled-scene
presentation retains its separate linear sampler. No sampler is shared across
those three domains.

This slice does not classify texture color spaces, alter texture content,
generate new mipmaps, enable HD replacements, or add lighting, materials,
shadows, MSAA, or post-processing. It changes neither fixed-step simulation nor
camera, draw order, geometry, or resource identity.

## Verification

Synthetic tests cover both exact profile mappings, forged profile and mode
values, every declared sampling mode, anisotropy boundaries, and atomic
diagnostic rejection of profile/policy mismatches. Native D3D11 tests prove
that a successful profile transaction changes the bound scene policy and the
published diagnostics while existing layout, UI, target, and failure-retention
tests continue to pass.

Two owner-local captures of the same authenticated mission were generated at
exact 1920x1080 output, a 1920x1080 render target, and 100% render scale. Both
submitted the same 278 scene draw calls and 1,311 triangles and reported the
same estimated 25.0 MiB GPU memory. The diagnostic overlay identified `Classic
/ TEX POINT` and `Enhanced / TEX ANISO 8X` respectively. A byte comparison of
the uncompressed pixel payload found changed color bytes in 14.05% of the
frame, while visual inspection confirmed unchanged composition and smoother
oblique room surfaces in Enhanced. Both captures and all source content remain
ignored private files.

The complete portable suite passes 123/123 tests. The native MSVC 19.51/Ninja
Windows product build succeeds and passes 133/133 tests, including the D3D11
renderer and both product smokes. Hosted iPhoneOS and iPhoneSimulator builds
remain the publication gate for the Objective-C++ Metal path; physical-device
visual inspection remains separate.

## Decision

**GO** for Classic point/mip-point and Enhanced trilinear 8x anisotropic
scene-texture sampling as the first pixel-affecting visual-profile stage.

**NO-GO** for inferring complete Enhanced rendering, exact original-game
filtering, global sRGB/linear classification, or physical-device Metal quality
from this result. Those remain separate evidence and implementation slices.
