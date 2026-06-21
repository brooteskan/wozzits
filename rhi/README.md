# wozzits-rhi

A new rendering layer for the Wozzits engine, built foundation-first.

## Why this repo exists

The existing Wozzits renderer identifies render programs with a compile-time
`BuiltinRenderProgram` enum that is switched on in many places across two
repositories (submit, pipeline factory, shader-pair mapping, the renderable
compiler, and a hand-maintained pipeline-realize list in the scene editor).
Any site that fails to handle a new enum member fails **silently** — the
symptom that made a new mesh-mask render path draw nothing at all.

`wozzits-rhi` is the clean-slate answer. Its organizing principle is:

> **No open identity enums.** Anything that is extended over time and must be
> agreed upon by many call sites is a *runtime registry of named entries*, not
> an enum. A missing entry is a checkable null, never a silent fallthrough.

This is the `TagRegistry` pattern (studied from O3DE's Atom RHI), rewritten as
plain C++ with no framework dependency.

> Note: "no enums" is precise, not blanket. *Closed, bounded* value sets
> (blend mode, cull mode, a resource's domain) stay enums and rely on
> exhaustiveness checks. Only *open, extended* identity sets become registries.

## Relationship to the existing engine

- The content-addressed **asset DAG** and **scene** systems in
  `wozzits-window-engine` stay as the authority for identity and content.
- This renderer is intended to **consume that asset library read-only** across
  the repo boundary; the cut line sits at the render-IR / resolver level.
- Integration is live: **topology A — `wozzits-window-engine` depends on this
  repo** (added as a header-only `INTERFACE` library, linked `PUBLIC`). The
  engine renders through it via `engine/rendering/rhi_scene_renderer`, with the
  concrete DX12 backend and bridges living engine-side (see "Integration
  status (window-engine)" below).

## Layout

```
include/wozzits/rhi/
  tag_registry.h            runtime name -> handle registry (the no-enums core)
  render_program.h          declarative program contract: closed pipeline-state
                            enums + descriptor semantics as Tags (the boundary)
  program_registry.h        generic "named programs as data" registry (shared
                            by render + compute)
  render_program_registry.h RenderProgramRegistry (alias of the generic)
  compute_program.h         compute pipelines as registered data (compute peer)
  handle.h                  generational handle + slot map (stale-handle safe)
  gpu_resource.h            GPU resource contract: identity, residency,
                            cpu_access, usage + the GpuBackend interface
  gpu_resource_registry.h   device-scoped owner of all GPU resources
  frame_graph.h             per-frame pass DAG: cull + order + barriers + alias
  draw_item.h               flat, sortable render-IR draw item (the seam)
tests/                      zero-dependency unit tests
```

## FrameGraph

The per-frame render graph — a **different DAG** from window-engine's asset
DAG, sharing nothing with it. The asset DAG is content-addressed and persistent
(*"what is this, is it current?"*); the frame graph is ephemeral, rebuilt every
frame and discarded (*"what order, what barriers, what shared memory?"*). It is
built only from rhi types, so this repo stays free of any engine dependency.

`compile()` does, as pure logic the backend then executes, the work the old
`dx12_submit` does by hand:

- **dead-pass culling** — ref-count reachability from marked outputs; passes
  whose results nobody consumes are dropped (transitively);
- **topological ordering** — producers before consumers, independent of the
  order passes were declared;
- **barrier derivation** — transitions emitted from declared per-pass resource
  state, and *only* on an actual state change (no redundant barriers);
- **transient aliasing** — lifetime intervals per transient + greedy grouping
  so non-overlapping transients can share backing memory.

`execute(plan, registry, recorder, frame_timeline)` runs the plan: it realizes
transients against the `GpuResourceRegistry` (one backing per alias group,
shared by its transients), issues the derived barriers through a backend-
agnostic `CommandRecorder`, invokes each surviving pass's callback in execution
order (resolving graph resources to GPU handles via `PassContext`), then records
the frame timeline on each transient and releases it — so the registry reclaims
it only once the GPU has passed that value. Pure planning and execution are
separate calls; both are unit-tested against fakes.

## GpuResourceRegistry

The single device-scoped owner that replaces the old renderer's scattered,
inconsistent caches (renderable cache, resident-field table, pipeline caches,
and translation-unit-static leaked buffers). One table owns the four things
those did inconsistently or not at all:

- **identity & dedup** — find-or-create by `(asset_id, variant)`; the variant
  is a registered Tag (the generalized `layout` discriminator), not an enum;
- **lifetime** — precise deferred release keyed on the GPU timeline value that
  last used a resource, not a fixed frame-latency guess;
- **mutability** — in-place `update()` for CPU-writable resources (the #145
  per-frame refresh), with a checkable failure for GPU-only ones;
- **device loss** — `on_device_lost()` destroys every backend resource and
  invalidates every handle in one sweep (via slot-generation bump).

It delegates all real GPU work to a `GpuBackend` interface, so it stays
device-agnostic and is unit-tested against a fake backend.

## The engine boundary (asset-library bridge)

`render_program.h` is the read-only contract for consuming the existing
engine's render programs. `wozzits-rhi` deliberately does **not** include any
`wozzits-window-engine` header — it stays standalone-buildable. The
engine -> rhi adapter lives at the seam, **engine-side** (topology A is
decided: the engine depends on this repo), not in this repo, and maps
`wz::engine::assets::RenderProgramData` into a `RenderProgramDesc`:

- the `BuiltinRenderProgram` enum is dropped — program identity becomes a
  registered name;
- closed pipeline-state enums (blend/depth/raster/topology/layout) map 1:1;
- the engine's `DescriptorSemantic` **enum** becomes a registered **Tag** (the
  open identity set the engine grows over time);
- shader `AssetKey`s become resolvable shader refs.

`tests/render_program_bridge_tests.cpp` exercises this against the real
`mesh_mask_style` program shape with zero engine dependency.

Compute pipelines get the same treatment via `compute_program.h`
(`ComputeProgramDesc` + `ComputeProgramRegistry`), reusing the binding structs.
Both registries are the one generic `ProgramRegistry<Desc, Max>` — a new
program kind is a new descriptor + alias, never a new enum or switch.

## Build

See [BUILDING.md](BUILDING.md). TL;DR from a VS 2022 x64 dev shell with
clang-cl + lld-link + Ninja on PATH:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --preset clang-debug
```

## Integration status (window-engine)

The build-topology decision is **made: topology A — `wozzits-window-engine`
depends on this repo** (header-only `INTERFACE` library, linked `PUBLIC`). The
concrete backend and bridges therefore live **engine-side** — rhi stays the
pure interface, since an rhi -> engine dependency would be a cycle. Already
landed in `wozzits-window-engine/engine/rendering/`:

- `EngineGpuBackend` (`rhi_gpu_backend.*`) — implements `wz::rhi::GpuBackend`
  over the engine's existing DX12 device / upload / release plumbing. Buffer
  resources are done; **texture / render-target creation and CPU writes are
  incremental** (TODOs in the `.cpp`).
- `rhi_dx12_command_recorder.*` — the DX12 `CommandRecorder` for the pixel-path
  slice; `rhi_dx12_pipeline.*` realizes rhi pipelines.
- `rhi_render_program_bridge.*` / `rhi_shader_bridge.*` — the engine -> rhi
  adapters that map `RenderProgramData` (and the `DescriptorSemantic` enum ->
  registered Tag) into the `render_program.h` contract.
- `rhi_scene_renderer.*` — drives a `FrameGraph` over the backend; this is the
  path `wozzits_app_v1` renders through today.

## Roadmap (next)

- Finish the backend's resource coverage (textures, render targets, the
  CPU-write path) and the recorder's barrier / transient handling.
- Bindless descriptor-index allocation — a free-list over a global descriptor
  heap, keyed off resource identity (deferred from the registry v0).
- Transient pooling across frames — reuse backings between frames instead of
  acquire/release each frame.
- Migrate asset **resolve** onto `GpuResourceRegistry` — engine asset
  materialization still writes into the legacy GPU-resident tables; the registry
  is positioned to replace them.
