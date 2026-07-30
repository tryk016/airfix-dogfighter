# EXP-20260729-058: durable render-presentation settings

- Date: 2026-07-29
- Status: implemented; Windows product validated
- Scope: portable AFRS codec/store, durable bounded I/O, Windows private-root
  binding, and launch precedence

## Question

Can both products share one strict settings file contract that recovers safely
from malformed or interrupted writes, preserves future schemas during a
downgrade, and lets Windows layer explicit launch values without contaminating
the durable profile?

## AFRS v1

AFRS is a canonical binary envelope capped at 4096 bytes:

```text
magic "AFRS" | envelope version | flags | semantic schema | exact size
SHA-256 | ordered TLV fields
```

The digest covers the first 16 envelope bytes and every byte after the digest.
Current schema v1 requires exactly four ascending fields:

1. exact little-endian IEEE-754 bits for render scale;
2. scene presentation;
3. visual profile; and
4. diagnostics Boolean.

The canonical v1 document is exactly 71 bytes. Tests pin its complete golden
byte vector. Decode rejects every truncation, oversized input, changed digest,
bad magic/version/flags/size, duplicate/missing/unknown/out-of-order field,
wrong field size, trailing byte, forged enumeration/Boolean, non-finite scale,
and out-of-range scale.

A checksummed schema newer than the current semantic version is returned only
as `{schemaVersion, exactBytes}`. It is never interpreted or re-encoded by a
downgrade.

## Bounded exact read

`readBoundedRegularFile` opens the final path without following a
symlink/reparse point and performs the complete read through that same OS
handle. Before allocating it requires:

- a regular file;
- exactly one hard link; and
- an initial size within the caller's limit.

It reads the declared amount, probes for growth, and checks identity, size, and
link count again before returning. Win32 and POSIX implementations expose
typed missing, wrong-type, size-limit, and I/O failures.

The settings store separately rejects a `settings/` leaf that is itself a
symlink/reparse point. Native adapters place this serialized store in an
application-private parent not writable by untrusted processes; standard
path-based APIs are not claimed as a defense against a concurrent hostile
replacement of the entire parent tree.

## Recovery and save transaction

Startup order is:

1. valid current v1;
2. valid backup v1 when current is absent or invalid; or
3. canonical defaults.

An I/O-unavailable, linked/wrong-type, or future record blocks writes. A future
current also blocks fallback to an older backup, preserving downgrade safety.
Only a valid current can be promoted to backup.

Save order is:

1. validate and canonically encode the complete candidate;
2. inspect current and backup for retained future/unsafe state;
3. rotate a differing valid current through a durable prepared backup;
4. write and synchronize the candidate prepared sibling;
5. atomically replace current; and
6. perform exact bounded read-back.

If replacement reports an error after the rename, exact candidate bytes plus a
successful file/directory durability retry prove success. Exact prior bytes
prove a safe pre-publication failure. Any other outcome is `commitUnknown`.
Errors carry only typed categories and the requested semantic record when
needed for recovery; they never contain a host path.

## Windows product binding

The production root is derived only from:

```text
SDL_GetPrefPath("tryk016", "Airfix Dogfighter") / settings
```

The returned UTF-8 root is converted to the native Windows filesystem encoding
and released through `SDL_free`. There is no CWD, environment-variable, or
public command-line settings-root override.

Effective startup state is resolved as:

```text
canonical defaults -> valid current/backup -> explicit sparse launch overrides
```

Launch flags can express either presentation mode, diagnostics on or off, a
50-200% render scale, and Classic or Enhanced. They remain session-only. Public
diagnostic capture forces diagnostics only for that invocation. Smoke,
capture, and content-validation modes bypass `SDL_GetPrefPath` entirely and
therefore cannot touch a developer's real profile during CTest or CI.

The final UI is a later slice. It will keep the persistent base separate from
launch overrides and use the existing D3D11 pre-publication gate so only the
durable base, never the launch overlay, is saved.

## Evidence

- Windows GCC 15.2 focused codec/store/durable tests: passed.
- Native MSVC 19.51 Windows product: complete build; 104/104 CTests passed.
- WSL Ubuntu GCC 13.3 clean portable build: 317/317 steps; 98/98 CTests passed.
- D3D11 renderer, default smoke, and 50% Original 4:3 smoke: passed.
- Hosted PR #61 validation: all seven checks passed. Portable/macOS/Windows/
  clangd and native D3D11/XAudio2 used
  [run 30502869591](https://github.com/tryk016/airfix-dogfighter/actions/runs/30502869591);
  unsigned iPhoneOS and iPhoneSimulator used
  [run 30502869598](https://github.com/tryk016/airfix-dogfighter/actions/runs/30502869598).
- Independent review: one linked-directory P2 found, fixed, and re-reviewed;
  final verdict GO with no remaining P0-P2.

All fixtures are synthetic. No original asset, owner preference, private path,
or generated settings document is included.

## Remaining iOS work

iOS must not block its main thread on storage. The current one-call Metal apply
API must first be split into:

1. main-thread immutable request capture;
2. off-main resource preparation;
3. serialized Application Support save;
4. main-thread revision/surface revalidation; and
5. immediate no-fail commit.

That coordinator and the `Application Support/AirfixDogfighter/settings`
adapter remain the next ADR-0014 persistence slice. A stale surface after a
successful save must reprepare the already durable candidate without saving it
again.

## Related

- [ADR-0014](../adr/0014-render-presentation-settings.md)
- [D3D11 transaction](EXP-20260729-055-windows-transactional-render-settings.md)
- [Metal transaction](EXP-20260729-057-metal-transactional-render-settings.md)
