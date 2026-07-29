# ADR-0014: cross-platform render presentation settings

- Status: accepted
- Date: 2026-07-29
- Deciders: project owner and implementation lead

## Context

ADR-0013 defines native-resolution rendering, Hor+ widescreen, optional
Original 4:3, a 50-200% scene render scale, diagnostic telemetry, and separate
Classic and Enhanced visual intent. The portable layout and both native
backends already execute the first three policies, but the products currently
have no single settings snapshot:

- Windows maps command-line values to three independent D3D11 setters;
- Metal exposes equivalent main-thread setters and the iOS shell forces the
  diagnostics overlay on;
- omitted Windows options are indistinguishable from explicit defaults, so
  they cannot safely be layered over future persistent preferences; and
- changing render scale releases the old scaled targets before the next frame
  proves that replacement targets can be allocated.

Settings must remain outside authenticated game content and deterministic
simulation. A corrupt setting or failed GPU allocation must never prevent the
application from falling back to the last complete renderable snapshot.

## Decision

### Portable snapshot

The render front end owns one allocation-free C++20 value:

```text
RenderPresentationSettings
  render scale percent: 50..200, default 100
  scene presentation: Hor+ | Original 4:3, default Hor+
  diagnostics overlay: disabled | enabled, default disabled
  visual profile: Classic | Enhanced, default Classic
```

All fields are orthogonal. Validation accepts a complete candidate or rejects
the complete candidate without clamping or partially copying fields. A forged
enumeration, non-finite scale, unsupported schema, or invalid Boolean is an
error.

`Enhanced` is initially a typed policy selector only. It does not claim that
the lighting, material, shadow, or post-processing stages in ADR-0013 exist,
and it may produce the same scene pixels as `Classic` until those stages are
implemented. It is distinct from the optional private HD texture mode.

The portable layer also owns:

- a sparse override value whose absent members preserve the underlying
  snapshot;
- deterministic delta classification for scale targets, layout, diagnostics,
  and visual-profile work; and
- a versioned, storage-neutral semantic record mapping with strict all-fields
  validation.

The record contains no content root, logical asset path, checksum, GPU or
device identifier, save-game state, or gameplay value.

This semantic record is not a file codec. The later persistence slice owns a
bounded canonical byte format, duplicate/missing/trailing-data detection, and
platform storage I/O.

### Sources and precedence

Effective settings are resolved in this order:

1. canonical defaults;
2. one valid persistent snapshot;
3. explicit launch-only overrides.

Omitted launch options do not overwrite persistent fields. Windows capture and
smoke options are always session-only and never modify the user's profile.
Both positive and negative Boolean/presentation overrides must be expressible
when launch options are used over persistence.

An absent persistent record silently selects defaults. A malformed, oversized,
wrong-type, linked, unsupported, or incomplete record is never partially
applied. A future schema is preserved for a newer application and ignored by a
downgrade. Recovery may use only a previously validated backup; otherwise the
application uses defaults.

### Storage boundary

Native adapters resolve their application-private settings directory:

- Windows uses the platform/SDL preference-directory API, never the current
  working directory or an environment-variable expansion performed by the
  game;
- iOS uses the application sandbox's Application Support directory.

The resulting `settings/` directory is a sibling of `content/`, `saves/`, and
`diagnostics/`. Content import, rollback, and garbage collection cannot inspect
or remove it. Tests inject a temporary root and never touch a real user
profile.

Writes use a prepared sibling file, file synchronization, atomic replacement,
directory synchronization where supported, and exact read-back after an
ambiguous post-rename error. Logs may expose only a settings error category,
schema version, and recovery outcome. They must not expose the resolved host
path.

### Prepare, persist, publish

A runtime change is one transaction:

1. build and validate a complete candidate snapshot;
2. derive the candidate layout for the current physical output;
3. prepare any replacement color/depth targets without releasing the current
   pair;
