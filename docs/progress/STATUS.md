# Project status

**Updated:** 2026-08-04
**Stage:** Phase 1 — static analysis and archive recovery in progress

## Now

- Planning and documentation system established.
- Original installation inventoried without executing binaries.
- Port strategy recorded in ADR-0001.
- Playable Windows x64 and private iOS ARM64 are accepted as parallel products
  over one portable C++20 core. Windows is the primary rapid-debug/parity
  environment and owns separate window, renderer, input, audio, and filesystem
  adapters; ADR-0007 and both product checklists define the boundary.
- ADR-0008 selects SDL3 for the Windows window/events/keyboard/mouse/controllers
  and D3D11/DXGI with HLSL for the Windows renderer. SDL3 does not own game
  rendering; iOS remains on its native Metal backend. ADR-0009 selects inbox
  XAudio2 2.9 for the Windows native audio backend and keeps SDL3 audio
  disabled. ADR-0010 selects AVAudioEngine for iOS over the same portable
  command/PCM contract.
- ADR-0013 makes native-resolution modern rendering an official cross-product
  requirement. At 100% render scale the 3D target must exactly match the output
  extent; standard widescreen is Hor+, Original 4:3 is comparison-only, and UI,
  text, safe areas, and input coordinates remain independent of scene render
  scale. Classic/Enhanced profiles, Low-Ultra tiers, staged lighting/material/
  HDR/effect work, platform budgets, diagnostics, aspect-ratio tests, and
  matched screenshots are specified. The implemented foundation now
  provides strongly typed domains, exact 100% target identity, Hor+, Original
  4:3 comparison math, safe-area/UI fitting, input transforms, and the
  ADR-0016 safe vertical-FOV increase. Both native
  backends consume the same 100% direct path and now allocate private
  color/depth scene targets plus a linear native-output presentation pass at
  non-100% scales. Windows exposes validated 50-200% and Original 4:3 through
  both command-line switches and its product settings surface; the iOS panel
  exposes the same persisted values. Safe FOV is an explicit `0..25` degree
  increase that expands the centered logical camera canvas without changing
  the recovered camera snapshot, physical render targets, UI, or simulation.
  Independent UI scale is now a durable 75-150% setting. Windows composes it
  with per-monitor DPI and uses a bounded auto-scrolling row window instead of
  shrinking enlarged content to fit; iOS recomputes native Dynamic Type fonts
  and spacing inside its safe-area scroll view. The root UI canvas, scene
  projection, render targets, mission, and simulation remain unchanged.
  Renderer diagnostics consume the same output-pixel scale policy. Windows
  direct D3D11 readbacks verify 1080p, 1440p, 4K, and 32:9. A controlled
  960x540 comparison uses 480x270 at 50% and 1920x1080 at 200%; owner-content
  images remain private. The shared renderer now also publishes smoothed FPS,
  CPU/GPU frame time, output and scene extents, render scale, draw calls,
  triangles, lights, and labelled GPU-memory measurements. D3D11 and Metal
  composite the same output-resolution developer panel after scene
  presentation; Metal applies UIKit safe-area offsets. Both native products
  now expose render settings through explicit pause/menu boundaries. The first
  sampling stage is also implemented: Classic retains the established
  point-sampled scene-texture path and Enhanced uses trilinear 8x anisotropic
  sampling on D3D11 and Metal. UI/diagnostic and final-presentation samplers
  remain independent. RGBA8 upload plans now also carry an explicit sample
  space: existing GTI stays conservatively `encoded-unclassified` and retains
  the exact UNORM path, while explicitly classified sRGB colour and linear
  data have matching D3D11/Metal format mappings. Unknown labels fail closed.
  Actual texture classification, end-to-end linear/sRGB output handling,
  lighting, materials, shadows, post-processing, and iOS runtime/device
  acceptance remain pending.
  Hosted iPhoneOS and iPhoneSimulator builds compile the complete Metal path;
  only physical-device visual/timing acceptance remains for this slice.
  A shared allocation-free gameplay-screen projection now composes the
  recovered world-to-reference-camera result with Hor+, Original 4:3, safe
  FOV, and the actual scene viewport in native output pixels. It is invariant
  to render scale, rejects camera/layout mismatch, and preserves off-screen
  coordinates and recovered depth visibility as separate labels for later
  HUD policy. Its first concrete consumer now implements the recovered
  weapon-crosshair distance scale, activation size latch, and centered output
  rectangle. Crosshair size follows output-fit UI scale plus the independent
  75-150% user setting and remains invariant to 3D render scale. Off-screen
  and depth labels stay explicit. The three recovered sight roles now load as
  one bounded format-8 `32x32` set through the exact authenticated content
  handle; plan-only source/RGBA preflight precedes decode, publication is
  atomic, and HUD-local texture IDs cannot collide with scene IDs. The exact
  nine registered weapon types now map to MG/RO/BO or the affirmative
  `WpParaMine` no-sight result. The portable binder preserves the type, role,
  HUD-local texture ID, revision, and exact authenticated-stream identity;
  primary and selected-secondary slots remain independent as in the recovered
  `AirCraft+0x490/+0x494` state. Windows and iOS now also prepare all three
  authenticated sight textures as separate D3D11/Metal resources inside the
  mission transaction, before one atomic publication, and include them in
  backend memory accounting. The final static sprite contract is now also
  recovered: full UVs, ARGB `0x7FFFFFFF`, source-alpha blending, and
  always-write depth enter one authenticated value packet. D3D11 consumes it
  in a private 1920x1080 validation capture and Metal prepares the equivalent
  provenance-gated encoder. Event `0x06` is now statically closed around the
  crosshair substage: after its independent AirCraft HUD call and the recovered
  type/active/camera gates, selected secondary renders before primary. A
  bounded value plan preserves that order and rejects mixed-provenance packets
  atomically. The independent earlier AirCraft HUD stage now has its first
  complete portable subsection: a bounded allocation-free armour/health-gauge
  plan preserves both camera samples, native gate order, the `639/640` anchor
  split, optional background/foreground textures, the smoothed-health annular
  mask with at most 32 black quads, and exact layer order in logical UI
  coordinates. The authenticated content session now retains both
  `Resource.up` and the manifest-selected localization archive on the same
  verified AFPACK handle. The format-8 `128x128` `armour_meter`/`armour` pair
  loads atomically with archive-aware provenance and separate HUD-local IDs,
  then D3D11 and Metal prepare both resources inside the complete mission
  transaction and GPU budget. The Cc screen-call chain now proves format-8
  source-alpha texture blending, opaque black mask fill, and ALWAYS/write
  depth. One authenticated fixed-capacity packet maps the recovered 640x480 UI
  domain into physical output pixels and scales around its bottom-left anchor;
  it never changes the native-resolution 3D target. D3D11 consumes it in a
  real private 1920x1080 half-health capture. Metal now owns the matching
  fail-closed four-point encoder, alpha/opaque pipelines, linear-clamp sampler,
  and ALWAYS/write depth state over the same packet. A second complete HUD
  primitive now recovers the six reusable four-digit states: native `%04d`
  formatting, cached `pow(0.0005,elapsed)` retention, strict shortest cyclic
  routing, binary32 retained positions, and exact 7x9 windows over the
  statically verified 16x128 vertical-atlas contract. Its portable state and
  fixed four-command plan are allocation-free. The selected format-8 atlas now
  loads atomically through the authenticated source-archive handle with its
  own HUD-local identity, and both products stage it inside the complete
  mission transaction and GPU budget. An identity-bound native-output packet
  maps fractional UV windows and independently scaled UI rectangles into
  physical pixels while preserving tint, source-alpha blend, linear clamp and
  ALWAYS/write depth. D3D11 and Metal contain dormant fail-closed consumers;
  ordinary frames still issue no manufactured HUD draw. The paired analog
  instruments are now a third complete primitive: their exact right-before-
  left order, centres, 64x64 faces, 7x30 rotated indicator, 225-degree span,
  pivot, UV window and tints are recovered. A cross-archive authenticated
  three-texture owner, allocation-free logical plan and identity-bound native-
  output packet preserve those rules. Both products stage the textures inside
  the atomic mission transaction and own dormant fail-closed arbitrary-quad
  consumers. The two rolling readouts beneath those instruments are now
  independently complete: right consumes an already-quantized
  `100*length(vector)` value, left consumes the already-quantized
  `100*(remaining/initial)+0.5` route, and both retain one shared HUD-clock
  snapshot. A bounded transactional plan preserves exact right-before-left
  order, `640x480` anchors, white/right and actor-tinted/left drawing, optional
  state pointers, and atlas-free state advancement. Its authenticated paired
  packet reuses the existing digit-atlas mapping and dormant generic
  D3D11/Metal consumers without adding GPU resources or live values. Both
  weapon panels are now a fourth complete primitive. Their two
  backgrounds precede primary and selected-secondary slots; each slot retains
  exact ammunition-digit, icon, and status-fill geometry. The dynamic
  authenticated `weapon_*.gti` catalog is archive-derived rather than
  hard-coded, while the status palette and destination-multiply blend preserve
  the recovered native contract. An allocation-free 14-command plan,
  cross-owner native-output submission, and D3D11/Metal consumers are staged
  but deliberately dormant. The final contiguous AirCraft identity/status
  cluster is now a fifth complete primitive: dynamic full-name `Ac*.gti`
  lookup, rolling health percentage, exact team-star/cross selection, and
  rolling technology level preserve native order and anchors. A bounded
  cross-archive owner discovers all aircraft icons without hard-coded names,
  while a ten-command allocation-free plan and identity-bound packet retain
  the shared digit atlas, UI/native-resolution separation, source-alpha blend,
  linear clamp, and ALWAYS/write depth. Both native products stage the entire
  set and own dormant fail-closed consumers. All five subsections now enter one
  authenticated full-stage render event in native order. One clock transaction
  supplies the same bit-exact elapsed snapshot to all six rolling states,
  publishes their replacements only with the complete event, and resets only
  after total validation; every failure aborts without consuming elapsed time.
  D3D11 and Metal own matching dormant full-event consumers, while ordinary
  frames remain disconnected. A private Windows validation capture now joins
  every authenticated HUD owner, a fixed representative full event, and a
  centred sight through the real D3D11 path without publishing any value to
  simulation. Native 1920x1080, 2560x1440, 3840x2160, and 3440x1440 captures
  preserve physical-resolution rendering, Hor+ scene expansion, the stable UI
  root, and diagnostics; all images remain outside Git. Live slot ownership,
  changing
  aim/collision production, explicit visibility policy, ammunition/status
  values, smoothed-health, digit, instrument, aircraft identity, team,
  technology, instrument-readout values and shared-clock producer wiring,
  physical iPhone visual
  acceptance, mission status outside this AirCraft cluster, and screen effects
  are not yet connected.
- The recovered CCF material contract now survives end to end from typed
  `0x2140/0x2150/0x2151` metadata and exact native defaults through world,
  aggregate mission, and player texture binding into each backend-neutral draw
  command. The native four blend modes, fixed-function texture-stage behavior,
  and per-room triangle/sprite/custom unsigned depth sort are statically
  documented. A bounded portable four-pass radix queue now reproduces the
  unsigned stable ordering for already-keyed caller-owned mixed-kind items.
  D3D11 and Metal consumption remains deliberately off until triangle-level
  runtime producers and the policy-conditioned x87 `_ftol` boundary are
  implemented; current output is unchanged.
- Windows now has a private `--capture-overview-frame` validation path for a
  complete installed mission. A backend-neutral camera fits all transformed
  non-empty model bounds at the requested aspect ratio; D3D11 then applies a
  diagnostic cutaway only to physical room-shell contributors while retaining
  ordinary two-sided rendering for placed objects and the player aircraft.
  The camera is an owning one-frame value and never enters the gameplay camera
  exchange. Local 1920x1080, 100%-scale Classic and Enhanced captures verify
  the full room, textured placed scene, player aircraft, and diagnostics
  overlay; the derived images remain private and outside Git.
- ADR-0019 plans an optional owner-local HD texture package without
  changing Classic GTI behavior. It records the required accepted-only
  manifest index, logical-path plus source-SHA identity, root-confined file
  capabilities, per-texture fallback, independent requested/effective mode,
  cache partitions, bounded residency, staged RGBA8/BC7/BC3/ASTC work, and
  synthetic-only public tests. The reviewed private corpus was revalidated
  locally as 1,833 logical textures, 1,689 accepted unique results, and 16,304
  mip files. No manifest, image, checksum, logical path, local root, or pipeline
  branch content was copied into this repository. The public stage-2
  implementation now parses bounded in-memory reviewed JSONL, retains only
  explicitly accepted records, normalizes logical identity, validates declared
  counts and 4x mip metadata, and exposes immutable path/SHA lookups with
  path-free diagnostics. Its tests use only synthetic bytes. ADR-0020 now adds
  the disconnected stage-3 root capability: it pins an absolute owner-selected
  root, resolves each component through native root-relative no-follow opens,
  rejects links/reparse points, special or multiply linked files, enforces
  generation and byte limits, and exposes only fixed statuses. Stage 4 now
  retains canonical GTI logical identity through global texture binding, hashes
  the actual GTI bytes, resolves accepted candidates through that pinned root,
  authenticates the manifest-selected base file, and represents every failure
  with a fixed redacted fallback reason. Every miss or failure invokes the
  unchanged GTI preparation with the identical request, bytes, and limits.
  Digest-only matching requires explicit caller policy and remains disabled in
  all products. Stage 5 now reauthenticates the candidate base and prepares the
  declared PNG RGBA8 mip chain through a hash-pinned, memory-only decoder. It
  enforces exact IHDR/dimensions, CRC/decode, straight-alpha metadata,
  base/mip-zero identity, fixed redacted failures, and checked encoded/in-flight/
  decoded CPU budgets. Only the base is cryptographically authenticated by the
  current schema; non-zero mips are labelled structural-only. Windows now also
  has bounded D3D11 publication/cache and a private acceptance pass. AFRS
  schema 4 persists the requested texture mode, schema 1-3 migrate to Classic,
  and a separate path-free runtime state resolves package availability,
  effective mode, fallback, and reload requirements. Both native settings
  panels now expose the independent mode. Windows enables Enhanced only for a
  ready package and atomically replaces the complete room/HUD visual snapshot
  without re-registering audio; failure preserves the old scene and active
  mode. iOS now imports an owner-selected folder copy into protected
  Application Support, discovers exactly one accepted reviewed manifest,
  persists an opaque AFTL locator, restores the shared portable session after
  restart, and enables Enhanced only while that session is ready. A package or
  mode change prepares the complete room/HUD/audio snapshot before the existing
  Metal transaction publishes it; absent or invalid packages safely resolve to
  Classic. No private resource enters CMake, the app bundle, Git, or CI.
  Physical-device HD acceptance, streaming, compressed derivatives, and final
  device budgets remain pending.
  The
  repository-boundary scanner and synthetic regression suite continue to
  reject private texture roots, raster/GPU texture formats, JSONL/NDJSON
  manifests and backups, and common archives, while the entire owner-local tree
  is ignored at repository root.
