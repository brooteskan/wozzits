# wozzits-window-engine

An experimental C++20 / Direct3D 12 rendering engine for Windows. This is personal research and development work — the API and architecture change frequently and there are no stability guarantees.

## What's here

- D3D12 rendering backend with a GPU resource and shader pipeline
- Asset system with a compiler registry, dependency graph, and typed asset modules (meshes, Gaussian splat clouds, scalar fields, textures, CSV/JSON/TOML data)
- Scene integration via [wozzits-scene-render](https://github.com/woguls/wozzits-scene-render)
- Optional V8 scripting support

## Status

Experimental. Not production-ready. Breaking changes happen without notice.

## Building

See [BUILDING.md](BUILDING.md) for prerequisites, workspace layout, and step-by-step instructions.

Quick start (VS 2022 x64 Developer Command Prompt required):

```powershell
# Clone required sibling repo
.\scripts\setup-workspace.ps1

# Configure, build, test
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```


## License

This project is licensed under the MIT License. See [LICENSE.txt](LICENSE.txt).