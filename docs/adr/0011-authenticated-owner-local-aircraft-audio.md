# ADR-0011: Decode aircraft audio from authenticated owner-local content

**Status:** Accepted

**Date:** 2026-07-29

**Deciders:** project owner and implementation lead

## Context

The reconstructed AirCraft audio state machines already emit six portable
sound roles and both native products can consume bounded PCM16 clips. The
remaining boundary was the lawful owner's original `Resource.up`: samples must
be usable without embedding them in source, an application bundle, a public CI
artifact, or a second unauthenticated cache.

The private AFPACK workflow already authenticates one immutable package handle,
parses its nested source archive, and exposes indexed reads through
`VerifiedContentSession`. Reopening a pathname or accepting loose WAV files
would detach audio from that proof and create a substitution race.

Read-only inspection of the supported v1.01 content confirms ordinary RIFF/WAVE
PCM16 sources for the recovered aircraft roles. Four continuous layers have
full-buffer infinite `smpl` loops. `engineStart` and `engineStop` are runtime
one-shots even though their files retain authored sampler metadata.

## Decision

Decode the six aircraft samples directly from the nested `Resource.up` owned by
one unchanged `VerifiedContentSession`.

- Resolve exact logical archive entries for `engineOn`, `engineIdle`,
  `engineTurn`, `engineStart`, `engineStop`, and `engineDive`.
- Preflight every unique entry and the aggregate source footprint before
  reading any sample.
- Read only by authenticated archive index through the session. Logical names
  are identifiers, never host filesystem paths.
- Parse RIFF/WAVE with a bounded, fail-closed portable PCM16 parser. Accept only
  integer PCM, mono or stereo, 8–192 kHz, frame-aligned 16-bit data, bounded
  chunks, and structurally valid forward whole-frame sampler loops.
- Require one full-buffer infinite loop for the four continuous roles.
  `engineStart` and `engineStop` remain one-shots; their sampler metadata does
  not override recovered runtime behavior.
- Preserve recovered clip IDs `0x1A`, `0x1B`, `0x1C`, `0x1D`, `0x1E`, and
  `0x20`. Voice IDs remain actor-instance allocations.
- Publish all six clips atomically with content revision, opaque transaction
  identity, source-footprint accounting, and decoded-PCM accounting. A missing,
  ambiguous, malformed, over-budget, cancelled, or identity-changed load
  publishes nothing.
- Register copied PCM into XAudio2 on Windows and a newly prepared
  AVAudioEngine backend on iOS. The iOS mission ticket, renderer candidate, clip
  set, bindings, and backend are committed as one main-thread ownership
  transaction.

No converted sample cache is added to AFPACK v1. The source WAV is small enough
to decode once at mission preparation, and avoiding another representation
keeps package compatibility and authentication simple.

## Consequences

### Positive

- Windows and iOS use the same authenticated clips, IDs, loop policy, limits,
  and portable bindings.
- No original or converted audio is committed, bundled by public CI, or
  uploaded to a service.
- Source substitution is detected because lookup, reads, parsing, and
  publication remain tied to one authenticated stream identity.
- Synthetic AFPACK/WAVE fixtures exercise the complete public CI path without
  private data.
- Windows can validate an installed private content root independently of the
  original installation and use it for rapid parity work.

### Negative and follow-up work

- Mission preparation retains temporary source bytes and decoded PCM before the
  native backend copies its representation. Explicit per-file, aggregate, and
  published-memory limits bound this cost.
- Only the six confirmed AirCraft roles are bound. Weapons, impacts, voices,
  ambient effects, and music need separate evidence and admission policies.
- The current backend loops complete buffers. Future subrange looping requires
  a portable command/backend extension only if observed runtime behavior needs
  it.
- Audible parity, output-route handling, and device changes still require
  physical Windows and iPhone acceptance.

## Private-content rules

- Conversion and installation operate on owner-supplied copies; the original
  installation is read-only.
- `.afpack` files, AFAC content roots, extracted WAV files, decoded PCM,
  captures, hashes of private payloads, and local paths remain outside Git.
- Public tests construct synthetic RIFF/WAVE and AFPACK documents.
- The Windows `afpack-install` tool requires an explicit private destination and
  transaction UUID. The application reads only a previously authenticated
  active content root.

## Follow-up

1. Feed committed bindings from the live 12 ms AirCraft producer into the
   shared audio coordinator.
2. Recover and bind the remaining gameplay sound families.
3. Add spatial listener/emitter and priority contracts after executable
   evidence establishes them.
4. Complete audible and route/device acceptance on Windows and both target
   iPhones.
