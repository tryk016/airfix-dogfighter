# Local development and analysis toolchain lock

This document distinguishes software that is installed and verified from
software that is only approved and awaiting an owner action. Paths are
deliberately omitted because all installations are machine-local.

## Windows baseline

**Captured:** 2026-07-21
**Install method:** Scoop, 64-bit packages
**Purpose:** primary Ghidra analysis and portable C++ development on Windows

The SHA-256 values below are the download hashes from the installed Scoop
manifests. Installation completed with Scoop's hash verification enabled.

| Package | Version | Bucket | License | Upstream artifact | SHA-256 |
|---|---|---|---|---|---|
| Temurin JDK | 21.0.11+10 | `java` | GPL-2.0-only WITH Classpath-exception-2.0 | [OpenJDK21U-jdk_x64_windows_hotspot_21.0.11_10.zip](https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.11%2B10/OpenJDK21U-jdk_x64_windows_hotspot_21.0.11_10.zip) | `d3625e7cadf23787ea540229544b6e2ab494b3b54da1801879e583e1dfee0a64` |
| Python | 3.14.6 | `main` | Python-2.0 | [python-3.14.6-amd64.exe](https://www.python.org/ftp/python/3.14.6/python-3.14.6-amd64.exe) | `14b3e9a710a3fcf0bd9b55ab6b60412bd91227563f813fc49040cabc0209e0bd` |
| CMake | 4.4.0 | `main` | BSD-3-Clause | [cmake-4.4.0-windows-x86_64.zip](https://github.com/Kitware/CMake/releases/download/v4.4.0/cmake-4.4.0-windows-x86_64.zip) | `156d70eb7625a7b469444df7d0861d2af8d5d0a437fce32c350372b08f5620e8` |
| Ninja | 1.13.2 | `main` | Apache-2.0 | [ninja-win.zip](https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip) | `07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65` |
| LLVM | 22.1.8 | `main` | Apache-2.0 with LLVM exceptions (manifest: NCSA) | [LLVM-22.1.8-win64.exe](https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/LLVM-22.1.8-win64.exe) | `16e5709785fef73c854646241c4a92c5cd574318d1b33c63330dd7721903e55c` |
| GCC/MinGW-w64 (Nuwen) | 15.2.0 | `main` | GPL-3.0-or-later | [components-20.0.7z](https://nuwen.net/files/mingw/components-20.0.7z) | `561d873b7f95dbb39a34b7ab00050dc6028808310a847721a8aea5e5b0bff1c9` |
| Ghidra | 12.1.2 (20260605) | `extras` | Apache-2.0 | [ghidra_12.1.2_PUBLIC_20260605.zip](https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip) | `b62e81a0390618466c019c60d8c2f796ced2509c4c1aea4a37644a77272cf99d` |
| actionlint | 1.7.12 | `main` | MIT | [actionlint_1.7.12_windows_amd64.zip](https://github.com/rhysd/actionlint/releases/download/v1.7.12/actionlint_1.7.12_windows_amd64.zip) | `6e7241b51e6817ea6a047693d8e6fed13b31819c9a0dd6c5a726e1592d22f6e9` |

Bucket sources at capture time:

- `main`: `https://github.com/ScoopInstaller/Main`
- `extras`: `https://github.com/ScoopInstaller/Extras`
- `java`: `https://github.com/ScoopInstaller/Java`

## Verified commands

- Temurin reports OpenJDK `21.0.11` LTS.
- Python reports `3.14.6`.
- CMake reports `4.4.0`.
- Ninja reports `1.13.2`.
- Clang/LLVM reports `22.1.8`, target `x86_64-pc-windows-msvc`.
- GCC reports `15.2.0`.
- Ghidra `analyzeHeadless` starts successfully when `JAVA_HOME` points at the
  locked Temurin installation.
- actionlint reports `1.7.12`; the initial portable CI workflow passes lint.

The installed packages are local tooling only. Production/CI dependencies must
still be declared by the repository and must not assume these absolute paths.

## Portable reverse-engineering tools

**Captured and verified:** 2026-07-27
**Install method:** official release archives expanded into ignored,
non-elevated, project-local directories

| Tool | Version | Status | License | Official source | SHA-256 / signature |
|---|---:|---|---|---|---|
| Rizin Windows shared64 | 0.9.1 (`c3a90e9226d977f58f4e9c75f78fa6b07afe13c7`) | Installed; `rizin -v` and `rz-bin -v` verified | LGPL-3.0-or-later core with GPL-3.0-or-later components; see upstream files | [release](https://github.com/rizinorg/rizin/releases/tag/v0.9.1), [archive](https://github.com/rizinorg/rizin/releases/download/v0.9.1/rizin-windows-shared64-v0.9.1.zip) | `45ffa004e26653eaee8d262b8e6eeae09402a7d319c1f20a2ba794bd6af1cc28`; upstream archive digest matched; executables are not Authenticode-signed |
| Cutter Windows x86_64 | 2.5.0, bundled Rizin 0.9.1 | Installed as a portable GUI; not launched during setup | GPL-3.0 | [release](https://github.com/rizinorg/cutter/releases/tag/v2.5.0), [archive](https://github.com/rizinorg/cutter/releases/download/v2.5.0/Cutter-v2.5.0-Windows-x86_64.zip) | `a04154a03a392dbf5886a629938582f7d23a93636fa0611c3e1c34905b197e69`; upstream archive digest matched; executable is not Authenticode-signed |
| x64dbg portable snapshot (`x32dbg`) | 2026.05.27 | Installed; only the x86 executable was signature-checked and has not been run | GPL-3.0 | [release](https://github.com/x64dbg/x64dbg/releases/tag/2026.05.27), [archive](https://github.com/x64dbg/x64dbg/releases/download/2026.05.27/snapshot_2026-05-27_12-11.zip) | `d41966dfc5b435a372798245300ca0ab7bb8e48bdbf48512c6fb20fcca427697`; `x32dbg.exe` Authenticode status `Valid`, signer `Open Source Developer, Duncan Ogilvie`, certificate thumbprint `CF70E6336E3B49973A0E85AA8E8EA8936A6FFB70` |
| `rzpipe` Python wheel | 0.6.2 | Installed in an ignored dedicated Python 3.14 virtual environment | MIT | [PyPI project](https://pypi.org/project/rzpipe/0.6.2/), [Rizin scripting guide](https://book.rizin.re/src/scripting/rz-pipe.html) | `a620e2547e54f901233be6e1eb283fd7c499f6d9bd1651795c2ff53e5fab1d77`; verified before offline venv installation |

Cutter's `rz-ghidra` plugin and Sleigh definitions are part of the verified
Cutter archive. They may provide optional pseudocode to the Rizin report, but
they do not make Cutter an independent evidence source from Rizin. Ghidra
12.1.2 remains the canonical decompiler and report generator.

### Binary Ninja Free Desktop

| Tool | Version | Status | License / use restriction | Official source | Published artifact digest |
|---|---:|---|---|---|---|
| Binary Ninja Free Desktop for Windows | 5.3 R2 (`5.3.9757`) | Installed from the owner-accepted, hash-verified official installer; executable version and signature verified; offline settings and manual pilot pending | Single designated user; non-commercial/internal evaluation under the current Free license; Free has no API or plugin support | [Free download](https://binary.ninja/free/), [Free license](https://docs.binary.ninja/about/license/free.html), [release](https://github.com/Vector35/binaryninja-api/releases/tag/stable/5.3.9757) | official installer `binaryninja_free_win64.exe`: `5f6096acb03dd70d1a0572257c44471a0b62653788348cf69dc829212882a618`; installed executable Authenticode status `Valid`, signer `Vector 35 Inc`, certificate thumbprint `0E164C61A81A1BD5E52964AA44E454047020EF02` |

The Free Desktop installer was fetched and run only after the owner personally
accepted Vector 35's current terms and confirmed its official SHA-256. Keep all
`.bndb` files local. Binary Ninja Cloud and every other upload-based analysis
service are prohibited for original files and derived binaries.

The workshop does not install dnSpy, ILSpy, or W32Dasm: the reference program is
native PE32/x86, so those tools add no material value to this workflow.

## Isolated WSL2 Linux toolchain

**Captured and verified:** 2026-07-27
**Distribution:** dedicated `Airfix-Dev`, Ubuntu 24.04 on WSL2
**Package source:** official Ubuntu archives over HTTPS with APT signature and
package-hash verification

| Component | Verified version |
|---|---:|
| CMake | 3.28.3 |
| Ninja | 1.11.1 |
| GCC/G++ | 13.3.0 |
| Clang / clangd | 18.1.3 |
| Git | 2.43.0 |
| Python | 3.12.3 |

`dpkg --audit` is clean. Automount and Windows interop are disabled inside this
distribution, so it neither builds on a mounted Windows worktree nor inherits
Windows executables. A clean native-filesystem configure/build produced all 135
targets and CTest passed 39/39 tests at commit `db3730d`. Other distributions,
the default distribution selection, and global `.wslconfig` were not changed.
See [WSL2.md](WSL2.md) for the operating runbook.