- ADR-0014 defines one cross-platform render-presentation settings transaction.
  Its portable C++20 foundation is implemented: render scale, Hor+/Original
  4:3, safe FOV, UI scale, diagnostics, visual profile, and requested texture
  mode form one validated snapshot; sparse overrides reject atomically; a
  deterministic delta identifies target/layout/UI/overlay/profile/texture
  request work; and a versioned semantic schema-4 record migrates schema 1 to
  exact zero FOV plus 100% UI scale and Classic textures, schema 2 to exact
  100% UI scale and Classic textures, and schema 3 to Classic textures. It
  fails closed on future schemas or malformed values. A second portable
  layer now prepares immutable active/candidate states against exact
  view/device/extent/generation stamps, validates stale candidates, owns an
  optional copyable target lease, and supplies bounded 0, 1, 2, 4, ...,
  120-frame retry policy. D3D11 applies the full snapshot as one prepare-before-
  publish transaction. Exact old color, RTV, SRV, depth, and DSV identities
  survive invalid candidates, unavailable surfaces, late allocation failure,
  failed scaled resize, and a rejected pre-publication persistence gate.
  Metal now also replaces its independent setters with one complete snapshot:
  non-100% prepares both private color/depth textures under the shared GPU
  ledger, final validation rejects a changed drawable surface, and one immutable
  lease survives through command-buffer completion. Failed positive resize
  retains the last good pair but does not render it to a mismatched drawable;
  zero extent retains it for restore. Exactly 100% remains a direct native
  target on both backends, diagnostics now defaults off on iOS, and Enhanced
  now selects the bounded scene-texture sampling policy above. It is still not
  a claim that lighting, materials, shadows, color-space work, or post-effects
  exist. The portable
  persistence slice now adds a canonical, checksummed, 4 KiB-bounded AFRS
  record plus strict current/backup/default recovery and atomic durable writes.
  ADR-0018 now extracts the byte-exact current/backup/partial transaction into
  one codec-neutral `airfix::io` boundary shared by AFRS and AFIP. Their codecs,
  semantic defaults, public diagnostics, future-schema policy, and serialized
  native adapters remain separate. Direct synthetic faults cover both publish
  stages, exact readback, unsafe partials, future preservation, and
  commit-unknown without paths in diagnostics. This foundation does not define
  or persist campaign/save-game state.
  Windows resolves the private `settings/` leaf only through SDL's preference
  API, layers one valid persistent snapshot under sparse launch-only overrides,
  and never touches the real profile during smoke, capture, or validation
  modes. Linked files/directories, oversized/malformed records, partial fields,
  and future schemas fail closed; future bytes are preserved and downgrade
  writes are blocked. iOS now resolves a protected private Application Support
  settings leaf off-main, loads and saves through a serial adapter, prepares
  immutable Metal candidates on a separate worker, and publishes only after a
  fresh main-thread surface/revision validation. A durable candidate that
  becomes stale is prepared again without another save, and first presentation
  waits for startup resolution. iOS now exposes the six persisted fields
  through a safe-area UIKit panel backed by a portable applied/draft/ticket
  model. Apply reports success only after durable save and Metal publication;
  touch and controller navigation use an explicit paused menu-input
  boundary and closing never auto-resumes. Windows binds the same model to a
  DPI-aware DirectWrite/Direct2D raster composed by D3D11, with keyboard,
  physical-pixel mouse, wheel, and gamepad navigation. Its synchronous
  transaction prepares GPU state, durably saves only the unmasked persistent
  base, and publishes last; an ambiguous save is accepted only when a fresh
  valid current record matches. Session-only launch overrides never enter the
  store. Pull-request Actions compile and link the complete iPhoneOS and
  iPhoneSimulator products successfully; physical-device iOS interaction and
  persistence acceptance remain pending.
- The native Windows x64 product foundation is implemented. A statically
  linked SDL3 shell owns the window and lifecycle events; a separate
  D3D11/DXGI backend compiles HLSL, uploads the same public scene used by Metal,
  preserves shared draw-command order, supports resize, and falls back from
  hardware D3D11 to WARP. Its hidden CTest mode reads the real back buffer and
  rejects a clear-only frame. SDL3 keyboard/mouse/standardized-gamepad events
  now feed the shared 60 Hz semantic input path with ordered tap preservation,
  dead-zone/trigger normalization, hot-plug, disconnect release, and focus
  neutralization. An authenticated mission now enters an explicit paused-ready
  session; pause/menu is the only resume path. Each eligible Windows frame is
  consumed exactly once by the same deterministic player-state boundary as
  iOS, and pause, focus loss, controller disconnect, or a rejected transition
  resets every physical source and never auto-resumes. The diagnostic
  state/hash changes, but the player remains at the authenticated spawn because
  no flight law is inferred from input intentions. One portable planner now
  validates exact pose limits, retained bytes, and bit-identical step-zero
  publication for both native renderers. D3D11 owns that pose runtime with its
  mission snapshot, retains one coherent pose lease through each gameplay draw
  pass, and exposes the same replacement-safe weak pose endpoint as Metal.
  Windows now also imports an owner-selected `.afpack` through the product CLI
  into its undisclosed SDL-private `content/` root. The one-shot transaction
  generates its own canonical UUID, rejects competing same-session import
  processes,
  preserves the old active generation on ordinary failure, redacts filesystem
  diagnostics, and supports validation or mission launch through
  `--installed-content` without repeating the host path. Native picker,
  progress, retry, and rollback-choice UI now run as a separate pre-game
  `--manage-installed-content` workflow over this same transaction. The
  manager offers rollback only for an authenticated previous generation,
  requires restoring that known-good generation before replacement import,
  allows only retry/close for an indeterminate store,
  requests cooperative cancellation, re-inspects after every ordinary
  operation, terminates on an ambiguous commit, and
  echoes no private path, checksum, generation, or backend diagnostic outside
  the owner-controlled OS picker.
  Windows now also owns the complete bounded gameplay-camera mission runtime
  already used by Metal. It validates the initial step-0/generation-1 packet,
  exposes a replacement-safe weak camera endpoint, and retains exactly one
  current camera lease across layout construction and every draw in a frame.
  A separate XAudio2 2.9 backend consumes the new bounded portable
  command/PCM16 contract, owns copied clips, pauses with focus, recovers
  outside its callback, and accepts commands safely without an output
  endpoint. The shared AirCraft audio
  coordinator now preserves the recovered
  phase-transition/destroyed-dive/modulation order. Windows can authenticate an
  owner-local AFPACK root, decode the six recovered PCM16 samples through the
  same retained content session, register the complete clip set, and derive
  explicit role/voice bindings. An explicit mission request now builds the
  manifest and complete room through that same session, prepares dense D3D11
  geometry and authored/generated texture mip chains, bootstraps the recovered
  camera projection, and renders with reverse depth through the shared
  full-target Hor+ layout. The hidden private validator requires visible GPU
  output, and a separate one-shot mode saves that checked frame as a
  non-overwriting private BMP for local parity work; an optional capture extent
  is accepted only when it matches the physical backbuffer. The public product
  smoke remains synthetic and data-less. The renderer is ready to consume
  changing camera generations, but the room, player, and camera remain at
  their bootstrap state because the trace-driven 12 ms AirCraft producer is
  not connected. The 60 Hz input loop deliberately does not advance the
  camera runtime, so this is not yet a playable build.
- The native iOS audio adapter consumes the same monotonic command batches.
  It converts bounded PCM16 registrations once into AVAudioEngine's standard
  deinterleaved Float32 representation, uses one player/varispeed graph per
  voice, retains only looping state across graph recovery, and rejects stale
  completion callbacks through voice generations. Ambient-session activation
  occurs only after deliberate gameplay resume. Pause/background paths
  deactivate it; interruption, route loss, and media-service reset force
  gameplay pause and never auto-resume. Mission preparation now carries the six
  authenticated owner-local samples through the opaque snapshot, registers all
  of them in a replacement backend, and commits renderer, backend, bindings,
  and ticket atomically. Live aircraft scheduling and physical-device audible/
  route acceptance remain pending.
- Cross-platform input/control/haptics system specified; semantic input
  architecture recorded in ADR-0002. Controller Profile Core V1 now adds a
  bounded immutable profile, strict controller-only remapping and recovery
  validation, deterministic integer Q15 calibration, complete replacement
  binding compilation, a canonical SHA-256-protected AFIP document, and a
  private durable current/backup/default store. An immutable runtime
  configuration now binds the resolved profile to its exact compiled table and
  used-control mask. Windows interactive startup loads the private AFIP store;
  iOS now loads the same AFIP store asynchronously from a protected private
  Application Support settings leaf before using the one-time pre-start seam.
  AFIP and render settings share one native serial persistence queue. Missing
  or unavailable storage selects the in-memory canonical default without
  starting input early; backup/default recovery and read-only state are shown
  only as path-free diagnostic categories.
  Unmapped controls are omitted from full snapshots and later events without
  weakening router rejection. Live replacement remains deferred until a
  host-owned pause transaction exists; a changed durable profile applies after
  restart. Configured axes preserve every changed calibrated value, triggers
  use an explicit fixed binary V1 policy, and iOS covers D-pad left/right.
  Windows now adds a DPI-aware pause-menu calibration editor for all four
  standardized stick axes. It uses the exact runtime transport/profile
  transform for a rate-limited raw/adjusted preview, validates every complete
  draft, preserves bindings, and saves atomically for the next launch without
  mutating the active SDL adapter. Backup/default recovery can request an
  explicit repair write; unavailable or blocked persistence disables only the
  save action. A synthetic, data-less capture mode proves the surface without
  reading the private store. iOS now provides the corresponding safe-area,
  touch/controller-operated editor and asynchronous durable save. It exposes
  only a connected flag and four raw standardized Q15 axes, rate-limits the
  exact shared raw/adjusted preview, uses validated menu bindings, and never
  mutates the active pre-start configuration. Current-only exact readback is
  the sole recovery proof for an ambiguous commit; all preview samples are
  zeroed at reset/lifecycle boundaries. ADR-0015 now adds a portable typed
  catalog and a bounded seven-action remap transaction to the same draft:
  moving to an unused control is atomic, conflicts cancel by default, only
  another unique supported action can be explicitly swapped, and protected or
  custom layouts fail closed. Reset restores default bindings without changing
  calibration, and save remains next-launch-only. Native text pickers now
  project that exact transaction through keyboard/mouse/controller input on
  Windows and touch/controller input on iOS. The iOS surface uses Dynamic Type,
  safe-area scrolling, accessibility labels, and a separate cancel-first
  conflict screen. Windows now also publishes every logical panel row even
  when DPI/UI-scale clipping moves it offscreen, derives raster and future
  assistive text from one bounded source, and accepts fail-closed owner-thread
  focus/invoke/decrement/increment actions. A monotonic semantic generation
  rejects delayed same-screen/ABA actions while ignoring preview-only analog
  samples. A Windows-only COM provider now attaches through `WM_GETOBJECT`,
  publishes stable bounded fragments and events, and returns typed actions to
  the SDL owner thread through a fixed 32-entry queue. Physical Narrator
  acceptance, glyphs, haptics, live replacement, and physical-device
  persistence acceptance remain.
- Local Git repository initialized on branch `main`; planning baseline committed
  as `59828ed`.
- GitHub remote `tryk016/airfix-dogfighter` connected and the planning baseline
  published.
- Phase 1 toolchain installed and pinned: Ghidra 12.1.2, Temurin JDK 21.0.11,
  Python 3.14.6, CMake 4.4.0, Ninja 1.13.2, LLVM 22.1.8, and GCC 15.2.0.
- Added a local-only reverse-engineering workbench around canonical Ghidra
  headless reports, scriptable Rizin/rzpipe reports, Cutter inspection, and a
  prepared but not yet executed x32dbg dynamic-analysis stage. Verified
  archives, databases, traces, working copies, and original-derived reports
  remain outside Git.
- Established a dedicated, isolated `Airfix-Dev` WSL2 distribution. Automount
  and Windows interop are disabled; a native Linux CMake/Ninja build completed
  135 targets and passed all 39 tests without modifying other WSL
  distributions.
- Cross-checked one rendering, one input, and one aircraft-flight function.
  Ghidra produced the most reliable ABI and pseudocode; Rizin independently
  matched exact boundaries and useful call/data sites, but misclassified
  calling conventions and needed an explicit, recorded function creation for
  the large flight routine.
- Initial LLVM report for `UdsPack.dll` found 44 named C++ exports covering
  `UpPackage`, `UpFile`, `UpFileInfo`, and `UpHashTable`.
- Recovered the complete header layout, both 24-byte record layouts, legacy
  signed-byte name hash, and compression opcodes for FMT-UDSP.
- Implemented portable C++20 metadata parser, listing/verifier CLI, decompressor,
  and malformed-input tests.
- All five supplied archives and all 1,911 compressed streams pass the new
  parser; original data remains external and read-only.
- Added three-platform portable CI including native ARM64 `macos-26`.
- Pull requests now pass through a fail-closed change classifier.
  Documentation-only changes retain public-boundary, Markdown-link,
  experiment/function-ledger,
  deterministic RE-tool, runtime-capture, and whitespace gates while skipping
  native build runners; every unclassified or build-affecting path retains the
  full Windows, Linux, macOS, iPhoneOS, and iPhoneSimulator matrix. Portable
  Ninja/Make builds use pinned `sccache`, with a weekly and cold-by-default
  manual run that deliberately bypasses the compiler launcher.
- Generated reproducible LLVM sections/imports/exports reports for all 16 PE
  modules and confirmed the plugin factory ABI names.
- Changed normal UDSP file opening to read only the header and metadata tail;
  `Resource.up` metadata buffering fell from 170,642,453 to 130,961 bytes.
- Added the stable-source UDSP foundation. `Archive::open(path)` now uses one
  handle, while `open(stream, sourceLabel)` and indexed stream entry reads keep
  the caller's binary seekable handle and treat the label as diagnostics only.
  Exact current size, archive/payload regions, index and output limits, and
  `streamoff`/`streamsize` representability are checked before the relevant
  allocation or I/O. Stream position is unspecified and shared access must be
  serialized; legacy `path + FileEntry` reads remain convenience APIs without
  backing-object provenance.
- Synthetic UDSP tests cover standalone and embedded prefix/archive/suffix
  layouts, compressed and uncompressed reads, zero-byte prefixes, poisoned
  labels, bad indices, exact/one-under limits, size mismatches, suffix
  containment, and sparse pre-allocation stream-limit rejection. This is not
  yet an end-to-end installed-AFPACK runtime asset path.
- Draft PR #1 is published; portable CI passes on Ubuntu, Windows, and ARM64
  macOS 26, including the post-fix AFPACK streaming tests on Windows.
