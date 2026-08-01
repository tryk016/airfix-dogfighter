# EXP-20260801-101: Metal aircraft health-gauge encoder

**Status:** authenticated Metal encoder implemented; ordinary-frame submission
remains disconnected

**Evidence ID:** `EV-20260801-001`
**Decision:** GO for the dormant provenance-gated Metal backend; NO-GO for
manufactured live health or an ordinary HUD draw

## Question

Can iOS consume the same authenticated health-gauge packet as D3D11 while
preserving the recovered four-point geometry, layer order, blend split, depth
state, and native-output mapping without inventing a gameplay producer?

## Existing inputs

The iOS mission transaction already owns the exact two-texture gauge set under
one immutable `AirfixMetalRoomResources` snapshot. The common C++20 submission
from EXP-20260801-100 binds every textured command to that set's revision,
transaction identity, role, and dense HUD-local texture ID. It also validates
the required background, one-or-more mask, and optional foreground order before
publication.

No logical path or source byte crosses the Objective-C public boundary. The
Metal resources remain private, heap-accounted, and owned for the complete
published-room lifetime.

## GPU ABI and shaders

`GpuGaugeUniforms` is the sole CPU/Metal ABI for this pass:

| Field | Offset | Size |
|---|---:|---:|
| four physical-output points | 0 | 64 bytes |
| native output extent | 64 | 16 bytes |
| normalized ARGB tint | 80 | 16 bytes |

Compile-time checks require total size 96 bytes, 16-byte alignment, and the
three exact offsets. The Metal vertex shader indexes each supplied four-point
quad as triangles `0,1,2` and `0,2,3`; it does not collapse the recovered
annular mask into an axis-aligned rectangle. Physical output pixels are mapped
directly to clip space, so the legacy 640x480 domain remains UI metadata rather
than an intermediate render target.

Two immutable render pipelines preserve the recovered state:

- texture commands sample full UV with linear clamp, white tint, source-alpha
  / inverse-source-alpha blending, and the authenticated background or
  foreground texture;
- mask commands use the supplied opaque `0xFF000000` tint with blending
  disabled and no texture;
- both use compare ALWAYS with depth writes enabled.

## Fail-closed encoding

Before opening a Metal render encoder, the backend revalidates all of the
following:

- installed room, gauge set, GPU texture array, command buffer, output target,
  and depth target are present;
- the packet belongs to the exact installed gauge transaction;
- the GPU texture array has the exact role count and every referenced dense ID
  resolves to a non-null texture;
- all four points of every command are finite and inside the native output;
- output and depth textures match the declared extent and required pixel
  formats;
- command count is nonzero and representable by Metal's host index type.

Any failure returns zero draw calls before opening an encoder. A successful
pass uses one encoder, switches only between the two prebuilt pipelines, and
submits two triangles per command in the packet's original order. No partial
prefix is drawn after a validation failure.

## Deliberate disconnection

The encoder is prepared in the native backend but is not called by the ordinary
`MTKView` frame callback. There is still no verified changing AirCraft producer
for displayed health, inactive state, or refresh/render ordering. Supplying a
fixed value to normal gameplay would turn a renderer milestone into invented
simulation behavior.

The bounded x32dbg protocol in
`docs/toolchain/AIRCRAFT-HEALTH-GAUGE-CAPTURE.md` remains the evidence gate for
that producer. A future caller may pass the already authenticated common packet
to the encoder without changing shaders, texture ownership, UI scaling, or the
native-resolution scene path.

## Validation contract

- Existing synthetic submission tests cover exact command order, state,
  identity, native mapping, UI scaling, forged packets, and atomic failures.
- The portable and complete Windows suites must remain unchanged.
- Hosted `iphoneos` and `iphonesimulator` builds must compile both the
  Objective-C++ encoder and the Metal shader library.
- Public-boundary scans must continue to reject private images, packages,
  captures, and local paths.

## Decision

**GO** for the authenticated dormant Metal encoder and its immutable pipeline
state.

**NO-GO** for ordinary-frame gauge rendering, claimed device visual parity, or
live health integration until the producer evidence and physical-device
validation exist.
