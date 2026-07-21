# Local analysis toolchain lock

**Captured:** 2026-07-21

**Install method:** Scoop, 64-bit packages

**Purpose:** reproducible static analysis and portable C++ development on Windows

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
