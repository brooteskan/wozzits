# Building wozzits-window-engine

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| CMake | 3.14 | |
| Visual Studio 2022 | 17.x | Desktop development with C++ workload, for Windows SDK/link libraries |
| LLVM / clang-cl | 17.x | Install the VS LLVM tools component or LLVM for Windows |
| Ninja | any | Bundled with VS 2022; otherwise install via `winget install Ninja-build.Ninja` |
| Git | any | |

An internet connection is required for first-time setup: `git submodule update` fetches `external/pmp`, and CMake fetches GoogleTest on the first configure.

## Getting the source

The repository is self-contained. The renderer (`rhi/`), scene layer
(`src/scene_render`), and math library (`src/algo_math`) are all vendored in-tree,
so no sibling repositories are required. The only build-time submodule is
`external/pmp` (mesh processing).

```powershell
git clone https://github.com/brooteskan/wozzits.git
cd wozzits
git submodule update --init external/pmp
```

Or run the setup script, which initialises the submodule and installs the
pre-push hook in one step (see [Setup script](#setup-script) below).

## Configure, build, and test

All commands must be run from a **VS 2022 x64 Developer Command Prompt** (or Developer PowerShell)
with `clang-cl`, `lld-link`, and Ninja on the path. MSVC compiler targets are not supported.

```powershell
cd wozzits

# Configure
cmake --preset clang-debug

# Build
cmake --build --preset clang-debug

# Run tests
ctest --preset clang-debug
```

Build output lands in `build/clang-debug/`. Substitute `clang-release` for an optimised build.

### Opening in Visual Studio

VS 2022 supports CMake presets natively. Open the `wozzits` folder in VS
(`File > Open > Folder`) and select the **Windows x64 Clang Debug** configuration from the
toolbar. VS will configure and build automatically.

### Running a specific test

```powershell
cd build/clang-debug
ctest -R asset_gaussian_splat --output-on-failure
```

### Tests that require GPU / windowing

Many render tests open a real window and create a DX12 device, so they need a GPU
and a display and will not run in a headless pass. Every on-device render suite
carries the ctest label `render` — the `rs_add_test_group(render ...)` groups and
the two standalone `wozzits_app_v1` render tests — so you can run exactly them:

```powershell
cd build/clang-debug
ctest -L render --output-on-failure
```

**Run `ctest -L render` after any change to the renderer, the rhi contract,
shaders, or binding layouts, and before committing such a change.** These suites
sit outside a quick run, and skipping them is exactly how render regressions
shipped green (#317 D1-C20 cross-wired a shipping shader's registers; D1-C21 left
a suite red for two days) — see #320. The `window`, `gpu`, and `game_app` groups
similarly need a display.

### Pre-push CI (local test gate)

A `pre-push` git hook builds and runs the tests **on your machine** before a push
reaches GitHub — the same idea as CI, but no cloud compute (per the no-cloud-CI
policy). A push is blocked if the build breaks or any test fails. The hook is
version-controlled in [`.githooks/`](.githooks/pre-push); enable it once per clone
by pointing git at it (`scripts/setup-workspace.ps1` does this for you):

```powershell
git config core.hooksPath .githooks
```

Because it builds, push from a shell with the toolchain on PATH (the VS 2022 x64
Developer Command Prompt). Pushing from Visual Studio's built-in git tooling also
works once the tree is already built (the incremental build is then a no-op): the
hook points `TMP`/`TEMP` at a writable, gitignored scratch dir under the build
folder, so the test suite runs even though VS launches hooks with those variables
unset (otherwise the tests' scratch dirs land in `C:\WINDOWS` and every one fails
with "Access is denied"). Knobs:

- **Skip one push:** `git push --no-verify`
- **Scope the tests** (e.g. the on-device render suites only, the fast high-value
  subset): `$env:WZ_PREPUSH_CTEST_LABEL="render"; git push`
- **Different preset:** `$env:WZ_PREPUSH_PRESET="clang-release"; git push`

By default it runs the full `ctest` suite.

## V8 scripting (optional)

V8 support is off by default. To enable it you need:

1. A built V8 (see `wozzits-v8` repo for instructions).
2. A `wozzits-v8` sibling repo cloned at the same level as this one.
3. A `CMakeUserPresets.json` with the V8 paths filled in.

Copy the template and update the paths:

```powershell
copy CMakeUserPresets.json.example CMakeUserPresets.json
# Edit CMakeUserPresets.json — replace all C:/path/to/v8/... entries
```

Then configure with the V8 preset:

```powershell
cmake --preset local-v8-debug
cmake --build --preset local-v8-debug
```

## Setup script

`scripts/setup-workspace.ps1` performs the one-time setup for a fresh clone —
it initialises the `external/pmp` submodule and installs the pre-push hook:

```powershell
# From the repo root:
.\scripts\setup-workspace.ps1
```

Pass `-SkipSubmodules` or `-SkipHook` to skip a step.