- Added a data-less UIKit/Metal iPhone shell generated through CMake, a portable
  lifecycle/content state machine, and a public-source boundary scanner.
- Added a bounded public-data Metal adapter that consumes
  `DrawSubmissionPlan`, owns buffers per mesh, and renders two reusable meshes
  through three ordered instances (`1 -> 0 -> 1`). Commands select transforms
  by `instanceIndex`; valid texture ID zero and the explicit one-pixel
  missing/coordinate-free fallback remain distinct. Private content is not
  bundled.
- Unsigned ARM64 `iphoneos` and `iphonesimulator` builds pass on Xcode 26.6 with
  verified minimum iOS 16.4 and no private files in either bundle.
- Recovered package-chain startup and the type/mode/graphics plugin discovery,
  version gates, metadata, factory calls, and unloading behavior.
- Classified `Dogfighter.exe` as the usable v1.01 bootstrap reference and the
  near-maximal-entropy `.icd` as an older protected/compressed artifact.
- Specified AFPACK v1 and implemented its bounded parser, deterministic local
  writer, streaming SHA-256 verifier, normalized-path checks, and atomic partial
  output behavior.
- Added explicit archive/metadata/entry/path/string-table/payload ceilings before
  allocation or reads. Bounded file-backed entry reads verify SHA-256 and reject
  both changed file size and same-size payload replacement.
- Implemented strict bounded JSON and semantic validation for AFPACK manifest
  v1, including exact game/source/capability compatibility, locale allowlisting,
  and a one-to-one audit against the container entry table.
- Added parser limits for nested UDSP archive/metadata/tables/names and declared
  stored, per-entry unpacked, and aggregate unpacked sizes. Checks precede
  metadata allocation and the real Resource archive passes the default policy.
- Added one-handle AFPACK validation across metadata, payload, manifest, and
  nested UDSP regions, followed by a second authentication pass that rejects
  same-size mutation during semantic validation.
- Defined and implemented canonical AFAC v1 activation records. Active and
  rollback package names are derived only from SHA-256, with bounded sizes,
  exact layouts, strict generation handling, and no stored input paths.
- Added cross-platform durable file primitives for exclusive creation, atomic
  replacement, and no-replace publication, including source-identity and path
  alias checks for the private installer transaction boundary.
- Composed those primitives into a bounded, cancellable private AFPACK
  installer with content-addressed publication, validated collision reuse,
  idempotent AFAC rotation, and an explicit recovery result for ambiguous
  post-rename durability. The portable implementation is linked into iOS.
- Implemented bounded startup content inspection and verified rollback. It
  authenticates only AFAC-derived current/previous packs, distinguishes
  malformed, unusable, and transient-unavailable states, rechecks stale active
  state on rollback, and requires exact durable readback after commit.
- Startup inspection now retains the authenticated pack handle in a move-only
  `ActiveContentLease` together with AFPACK, manifest, and exact
  `source/Resource.up` metadata. `VerifiedContentSession` adopts this proof
  without a pathname reopen, redundant full hash, or repeated metadata parse.
- Added the native iOS private-content coordinator: document picker, bounded
  security-scoped copy into app-private storage, serialized install/inspection/
  rollback, lifecycle cancellation, progress and recovery UI, strict orphan
  temporary cleanup, and `AppSession` readiness gating.
- The native coordinator now consumes the exact ready `ActiveContentLease` into
  a `VerifiedContentSession` on its serialized content worker. Authenticated
  handles never move to the main or render threads, and install/rollback closes
  every reader before entering the serialized writer transaction.
- Added bounded UDSP-in-container reads and completed a real English content
  pack round trip (192,755,765 bytes, 3 entries) outside Git; the temporary pack
  was removed after verification.
- Inventoried the selected Resource/English payload set and validated portable
  GTI metadata parsing for 1,985 files/3,990 image variants plus CCF framing for
  all 286 scene signatures.
- Recovered GTI pixel-size/alpha/palette semantics and CCF's inclusive
  six-byte nested chunks plus the four stable top-level scene sections.
- Indexed every direct child in all 286 CCF scenes; material/room IDs are
  stable, and the independent blueprint/placed-node tables have equal physical
  counts in every file but no assumed index mapping.
- Implemented faithful base-level RGBA8 conversion for every observed GTI pixel
  format and decoded all 3,990 variants; a private aircraft texture diagnostic
  confirms channel/orientation correctness and the paletted/32-bit paths agree
  closely.
- Implemented bounded `AfChunkContainer` framing and object-definition parsing;
  all 299 selected containers/7,376 chunks and all 215 object definitions pass.
- Implemented bounded HOUS, complete observed FHOU placement/state, BRIF, and
  PATH semantics. Read-only validation covers 29 worlds/493 rooms/47,589 radar
  line records; 31 levels with 2,444 static placements, 1,176 model placements,
  and 172 runtime-state records/643 words; 23 briefings; and 833 path deltas.
- Added a metadata-only level dependency resolver for the world and every
  static/model object definition. It preserves source order/data, reports
  missing/invalid/not-found/unique/ambiguous paths, applies aggregate limits,
  and never reads payloads or guesses extensions. All 31 world, 2,444 static,
  and 1,176 model references resolve uniquely in the selected corpus.
- Implemented strict CCF material metadata parsing for all 10,385 records,
  exposing material names/references and 10,256 primary plus 263 environment
  texture dependencies without assigning unproven numeric semantics.
- Audited the CC0 `RonnyReverse/cc-tools` project at a pinned commit; its CCF
  mesh map is useful independent evidence, while the local bounded parser and
  corpus/Ghidra validation remain authoritative.
- Implemented bounded CCF mesh geometry parsing for all 6,995 records,
  validating 136,363 vertices and 170,039 indexed triangles plus UV, paint,
  transform, and control metadata without exporting proprietary geometry.
- Added bounded UDSP logical-path lookup with legacy hash/case behavior, full
  collision comparison, traversal rejection, and explicit ambiguous results as
  the first dependency-resolver primitive.
- Indexed all 9,328 CCF blueprints as 2,062 roots and 7,266 ordered edges with
  maximum depth 7 and zero missing parents, duplicate references, or cycles.
  Resolved all 215 object `CCFF`/`MESH` selectors through 90 mesh roots, 124 null
  roots, and one no-selector case.
- Implemented the portable deterministic input core: stable action IDs, Q15
  axes, bounded sequence ordering, multi-source arbitration, context releases,
  cancellation, lifecycle neutral gating, and default touch/controller/test
  bindings. Native iOS now transports the full baseline gameplay-action surface:
  a safe-area-aware multitouch flight/throttle/combat/camera/weapon overlay and
  a bounded Apple extended-controller adapter feed a validated portable batch
  reconciler and renderer-independent fixed 60 Hz pump. Context transitions,
  controller generations, FIFO overflow, lifecycle and gameplay boundaries
  reset fail closed. Remapping, profiles, menu UI, glyphs, haptics, and device
  acceptance remain pending.
- Expanded the deterministic simulation consumer. Portable
  `PlayerAircraftState` preserves uninterpreted flight/throttle/camera Q15
  intentions, primary/secondary/rear-view held state and exact edges, discrete
  weapon selection, action counters, its own completed-step count, monotonic
  source tick, and an explicit canonical hash. Invalid schema, Q15, weapon,
  tick, and counter transitions are rejected without partial mutation. A
  shared presentation coordinator commits state only with a successful or
  temporarily busy actor-pose publication; an expired endpoint or other
  publication failure is terminal and atomic. Both native products use the
  replacement-safe endpoint when an authenticated player runtime exists; a
  mission without a player remains on the explicit headless path. Both products
  advance exactly once per eligible running frame and fail closed. The
  currently published pose is still the authenticated spawn: movement,
  throttle integration, camera behavior, and weapon spawning remain
  intentionally absent.
- Recovered the 63-slot `AirCraft.type` vtable and 16 function entries missed
  by automatic analysis. The per-step method at `0x10003F40` consumes a float
  time delta and contains 15 force, five torque-only, and one force-only call
  sites. The static-collision method at `0x10007920` consumes collided polygons
  and resolves static/BSP contact lists.
- Recovered the complete player/AI control-event join. Player commands and
  analog input emit signed `PITCH_SET`, `BANK_SET`, and
  `THRUST_SET/APPLY` payloads into four `AfVehicle` fields read directly by
  the flight-force step. Discrete keyboard commands also emit
  `TURN_SET (-32/0/+32)` into a fifth sleep-gate control whose AirCraft
  force-law consumer remains unknown. The similarly named
  `THROTTLE_SET/APPLY` events are
  confirmed no-ops for the AirCraft inheritance path. A static Ghidra/Rizin
  cross-check now also fixes the exact `PITCH_SET`/`BANK_SET` branches:
  signed `int32` payloads are multiplied in x87 by the binary32 constant at
  bits `0x3D3020C5`, then overwrite vehicle `+0x448`/`+0x44C`.
  Active nonzero products clear the shared signed rest duration; active zero
  writes positive zero without clearing it; inactive events do not mutate
  either field. Keyboard, successfully range-configured DirectInput, and AI
  producers are separately bounded. The follow-up static call/data-flow pass
  now proves immediate producer order: the head-inserted input list traverses
  joystick, mouse, keyboard; one joystick snapshot supplies bank before pitch;
  mapped keyboard commands follow buffered-record and binding-text order; AI
  fully processes changed pitch before changed bank. Local input drains before
  `SetTime`; its periodic heap can run zero, one, or multiple 12 ms AirCraft
  refreshes, and eligible AI work occurs near each vehicle refresh end. Exact
  full-domain PC53/RN vectors remain a startup-compatible conditional model,
  not a branch-time observation. `EV-20260730-008` now extends the static
  numeric-policy audit across all 15 supplied runtime modules: only the
  executable imports `_controlfp`, its PC53 request precedes the game shell,
  its local initializer ranges are empty, and 4,277 Rizin-recognized functions
  contain no decoded environment writer. Live consumer-thread RC/exception
  state and external Windows/DirectX/driver behavior remain unobserved, so the
  compatibility label is not promoted to native parity. DirectInput driver
  conformance, remote-network ordering, and wall-clock cadence also remain
  explicitly unproven. The
  isolated turn/pitch/bank typed-write reducers are implemented over the full
  signed native payload domain. They return exact binary32 bits under an
  explicitly named startup-compatible PC53/nearest-even policy using shared
  host-FP-independent integer rounding. A simulation-thread-confined owner
  commits those writes and the thrust writes with their shared rest clear as
  one transaction and exposes the five controls in native sleep-gate order.
  The discrete command callback is now also represented by a pure eight-flag
  reducer: each already-ordered bool invocation updates exactly one flag and
  emits one typed `TURN_SET`, `PITCH_SET`, `BANK_SET`, or `THRUST_APPLY`
  input, including repeats, opposite-held releases, and zero releases. It is
  intentionally separate from the Q15 `InputFrame`, which cannot retain
  opposing-command order or in-tick taps. One allocation-free step now
  composes exactly one such caller-ordered invocation through the matching
  decoder and transactional field owner. Its staged result retains the earlier
  callback flag update when an inactive event is ignored or an active
  reconstruction policy is rejected, while control-event state changes only
  after a typed write commits. It owns no batch, replay format, producer,
  source order, Q15 conversion, or clock. A separate pitch/bank-only step now
  gives already-formed analog or AI `PITCH_SET`/`BANK_SET` events the same
  one-event decoder-to-owner boundary without passing through the discrete
  callback flags. Its only order is call order: it preserves joystick
  BANK-before-PITCH, AI PITCH-before-BANK, keyboard arrival order, equal
  repeats, active zero, inactive drops, and last-processed SET per axis. It
  deliberately exposes no collection, producer tag, retry, replay, timing, or
  generalized turn/thrust API.
  Live producer, scheduler, force-law, and player-state wiring remains
  intentionally absent.
- `EV-20260803-001` closes one conditional AirCraft AI-to-physics subcycle.
  Numeric task events set modes `1`, `4`, and `3` from a DWORD UID, a vec3 plus
  room ID, or one byte; `DiscardTask` returns any active mode to zero. The
  selected mode-1 target-loss path clears the task/target fields and, under
  explicit finite predicates, leaves raw controls `{50,0,0,-100,-100}`.
  Its dispatcher tests thrust, pitch, bank, primary attack, then secondary
  attack, fully processing each present event before the next. Under an
  explicitly labelled PC53/nearest-even oracle and the reachable prior cache
  vector `{255,32,32,1,1}`, the signed payloads are `{128,0,0,0,0}`. The
  pass also corrects the earlier unconditional AI change-suppression model:
  `HasChanged` uses f32
  `0x3C23D70A`, while `GetRelative` uses f64
  `0x3F847AE147AE147B`, so unchanged raw zero still reports changed at
  PC53/nearest-even. Live PC/RC and imported `_ftol` policy remain unknown.
  AI writes occur after the current force step; a later force step consumes
  the fields and the following Euler step consumes those accumulators.
  Numeric state/layout, ordered dispatch, event-to-field reduction, and this
  structural phase join are GO. Full behavior, task-wrapper semantics,
  bit-parity conversion/rigid-body runtime, wall-clock cadence, and
  cross-producer ordering remain NO-GO.
- The recovered `AIControls` layout and five-event AirCraft dispatcher now
  have an isolated portable C++20 implementation. Compile-time assertions
  preserve the four eight-float arrays and exact `0x80`-byte layout. The
  dispatcher preserves `thrust -> pitch -> bank -> primary -> secondary` and
  completes `cache -> Save -> Process` synchronously for each changed channel
  before inspecting the next. Exact-bit vector tests cover all three range
  shapes, the `{128,0,0,0,0}` witness, repeated raw zero, callback mutation,
  invalid configuration, and numeric-environment rejection. It remains an
  explicit PC53/round-to-nearest-even oracle and is not connected to the live
  12 ms scheduler, Q15 input, task wrappers, vehicle owner, or rigid body.
- Recovered the scheduler-visible aircraft order:
  `EulerODE -> ResetForceAndTorque -> CalcAuxiliary -> slot45 force
  accumulation -> collision/slot30 -> slot44 refresh`. `EulerODE` consumes
  the prior accumulated forces; slot45 prepares the next integration step.
  AirCraft uses a 12 ms dependant interval converted to `0.012f` seconds.
  All 21 force/torque sites, direct state transitions, helper transforms, and
  constructor-field joins in slot45 are now statically mapped. The surrounding
  signed 64-bit rest timer, exact 2000 ms sleep gate, ground/water speed
  thresholds, exported `IsOnGround` byte, and threshold transition are also
  recovered. Slot 44's engine-start transition now proves the `+0x564`
  smoothing branch, while slot 30 and slot 44 prove the full `+0x568`
  collision-degraded thrust-integrity lifecycle and its next-step timing.
  Slot 44's counter now also proves the five-call engine-audio cadence, exact
  start/running/stop command order, five sound roles, and the speed/thrust/
  orientation pitch-volume equations. Isolated allocation-free C++20 helpers
  specify sleep; already-formed native `THRUST_SET/APPLY` typed writes and
  nonzero rest-clear directives; the ordered health-gated target/apply/clamp followed
  by unconditional smoothing in an executed slot-45 step; collision
  degradation; recovery/clamp; and the bounded engine-only command stream
  without pretending the frozen player simulation owns scheduling, event
  sampling, rigid-body integration, or audio playback. Physical units,
  Q15-to-native event timing, runtime contact/audio traces, deterministic PRNG
  ownership, full x87 numeric tolerance, mixer integration, and dynamic
  actor-to-render publication remain unknown.
