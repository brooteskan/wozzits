# wozzits-window-engine

An experimental C++20 / Direct3D 12 rendering engine for Windows. This is personal research and development work — the API and architecture change frequently and there are no stability guarantees.

## What's here

- D3D12 rendering backend with a GPU resource and shader pipeline
- Asset system with a compiler registry, dependency graph, and typed asset modules (meshes, Gaussian splat clouds, scalar fields, textures, CSV/JSON/TOML data)
- Scene integration via the in-tree scene layer ([`src/scene_render`](src/scene_render))
- Project-authored behavior plugins documented in [docs/behavior_plugins](docs/behavior_plugins/README.md)
- Optional V8 scripting support

## Repository layout

This is the `wozzits` monorepo; the window engine (CMake target `window_engine`) is the primary library, with the renderer and editor alongside it.

- `window_engine/` — engine headers and public interface, grouped by subsystem
- `src/` — engine implementation and vendored libraries (`scene_render`, `algo_math`, …)
- `rhi/` — standalone header-only D3D12 render hardware interface, consumed by the engine
- `editor/` — the scene editor
- `examples/` — standalone samples, including the behavior plugin template
- `docs/` — engine documentation (asset system, behavior plugins, …)
- `external/` — third-party dependencies (`pmp` submodule, gltf, ply, rtaudio)
- `tests/` — test suites and support

## Status

Experimental. Not production-ready. Breaking changes happen without notice.

## Building

This repo is self-contained — the renderer (`rhi/`), scene layer (`src/scene_render`), and math library (`src/algo_math`) are all vendored in-tree, so no sibling clones are needed. The only submodule is `external/pmp`, and CMake fetches GoogleTest on the first configure. See [BUILDING.md](BUILDING.md) for prerequisites and full details.

Quick start (run from a VS 2022 x64 Developer Command Prompt with clang-cl, lld-link, and Ninja on PATH):

```powershell
# Fetch the pmp submodule (vendored mesh-processing library)
git submodule update --init external/pmp

# Configure, build, and test
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --preset clang-debug
```

Use the `clang-release` preset for an optimised build.


## License

This project is licensed under the MIT License. See [LICENSE.txt](LICENSE.txt).
