# Building wozzits-rhi

## Prerequisites

| Tool | Minimum | Notes |
|------|---------|-------|
| CMake | 3.21 | |
| Visual Studio 2022 | 17.x | Desktop C++ workload, for the Windows SDK |
| LLVM / clang-cl | 17.x | VS LLVM tools component or LLVM for Windows |
| Ninja | any | Bundled with VS 2022, or `winget install Ninja-build.Ninja` |

The seed has **no third-party dependencies** — tests use a tiny in-repo
harness, so first configure works offline.

## Configure, build, test

Run from a **VS 2022 x64 Developer Command Prompt** (or Developer PowerShell)
with `clang-cl`, `lld-link`, and Ninja on PATH — matching the
`wozzits-window-engine` toolchain.

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --preset clang-debug
```

Build output lands in `build/clang-debug/`. Substitute `clang-release` for an
optimised build.

## Running a single test

```powershell
cd build/clang-debug
ctest -R tag_registry --output-on-failure
```