- `EV-20260730-004` now bounds the isolated `CcRigidBody` integrator itself.
  The state is exactly 13 binary32 values at `+0x40`: position,
  `(w,x,y,z)` quaternion, linear momentum, and world angular momentum.
  `Derive` reconstructs `R`, linear velocity,
  `R * bodyInverseInertia * transpose(R)`, and row-vector angular velocity;
  it then emits the left-Hamilton quaternion derivative, accumulated force,
  and torque minus angular-momentum damping. The report records every material
  x87 operation and binary32 spill, including matrix `k=0,2,1` accumulation,
  quaternion helper temporaries, and the X/Y-stored versus Z-retained damping
  asymmetry. `EulerODE` updates all 13 lanes before `PostODE` normalization;
  reset and post-step auxiliary refresh remain explicit later phases. Ghidra
  12.1.2 and Rizin 0.9.1 agree on all main and helper boundaries. Exact
  PC24/RNE, PC53/RNE, and PC64/RNE discriminators are documented, but the live
  x87 control word and exception state remain unknown, so no pure kernel or
  runtime integration was added. The complete supplied-module static audit
  finds no later recognized environment writer or mutator import, narrowing
  the remaining numeric work to a hardware-breakpoint consumer-site CW/SW
  capture without changing that implementation NO-GO. An offline exact-
  rational oracle now executes the finite recovered schedule under explicit
  PC24/PC53/PC64 and rounding-mode policies, including every documented
  binary32 spill, post-Euler normalization, reset, and auxiliary refresh. It
  rejects unobserved non-finite/exception behavior and is not product runtime.
  A public, fail-closed capture validator now authenticates the exact
  module/RVA sites, decodes raw
  CW/SW/MXCSR, enforces one thread and stable policy, checks zero/one/many
  12 ms refresh ordering, and executes the 11 event-store plus computed V1-V6
  exact-bit oracles without a duplicate rigid-body result table. Its tests are
  entirely synthetic; no real capture has been run, so
  the rigid-body and live-flight NO-GO remains unchanged.
- Recovered the complete player spawn/type/primary-actor event chain and the
  fixed 16-entry mission start table. The selector uses requested index modulo
  count, with the primary receiving CCF room as the empty-table fallback.
- Added a bounded, non-executing parser for the exact AFS `AddStartPos` call and
  an atomic exact-name resolver to physical `CcfMetadata::rooms`. A private
  aggregate census resolves all 20 campaign setup starts uniquely with zero
  parse, missing, ambiguous, or capacity issues; no original script, room name,
  or path is published. This legacy helper deliberately remains a
  selected-main-CCF normalizer; the ordered world catalog below is the
  multi-source lookup contract.
- Recovered `CcName` and `CcWorld::GetRoomByName`, the exact mission-root CCF
  load order/flags, anonymous-root lifetime, newest-first ordinary-room lookup,
  and the gate that runs `AddStartPos` only after world initialization. Added a
  bounded ordered multi-CCF catalog with distinct runtime-room indices,
  source/physical contributor lists, root/full-name and ordinary/name-only
  matching, ASCII-only parity, and fail-closed world start selection.
- A private full-sequence census covers 20 main CCFs, seven backgrounds, and
  2,101 object CCF load calls. All 20 starts resolve uniquely to ordinary main
  rooms; backgrounds and object CCFs add no named ordinary rooms, and the
  effective namespace has no non-empty exact/ASCII-fold collision. Only these
  aggregate counts are published.
- Recovered the per-load `CcLoadedScene` room-reference lifetime, placed record
  masks, source-local mesh/material identity, receiver fallback, room content
  ownership, and `FreezeAll` rebuild boundary. A bounded source-aware mission
  draw plan now scans each enabled source once in load/physical order, applies
  transient last-wrapper room-reference rebinding, emits root fallback exactly
  once per placed record, and suppresses all placed geometry for flag `0x2000`.
- Added atomic multi-CCF runtime-room assembly with global first-use mesh slots,
  per-source material binding, parallel numeric provenance, aggregate limits,
  and direct compatibility with the existing `DrawSubmissionPlan`. Repeated
  room sections, two CCFs contributing one ordinary room, cross-source local-ID
  collisions, same-source mesh reuse, disabled placed scenes, late failures,
  forged state, and exact/one-under limits have synthetic coverage.
- Recovered the grouped player-aircraft visual as the selected skin's up to
  three complete blueprint hierarchies. The broader actor-owned UID graph also
  includes weapons and auxiliary effects and is not a valid visual selector.
  The original roles of the three blueprint slots remain deliberately unnamed.
- Added deterministic headless Ghidra exporters for exact scalar values and
  instruction listings. The report wrapper can now process a literal program
  already in the ignored local Ghidra project without requiring or recording
  the original file path; its import/reuse/error contracts have Windows tests.
- Established the recovered right-handed CCF basis, row-vector matrix
  convention, reverse-cross winding, degenerate `+Y` normal, and raw UV policy;
  the bounded API-neutral converter validates all 6,995 corpus meshes.
- Implemented owned RGBA8 decoding for complete GTI mip chains. Corpus policy
  uploads all authored levels for 3,974 variants and uses base-plus-Metal
  generation for 16 legacy dimension anomalies, decoding 9,539 levels safely.
- Recovered the engine's exact `TEXU\\source.gti` lookup rule. Full selected
  subtrees cover 2,687 blueprint nodes, 1,858 mesh instances, 2,842 material
  uses, and 2,959 uniquely resolved texture edges with no dependency errors.
- Implemented a deterministic seam-safe draw-mesh payload plus bounded CPU
  rasterizer. A private 111-triangle textured model renders correctly in both
  explicit V policies; generated PPM/PNG files remain ignored and outside Git.
- Confirmed that object loading skips the independent `0x4000` placed table,
  selects a `0x3000` blueprint by name, and recursively instances descendants.
  The bounded graph resolver and multi-instance diagnostic render a private
  grouped aircraft containing 46 nodes, 25 mesh instances, 21 null groups, 663
  triangles, and maximum depth 3. Raw V preserves its markings; explicit V flip
  does not. Private outputs remain ignored and outside Git.
- Decoded all independent `0x4000` placed object/null/light records, including
  transforms, references, bounded light properties, zero-copy null payloads,
  and opaque BSP containers. All 286 CCF scenes parse 9,328 placed records:
  6,995 objects, 2,130 nulls, and 203 lights, all using the matrix orientation.
- Parsed all 392 CCF room prefixes and implemented bounded placed-scene
  reference resolution across instantiated nodes, mesh prototypes, rooms, and
  portals. The selected corpus forms 2,062 roots and 7,266 edges at maximum
  depth 7 with zero missing, ambiguous, or cyclic dependencies.
- Decoded bounded room fog and static/portal BSP into flat iterative arenas:
  98,095 nodes and 198,210 polygon descriptors across all 286 scenes. The
  fail-closed spatial resolver joins every descriptor to its unique placed
  object, mesh, in-range triangle, ordinary room, and (for all 332 portal
  descriptors) target room with zero corpus issues.
- Implemented a bounded conservative explicit-CCF-room draw plan and backend-
  neutral assembly. They ignore BSP visibility, share first-use meshes,
  preserve physical instance order, and apply each placed authored-world
  transform exactly once. Across all 392 rooms in 286 scenes, coverage proves
  that all 6,995 objects appear exactly once; assembly yields 170,039
  triangles with zero plan, transform, geometry, material, or coverage issue.
- Added exact world `CCFF` source-entry validation, a generic bounded
  `TEXU\\source.gti` resolver, and a fail-closed runtime texture plan. Expected
  dependencies are checked one-to-one before dense first-use IDs are assigned
  across primary, secondary, and environment roles.
- Added backend-neutral GTI upload metadata with the recovered `8, 7, 4, 3, 6`
  variant preference, authored-chain/base-plus-generated-mips policy, hard
  parser bounds, and checked decoded/upload/resident RGBA8 budgets. Complete
  object-subtree validation resolves 2,959 edges into 614 per-object unique
  import requests: 612 authored-chain and two generated-chain imports.
- Added atomic GTI runtime materialization. `prepareGtiUpload` enforces the
  source limit before parsing, composes parse/plan/decode, validates every
  decoded level and aggregate byte total against its plan, and publishes the
  plan plus owned RGBA8 levels only together. Authored chains materialize in
  full; legacy anomalies expose only the valid base for backend mip generation.
- Added fail-closed backend draw submission. `buildDrawSubmissionPlan`
  preflights all aggregate/source/command limits before output allocation,
  validates complete mesh/range/bounds/transform/material/texture invariants,
  and emits deterministic mesh uploads plus instance-then-range commands only
  on complete success. Optional primary/secondary/environment roles and valid
  texture ID zero remain distinct.
- Wired that plan into the synthetic Metal adapter. Vertex/index sizes, draw
  offsets and ranges, instance/mesh relationships, a 64 MiB packed-CPU budget,
  a 64 MiB aggregate GPU budget, and `device.maxBufferLength` are checked
  before packed payload or Metal-buffer allocation. Valid empty/no-draw meshes
  retain nullable resources in strongly owned per-mesh wrappers; commands may
  reference only populated resources. Renderer state is published only after
  the entire pipeline, buffer, texture, and immutable resource snapshot is
  ready, with C++ failures contained at the Objective-C boundary.
- All 29 world definitions bind uniquely to their named CCF. Explicit physical
  CCF-room selection validates all 135 rooms and assigns all 4,911 objects
  exactly once to 105 non-empty ordinary rooms. All 29 receiver rooms and one
  ordinary room are valid empty plans. The World `ROOM` catalog is not treated
  as an index, ID, name, or reference join to CCF rooms, and BSP remains
  disabled as a visibility rule.
- Implemented finite, invertible parent-relative local transform derivation and
  composition in runtime column-vector order. Round-trip tests include
  non-commutative rotations and shear; `rawScalar` is not treated as scale.
- Added the portable `airfix_content` ownership boundary. Recovery returns a
  move-only lease that binds its already authenticated AFPACK handle to
  `{generation, size, digest}`, parsed pack/manifest, and exact
  `source/Resource.up` archive. `VerifiedContentSession::adopt` consumes it
  without reopen, rehash, or reparse and exposes only serialized bounded nested
  reads. A separate synthetic-stream entry point still covers wrong
  size/digest/generation, cancellation during hashing, malformed source UDSP,
  exact/one-under limits, and a poisoned label that points at another real file.
- Implemented an atomic `WorldRoomLoader` on that session. A request contains
  only an exact World logical path and a legacy single-room index; CCF
  and GTI entry indices are derived internally from the same archive. The
  loader validates World -> `CCFF`, room dependencies, dense first-use texture
  IDs, GTI plans and RGBA data, room assembly, and draw submission before
  publishing one revision-tagged result. It preserves raw UVs and does not use
  World `ROOM` or BSP as an inferred selection/visibility rule.
- Global limits cover unique texture count, aggregate source bytes,
  decoded/upload/resident RGBA, and published CPU data. GTI plan-only
  preflight rejects exact aggregate overruns before decode; compressed source
  budgets account for the simultaneous stored and unpacked buffers before I/O.
  Synthetic end-to-end tests cover valid/empty rooms, exact `CCFF` among two
  CCFs, texture ID zero, two-texture budgets, deduplication, cancellation,
  malformed later GTI atomicity, compressed peak allocation, and published CPU
  exact/one-under bounds.
- A private external smoke run exercised the production writer, installer,
  AFAC inspection, same-handle lease adoption, and `WorldRoomLoader` against an
  ordinary room from the owner's original content. It published 62 meshes,
  62 instances, 23 dense textures, and 167 validated draw commands. Generated
  private artifacts and original-derived bytes never entered Git.
- Added a portable `WorldRoomPublicationGate` and the native mission handoff.
  Main-thread state binds each request to the exact active
  `{generation, size, digest}` revision and a monotonically increasing serial;
  replacement, lifecycle invalidation, content changes, and serial exhaustion
  fail closed. Exact ticket/revision `consume` and `abandon` operations are
  additionally confined to the gate's owner thread; stale or cross-thread
  completion cannot mutate a newer request.
- The native request is an explicit private setup logical path, Level logical
  path, and `uint32_t` start index. The content worker builds
  `MissionLoadManifest` and then `LoadedMissionWorldRoom` through the same
  pinned `VerifiedContentSession` and revision. A private Objective-C++
  snapshot owns that aggregate C++ result exactly once, exposes aggregate
  counts only, and does not expose or log the requested paths. A stale,
  unconsumed payload is detached and destroyed on a serial off-main queue.
- Implemented two-phase aggregate-mission Metal publication. Complete mesh buffers,
  authored RGBA8 mip levels, generated mip chains, texture arrays, draw offsets,
  payload, submission state, setup/start selection, and numeric provenance are
  prepared off-main. On main, renderer validation is read-only, then the exact
  current publication ticket is consumed and the already validated candidate
  is installed with a no-fail constant-time strong-pointer assignment. A
  failed or discarded current candidate is conditionally abandoned; stale
  candidates cannot clear newer tickets. Preparation checks a 128 MiB packed-CPU
  ceiling, 256 MiB logical and heap-plan ceilings, and
  `device.maxBufferLength`. Buffers and textures are created only from retained
  shared/tracked Metal heaps. CPU texture pixels are released after Metal owns
  the complete texture set, while setup/start selection and mesh/instance
  provenance remain retained by the published room.
- A move-only reservation token admits the heap descriptor plan before
  allocation, then charges each accepted snapshot by the measured
  `MTLHeap.currentAllocatedSize` exactly once. Page-rounding growth must obtain a
  supplemental reservation before publication. The 384 MiB aggregate ledger
  includes candidates, current and retired snapshots, and GPU-in-flight owners.
  Because Metal publishes no pre-creation upper bound for heap page rounding,
  the short unpublished creation window is not claimed as a hard physical byte
  ceiling.
- Draw submissions retain their immutable snapshot through the Metal command
  buffer completion callback, while final large-resource destruction is
  dispatched to a serial off-main release queue. Resources and heaps are
  destroyed before their exact measured debit is released.
