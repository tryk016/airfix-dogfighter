# Code intelligence with clangd

The repository provides one reproducible CMake preset for the portable C++20
code. It generates the compilation database used by clangd and other Language
Server Protocol (LSP) clients without selecting a machine-specific compiler or
storing a local path in version control.

This setup covers the portable libraries, tools, and synthetic tests. It does
not read or require original Airfix Dogfighter files.

## Quick start

Install:

- CMake 3.25 or newer;
- Ninja;
- a C++20 compiler available to CMake;
- clangd, either directly or through the editor integration.

From the repository root, run:

```sh
cmake --preset code-intelligence
cmake --build --preset code-intelligence
ctest --preset code-intelligence
```

CMake writes the generated database to
`build/code-intelligence/compile_commands.json`. The committed `.clangd`
configuration tells clangd to look in that directory. Both the build tree and
every generated `compile_commands.json` are ignored by Git.

Re-run the configure command after changing `CMakeLists.txt`, adding or removing
a source file, or changing compilers. To deliberately discard a stale CMake
cache and detect the current toolchain again, use:

```sh
cmake --preset code-intelligence --fresh
```

Normal source edits do not require regenerating the database.

## VS Code

1. Install the official
   [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd).
2. Open the repository root, not the build directory.
3. Generate the database with `cmake --preset code-intelligence`.
4. Reload the window or restart the language server if it was already running.

Do not run two C/C++ language servers for the same workspace. If the Microsoft
C/C++ extension is also installed, disable its IntelliSense for this workspace
while clangd provides completion and diagnostics.

No repository-local VS Code settings are required. In particular, do not commit
a compiler path. If a GCC or MinGW installation uses implicit system include
paths that clangd cannot discover, add a narrowly scoped `--query-driver`
argument to your **user** settings:

```json
{
  "clangd.arguments": [
    "--query-driver=C:/path/to/trusted/toolchain/bin/g++.exe"
  ]
}
```

clangd may execute an allowed query driver to inspect its target and default
include paths. Only allow a compiler you installed and trust; avoid broad
wildcards. The allowlist must match the driver path that clangd tries to query,
which can differ from the resolved path stored in the database when a shim or
symlink is involved. Run clangd with verbose logging and use the path in its
`System include extraction: not allowed driver ...` message.

## CLion

Open the repository root as a CMake project. CLion reads the committed
`CMakePresets.json`; enable the `code-intelligence` configure preset if it is
not enabled automatically, then select its matching build preset.

CLion can build its project model directly from the CMake preset, so copying or
symlinking `compile_commands.json` into the source tree is unnecessary. The same
generated database remains available to external clangd-based tools.

Machine-specific overrides belong in `CMakeUserPresets.json`. CMake loads that
file alongside the project preset, and this repository intentionally ignores it.
Do not put local toolchain paths in `CMakePresets.json`.

## Other LSP clients

Launch clangd with the repository root as the workspace. clangd discovers
`.clangd` there and resolves its compilation database path relative to that
file. A client that explicitly overrides the database directory should use the
locally generated `build/code-intelligence` directory.

If the client starts clangd before the database exists, generate it and restart
the server. Check the clangd log when standard-library headers are unresolved;
on Windows with GCC or MinGW, a trusted, user-local `--query-driver` setting may
be required as described above.

The relevant upstream references are:

- [CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html);
- [CMake compilation database generation](https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html);
- [clangd project configuration](https://clangd.llvm.org/config);
- [how clangd uses compile commands](https://clangd.llvm.org/design/compile-commands).

## Windows and Apple-platform boundaries

The preset intentionally selects the portable build and disables
`AIRFIX_BUILD_IOS_APP`. On Windows its compilation database accurately describes
the `.cpp` translation units, tools, and tests for the compiler selected by
CMake. It does not contain a usable Apple SDK, deployment target, framework
search paths, or Xcode build settings.

Configure Ninja from the appropriate compiler environment. In particular, start
it from Visual Studio Developer PowerShell when using MSVC or `clang-cl`, so the
Windows SDK and standard-library environment are available. Do not change
compilers inside an existing build tree; use `--fresh` or a separate binary
directory selected in an ignored `CMakeUserPresets.json`.

Consequently, a Windows-generated database cannot provide authoritative
diagnostics or completion for:

- Objective-C++ `.mm` platform adapters;
- UIKit, MetalKit, or GameController framework APIs;
- Metal Shading Language `.metal` sources;
- Apple-only availability, signing, or bundle configuration.

Do not add fake Apple headers or hard-coded SDK paths to the portable database.
The unsigned iPhoneOS and iPhoneSimulator GitHub Actions jobs remain the source
of truth for Apple compilation. Later, a Mac with Xcode can provide native
Objective-C++, UIKit, and Metal indexing; those local Xcode settings must remain
uncommitted.

The CMake `CMAKE_EXPORT_COMPILE_COMMANDS` facility is supported by Makefile and
Ninja generators, not by the Xcode generator used for the iOS application.

## Troubleshooting

List the shared presets:

```sh
cmake --list-presets
cmake --build --list-presets
ctest --list-presets
```

If diagnostics do not match the selected compiler:

1. inspect the compiler reported by the configure command;
2. regenerate with `cmake --preset code-intelligence --fresh`;
3. restart clangd;
4. enable verbose clangd logging;
5. if implicit GCC/MinGW headers are still missing, allow only the trusted path
   reported by `System include extraction: not allowed driver ...`.

Never edit or commit `compile_commands.json`. It is generated state containing
machine-specific source, build, and compiler paths.
