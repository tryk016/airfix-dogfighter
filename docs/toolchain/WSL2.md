# Isolated local Linux builds with WSL2

`Airfix-Dev` is a dedicated Ubuntu 24.04 WSL2 distribution for fast local
configuration, compilation, and testing of the portable C++20 code. It is
deliberately independent from every other installed distribution and from the
Windows worktree.

This environment is not required by CI, is not an iOS build host, and is not a
sandbox for executing the original Windows game.

## Isolation contract

- Never change the global WSL default distribution or the user's global
  `.wslconfig` for this project.
- Never use `wsl --shutdown`; it would stop distributions belonging to other
  projects.
- Never unregister, import over, reset, or delete a distribution without a
  separately reviewed recovery plan.
- Target every administrative command explicitly with
  `wsl -d Airfix-Dev ...`.
- Keep the Git clone in the distribution's native ext4 filesystem, not under
  `/mnt/c`, `/mnt/e`, or another Windows mount.
- Do not copy original game files into WSL. Reverse engineering remains in the
  offline Windows workshop.
- Do not install background services or expose listening ports.

The committed [Airfix-Dev.wsl.conf](../../tools/wsl/Airfix-Dev.wsl.conf)
disables Windows-drive automount, Windows executable interop, and inheritance
of the Windows `PATH`. The default non-root user is `airfix`.

## Verified toolchain

The exact captured versions are in [LOCK.md](LOCK.md). The distribution contains
only the development packages needed for the portable build:

```text
build-essential
ca-certificates
clang
clangd
cmake
git
ninja-build
python3
python3-venv
```

APT uses the signed official Ubuntu 24.04 archives over HTTPS. Package hashes
must never be bypassed. A mirror hash mismatch is an error to diagnose, not a
reason to use `--allow-unauthenticated`, disable signature checks, or install a
downloaded `.deb`.

## Initial repository checkout

The clone contains only public reconstruction sources:

```powershell
wsl -d Airfix-Dev -u airfix -- bash -lc '
  mkdir -p /home/airfix/src
  git clone --branch agent/udsp-parser-ci --single-branch \
    https://github.com/tryk016/airfix-dogfighter.git \
    /home/airfix/src/airfix
'
```

For another branch, fetch and check out that explicit branch inside the clone.
Do not bridge to the Windows worktree and do not use a shared Git index.

## Clean configure, build, and test

The code-intelligence preset is the repeatable Ninja configuration:

```powershell
wsl -d Airfix-Dev -u airfix -- bash -lc '
  set -eu
  cd /home/airfix/src/airfix
  cmake --preset code-intelligence
  cmake --build --preset code-intelligence --parallel 4
  ctest --preset code-intelligence
'
```

Before validating a newly pushed commit:

```powershell
wsl -d Airfix-Dev -u airfix -- bash -lc '
  set -eu
  cd /home/airfix/src/airfix
  git fetch --prune origin
  git checkout agent/udsp-parser-ci
  git pull --ff-only
  cmake --preset code-intelligence
  cmake --build --preset code-intelligence --parallel 4
  ctest --preset code-intelligence
'
```

Use `--ff-only`; a validation clone must not create local merge commits. A
fresh build can be obtained safely by using a new binary directory or a new
clone. Do not delete a computed path or another project's build tree.

## Diagnostics and lifecycle

Read-only checks:

```powershell
wsl --list --verbose
wsl -d Airfix-Dev -u root -- dpkg --audit
wsl -d Airfix-Dev -u airfix -- cmake --version
wsl -d Airfix-Dev -u airfix -- ninja --version
```

When an explicit restart is needed for this distribution's own `wsl.conf`,
terminate only this distribution:

```powershell
wsl --terminate Airfix-Dev
```

Ordinary commands may leave it in `Stopped` state automatically. That is
expected. Never start, stop, update, or repair the other distributions as part
of the Airfix workflow.

## Current validation checkpoint

At public commit `db3730d`, a clean native-filesystem configure generated the
compilation database, built all 135 targets, and passed all 39 portable CTest
cases. The corresponding GitHub Actions Windows, Ubuntu, macOS, clangd-preset,
`iphoneos`, and `iphonesimulator` jobs were also green. WSL is therefore a
latency optimization, not a replacement for the cross-platform CI matrix.