- Added a neutral scene-bound dynamic pose exchange. It publishes bounded,
  sorted absolute instance transforms through two preallocated SPSC slots,
  rejects stale identities and malformed frames atomically, and uses stable
  RAII leases plus authored-transform fallback. Its shared control block keeps
  outstanding leases safe after exchange destruction and prevents allocator
  address reuse from reviving stale handles. The authenticated player runtime
  now connects this exchange to Metal and the recovered primary-actor/
  skin-hierarchy contract.
- Added global source-aware mission texture binding. It replays the canonical
  room plan, validates every supplied CCF logical-path/file-index pair against
  the same UDSP metadata, resolves each source-local `TEXU`, preserves
  `{sourceIndex, materialReference}` isolation, and assigns dense global IDs by
  first authenticated archive-file use. The same GTI is deduplicated across
  sources and roles without reading its payload. The caller must bind parsed
  metadata and the entry index in one immutable load transaction; the binder
  does not prove independently constructed metadata provenance. Late-source,
  ambiguous, forged, overflow, and exact/one-under limit failures clear all
  output.
- The mission texture-binding regression target brought the portable suite to
  40/40.
- Added the authenticated `MissionLoadManifest` metadata transaction. The
  move-only, private-constructor result retains the exact `ContentRevision` and
  owns every published path/index identity. Moving transfers validity and
  explicitly invalidates the source; `valid()`, `belongsTo()`, and result
  `success()` reject moved-from state. During construction, the builder also
  pins the opaque identity of the exact authenticated handle. The request
  requires independent explicit setup and Level logical paths. It performs
  exact setup lookup only; there is no Level-name derivation, extension search,
  sibling search, or path fallback.
- One `VerifiedContentSession` handle reads the setup AFS, Level, World, and
  every unique physical Level `OBJE` definition. The non-executing setup parser
  recognizes only bounded `AddStartPos` calls, accepts finite values, and
  enforces the fixed 16-entry legacy capacity. The manifest owns the canonical
  setup path/index, checked setup-source footprint, and parsed starts; it
  retains no raw AFS bytes or parser views. A present but empty authenticated
  setup is valid. The handle token is not published: `belongsTo()` means the
  same authenticated `ContentRevision` and intentionally permits a separately
  authenticated session for those bytes.
- Exact unique lookup continues from Level -> World -> main `CCFF`, optional
  first `BCKD`, and every physical `OBJE` definition -> `CCFF`. Repeated
  object-definition archive indices reuse one parse-cache entry, while CCF
  descriptors retain main/backdrop/physical-OBJE order and repeated entries. A
  referenced `OBJE` that parses with a `MODL` root fails closed. Level `MODL`
  paths are resolved as metadata only, are not read, and never become
  mission-root CCF sources.
- No CCF or GTI payload is read in this phase. Per-entry compressed-source peak,
  aggregate unique definition source, per-entry and aggregate planned CCF,
  descriptor-count, and published-CPU limits are checked before publication;
  the conservative CCF aggregate charges every descriptor, including repeats.
  Cancellation, callback exceptions, allocation/overflow, or any late
  lookup/read/parse/type/limit error clears the candidate and returns no partial
  manifest. World/`OBJE`/`MODL` resolution is cancellable per item, and a
  callback-driven session replacement or move-out returns
  `sessionIdentityChanged`, even with the same revision. Handle identity and
  revision are checked before/after callbacks and before publication.
- The manifest optionally authenticates one explicit player `MODL`-root visual
  definition. Its canonical definition and model-CCF identities, visible
  slot-zero blueprint selector, texture root, and checked source footprints are
  retained as a separate descriptor. Level `OBJE` placements still require
  `OBJE`; an `OBJE`-root player definition now fails closed. The player CCF is
  planned but never read during manifest construction and is not inserted into
  the room-catalogue load list.
- Added `MissionWorldRoomLoader`, the atomic consumer of that proof. It
  validates the full descriptor shape and exact logical-path/file-index
  identity, pins the loader session's revision and opaque transaction marker,
  and reads each unique physical CCF once through that handle. Repeated CCF
  descriptors remain separate semantic sources and produce the exact
  main/backdrop/physical-OBJE catalogue order; the result records the
  descriptor-to-cache mapping without publishing raw metadata pointers.
- The loader request contains no caller start span. It copies the
  manifest-owned starts, builds `MissionWorldRoomCatalog`, resolves the fixed
  table with modulo selection, and uses root fallback for a present-empty
  setup. It then constructs one selected-room global texture namespace,
  materializes every unique GTI, preserves mesh/instance source provenance,
  and validates `DrawSubmissionPlan`. The loaded result carries the canonical
  setup identity and source footprint without retaining raw AFS. CCF/GTI source
  footprints, decoded/upload/resident RGBA, and published CPU have independent
  checked limits. Retained CCF metadata has a separate post-parse logical
  counter; parser admission remains governed by the CCF parser's hard caps, so
  this is not a peak-allocation or process-RSS ceiling. A late CCF, GTI,
  assembly, submission, callback, cancellation, or session-identity failure
  publishes no room.
- `VerifiedContentTransactionIdentity` now uses a retained private marker rather
  than an `ifstream` address. Session moves carry the marker, copied guard
  tokens keep displaced markers alive, and independently opened same-revision
  sessions receive distinct identities, closing allocator-address ABA.
- Public synthetic coverage proves four semantic loads over three physical CCF
  reads, and a second case reuses one physical CCF across different semantic
  roots and `0x2000` placement flags. It covers legacy start selection,
  authenticated empty-setup root fallback, exact setup provenance, first-use
  texture-ID ordering, independently recomputed ownership counters, exact/N-1
  compressed and aggregate budgets, callback-time destruction of snapshotted
  inputs, separately authenticated equal revisions, moved and replaced
  sessions, identity-versus-callback-error precedence, cancellation, and late
  atomic failures. The portable suite now passes 47/47.
- Joined the optional authenticated player actor into the aggregate
  mission-world loader and its allocation-free publication boundary. The
  loader shares physical CCF reads, preserves the room's mesh/instance and
  texture-ID prefixes, appends actor-only resources in deterministic first-use
  order, applies the authenticated absolute spawn pose exactly once, and emits
  one validated draw model and submission plan. Typed provenance, independent
  count/byte ceilings, fail-closed session/callback/cancellation handling, and
  synthetic shared/separate-source and tamper cases cover the join. Native
  physical-device rendering and visual acceptance remain pending.
- Added a bounded portable player-pose frame builder and reusable
  `PlayerActorPoseRuntime`. Metal preparation derives an exact actor-only
  step-zero frame from authenticated provenance, verifies it bit-for-bit
  against the final authored instances, and admits source storage, scratch,
  and two exchange slots under independent iOS ceilings and the CPU budget.
  The main-thread simulation consumer publishes accepted later steps without
  allocation before committing their deterministic state; a busy exchange
  drops only that visual update. Each rendered frame acquires at most one
  lease, and static instances retain their authored fallback. The producer
  currently republishes the authenticated spawn world because the movement
  law has not yet supplied a changing actor pose.
- Recovered the bounded legacy screen-projection and room render chain.
  Constructor defaults, engine near/far/FOV values, exact FOV clamp/focal
  formula, near fallback, screen centre, Y inversion, far/near clipping order,
  render-list drain, and concrete Direct3D triangle staging are confirmed by
  Ghidra and Rizin. `RenderRoom` uses object lists and clipped portal recursion
  with a default depth limit of two; it does not traverse static-BSP children.
  Portal-BSP is instead confirmed in camera movement room transitions. The
  exact scalar projection is now implemented as a validated, allocation-free
  portable C++20 contract. The camera SRT layout and exact world-to-camera
  transform are also recovered: subtract camera translation, apply three
  transposed-basis dot products, then multiply by cached inverse-square scale,
  without changing axis signs. That point transform is implemented behind an
  immutable, normalized-Gram-validated, allocation-free C++20 boundary.
  The active Direct3D path is now confirmed as a normal Z-buffer carrying
  manual reverse depth `near / cameraZ`, with `greaterEqual` comparison and
  `[0,1]` viewport depth. Both concrete Direct3D clear paths use
  `TARGET|ZBUFFER` with exact depth `0.0`. Startup uses a logical 640x480
  canvas; no adaptive widescreen policy was found. A follow-up symbol/string
  audit corrected the fixed `(35,35)-(555,345)` rectangle from “gameplay” to
  the deferred House Editor. The portable projection result now preserves
  camera Z and the exact reciprocal-depth scalar, while an immutable,
  allocation-free legacy layout contract preserves the recovered 640x480
  aspect-fit comparison without presenting that port policy as original
  behavior. The newer native-render layout drives both product backends with
  full-target Hor+ by default. A second portable
  contract fail-closed maps raw depth modes `1` through `4` to the recovered
  compare/write presets and confirmed zero clear. The actual gameplay camera
  is now separated from the House Editor: it uses a full-current-screen
  viewport, projection defaults `0.25/200/90`, three persistent chase tuples,
  and a held rear tuple. Preset selection/cycling is implemented as another
  fail-closed portable contract. The exact vehicle quaternion matrix plus
  stateless per-refresh target/smoothing recurrence are also implemented.
  Backend-neutral camera collision primitives now cover the exact sphere
  radius, per-axis collision reduction, aircraft-specific recovery, line-hit
  interpolation, raw portal arguments, and final look-at matrix. The actual
  retained all-room BSP arena now survives CCF-cache destruction and supplies
  bounded allocation-free static/portal line tracing, exact legacy list/tie
  order including binary32 spills and unordered traversal, portal
  mesh/type/visibility gates, and source-aware runtime-room targets. Catalog
  replay, pre-allocation aggregate limits, exact publication topology, and a
  hard 256-transition safety ceiling protect the retained backend. A new
  allocation-free runtime/source-world adapter now applies the mission basis
  and units in both directions, transforms plane normals by inverse transpose,
  preserves diagnostic legacy fractions, and rejects any out-of-segment hit
  before a camera-facing portal trace can change rooms. An allocation-free
  camera room-state boundary now returns a complete candidate position and
  final room only after the full portal chain succeeds; later-hop failures
  retain diagnostic evidence without publishing an intermediate room.
  Sphere-contact recovery now also carries the first CCF `0x2152` u32 from a
  uniquely resolved triangle material into every retained spatial polygon.
  Native evidence confirms that the gameplay camera deletes collected
  contacts for non-null material modes `0` and `8`; numeric labels remain raw.
  The constraint projection stage now also reproduces strict plane admission,
  directional override selection, oldest-active ordering, one-/two-plane
  projection, and conflicting-plane stop behavior without allocation.
  Static sphere collision now reproduces the seven-axis triangle candidate
  test, B-then-A BSP traversal, visible type-zero portal-room discovery,
  material filtering, face/edge/vertex selection, native tie order, and
  iterative constrained correction through caller-owned bounded workspaces.
  The corrected centre now feeds factor reduction and portal tracing in native
  order, producing one allocation-free all-or-nothing proposal containing
  position, final room, and all three axis factors. Workspace, geometry,
  basis, factor, and later portal failures expose no partial state.
  The retained-static vehicle-to-camera line stage now consumes only a
  complete sphere result, preserves a miss, and on a hit applies the exact
  adapted point plus the native second portal update before exposing state.
  A preallocated single-writer camera owner commits only that completed result
  and publishes immutable `{step, generation, position, room, factors}` copies
  through a bounded two-slot SPSC exchange. Invalid, stale, exhausted, or
  contended commits retain the exact prior state; concurrent synthetic tests
  reject torn fields. Ghidra and Rizin independently confirm that both camera
  matrix reset paths establish unit scale and inverse-square, allowing one
  acquired generation and vehicle anchor to build an immutable look-at,
  world-to-view, and scalar gameplay projection snapshot without guessed SRT
  state. Placed/player dynamic line collision and transparent line portals are
  now runtime-owned; dynamic-object sphere collision remains unimplemented.
  The private mission-room Metal path now consumes the immutable pose through
  a tested homogeneous clip packet, recovered reverse depth, explicit far
  clip, and the shared native-resolution Hor+ viewport. Until
  the simulation publishes changing event-5 generations, it starts at an
  explicit camera0 target anchored to the authenticated spawn. The synthetic
  public-data path remains diagnostic and no full parity claim has been
  introduced. A producer-side coordinator now composes recovered AirCraft
  factor recovery, cumulative mode/rear input, chase, the complete
  retained-static collision path, pose, clip packet, and atomic state commit
  as one allocation-free transaction. Ghidra and Rizin independently confirm
  that its factor-recovery argument is the same scheduler delta converted from
  milliseconds to seconds; the nominal 12 ms AirCraft interval supplies
  `0.012f`, not the 60 Hz input-pump interval. Exported Ghidra symbols and
  independent Rizin instructions now name the two explicit inputs as live
  `NfActor` health and the `AfVehicle` inactive latch. A fixed
  mission-lifetime SPSC exchange transports only complete
  owning clip packets, and Metal retains one lease through encoding. A new
  non-moving mission runtime now owns the arena, immutable placed/player
  colliders, exact-size camera workspaces and dynamic-line buffers,
  coordinator, and exchange under checked portable and iOS memory ceilings.
  It republishes and portal-traces only a complete producer-supplied
  placed/player frame; a failed republish retains the prior frame. The atomic
  room transaction publishes a
  weak producer endpoint that expires safely on mission replacement. The
  endpoint remains deliberately inactive and the exchange contains only the
  explicit bootstrap until complete recovered aircraft inputs exist.
- The authenticated setup-to-room provenance chain now crosses the native iOS
  publication boundary together with an immutable `PlayerSpawnPose`. Ghidra
  Headless and Rizin independently confirm x/y/z radians, mode zero, exact
  Z-X-Y construction, and absolute-world runtime semantics. The loader uses
  the room's basis and unit policy for both geometry and pose; native
  publication recomputes the pose, consumes the exact ticket, commits Metal,
  then assigns pose and a fresh input-intention state before readiness.
  Runtime room switching now has retained pointer-free BSP and portal-room
  data; applying it to the stateful simulation/camera coordinator remains.
- Added optional build-generated initial mission configuration. Public defaults
  are empty/data-less. A future protected private workflow may supply Base64
  setup, Level, and optional player-object logical paths plus a decimal
  `uint32_t` start index; CMake validates them and generates a local header
  without committing private paths. Public pull-request workflows never read
  these inputs or signing secrets. AFPACK v1 contains no launch metadata; an
  authenticated bounded mission catalogue remains a target for AFPACK v2.
- Recovered AirCraft sound ID `0x20` as the `enginedive` sample. On the shared
  fifth-call audio pass, health `<= 0` starts or retains it and applies
  `clamp(-0.15 * velocity.y - 0.2, 0, 1)` volume; positive health stops it.
  A separate allocation-free C++20 transition preserves the exact command
  order and deliberately initializes the original constructor's unwritten
  `+0x566` state byte to false. A portable coordinator now runs it only on the
  shared fifth-call cadence, splices it between engine phase transitions and
  common modulation, and translates the combined stream to generic audio
  commands with validated caller-supplied clip/voice bindings. Live aircraft
  inputs and private decoded samples remain unwired.
