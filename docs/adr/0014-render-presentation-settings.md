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
  vertical FOV increase: 0..25 degrees, default 0
  UI content scale: 75..150 percent, default 100
```

All fields are orthogonal. Validation accepts a complete candidate or rejects
the complete candidate without clamping or partially copying fields. A forged
enumeration, non-finite scale, unsupported schema, or invalid Boolean is an
error.

`Enhanced` is a typed visual-policy selector. Its first implemented behavior is
bounded trilinear 8x anisotropic sampling for scene textures on D3D11 and
Metal; Classic retains the established point/mip-point scene path. UI overlays
and the final render-scale presentation use separate sampler contracts. This
does not claim that the lighting, material, shadow, color-space, or
post-processing stages in ADR-0013 exist. It is distinct from the optional
private HD texture mode.

The portable layer also owns:

- a sparse override value whose absent members preserve the underlying
  snapshot;
- deterministic delta classification for scale targets, layout, diagnostics,
  and visual-profile work; and
- a versioned, storage-neutral semantic record mapping with strict all-fields
  validation.

ADR-0016 extends the record to schema 2 with the safe vertical-FOV field.
ADR-0017 extends it to schema 3 with independent UI scale. Schema 1 migrates to
exact zero-degree FOV and 100% UI scale; schema 2 migrates to exact 100% UI
scale; later schemas remain opaque. The record contains no content root,
logical asset path, checksum, GPU or device identifier, save-game state, or
gameplay value.

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
4:3 and safe FOV change layout only. Diagnostics toggles only the
output-resolution overlay.
None of these changes reloads a mission, recreates the app, resets input,
changes `AppSession`, or modifies simulation state.

Windows applies the transaction on its SDL/render thread. iOS performs storage
I/O on a serialized settings queue and commits the already validated candidate
to Metal on the main thread before the first drawable that uses it.

The D3D11 boundary exposes an optional allocation-free publication gate. It is
called only after the candidate layout and complete replacement color/depth
bundle have been prepared, and before the active bundle, diagnostics resources,
or settings snapshot are changed. A later Windows durable store uses this gate
to perform the persistence step. Gate rejection discards the prepared candidate
and preserves the previous effective state. Once the gate accepts, publication
contains only no-fail unbinding, resource exchange, resource release, and value
assignment. Session-only launch and smoke changes omit the gate.

Metal uses the same ordering through a single-executor portable transaction.
Each prepared candidate records the exact view identity, device identity,
positive drawable extent, surface generation, active revision, derived scene
target extent, and an optional immutable target bundle. Final validation rejects
a stale revision or any changed surface field immediately before a no-fail
main-thread move publication.

The target bundle owns both Metal color/depth textures and their exact
`SnapshotGpuBudgetLedger` reservation. A copied active snapshot is the frame
lease and remains captured through command-buffer completion, so replacing the
active bundle cannot release resources still referenced by the GPU. Exactly
100% owns no intermediate bundle; every non-100% value requires a complete pair
even if integer rounding happens to produce the output extent.

Positive resize prepares and publishes a complete replacement before the next
matching drawable is rendered. Failed preparation retains the last good
snapshot, suppresses rendering to the mismatched drawable, and retries
immediately, then after 1, 2, 4, ... up to 120 frames. Zero extent suspends
rendering without deleting settings or the retained target bundle.

The iOS adapter implements that persistence sequence without blocking the main
thread. Main captures an immutable request, a dedicated serial worker prepares
the Metal candidate, the settings store durably saves the persistent base, and
main revalidates the live revision and surface immediately before the same
no-fail commit. A successful save creates a publication obligation: if the
surface became stale, iOS prepares the already durable value again without a
second save. Startup rendering remains gated until the recovered value or safe
defaults have been resolved.

### User interface boundary

Windows and iOS now expose these values through native-resolution settings
surfaces with mouse, keyboard, touch, and controller navigation as applicable.
Both use the same transaction and show safe failures without revealing paths
or record contents.

Low-Ultra quality tiers, per-effect controls, and the private HD texture
selector remain separate settings extensions because their runtime resource
and migration policies are not yet complete. Safe FOV is the schema-2
extension specified by ADR-0016; independent native UI scale is the schema-3
extension specified by ADR-0017.

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
- Persistent storage remains a native adapter and the final UI remains a
  separate product slice, while validation, precedence, and publication
  sequencing are portable and testable without game data.
- Adding later quality/FOV/texture fields requires a deliberate schema
  migration rather than silently changing defaults.
- Classic/Enhanced selection can gain bounded stages incrementally, but status
  and screenshots must identify the stages actually implemented and continue
  to state that complete visual parity is pending.

## Verification

Public synthetic tests must cover:

- exact defaults and semantic record round-trips;
- 49/50/100/200/201%, FOV 0/25 and adjacent invalid values, NaN, infinity,
  forged enums, invalid stored Boolean values, sparse overrides, schema-1
  migration, and future schemas;
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
- [x] Replace the independent D3D11 setters with transactional complete-
  snapshot application and a pre-publication persistence gate.
- [x] Replace the independent Metal setters with an equivalent prepared
  complete-snapshot transaction.
- [x] Add Windows private storage, launch-override precedence, recovery, and
  synthetic restart tests.
- [x] Add the equivalent iOS Application Support adapter and hosted build
  coverage.
- [ ] Capture and compare the public synthetic Windows matrix.
- [x] Add the iOS touch/controller render-settings UI as a separate slice.
- [x] Add the equivalent Windows keyboard/mouse/controller product settings UI.
- [x] Extend both product settings surfaces and schema migration with the
  ADR-0016 safe vertical-FOV control.
