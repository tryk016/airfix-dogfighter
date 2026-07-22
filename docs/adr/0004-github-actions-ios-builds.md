# ADR-0004: GitHub Actions builds signed data-less iOS application

**Status:** Accepted

**Date:** 2026-07-21

**Deciders:** project owner and implementation lead

## Context

The owner does not require a local Mac/Xcode environment and wants iOS builds to
run through GitHub Actions. Original game data exists only in the owner-provided
source directory and must not be uploaded. The application is a private sideload
with deployment target iOS 16.4. Physical tests use iPhone 17 Pro Max on iOS
26.6 and iPhone SE (3rd generation) on iOS 26.3.

## Decision

Use an explicit GitHub-hosted macOS runner and pinned installed Xcode for iOS
compilation. Pull requests build/test without secrets or signing. A protected,
manually triggered workflow imports a certificate and provisioning profile from
GitHub environment secrets into an ephemeral keychain and exports a private IPA.

The application bundle contains no original or converted Airfix data. A local
Windows converter creates a versioned private `.afpack`, transferred to and
validated/imported by the installed app. CI uses synthetic fixtures only.

Set the deployment target to iOS 16.4. Do not claim runtime testing on 16.4; the
accepted runtime matrix is iOS 26.6 and 26.3 on the two owner devices.

## Options considered

### A. GitHub-hosted macOS plus separate data package — selected

| Dimension | Assessment |
|---|---|
| Local Mac dependency | None |
| Original-data exposure | Low |
| Reproducibility | High |
| Signing complexity | Medium |
| Device feedback speed | Medium |

### B. GitHub-hosted macOS with assets stored as CI secrets/artifacts

Rejected because the resource set is large, copyrighted, difficult to rotate,
and likely to leak through caches, logs, or artifacts. GitHub secrets are not a
content-distribution system.

### C. Local/self-hosted Mac build

Rejected as the planned primary path because the owner explicitly selected
GitHub Actions and no local Mac is needed. It remains an emergency fallback only
if hosted-runner/Xcode constraints make a required build impossible.

## Consequences

- No local Mac is a project blocker.
- A private GitHub repository, macOS Actions minutes, signing certificate,
  provisioning profile containing both device UDIDs, and protected secrets are
  required before signed builds.
- Build workflows pin runner/Xcode and record actual tool versions.
- Signed IPA generation is manual/protected; untrusted contributions never see
  signing secrets.
- The application must boot without game data and implement secure `.afpack`
  import/replacement before the first full device slice.
- Physical testing remains manual. CI cannot certify touch, controllers, audio,
  thermals, or rendering on either iPhone.
- iOS 16.4 is a deployment/availability contract without runtime coverage in the
  accepted device matrix.
- The Windows-compatible installation path for the produced IPA must be proven
  during the first device spike.
- A MacBook is not a default prerequisite. Escalate clearly if work becomes
  blocked without local Xcode or measured evidence shows at least 20% saving for
  the affected remaining work; see `docs/process/MACBOOK-GATE.md`.

## Revisit conditions

- GitHub hosted images cannot provide a compatible Xcode/SDK.
- Actions costs or queue times become unacceptable.
- A secure/private content transfer method cannot be made usable.
- The owner later provides a local Mac or self-hosted macOS runner and prefers it.
- The MacBook gate reports a hard blocker or >=20% measured acceleration.
- Actual runtime verification on iOS 16.4 becomes a requirement.

## Action items

1. [x] Connect the source remote; keep signing disabled while it is public.
2. [x] Add explicit macOS runner/Xcode preflight workflow.
3. [x] Build signing-free simulator and device targets at deployment target
   16.4.
4. [x] Define and implement the private `.afpack` package boundary.
5. [ ] Register both device UDIDs and create/export signing materials.
6. [ ] Configure the protected `ios-private` environment and secrets.
7. [ ] Build, download, install, and verify the first IPA on both phones.