- Recovered the primary weapon path from `EVENT_PRIMARY_ATTACK` through the
  AirCraft `WpMgun` pointer and persistent `AfWeapon::Fire` state into the
  separate `WpMGun` time-dependant refresh. The five technology levels use
  exact `0.12..0.08` second intervals and `40..100` projectile speeds; the
  refresh preserves a tenfold zero-ammunition cadence, selects/wraps the
  attachment-provided barrel, decrements only nonzero ammunition, creates a
  private `WpMGunAmmoTechN`, and processes projectile event `0xE2`.
  Allocation-free C++20 helpers preserve that timing/state transition and the
  complete semantic payload: rotated muzzle input, exact target-velocity lead,
  packed field meanings, zero target UID, five impact-damage profiles, exact
  acceleration bits, four-second lifetime, unobstructed ballistic step, actor
  damage command, material-15 impact interpolation, and bounded ricochet
  request. A transactional coordinator now selects the emitted rotated muzzle,
  caps the technology profile, produces the complete payload, and returns the
  already-advanced cadence/barrel/ammunition state so a later private
  allocation failure cannot incorrectly roll it back.
  A caller-owned fixed-capacity runtime now preserves that native branch
  order without importing private PE32 objects: it commits weapon state before
  capacity acquisition, delays muzzle/event-only reads until a reusable slot
  exists, activates semantic event `0xE2`, and returns a generation-tagged
  handle. First-free selection is an explicit deterministic port policy;
  saturated generations fail closed and reuse cannot revive stale handles.
  Root ID zero, monotonic ordinary-room IDs, and newest-first lookup now map
  bidirectionally through the immutable room count retained by the mission
  runtime, without native pointers or a surviving build catalogue.
  `PhLine::GetBspCollision` now also confirms static-before-dynamic traversal
  through one strict-nearest fraction plus recursive visible type-zero portal
  continuation. An allocation-free single-hit decision seam implements the
  exact shared material-8, portal, client/server actor-gate, actor-contact,
  surface, normal, position-swap, and material-15 water behavior. Bounded
  per-mesh dynamic BSP construction now preserves the native splitter,
  prepend, clipping, material/provenance, and radius contracts. A non-portal
  allocation-free adapter lets retained static geometry and caller-ordered
  unit-scale dynamic instances compete through that same strict-nearest
  fraction. A second bounded allocation-free adapter now consumes flat
  per-room object ranges, follows automatic visible type-zero portals, replaces
  portal provenance with the later hit, composes whole-segment fractions, and
  fails typed on malformed ranges, out-of-segment hits, or cycles. A separate
  generic simulation coordinator now repeats the complete query after a
  projectile-level type-zero portal, preserves the full endpoint, returns all
  existing terminal decisions, and fails closed on query/decision errors or
  bounded cycles without allocation. An authenticated adapter now maps its
  current signed room through the immutable room count owned by the published
  runtime arena, so the temporary build catalogue need not survive mission
  loading. It invokes the published runtime portal trace, preserves the exact
  `0x2152` material bits, and
  resolves the primary player's server actor gates from state committed in
  the same complete collision-frame generation. A typed callback remains for
  future non-player actors. A fail-closed terminal reducer now commits the
  resulting endpoint and room, preserves the material-15 water flag, applies
  actor/self damage and deactivation rules, and returns bounded surface or
  ricochet command data. The published runtime wrapper joins that reducer to
  both actor and portal/surface paths while rejecting flight/query mismatch.
  A higher allocation-free transaction now resolves the creator UID, invokes
  the recovered `DisableBsp`, brackets the complete query/portal/terminal
  path, and invokes `EnableBsp` only when BSP was previously enabled. Missing
  or already-disabled creators preserve the native no-enable path; rejected
  disable prevents tracing and rejected enable suppresses terminal
  commit/command data.
  A bounded allocation-free step now advances every already-active
  generation-tagged slot in stable supplied-index order through ballistic
  flight/lifetime and that complete published collision transaction. Inactive
  slots do not inspect their profiles; lifetime expiry skips collision; one
  rejected slot stays unchanged without blocking later slots; failed creator
  BSP restoration aborts the remainder. Successful state commits expose only
  bounded damage or surface/ricochet requests for a later live dispatcher.
  The fixed pool and slot order are deterministic port policy rather than a
  native scheduler claim. Live event/effect dispatch, authored muzzle
  transforms, non-player actor and dynamic-portal publication/resolution,
  concrete creator BSP callbacks, scheduling, tracer/effect realization,
  secondary weapon families, dynamic camera-sphere contacts, runtime traces,
  and product integration remain pending.
  The authenticated player CCF now also produces bounded immutable per-mesh
  colliders and ordered actor-local instances inside the atomic mission load.
  A two-pass `noexcept` frame adapter publishes a supplied live player
  world-pose, object ID, active flag, and current room into exact-size combined
  line object/range spans without allocation or partial mutation.
- The preserved placed-object `0x4101` property is now decoded as the same
  bounded flat `F0C0`/`F0C1` format used by room BSP while its raw wrapper and
  extension children remain available. Ghidra and Rizin confirm deferred
  `(placedObjectReference, polygonIndex)` binding, first-use mesh caching, and
  reverse runtime root-list order. All 1,160 selected wrappers parse into
  10,641 nodes and 16,212 polygons with zero owner/index mismatches.
  `MissionPlacedDynamicBspAssembly` now authenticates the room catalogue and
  placed scene, binds those polygons to physical triangles/material modes,
  reproduces source-local first-use mesh caching and per-room prepend order,
  converts unit-scale F050 world transforms into runtime coordinates, and
  emits exact ranges consumed directly by the allocation-free combined line
  query. The later cached-object no-relink branch is preserved as observed;
  the selected corpus has no repeated serialized cache key, so that branch
  remains a controlled-runtime-trace target. The authenticated mission
  snapshot now owns this assembly, charges its exact nested bytes to the global
  CPU budget, and validates its room/source provenance at publication.
  A two-owner mesh view plus a two-pass frame adapter compose the live player
  ahead of each room's unchanged placed list without copying geometry,
  allocation, or partial output. The mission-lifetime runtime now owns those
  immutable assets and exact flat buffers, exposes the last complete frame,
  stores the primary player's object ID, active flag, and both recovered
  projectile gates in the same successful transaction, and traces against its
  owned static arena without allocation. Failed republishes retain both the
  previous geometry and actor state. Other live actors, a real changing
  producer, controlled traces, and dynamic sphere collision remain separate.
- `EV-20260730-005` closes the isolated single-player mission-outcome state.
  AFS `MissionFail` (`0x47`) and `MissionSuccess` (`0x48`) set independent
  one-byte failed/accomplished fields. Only the first terminal call requests
  `pause` followed by `menu`; repeats and conflicts still set their selected
  byte and can leave both true without another request. Direct
  `NfMission::Fail` sets only failed, while the portable reset projection
  clears both flags. The new allocation-free C++20 transition covers all four
  starting states, both call orders, repeats, conflicts, reset, direct fail,
  and unsupported identifiers. It intentionally owns no trigger evaluation,
  AFS process, scheduler, campaign/save state, multiplayer, or platform menu.
- `EV-20260730-006` closes the separate downstream result/progression decision.
  The native result screen succeeds only for an existing mission with
  accomplished true and failed false, so failure wins when both flags are set.
  Success always replaces typed Axis/Allied thread state, then adds a missing
  side maximum or replaces it only when the existing signed int32 maximum is
  smaller than `mission_number + 1`. The new allocation-free consumer returns
  only Retry/Continue and value-only progress directives, rejects unknown
  sides and the native `INT32_MAX` wraparound, and owns no FourCC, roster,
  score/stat mutation, file write, AFS wiring, or UI execution. Ordinary
  mission refresh also now has a static process-before-trigger-poll boundary,
  but unresolved VM/lifecycle/global-order evidence still forbids live AFS
  integration.
- `EV-20260731-001` closes the AFS compiled-function activation and initial
  single-process order boundary. The compiler maps `event` to Autoexec off and
  `action`/`timer` to Autoexec on, compiles function declarations in source
  order, prepends each record, and then appends accepted records to the process
  FIFO; initial actions/timers therefore execute in reverse source order.
  `LegacyAfsFunctionSchedule` implements only this already-classified rule and
  atomically rejects forged kinds. A comment/string-aware read-only census of
  all 52 authenticated local AFS objects found 114 events, 328 actions, no
  timers, 47 mission-fail calls, 20 mission-success calls, and no dynamic
  `Call`, `KillProcess`, or `LoadScript` use. No script text, logical name, or
  private/original dependency is public. The live `Load`/`Start` lifecycle, VM
  execution, dynamic process mutation, and global dispatcher/presentation
  order remain NO-GO.
- `EV-20260731-002` closes the exact five-word AFS mission-outcome call site.
  All 67 recovered calls supply source literal `true`; token registration maps
  it to tag `0x137`, and the compiler emits
  `0x1B, 1, 0x24, 0x26, descriptor-token`. The registered
  `MissionFail`/`MissionSuccess`
  descriptors each require one ignored type-`2` argument word and carry an
  explicit handler plus identifier `0x47`/`0x48`. A fail-closed C++20
  recognizer validates that exact projection and composes the typed result
  with the existing outcome transition. An aggregate-only scan found 67
  structurally consistent outcome calls across 52 AFS members without
  publishing script text, paths, or content. The complete VM, live stack,
  process ordering, and runtime integration remain excluded.
- `EV-20260730-007` independently cross-checks the complete 17-record
  `AirCraft.type` registry in Ghidra and Rizin. The portable catalogue retains
  exact source words and registration order without applying constructor
  arithmetic or assigning speculative meanings. Authenticated canonical player
  object identities now carry the matching typed record; generic visuals carry
  none, and the native publication boundary rejects forged or missing
  associations. A separate allocation-free refresh gate composes the already
  committed five-control state with the recovered signed rest/sleep helper,
  committing only rest duration and returning integrate/clear directives.
  Producer ordering, the 12 ms scheduler, rigid-body mutation, the force law,
  and live pose publication remain intentionally unwired.

## Confirmed

- Reference readme identifies v1.01.
- 33 files total 356,442,590 bytes.
- All 16 executable modules inspected are PE32/x86.
- `.mode` and `.type` files are DLLs despite custom extensions.
- `Resource.up` and language `.up` files begin with `UDSP 01 01` and are not
  standard archives recognized by 7-Zip.
- Original rendering uses Direct3D/DirectX 7-era interfaces and optional Glide.
- The locked Phase 1 toolchain is installed and its versions and source hashes
  are recorded in `docs/toolchain/LOCK.md`.
- The iOS control baseline requires full touch-only play plus controller-only
  play, hot-plug recovery, remapping, deterministic input frames, safe-area
  layouts, and optional haptics.
- Accepted v1.0 scope: a playable native Windows x64 product plus a private iOS
  ARM64 sideload over shared C++20 game, physics, resource, save, and semantic
  input code. Windows is the primary rapid-debug and controlled
  original-comparison environment; each product owns separate platform,
  renderer, physical-input, audio, and filesystem adapters.
- The iOS product retains its iOS 16.4 minimum deployment target, iPhone 17 Pro
  Max/iOS 26.6 and iPhone SE 3/iOS 26.3 runtime devices, available Apple
  Developer account, and no-App-Store policy. Version 1 remains single-player
  only, with no editors, multiplayer, or original CD music.
- Original and converted game data remains owner-private and outside Git,
  public Actions logs, caches, artifacts, and public packages for both products.
- Native-resolution 3D at 100% render scale and Hor+ widescreen are accepted
  product requirements on both platforms. Classic preserves the original
  model-kit art direction at high resolution; Enhanced adds modern rendering
  features without affecting deterministic gameplay or physics.
- GitHub Actions hosted macOS/Xcode currently provides unsigned iOS compile
  validation. Interactive device debugging and profiling later use local Apple
  tooling; private signing must keep original data local in an imported
  `.afpack`.

## Next

1. Build the trace-driven 12 ms AirCraft producer without treating the separate
   60 Hz input clock as physics. Confirm the Q15-to-legacy-event timing and
   payload policy, then publish one coherent changing pose/health/inactive/
   kinematic sample to Windows and iOS before connecting live camera and audio.
   The backend-neutral runtime planner and one-lease-per-frame Metal/D3D11
   consumers plus the isolated native-field target/apply/clamp/smoothing step
   and the separate already-formed signed native `THRUST_SET/APPLY` typed-write
   reducer are complete. The exact signed
   `TURN_SET`/`PITCH_SET`/`BANK_SET` branch structure, rest-clear behavior,
   and producer ranges where producer paths exist are now statically
   recovered; the separate pure structural reducers now implement the
   explicitly labelled full-domain PC53/RN compatibility policy while the
   model remains conditional on the live x87 control word. A
   simulation-thread-confined owner commits typed control writes and their
   shared rest clear transactionally, but owns no producer ordering or clock.
   A separate single-invocation step now composes the discrete command reducer,
   typed decoder, and that owner without adding a batch or scheduler. The
   pitch/bank-only one-event step separately accepts already-formed analog/AI
   events and preserves caller order without a queue, producer cache, replay,
   or source identity. The recovered eight-channel `AIControls` value object
   and exact five-route AI dispatcher are now also implemented as an isolated
   policy-gated oracle; they still own no task evaluation, producer cadence,
   cross-source ordering, or vehicle/weapon integration.
   The game-owned static x87-writer search is complete, but external-library
   behavior, RC, exception state, and the actual consumer-thread CW/SW remain
   unobserved. Keep all control reducers and that owner unwired from
   `PlayerAircraftState` until controlled traces prove that numeric mode,
   Q15-to-event ordering, and sample-and-hold timing; do not
   manufacture a complete native flight-control scheduler from the nominal
   12 ms interval.
2. Obtain controlled runtime traces for free flight and the ground, inverted,
   water, collision, engine-transition, and too-high branches; establish
   x87-versus-portable numeric tolerances and deterministic replacement PRNG
   sequencing before composing the statically recovered 12 ms flight law.
3. Keep the isolated mission-outcome state, initial AFS function schedule, and
   downstream result/progression consumer unwired until representative
   bytecode, a bounded VM, the `Load`/`Start` lifecycle, mutation during
   refresh, and global dispatcher/presentation order are proven. Then recover
   the remaining score/stat producers and corruption behavior before wiring
   them. The bounded `AFCS` schema covers the recovered numeric state and its
   isolated store now supplies current/backup/default selection, future-schema
   preservation, repair signalling, and ADR-0018 atomic publication inside an
   injected private leaf. The legacy adapter, per-profile root selection,
   save scheduling, error UI, and lifecycle remain required. Neither the codec
   nor the dormant store is save-game migration.
   Preserve one already-ordered outcome call per transition; do not
   batch, sort, deduplicate, replay, or let the value-only consumer execute UI
   or disk effects.
