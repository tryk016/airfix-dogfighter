# ADR-0003: Private single-player sideload for version 1.0

**Status:** Accepted

**Date:** 2026-07-21

**Deciders:** project owner and implementation lead

## Context

The project needs a firm delivery boundary before reconstruction begins. The
available installation does not include the original CD image/audio tracks. The
owner has an Apple Developer account and an iPhone 17 Pro Max, wants iOS 16.4 as
the minimum deployment target, and does not intend to distribute through the App
Store. Multiplayer, House Editor, and Paint Room are not required for v1.0.

## Decision

Version 1.0 is a private signed sideload containing the single-player game path,
touch and physical-controller support, saves, presentation, audio available in
the installed packages, and the required iOS lifecycle behavior.

Set `IPHONEOS_DEPLOYMENT_TARGET` to 16.4. Use iPhone 17 Pro Max as the primary
physical device. Do not implement multiplayer, House Editor, Paint Room, App
Store services, or CD music for v1.0.

## Consequences

- Scope and parity work focus on `Singleplayer.mode` and shared actor/engine
  modules. `Dogfight.mode` is analyzed only where shared interfaces require it.
- Network protocol reconstruction is not on the v1.0 critical path.
- Editor-specific UI/actions/data are documented only when encountered and do
  not block release.
- Audio startup, saves, and UI must treat missing music as a normal supported
  configuration.
- App Store metadata, review, public entitlement services, analytics, and public
  distribution packaging are removed from the roadmap.
- Development/ad hoc signing and device registration remain required.
- iPhone 17 Pro Max validates the real device path but not minimum-OS hardware;
  iOS 16.4 compatibility needs deployment-target and simulator/availability
  checks.
- A Mac with suitable Xcode is still required and not yet confirmed.

## Revisit conditions

- The owner explicitly changes the private-distribution decision.
- Original CD audio becomes available from a lawful source.
- Multiplayer or either editor is requested for a post-v1 milestone.
- An API/dependency cannot support the iOS 16.4 deployment target.

## Action items

1. [ ] Make absent music a tested asset configuration.
2. [ ] Mark multiplayer and editor parity scenarios deferred for v1.0.
3. [ ] Set the iOS deployment target to 16.4 when the Xcode target is created.
4. [ ] Register and test the iPhone 17 Pro Max with the developer account.
5. [ ] Identify the Mac/Xcode host before the first iOS device spike.

