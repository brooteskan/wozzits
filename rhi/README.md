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
- The live renderer in `wozzits-window-engine` remains in production and is
  unaffected by this repo.

## Layout

```
include/wozzits/rhi/
  tag_registry.h            runtime name -> handle registry (the no-enums core)
  render_program.h          declarative program contract: closed pipeline-state
                            enums + descriptor semantics as Tags (the boundary)
  render_program_registry.h render programs as registered data, no enum
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
engine -> rhi adapter lives at the seam (engine-side / an integration target),
not in this repo, and maps `wz::engine::assets::RenderProgramData` into a
`RenderProgramDesc`:

- the `BuiltinRenderProgram` enum is dropped — program identity becomes a
  registered name;
- closed pipeline-state enums (blend/depth/raster/topology/layout) map 1:1;
- the engine's `DescriptorSemantic` **enum** becomes a registered **Tag** (the
  open identity set the engine grows over time);
- shader `AssetKey`s become resolvable shader refs.

`tests/render_program_bridge_tests.cpp` exercises this against the real
`mesh_mask_style` program shape with zero engine dependency.

## Build

See [BUILDING.md](BUILDING.md). TL;DR from a VS 2022 x64 dev shell with
clang-cl + lld-link + Ninja on PATH:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --preset clang-debug
```

## Roadmap (next)

- Frame-graph execution — wire `compile()`'s plan to a real `GpuBackend`:
  realize transients against `GpuResourceRegistry`, issue derived barriers, run
  pass callbacks. v0 is pure planning; this is the execution half.
- Bindless descriptor-index allocation — a free-list over a global descriptor
  heap, keyed off resource identity (deferred from the registry v0).
- Engine-side `from_engine()` bridge adapter — gated on the build-topology
  decision (engine depends on rhi, or a separate integration target).
- Asset-library bridge, engine-side half — the actual `from_engine()` adapter
  that reads realized `RenderProgramData`. Needs a build-topology decision
  (engine depends on rhi, or a separate integration target depends on both);
  the rhi-side contract it targets already exists (`render_program.h`).