4. Complete ADR-0015 acceptance around the implemented native text pickers.
   Run keyboard/mouse/controller and physical Narrator acceptance against the
   implemented bounded Windows COM provider. Until that passes, do not claim
   Narrator support. Before supporting live replacement, add a host-owned
   pause transaction that
   prepares a fresh pair. The platform-neutral touch-layout geometry/profile V1
   now preserves the default UIKit layout, supports automatic/forced compact
   density and semantic-preserving left-handed mirroring, and cancels active
   touches before relayout. Its separate private AFTC V2 document and
   recovery-safe iOS editor now persist handedness, density, a `50-100%`
   resting-background strength, and automatic-controller-hide/always-visible
   policy without storing device metadata. V1 loads migrate safely and request
   an explicit repair save. Connecting a controller during active gameplay
   hides through the existing cancel-all boundary; disconnect still releases
   and pauses before touch controls may return. Next add controller glyphs,
   haptics, free placement, and finished menu bindings before touch-only and
   controller-only acceptance on both target iPhones. Never mutate
   position-indexed active router state in place or accept a live caller's
   unauthenticated mission claim.
5. Connect the implemented weak camera-runtime endpoint to the recovered live
   AirCraft producer once it supplies distinct chase position, world anchor,
   vehicle rotation, live health, inactive state, and the confirmed scheduler
   delta in seconds without resampling the separate 60 Hz input pump. Replace the
   camera0-only publication only when that complete input exists. Preserve
   scalar clip and reciprocal depth while validating the new Hor+ port policy
   separately from recovered 4:3 evidence; dynamic-object and transparent
   collision-portal paths remain separate.
6. Validate the implemented runtime portal-to-room-state proposal against
   controlled multi-room executable traces when the isolated runtime is ready.
7. Keep BSP render culling disabled until its separate runtime semantics are
   proven against executable evidence.
8. Feed the runtime-owned player collider and concrete resolver from the
   recovered changing actor producer. Extend the same generation-matched
   publication/resolution to other live actors, then connect the implemented
   creator BSP guard callbacks and already reduced terminal damage/surface
   commands to private live actors/effects. Feed live muzzle transforms into
   the fixed-capacity activation runtime, advance its active slots through the
   collision transaction, and add tracer/effect adapters before wiring
   primary-fire intent into runtime.
9. In parallel with gameplay reconstruction, extend the implemented ADR-0013
   resolution/Hor+/UI/input/offscreen-target foundation with independent UI
   scale plus HUD/reticle/weapon/effect projection integration. Render scale,
   Original 4:3, safe FOV settings, and the backend-neutral
   world/reference-camera-to-native-output bridge are implemented. Connect
   actual HUD, reticle, weapon, and screen-effect consumers next without
   inventing their visibility or gameplay rules. The Windows capture-only
   complete HUD/sight validation is implemented; ordinary frames remain
   correctly disconnected from fixed validation values.
   Validate the implemented diagnostic overlay on both physical iPhones before
   lighting, materials, shadows, or post-processing. Then implement Enhanced
   as a scalable raster pipeline in this order: end-to-end color correctness;
   dynamic directional, point, and spot lights; plastic-tuned materials;
   cascaded/atlas/budgeted-cube shadow maps; reflection probes and selected
   planar reflections; optional High/Ultra SSR and SSAO; and finally HDR,
   atmosphere, and post-processing. Ray tracing is explicitly outside the
   product roadmap: D3D11/HLSL and Metal remain the primary render paths, with
   quality budgets covering iPhone SE 3 through high-end iPhone and Windows
   targets.
10. Keep ADR-0019's private HD texture mode as a separate staged workstream.
    Public-boundary hardening and the bounded, accepted-only reviewed-manifest
    parser/index, root-confined file capability, source resolver, base checksum,
    byte-identical per-texture GTI fallback, and bounded PNG RGBA8/mip-chain
    preparation are complete. The Windows-only session opt-in pilot now adds
    explicit private root/mode arguments, reload-only activation, D3D11 RGBA8
    upload, exact per-texture fallback, and a 512-entry/256 MiB generation-aware
    cache. Private 1920x1080 acceptance loaded all 34 mission textures through
    Enhanced with no fallback. The portable requested/effective model and AFRS
    schema-4 persistence are now complete, and Windows startup resolves the
    durable preference against a session-local package locator with safe
    Classic fallback. Generic native UI and the atomic Windows in-process
    mission reload are complete. The iOS owner-import, durable locator/session,
    availability-gated settings, and atomic Metal mission-reload source path are
    now implemented. Hosted unsigned iPhoneOS and iPhoneSimulator builds are
    green. Next validate both target phones; do not add private resources to
    CMake/CI or begin streaming/compressed derivatives.

## Open questions

- Which D3D feature-level floor, exact Windows 10 release floor, and private
  packaging procedure will be used?
- Which Windows-compatible method will install the signed Actions IPA on both
  registered phones?
These questions do not block static analysis or the archive work.

## Latest validation

- Touch preferences V2 add a portable visibility decision, exact AFTC V1-to-V2
  migration, repair detection, and an iOS Auto-hide/Always visible setting.
  Automatic mode shows controls only during active gameplay without a connected
  controller. A connect hides through `AirfixTouchControlsView::setHidden`,
  which neutralizes every held touch first; disconnect retains the existing
  reset-and-pause path. Focused semantic, codec, migration, repair, and durable
  store tests pass locally; hosted Objective-C++ builds remain the native gate.
- The isolated AirCraft AI controls slice builds two new C++20 test targets.
  Focused validation passes the exact state-layout/range/mapping vectors and
  the immediate five-event dispatcher contract, including the recovered
  `{128,0,0,0,0}` witness and callback-visible per-route sequencing. Full
  portable and hosted validation remains the publication gate; live x87,
  `_ftol`, scheduler, and runtime integration claims remain excluded.
- Private touch preferences V1 introduced the strict bounded AFTC codec, shared
  current/backup durable store, future-schema downgrade protection, and an iOS
  pause panel for handedness, automatic/compact density, and `50-100%` resting
  overlay strength. Publication saves first and then atomically applies the
  validated snapshot; geometry changes neutralize held controls, while
  opacity-only changes preserve capture ownership. Synthetic semantic, codec,
  corruption, recovery, repair, and store tests pass locally; hosted iOS
  Objective-C++ builds remain the native publication gate.
- The touch-layout geometry/profile V1 adds a platform-neutral C++20 safe-area
  model, automatic/forced compact density, semantic-preserving left-handed
  mirroring, enlarged capture geometry for the broad throttle handle, and
  fail-closed profile/bounds validation. UIKit consumes the model, cancels
  active touches before relayout, and presents a translucent bordered overlay
  with a vertical throttle and horizontal handle. A complete clean portable
  GCC/Ninja build passes 160/160 CTests. Changed-source clang-format,
  `git diff --check`, synthetic public-boundary tests, the actual 834-file
  public scan, documentation integrity (119 experiments/408 catalogue rows),
  CI-classifier tests, and 12/12 Rizin normalization tests pass. Hosted Apple,
  Windows, Linux, macOS, and clangd builds remain the publication gate.
- The iOS HD package slice now includes a portable package session, canonical
  AFTL locator codec, durable current/backup store, bounded accepted-manifest
  discovery, owner-copy installation, restart inspection, and native iOS
  import/reload integration. Synthetic tests cover unique/absent/ambiguous
  discovery, traversal/link/type/limit rejection, locator future-schema
  protection, install/replace/restart, and failed replacement cleanup. The
  complete portable Ninja build passes 159/159 tests. The complete MSVC 19.51
  Release Windows product passes 174/174 tests, including D3D11, XAudio2,
  native/scaled smoke, package, settings, UI semantics, and UI Automation.
  The initial Visual Studio generator attempt exposed only a process-local
  duplicate `Path/PATH` MSBuild defect; the same source passed through the
  established Visual Studio developer environment and Ninja. Hosted runs
  `30884920194` and `30884920214` pass the Windows product, portable Windows,
  Linux, macOS, clangd, iPhoneOS, and iPhoneSimulator jobs. Physical-device
  validation remains pending; no private resource entered the repository or
  app bundle.
- The generic TextureMode UI and Windows mission-reload slice passes a clean
  GNU 15.2/Ninja portable build (495/495 edges, 154/154 CTests) and a clean
  MSVC 19.51/Ninja Windows Release product build (169/169 CTests). The Windows
  total includes D3D11 renderer, native/scaled product smoke, XAudio2, UI
  raster/semantics/Automation, settings, package, and reload-state coverage.
  The first Windows test pass found two sandbox-blocked Python commands; after
  selecting the already installed accessible interpreter, the identical full
  suite passed. Public-boundary tests and the 816-file scan, documentation
  integrity (119 experiments/408 catalogue rows), 12/12 Rizin normalization,
  CI-classifier tests, and `git diff --check` pass. Hosted runs
  `30860088903` and `30860088926` pass Linux, macOS, Windows, clangd,
  iPhoneOS, and iPhoneSimulator for this branch.
- The durable texture-mode slice passes a clean MSVC 19.51/Ninja Windows
  product build and all 168/168 CTests, including D3D11/XAudio2 product smokes.
  Its synthetic coverage includes AFRS schema-1/2/3 migration, schema-4 golden
  bytes and Enhanced round-trip, forged state rejection, requested/effective
  fallback, reload policy, and persisted-preference command-line composition.
  A clean GNU 15.2/Ninja portable build completes all 492 edges and passes
  153/153 CTests. Public-boundary regressions and the 813-file scan,
  documentation integrity (119 experiments/408 catalogue rows), 12/12 Rizin
  normalization, CI-classifier tests, and `git diff --check` pass. Hosted runs
  `30857993665` and `30857993651` pass Linux, macOS, Windows, clangd,
  iPhoneOS, and iPhoneSimulator for this branch.
- The Windows session-only HD texture pilot passes its synthetic parser,
  package, resolver, loader/publication, CLI, and D3D11 cache tests. The public
  manifest parser now follows the schema-valid `mixed` representative-category
  rule and accepts the complete owner-local reviewed index. A private
  authenticated 1920x1080 mission comparison loaded 34 Enhanced textures, zero
  Classic textures, and zero fallbacks; both captures and every private input
  remain outside Git. The complete MSVC product passes 167/167 tests and the
  complete portable C++20 build passes 152/152. Synthetic boundary tests, the
  811-file public scan, documentation integrity (119 experiments/408 catalogue
  rows), 12/12 Rizin normalization, CI-classifier tests, and `git diff --check`
  pass. Hosted Linux/macOS/Windows, clangd, and unsigned iOS remain the final
  publication gate.
- The private HD texture stage-5 preparer passes complete GCC 15.2/Ninja and
  MSVC 19.51/Ninja portable builds with 152/152 CTests in each tree. A full
  MSVC Windows-product build passes 166/166 CTests, including D3D11/XAudio2,
  native/scaled product smoke tests, and UI Automation. Its new suite contains
  generated in-memory PNG bytes only and covers signature/IHDR/CRC, exact RGBA8
  and non-interlace, natural mip dimensions, missing/extra numbered levels,
  base/mip-zero identity, opaque/binary/translucent alpha, stale generation,
  file failures, and independent encoded/chain/in-flight/decoded budgets.
  Clang analyzer/bugprone checks are clean; product outputs stage exact SDL3
  and LodePNG licenses. Hosted Linux/macOS/Windows and unsigned iOS builds
  remain the publication gate.
- The private HD texture stage-4 resolver passes a complete Windows GCC
  15.2/Ninja build with 151/151 CTests. A fresh MSVC 19.51/Ninja build compiles
  all 485 portable edges and passes all 149 native CTests; CTest could not
  spawn its two Python interpreters inside the local sandbox, and both scripts
  then pass directly as 9/9 and 8/8. The five focused resolver/identity suites
  pass from the MSVC tree. Documentation integrity (117 experiments, 408
  catalogue rows), 12/12 Rizin exporter tests, synthetic boundary tests, the
  actual 801-file public-boundary scan, and `git diff --check` pass. Hosted
  Linux/macOS/Windows and unsigned iOS builds remain the publication gate. The
  new production resolver is clean under Clang analyzer/bugprone checks, and
  all new C++ files pass clang-format with warnings as errors.
- The bounded reviewed HD manifest parser/index passes a complete GCC
  15.2/Ninja build with all 144/144 portable CTests and a complete Visual Studio
  2026 MSVC 19.51/Ninja Windows product build with all 158/158 CTests, including
  both data-less product smokes. Its dedicated suite uses only synthetic
  in-memory JSONL. Public-boundary regression tests, the 759-file repository
  scan, and `git diff --check` pass; hosted Linux/macOS/Windows and unsigned iOS
  builds remain the publication gate.
- The portable legacy sorted-render queue passes a fresh complete Windows GCC
  15.2/Ninja build with 121/121 CTests and a complete MSVC 19.51/Ninja Windows
  product build with 131/131 CTests, including both D3D11 product smokes.
  Exact commit `88235e8` additionally builds all 376 steps from a clean source
  export under the dedicated WSL2 GCC 13.3/Ninja environment and passes
  121/121 CTests. Synthetic public-boundary tests, the 626-file repository
  scan, the 340-row/14-column unique function catalogue, and
  `git diff --check` pass. Two independent reviews report no code finding;
  their sole P2 stale validation-provenance finding is corrected. PR #90
  hosted runs `30622519149` and `30622518942` pass all seven Windows product,
  Windows/Ubuntu/macOS portable, clangd, iPhoneOS, and iPhoneSimulator jobs.
- The recovered CCF material render contract passes fresh Windows GCC
  15.2/Ninja and independent WSL2 GCC builds with all 120 portable CTests. An
  isolated MSVC 19.51/Ninja build completes all 669 steps, links the Windows
  product, and passes all 130 CTests including both native D3D11 product
  smokes. Synthetic public-boundary tests, the 623-file repository scan,
  340-row/14-column unique function catalogue, local-path review, and
  `git diff --check` pass. Independent final review reports no remaining
  finding. Hosted platform builds remain the publication gate.
- The AFS mission-outcome bytecode boundary recognizes only the recovered
  `0x1B, 1, 0x24, 0x26, descriptor-token` call site and validates the exact
  immediate argument plus one-word descriptor shape before returning the
  existing typed outcome call. Complete
  clean Windows GCC 15.2, WSL2 GCC 13.3, and Windows MSVC 19.51/Ninja builds
  each pass `120/120` CTests. Focused WSL2 Clang 18.1 warnings-as-errors, both
  Rizin wrapper suites, all 12 exporter tests, synthetic public-boundary tests,
  the 617-file scan, changed-document links, local-path and evidence-ID checks,
  the 330-row/14-column unique catalogue, and `git diff --check` pass. Focused
  WSL2 ASan/UBSan also passes. A read-only formatting check and hosted platform
  builds remain publication gates.
