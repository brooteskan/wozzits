# Building wozzits-window-engine

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| CMake | 3.14 | |
| Visual Studio 2022 | 17.x | Desktop development with C++ workload |
| Ninja | any | Bundled with VS 2022; otherwise install via `winget install Ninja-build.Ninja` |
| Git | any | |

An internet connection is required on first configure — CMake fetches GoogleTest automatically.

## Workspace layout

`wozzits-window-engine` depends on `wozzits-scene-render` (which in turn provides `algo_math`).
Both must sit as siblings under the same parent directory:

```
wozzits/
  wozzits-window-engine/   ← this repo
  wozzits-scene-render/    ← required sibling
```

The quickest way to get there is to run the setup script (see [Setup script](#setup-script) below),
or clone manually:

```powershell
git clone https://github.com/woguls/wozzits-scene-render.git
git clone https://github.com/woguls/wozzits-window-engine.git
```

## Configure, build, and test

All commands must be run from a **VS 2022 x64 Developer Command Prompt** (or Developer PowerShell),
so that MSVC (`cl.exe`) and Ninja are on the path.

```powershell
cd wozzits-window-engine

# Configure
cmake --preset windows-debug

# Build
cmake --build --preset windows-debug

# Run tests
ctest --preset windows-debug
```

Build output lands in `build/windows-debug/`. Substitute `windows-release` for an optimised build.

### Opening in Visual Studio

VS 2022 supports CMake presets natively. Open the `wozzits-window-engine` folder in VS
(`File > Open > Folder`) and select the **Windows x64 Debug** configuration from the
toolbar. VS will configure and build automatically.

### Running a specific test

```powershell
cd build/windows-debug
ctest -R asset_gaussian_splat --output-on-failure
```

### Tests that require GPU / windowing

The `window`, `gpu`, and `game_app` test groups open a real window and require a display.
They are excluded from headless CI runs. Run them locally after a full build.

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

`scripts/setup-workspace.ps1` automates the workspace setup step:

```powershell
# From the wozzits-window-engine directory:
.\scripts\setup-workspace.ps1
```

It clones any missing sibling repos into the parent directory and reports what it found
or created. Run it with `-Help` to see all options.
