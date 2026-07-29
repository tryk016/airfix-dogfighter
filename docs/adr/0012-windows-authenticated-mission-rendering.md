# ADR-0012: authenticated mission rendering on Windows

- Status: accepted and implemented
- Date: 2026-07-29

## Context

The Windows x64 product is the rapid-debug and parity environment for the
shared reconstruction. Its initial D3D11 shell deliberately rendered only a
synthetic scene so public builds and CI never required original game data. The
portable content layer can now authenticate an owner-created AFPACK, build an
explicit mission manifest, and publish one bounded `LoadedMissionWorldRoom`.
Windows needs to consume that result without weakening the data-less boundary,
mixing content revisions, or reverting to DirectX 7 behavior.

AFPACK v1 intentionally contains no mission launch catalogue. Selecting a
mission by searching for similar filenames would make startup ambiguous and
would bypass the explicit provenance contract already used by iOS.

## Decision

The Windows application accepts an owner-local content root and, optionally,
an explicit setup logical path, Level logical path, player-object logical path,
and unsigned start index. The setup and Level pair is mandatory whenever any
mission option is present. Values are runtime inputs only: public presets,
tests, workflows, and source files remain empty and data-less.

One `VerifiedContentSession` owns the entire launch transaction:

1. load and validate all six aircraft audio clips;
2. build the exact mission manifest;
3. load the selected room, geometry, texture data, spawn pose, and collision
   assets;
4. verify that the content revision is unchanged; and
5. prepare every D3D11 resource before replacing the visible scene.

The renderer independently rebuilds and compares the portable draw submission
plan at its native boundary. It creates immutable vertex/index buffers, dense
RGBA8 shader resources, every authored mip level, and backend-generated mip
chains where requested. Failure leaves the prior public diagnostic scene
installed.

The mission pipeline uses a separate HLSL gameplay vertex shader. It preserves
the recovered scalar camera projection as homogeneous clip values, supplies a
far-plane clip distance, clears reverse depth to zero, compares
greater-or-equal, and aspect-fits the shared 640x480 logical canvas into the
current drawable. D3D11 and DXGI execute the result; no DirectX 7 API is
emulated.

ADR-0013 subsequently classifies this 640x480 aspect-fit as a temporary
parity/reference presentation. It is not the target render resolution or the
default widescreen policy. The production path must render at the selected
physical extent at 100% render scale and use Hor+ by default.

`--smoke-test` remains exclusive, synthetic, and proprietary-data-free. The
private hidden validator additionally renders one authenticated mission frame,
reads the actual D3D11 back buffer, and requires visible non-clear geometry.
An exclusive owner-local capture option can write that checked frame to a new
top-down BGRA8 BMP. It refuses existing outputs; screenshots are private
derived content and remain outside Git.

## Consequences

- Windows and iOS now consume the same authenticated mission-room and camera
  contracts through separate native Metal and D3D11 backends.
- Public CI continues to prove the native graphics path without possessing or
  discovering private content.
- A private owner can validate parsing, GPU preparation, HLSL projection, draw
  submission, textures, audio, and a visual comparison frame in local
  commands.
- Mission selection remains explicit until a future authenticated AFPACK
  version defines a bounded launch catalogue.
- The rendered room is not yet gameplay. The camera and actor remain at the
  authenticated bootstrap state until the reconstructed simulation publishes
  changing pose and camera generations.
- Private logical paths may be visible to the local operating system as
  process arguments. They must not be copied into Git, CI logs, shared shell
  scripts, screenshots, or issue reports.
