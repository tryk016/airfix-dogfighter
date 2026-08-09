# ADR-0004: Public unsigned iOS builds with owner-local signing

**Status:** Accepted, superseding the hosted private-signing variant

**Date:** 2026-07-21; revised 2026-08-09

**Deciders:** project owner and implementation lead

## Context

The repository is intentionally public. Original game data and private HD
derivatives must stay owner-local. The first physical tests target iPhone 17 Pro
Max on iOS 26.6 and iPhone SE (3rd generation) on iOS 26.3, while the deployment
target remains iOS 16.4.

An earlier revision prepared a protected GitHub workflow that would import an
Apple certificate and provisioning profile. The owner subsequently selected a
simpler boundary: GitHub compiles only unsigned, data-less products; signing is
performed locally at installation time through Xcode or an owner-selected local
sideloader. The public repository therefore needs no signing secrets and does
not need to change visibility.

## Decision

Use the pinned GitHub-hosted macOS/Xcode environment for:

- unsigned ARM64 `iphoneos` Release compilation;
- unsigned ARM64 simulator compilation and real public Metal/lifecycle smoke;
- deterministic construction of a strictly allow-listed, data-less unsigned
  IPA on trusted `main`; and
- a path-free manifest containing source, toolchain, dependency-lock, and IPA
  identity.

Do not sign in GitHub Actions. Do not configure GitHub environments, Apple
certificates, profiles, device IDs, account sessions, or private mission inputs.

Use either of these local paths:

1. Generate a local Xcode project from the exact trusted revision, explicitly
   enable automatic development signing, select a connected iPhone, and run.
2. Verify the downloaded unsigned IPA and manifest, then pass the IPA to a
   reviewed local sideloader that signs without uploading code or credentials.

The installed application imports the private AFPACK and optional HD package
after installation. Neither belongs in the application artifact.

## Options considered

### A. Public unsigned build plus local signing — selected

| Dimension | Assessment |
|---|---|
| Public-source compatibility | High |
| Signing-secret exposure | None in GitHub |
| Reproducible compilation | High |
| Reproducible signature | Local-tool dependent |
| Windows-only installation | Possible through an accepted local sideloader |
| Official Apple debugging path | Xcode on Mac |

### B. Protected hosted signing workflow

Rejected for the current project. It requires a private signing boundary,
GitHub environment secrets, certificate/profile rotation, and protected signed
artifacts even though the owner needs only private device installation.

### C. Commit or upload game data with the application

Rejected. Copyrighted original files and private derived textures are not CI
inputs, repository content, or public artifacts.

## Consequences

- The GitHub repository remains public.
- GitHub Actions publishes a public **unsigned** package only from trusted
  `main`; pull requests publish no device IPA.
- The unsigned IPA is not installable until a local tool signs it.
- Xcode remains the reference signing and debugging path. A Mac is not needed
  for ordinary development or CI, but is needed for the official Product > Run,
  LLDB, Metal, and Instruments workflows.
- A sideloader must be assessed independently and must sign locally. Cloud
  signing and binary upload are outside the accepted boundary.
- The manifest proves compilation/package identity, not the later local
  signature or device behavior.
- Physical touch, controller, audio, lifecycle, safe-area, Metal, memory, and
  thermal observations remain manual.
- Original and converted resources remain outside the IPA and are imported
  privately after installation.

## Revisit conditions

- Apple changes local development signing or device-installation requirements.
- No acceptable local Windows sideloader supports the unsigned package.
- The owner chooses TestFlight, App Store, or managed external distribution.
- Repeated physical debugging makes a permanent local/self-hosted Mac pipeline
  materially faster.
- The public artifact boundary needs additional executable or resource files.

## Action items

1. [x] Keep repository and CI data-less.
2. [x] Build unsigned simulator and device targets at deployment target 16.4.
3. [x] Run a real public simulator Metal/audio-probe/lifecycle smoke.
4. [x] Add deterministic unsigned IPA packaging and synthetic boundary tests.
5. [x] Add explicit local-Xcode automatic-signing project generation.
6. [ ] Download and independently verify the first `main` unsigned artifact.
7. [ ] Select Xcode or a reviewed local sideloader and sign during installation.
8. [ ] Execute the physical checklist independently on both iPhones.
