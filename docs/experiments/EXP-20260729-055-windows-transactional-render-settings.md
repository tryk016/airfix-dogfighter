# EXP-20260729-055: transactional D3D11 presentation settings

- Date: 2026-07-29
- Status: implemented locally; hosted CI pending
- Scope: Windows D3D11 render-presentation snapshot and resource publication

## Question

Can the Windows renderer change render scale, scene presentation, diagnostics,
and visual-profile intent at runtime without ever publishing settings that do
not own a complete matching GPU-resource state, while retaining an insertion
point for later durable persistence?

## Transaction boundary

`AirfixD3D11Renderer` now owns one validated
`RenderPresentationSettings` snapshot. Applying a non-identical candidate:

1. validates and classifies the complete candidate;
2. rejects a missing output surface before mutation;
3. derives the native layout for the current physical output;
4. prepares a complete private D3D11 target bundle when scale changes away
   from 100%;
5. invokes an optional `noexcept` publication gate;
6. unbinds and exchanges resources without allocation; and
7. publishes the settings value.

The bundle contains the color texture, RTV, SRV, depth texture, DSV, and exact
render-target extent. Candidate ownership is local until step 6. If color,
RTV, and SRV succeed but the controlled late-failure point fires before depth
creation, normal `ComPtr` destruction discards the partial candidate while the
active bundle remains untouched.

At exactly 100% the prepared bundle is intentionally empty and publication
releases the old intermediate bundle. Rendering then targets the physical
DXGI backbuffer directly; this does not substitute a historical logical
resolution.

## Persistence insertion point

The optional publication gate runs only after all fallible GPU preparation and
before the renderer changes settings or resources. The later Windows settings
store can therefore execute:

```text
validate -> prepare GPU -> durable save gate -> no-fail renderer publish
```

A rejected gate produces a typed `publicationGateRejected` result. The
prepared candidate is destroyed and the exact previous snapshot and five D3D11
objects remain active. Session-only command-line and smoke transitions do not
provide a gate.

The gate is a Windows render-thread mechanism. The Metal backend will use an
equivalent prepared-candidate token so iOS storage can remain off the main
thread and revalidate drawable identity before commit.

## Resize and surface suspension

Scaled scene targets are independent of swapchain RTV/depth resources.
Resize prepares a replacement scaled bundle before releasing swapchain views
and publishes it only after the new swapchain targets exist. A controlled
scaled-target preparation failure:

- leaves the old output surface, settings, and exact scaled bundle active;
- records the requested physical extent as pending; and
- retries first before the next frame, then uses a bounded 1, 2, 4, ...,
  120-frame backoff after consecutive failures.

An explicit resize signal, successful publication, or zero-extent suspension
resets the retry schedule. Persistent allocation failure therefore continues
to render the last complete surface when one exists without creating and
destroying a partial GPU candidate every frame.

Zero physical extent suspends the output surface without deleting the scaled
bundle. Restore may reuse the preserved bundle when its extent still matches,
or prepare a complete replacement before publication.

Failure after `IDXGISwapChain::ResizeBuffers` begins remains a fatal native
surface/device error because D3D11 cannot roll the old backbuffer views back.
It does not partially publish a new settings snapshot or scaled bundle.

## Startup fallback

A rejected startup presentation override no longer terminates the product.
The shell logs only a stable issue category and continues with the renderer's
active complete snapshot. It does not reveal paths, content identifiers, or
resource details. Runtime smoke transitions remain strict and fail the test if
a requested candidate cannot be applied.

## Runtime evidence

The data-less native renderer test verifies:

- direct 100% state has no intermediate bundle;
- 100 -> 50 -> 200 -> 100 transitions render visible GPU output;
- resize at 50% and 200% publishes the expected physical and scene extents;
- two consecutive late scaled-resize failures preserve all five old COM
  identities; the next backoff frame performs no allocation and retains the
  old extent, then a later retry publishes the requested extent;
- zero-extent suspend/restore retains a compatible scaled bundle;
- surface-unavailable, invalid, late-allocation, and gate rejection preserve
  settings and exact resource identity;
- an accepted gate retry publishes a different complete bundle;
- Original 4:3 reaches the actual scene viewport used by D3D11;
- enabling diagnostics creates the output-resolution overlay resources and
  disabling it releases them without replacing scene targets; and
- Enhanced is retained in the snapshot without claiming modern visual passes.

The product smoke also performs a visible 100 -> 50 -> 200 -> 100 sequence and
checks each diagnostic render-target extent against the portable layout.

## Local validation

- clean MSVC 19.51/Ninja Windows product build: passed;
- clean Windows CTest: 100/100 passed;
- portable code-intelligence preset: 95/95 passed;
- public-source boundary: 482 files passed;
- `git diff --check`: passed apart from Git's existing LF-to-CRLF notices.

The Windows build used the pinned local SDL 3.4.12 source copy and did not
download or bundle original game content.

## Limits and next work

- The Windows durable store and sparse CLI-over-persisted precedence are not
  implemented in this slice.
- Metal still exposes independent setting fields and must receive the
  equivalent prepared snapshot transaction.
- The final settings UI, quality tiers, safe FOV control, and Enhanced
  lighting/material/post-processing stages remain pending.
- Runtime device-loss recovery and failures after swapchain buffer replacement
  are separate surface-recovery work; this experiment proves scaled-resource
  transaction safety, not complete D3D device recreation.
- No private texture, original asset, owner-derived capture, local content
  path, or content checksum is present.

## Related

- [ADR-0014](../adr/0014-render-presentation-settings.md)
- [Native render-scale targets](EXP-20260729-050-backend-render-scale-targets.md)
- [Frame diagnostics overlay](EXP-20260729-051-render-frame-diagnostics-overlay.md)
