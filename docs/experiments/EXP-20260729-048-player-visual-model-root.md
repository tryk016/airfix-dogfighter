# Player visual MODL-root admission and Windows capture

**Date:** 2026-07-29

**Evidence:** `EV-20260729-003`

**Scope:** authenticated player-definition root kind and the first owner-local
Windows room-plus-aircraft frame

## Question

Why did an explicit owner-local player visual fail before the Windows renderer
could receive the already implemented combined room/player draw model?

## Method

The investigation used only the independently written parser and Windows x64
product against an owner-created AFPACK outside Git:

1. inspect the bounded parsed root kind and required semantic fields of the
   explicitly selected player visual definition;
2. compare that evidence with the synthetic manifest fixture;
3. correct the fixture and manifest kind gate without relaxing path, source,
   CCF, blueprint, texture, budget, or transaction validation;
4. run the complete synthetic manifest/room-loader suites;
5. validate every available aircraft definition through the authenticated
   manifest path; and
6. render one representative definition through the actual D3D11 mission
   pipeline and capture the checked back buffer privately.

No original filename, logical launch path, binary payload, local filesystem
path, AFPACK, or screenshot is committed.

## Result

The file extension and the AF chunk root describe different things. The
player's selected aircraft definition is stored in an object-definition file
whose container root is `MODL`, not `OBJE`. It carries the model CCF, the
slot-zero blueprint selector, and optional texture root required by the
existing authenticated player scene path.

The prior synthetic fixture defaulted to `OBJE`, causing the production
manifest to reject real player definitions before CCF preparation. The corrected
contract:

- requires `MODL` for the explicit player visual;
- continues to require `OBJE` for Level `OBJE` placement definitions;
- rejects an `OBJE`-root player visual with the existing typed kind-mismatch
  issue;
- reconstructs the transient loader definition with kind `model`; and
- preserves all existing exact-lookup, allocation, transaction, texture,
  blueprint, collision, and scene-publication checks.

All 14 available aircraft definitions passed the corrected authenticated
manifest gate. Thirteen completed the current room/player load pilot; one
reached a separate downstream room-loader failure that is not caused by root
kind and remains a focused follow-up. The representative selected aircraft
completed manifest, room, player CCF, texture, slot-zero hierarchy, spawn-pose,
combined draw, D3D11 resource preparation, reverse-depth rendering, visible
back-buffer validation, and private BMP capture.

The captured frame is 960x540 with the current centered 720x540 parity viewport.
It visibly contains the textured aircraft at the authenticated start pose in
the textured mission room. This is evidence of static player visual integration,
not evidence of live flight, HUD, Hor+, native-resolution ADR-0013 completion,
or enhanced lighting.

## Confidence and limits

Confidence is 3/3 that the explicit player visual requires a `MODL` root and
that the representative room-plus-aircraft frame crossed the real Windows
D3D11 path. The synthetic tests independently prove rejection of the former
`OBJE` assumption.

The current frame still uses the camera bootstrap and parity-first 4:3 viewport.
The aircraft does not move because Windows has not yet connected semantic input
to a changing AirCraft/player pose and gameplay-camera producer. The one
aircraft-specific downstream loader failure must be isolated separately before
claiming every selectable aircraft is fully loadable.

## Portable acceptance

- A synthetic `MODL` player definition succeeds and publishes the derived
  descriptor.
- A synthetic `OBJE` player definition fails atomically with
  `objectDefinitionKindMismatch`.
- The room loader preserves kind `model` when reconstructing the bounded
  transient definition.
- The complete Windows product suite and public data-less D3D11/XAudio2 smoke
  remain green.
- The owner-local capture remains ignored and outside public infrastructure.
