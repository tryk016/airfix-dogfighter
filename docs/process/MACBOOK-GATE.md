# MacBook escalation gate

**Status:** accepted project policy

**Decision date:** 2026-07-21

## Policy

Continue on Windows plus GitHub Actions by default. Do not request a MacBook for
convenience, preference, or hypothetical future work.

Escalate to the project owner only when one of these conditions is met:

1. **Hard gate:** the current required task cannot be completed or verified
   safely with Windows, GitHub Actions, and the available iPhones.
2. **Measured acceleration:** using a MacBook is expected to reduce the remaining
   time for a concrete task or milestone by at least 20%.

The threshold applies to the affected remaining work, not the total project. A
20% estimate must include the current path, proposed Mac path, evidence, and
assumptions.

## Required owner notification

When the gate is reached, send a clearly labeled message using this structure:

```text
MACBOOK GATE — BLOCKER
Current task:
Why Windows + Actions cannot complete it:
What must be done on the MacBook:
Expected duration:
Files/accounts/devices needed:
Can other work continue in parallel: yes/no
```

or:

```text
MACBOOK GATE — >=20% ACCELERATION
Current task/milestone:
Measured current feedback cycle:
Expected MacBook feedback cycle:
Estimated saving: N% (calculation)
What moves to the MacBook:
What remains on Windows/Actions:
Recommendation: move now / continue without Mac
```

Do not bury the notification in a general status update. State explicitly
whether work is blocked or merely slower.

## Acceleration calculation

Use:

```text
saving_percent = (time_without_mac - time_with_mac) / time_without_mac * 100
```

Estimate total remaining iteration time for the affected work, including:

- GitHub Actions queue and build time;
- artifact download and Windows-to-iPhone installation;
- reproduction/setup time;
- number of expected debug/test iterations;
- ability to inspect logs, captures, and symbols;
- context switching and manual transfer work.

Prefer measured data from at least one real CI/device iteration. If no
measurement exists, label the estimate low-confidence and do not recommend a
move based only on an unverified percentage unless the hard-gate rule applies.

## Work that does not currently need a MacBook

- File inventory, hashing, PE inspection, Ghidra, and debugger research on the
  Windows reference build.
- `UDSP` and other asset-format reconstruction.
- Local Windows asset conversion and `.afpack` production.
- Portable C++ core, headless tests, Windows reference harness, and most parity
  analysis.
- GitHub Actions simulator/device compilation and signed IPA export.
- Documentation, test fixtures, CI configuration, and static API availability
  checks.
- Manual gameplay tests when the signed IPA installs and provides sufficient
  diagnostics.

## Likely hard gates

These are candidates, not automatic reasons to move:

- A reproducible iOS crash requires interactive LLDB attachment to the owner's
  physical device and logs/symbols from the CI build are insufficient.
- A Metal rendering fault requires Xcode GPU frame capture, shader debugging, or
  on-device Metal validation unavailable through the current workflow.
- Sustained performance, memory, energy, or thermal diagnosis requires
  Instruments connected to the physical device.
- Code signing, provisioning, entitlement, or installation failure cannot be
  resolved with GitHub Actions logs and the selected Windows IPA installation
  method.
- An iOS lifecycle/audio/controller bug cannot be observed with adequate timing
  and state detail from in-app diagnostics.
- A required on-device test must be launched repeatedly under Xcode automation
  and cannot run against the owner's phone from a hosted runner.

## Likely >=20% acceleration cases

- Rapid Metal/UI/input tuning requires many device iterations and the
  Actions-build-download-install cycle dominates the work.
- Touch layout changes must be tried across both devices dozens of times and
  direct Xcode deploy shortens every loop materially.
- Instruments-guided optimization replaces repeated diagnostic IPA builds.
- Native crash debugging with symbols/breakpoints replaces log-driven binary
  search over many cycles.

Each case still requires a calculation based on observed iteration times.

## Migration boundary

Reaching the gate does not automatically move the whole project off Windows or
GitHub Actions.

Preferred split:

- Windows remains the original-file, Ghidra, format, conversion, and reference
  runtime workstation.
- GitHub Actions remains the reproducible clean build and signing authority.
- MacBook becomes the interactive Xcode/LLDB/Metal/Instruments/device-debugging
  workstation.

Move additional tasks only when dependencies or measured efficiency justify it.
Synchronize source through the private Git repository. Transfer only the private
validated `.afpack` needed for device work; do not casually duplicate the full
original installation or analysis dumps.

## Current decision

No MacBook is required during the current planning, static-analysis, archive,
toolchain, or portable-foundation work. The first gate evaluation occurs before
interactive physical-device debugging, and is repeated after measuring the first
complete Actions-to-device feedback loop.