4. durably save the candidate when the change is persistent; and
5. publish the already prepared settings/resources with a no-fail commit.

Failure at any preparatory or persistence step keeps both the previous durable
record and the previous effective renderer snapshot. A renderer never reports a
new scale while drawing with resources prepared for a different scale.

Render scale may replace only the private scene color/depth targets. Original
4:3 changes layout only. Diagnostics toggles only the output-resolution overlay.
None of these changes reloads a mission, recreates the app, resets input,
changes `AppSession`, or modifies simulation state.

Windows applies the transaction on its SDL/render thread. iOS performs storage
I/O on a serialized settings queue and commits the already validated candidate
to Metal on the main thread before the first drawable that uses it.

### User interface boundary

Persistence and runtime binding do not by themselves constitute the final
settings screen. A later UI slice will expose these values to mouse, keyboard,
touch, and controller navigation. It must use the same transaction and must
show a safe user-facing failure without revealing paths or record contents.

Safe FOV, UI scale, Low-Ultra quality tiers, per-effect controls, and the
private HD texture selector remain separate settings extensions because their
runtime resource and migration policies are not yet complete.

## Options considered

### Keep independent backend fields

Rejected. It duplicates validation, cannot atomically publish a complete
snapshot, and makes Windows/iOS persistence semantics diverge.

### Store presentation state in `AppSession`

Rejected. `AppSession` owns lifecycle, content readiness, pause, and input
boundaries. Rendering preferences must not change deterministic session state.

### Treat command-line defaults as persistent values

Rejected. It would overwrite preferences whenever a switch was omitted and
would let diagnostic capture runs contaminate the user's profile.

### Publish a new scale and allocate on the next frame

Rejected for runtime UI. Allocation failure would leave the published setting
and actual GPU resources inconsistent and could create a repeatable blank-frame
or crash loop.

## Consequences

- Both products consume one validated settings vocabulary and one migration
  version.
- Backend changes require candidate-resource preparation before publication.
- Persistent storage and final UI remain native adapters, but their validation
  and precedence are portable and testable without game data.
- Adding later quality/FOV/texture fields requires a deliberate schema
  migration rather than silently changing defaults.
- Classic/Enhanced selection can be wired before Enhanced effects exist, but
  status and screenshots must continue to state that visual parity is pending.

## Verification

Public synthetic tests must cover:

- exact defaults and semantic record round-trips;
- 49/50/100/200/201%, NaN, infinity, forged enums, invalid stored Boolean
  values, sparse overrides, and future schemas;
- in the persistence slice, missing/duplicate fields, trailing data, oversized
  input, and canonical byte round-trips;
- sparse override precedence and all-or-nothing rejection;
- missing/current/backup/default recovery and ambiguous durable replacement;
- save failure preserving the previous effective and durable snapshot;
- runtime transitions 100 -> 50 -> 200 -> 100, Hor+ <-> Original 4:3, and
  diagnostics off <-> on;
- failed target allocation preserving the old settings and resources;
- unchanged deterministic simulation hashes across every presentation/profile
  combination; and
- data-less D3D11 captures at representative native, scaled, 4:3, and
  ultrawide configurations plus hosted iPhoneOS/iPhoneSimulator builds.

Physical-device acceptance later verifies save, force-quit, relaunch, safe
area, orientation, memory pressure, and target-allocation fallback on iPhone SE
3 and iPhone 17 Pro Max.

## Action items

- [x] Add the portable snapshot, override, delta, validation, and versioned
  semantic-record mapping.
- [ ] Replace independent backend setters with transactional complete-snapshot
  application.
- [ ] Add Windows private storage, launch-override precedence, recovery, and
  synthetic restart tests.
- [ ] Add the equivalent iOS Application Support adapter and hosted build
  coverage.
- [ ] Capture and compare the public synthetic Windows matrix.
- [ ] Add the final cross-input settings UI as a separate slice.