- The native ADR-0015 text pickers expose the same bounded seven-action
  transaction through keyboard/mouse/controller input on Windows and
  touch/controller input on iOS, while retaining one complete AFIP draft and
  next-launch-only save. The complete portable GCC/Ninja build passes 117/117
  CTests; the complete MSVC 19.51/Ninja Windows product links and passes
  127/127 CTests, including both D3D11 product smokes. Clang-format
  warnings-as-errors, synthetic public-boundary tests, the 604-file repository
  scan, and `git diff --check` pass. Independent review found two P2 issues,
  verified the save-freeze and localization fixes, and returned final GO with
  no remaining P0-P3 finding. Hosted Apple compilation and physical-device
  acceptance remain gates.
- The value-only mission outcome consumer preserves the exact
  `missionExists && accomplished && !failed` predicate, failure precedence,
  typed campaign side, signed maximum comparison, and always-set-thread
  success directive while failing closed on unknown sides and
  `INT32_MAX`. The complete portable GCC/Ninja build passes 116/116 CTests;
  the complete MSVC 19.51/Ninja Windows product links and passes 126/126
  CTests, including both D3D11 product smokes. Focused Clang 22
  warnings-as-errors, clang-format, synthetic public-boundary tests, the
  600-file repository scan, the 322-row/14-column unique catalogue, and
  `git diff --check` pass. Independent review reproduces the GCC Release
  build and all 116 CTests, checks the implementation against the raw Ghidra
  evidence, and returns GO with no P0-P3 finding. Hosted platform builds
  remain the publication gate.
- The ADR-0015 portable binding-remap core exposes seven typed gameplay actions
  through cancel-first move, unique-supported-action-only atomic swap, and
  default-binding reset operations while preserving calibration and the
  immutable active router. The portable GCC/Ninja build passes 115/115 CTests;
  the complete MSVC 19.51/Ninja Windows product links and passes 125/125 CTests,
  including both D3D11 product smokes. Clang-format warnings-as-errors,
  synthetic public-boundary tests, the 596-file repository scan, and
  `git diff --check` pass. Independent review found four issues, verified their
  fixes, and returned final GO with no remaining P0-P3 finding. Hosted Windows,
  Linux, macOS, and Apple builds remain the publication gate.
- The private iOS controller-calibration slice reuses the complete portable
  active/persisted/draft model and exact runtime preview transform. Repair
  classification now has portable coverage for clean defaults, current,
  backup, invalid-current fallback, and future-schema blocking. A complete
  GCC/Ninja build passes 114/114 CTests; a complete MSVC 19.51/Ninja rebuild
  links `AirfixDogfighter.exe` and passes 124/124 CTests. Hosted Apple
  `iphoneos`/`iphonesimulator` compilation, independent final review, and
  physical iPhone save/lifecycle acceptance remain gates.
- Controller Profile Core V1 and its native startup slice pass a fresh
  362-step Windows GCC 15.2/Ninja build with 113/113 CTests and a fresh
  642-step MSVC 19.51/Ninja build of `AirfixDogfighter.exe` with 121/121
  CTests. Its default
  transform is exhaustively identical over all 65,535 valid Q15 values on all
  four axes. Portable fresh-pair tests prove old queued input is discarded,
  a held full snapshot stays blocked, exactly two neutral ticks reopen input,
  and the next press uses the replacement table. Sparse-profile tests prove
  unmapped full-state and later events never poison the router. Clang 22
  warnings-as-errors, synthetic public-boundary tests, and the 579-file
  repository scan pass. The iOS startup policy independently covers late
  profile completion, hidden/background completion, and paused Resume gating.
  Independent final re-review reproduces the old sparse-profile failure,
  confirms the fix, and reports GO with no P0-P3 finding. Hosted Apple
  compilation remains the release gate for this new slice.
- The isolated mission-outcome state passes fresh complete Windows GCC
  15.2/Ninja and MSVC 19.51/Ninja builds plus 108/108 CTests in each. Clang
  22.1.8 passes a warnings-as-errors syntax check. Ghidra and Rizin agree on
  all sixteen selected `AfEngine.dll` boundaries; all three RE wrapper suites,
  12 Rizin exporter tests, public-boundary tests and the 561-file scan, the
  313-row/14-column unique catalogue, changed-document links,
  changed-addition local-path and reserved-ID scans, clang-format, and
  `git diff --check` pass. Review's sole P2 confidence overclaim was corrected
  by capping the static evidence at medium/`2`; final re-review reports GO with
  no remaining P0-P3 finding. Hosted platforms remain the publication gate.
- The one-event pitch/bank step passes fresh complete Windows GCC 15.2/Ninja
  and MSVC 19.51/Ninja builds plus 107/107 CTests in each. Independent review
  reports GO with no P0-P3 findings after its own clean GCC build and Clang 22
  warnings-enabled syntax check. Clang-format, all three reverse-engineering
  wrapper suites, 12 Rizin exporter tests, changed-document links,
  changed-scope local-path scanning, the 553-file public-boundary scan, and
  `git diff --check` pass. Hosted platform and Apple SDK builds remain the
  publication gate.
- The bootstrap/module-loading cross-check matches all nine published
  representative function boundaries exactly between Ghidra 12.1.2 and Rizin
  0.9.1. The Ghidra, working-copy, and Rizin wrapper suites, 12 normalized
  Rizin exporter tests, changed-document links, changed-scope local-path scan,
  the 280-row unique function catalogue with only its two known historical
  missing references, the 547-file public-boundary scan, `git diff --check`,
  and the complete existing 115-test Windows product suite pass. No original
  executable or plugin was run.
- The staged native control-command step passes fresh complete Windows GCC
  15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds plus all 104 CTests.
  A fresh Clang 22.1.8 build passes the dedicated exhaustive test; one
  independent review additionally passes both GCC and Clang
  warnings-as-errors builds. All three reviewers report GO with no P0-P3
  findings. Public-boundary tests, 12 Rizin export tests, all three
  reverse-engineering wrapper suites, the 527-file public scan,
  changed-document links, local-path scan, clang-format, and
  `git diff --check` pass.
- The discrete native control-command slice passes complete clean Windows GCC
  15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds plus all 103 tests.
  Fresh Clang 22.1.8 and independent GCC warnings-as-errors builds pass the
  dedicated exhaustive reducer test. Ghidra/working-copy/Rizin wrapper suites,
  12 Rizin export tests, synthetic boundary tests, the 523-file public scan,
  268-row function catalogue, changed-document links, local-path scan, and
  `git diff --check` pass. Independent review's sole P2 catalogue overclaim is
  fixed; final review reports no open P0-P3. Pull-request Actions runs
  `30511495498` and `30511495515` pass all seven hosted Windows product,
  Windows/Ubuntu/macOS portable, clangd, iPhoneOS, and iPhoneSimulator gates.
- The backend render-scale slice passes independent complete MSVC 19.51 and
  MinGW GCC 15.2 Windows product rebuilds and all 94 tests in each build. Both
  public D3D11 smoke modes render a visible frame; the added scaled mode
  exercises a 50% offscreen color/depth target, Original 4:3 viewport, linear
  presentation pass, and unchanged output backbuffer without private content.
  Controlled owner-local 50% and 200% captures are distinct 960x540 outputs
  and remain outside Git. Hosted Visual Studio, iPhoneOS, and iPhoneSimulator
  results are recorded by the merge gate for this slice; physical-device Metal
  acceptance remains pending.
- The native-render-layout slice passes a clean 292-step GCC 15.2/Ninja build
  and all 90 portable tests. Full MSVC 19.51 and MinGW GCC 15.2 Windows product
  builds each pass 93/93 tests, including the data-less D3D11/XAudio2 smoke.
  A representative private D3D11 mission capture made after physical-size
  validation is exactly 3840x2160; the original installation and owner data
  remained unchanged and outside Git. The 459-file public-boundary scan,
  `actionlint`, changed-document link and local-path checks, and
  `git diff --check` pass. GitHub Actions runs `30470411401` and `30470413579`
  pass all seven hosted Visual Studio product, Windows/Ubuntu/macOS portable,
  clangd, iPhoneOS, and iPhoneSimulator gates.
- The authenticated aircraft-audio slice passes a fresh 289-step GCC 15.2
  build and all 89 portable tests. A fresh native MSVC 19.51/Ninja product
  build compiles 548 steps and passes 91/91 tests, including the real hidden
  D3D11/XAudio2 smoke; the existing MinGW product build independently passes
  the same 91/91 suite. Synthetic public-boundary tests and the 449-file scan,
  `actionlint`, changed-source clang-format, changed-scope local-path scanning,
  and `git diff --check` pass. A complete owner-local AFPACK also passes the
  hidden authenticated load/register path outside Git. GitHub Actions runs
  `30457131783` and `30457131656` pass the hosted Visual Studio product,
  Windows/Ubuntu/macOS portable, clangd, `iphoneos`, and `iphonesimulator`
  publication gates.
- The native iOS audio slice passes a fresh 281-step MinGW GCC 15.2 Release
  build and all 87 portable tests. Xcode 26.6 compiles both ARM64 `iphoneos`
  and `iphonesimulator` data-less bundles with deployment target 16.4 after
  compiling the AVAudioEngine Objective-C++ backend. The synthetic and actual
  441-file public-boundary scans, changed-source formatting, changed-scope
  local-path scan, and `git diff --check` pass. Audible output, interruption
  timing, and wired/Bluetooth route behavior remain explicitly unverified
  until physical-device acceptance.
- The AirCraft audio-composition slice passes a fresh 281-step MinGW GCC 15.2
  Release build and all 87 portable tests; the code-intelligence build also
  passes 87/87. The native product build passes 89/89 tests. Its hidden app
  validates D3D11 readback/resize, registers silent synthetic PCM, and drives
  XAudio2 2.9 through reconstructed engine start/running/shutdown plus
  destroyed-dive/recovery commands. Clangd reports zero errors in both new
  translation units. The three reverse-engineering wrapper suites, twelve
  Rizin normalization tests, synthetic and 438-file public-boundary scans,
  `actionlint`, new-source formatting, the 17-file local-path scan, and
  `git diff --check` pass.
- The first Windows product slice passes a local MinGW GCC 15.2 Release build
  and 85/85 CTest cases. The final test creates a hidden SDL3
  window, compiles HLSL, renders the shared scene through D3D11/DXGI, reads the
  back buffer, verifies visible pixels, recreates the swap-chain targets, and
  verifies a second frame. The same change
  passes a separate 271-step portable build with 84/84 tests, public-boundary
  unit tests and the 422-file scan, `actionlint`, formatting validation, and
  `git diff --check`. Pull-request Actions run `30439266445` passes the
  Visual Studio 2026 D3D11 product job and all portable Ubuntu, Windows, macOS,
  and clangd jobs; iOS run `30439256007` passes both `iphoneos` and
  `iphonesimulator`.
- Merged projectile runtime-pool main commit `bfde965` passes the exact-main
  Ubuntu, Windows, ARM64 macOS, clangd/code-intelligence, `iphoneos`, and
  `iphonesimulator` Actions jobs.
- The portable projectile runtime-pool slice passes a fresh 268-step
  Windows GCC 15.2 Release build and separate code-intelligence build plus
  83/83 tests in both. Its compilation database has 171 portable entries and
  no Apple-only source; clangd 22.1.8 reports zero errors in all three affected
  translation units with the trusted GCC query driver. The Ghidra,
  working-copy, and Rizin wrapper suites, twelve Rizin normalization tests,
  synthetic public-boundary tests and the 410-file public scan, the 265-row
  unique function catalogue with valid source paths and only its two known
  missing references, `actionlint`, the 19-file local-path scan, and
  `git diff --check` pass. Exact commit `6c5d386` compiles all 268 steps with
  GCC 13.3 and passes 83/83 tests in 26.23 seconds in a fresh isolated
  `Airfix-Dev` WSL clone. The exact-main GitHub Actions publication gate passed
  all six jobs.

- The original single-player frontend/campaign/profile flow is now statically
  joined under `EV-20260803-002`: startup user load, eight main-menu routes,
  profile select/create/delete, two Axis/Allied ten-row campaign selectors,
  selection/briefing/start, pause/result/return, success-only next-row
  progression, exact result score terms, cumulative stats, `SCOR`, and the
  remembered roster write. Ghidra 12.1.2 is primary and Rizin 0.9.1
  independently matches the central function ranges/call graph. The original
  roster reader is non-transactional and unsafe on malformed extents; its
  direct `wb` writer is non-atomic and has a short-write success defect. A new
  bounded valid-file importer and portable campaign model now implement the
  evidence-ready subset with synthetic malformed-input and transition tests.
  The canonical 112-byte `AFCS` schema-1 codec now preserves optional
  `THRD`/`AXMI`/`ALMI`/`SCOR` plus the eight neutral cumulative counters,
  rejects noncanonical current content, and preserves valid future schemas
  byte-for-byte. A separate filesystem target now composes that codec with the
  shared durable document pair, selecting current, backup, or the canonical
  empty record and blocking downgrade/unsafe writes within an injected
  already-partitioned private profile leaf. A separate fail-closed adapter now
  copies the validated legacy numeric subset to `AFCS`, rejects unsupported
  `THRD`, and reports omitted identity, portrait, medal, and unknown records so
  the source remains retained. All pieces remain deliberately unwired.
  Complete frontend parity, difficulty, normal reward rules, complete profile
  migration, profile identity/medal schema, platform root/lifecycle
  integration, and bit-identical legacy output remain NO-GO/unimplemented.

- `EV-20260803-003` / `EXP-20260803-116` now maps the complete static AFS
  mission-scripting boundary without changing runtime code. Ghidra Headless
  12.1.2 and independent Rizin 0.9.1 agree on the selected parser/compiler,
  lifecycle, process, execution, scheduler, and interpreter ranges, calls,
  xrefs, structures, and the 70-entry `0x01–0x46` dispatch. Local process
  ordering, immediate named first steps, delay/yield, ordinary
  process-before-trigger polling, reset, and destruction are recovered.
  A fail-closed parser policy and static process model are evidence-ready;
  parser implementation, typed pointer/call replacement, safe re-entrant
  lifetime, the full interpreter, outer Load/Reset/Start and pause ownership,
  global AI/physics order, numerical/time parity, multiplayer, and platform
  integration remain NO-GO/unimplemented. Final independent Ghidra, Rizin, and
  closed-diff reviews report no open P0–P3 finding; wrapper, documentation,
  link, public-boundary, path, ID, UTF-8, VA/RVA, opcode, call-edge, and diff
  validation all pass.

## Blockers

- None for Phase 1 static analysis.
- No owner action is required to begin the Windows x64 product spike. The
  platform API/minimum-OS choices are engineering decisions, not blockers.
- The connected repository is public. Before the first signed device spike,
  signed IPA artifacts must be encrypted/protected (or repository visibility
  deliberately changed), because provisioning data must not be published.
- A signing certificate/profile for both device UDIDs, protected Actions
  secrets, and a Windows IPA installation path are required before that spike.
- No public distribution is planned; private signed/converted artifacts must not
  be shared.
