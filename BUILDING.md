# Building wozzits

The short path: build the app, run the editor, run the tests. Deeper build
details (individual presets, GPU test suites, V8 scripting, the pre-push hook)
can be documented elsewhere as needed.

## Prerequisites

- **Visual Studio 2022** with the Desktop C++ workload and **LLVM / clang-cl** — the engine builds with `clang-cl` + `lld-link` + Ninja; MSVC is not supported.
- **CMake 3.14+** and **Ninja** (Ninja ships with VS 2022).
- **.NET 10 SDK** — for the editor and the build tool.
- **Git**.

Run the commands below from a **VS 2022 x64 Developer Command Prompt** so
`clang-cl`, `lld-link`, and `ninja` are on `PATH`. A GPU and a display are needed
to actually run the editor. The first build needs internet (CMake fetches
GoogleTest; the editor restores NuGet packages).

## Smoke test: build and run the editor

From the repo root:

```powershell
git submodule update --init external/pmp
.\build.cmd build
```

`build.cmd` builds the C++ engine and publishes the .NET editor into one folder
(default `D:\wozzits-app`). Launch it:

```powershell
D:\wozzits-app\Wozzits.Editor.App.exe
```

To change where it installs, copy `build.env.example` to `build.env` and edit
`WOZZITS_APP_INSTALL_DIR`. Run `.\build.cmd --help` for options (`--config
Release`, `--dry-run`, …).

## Run the tests

```powershell
.\build.cmd test
```

This runs the engine tests (`ctest`) and the editor tests (`dotnet test`).
