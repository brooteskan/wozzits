# SceneRuntimeBundle Contract

Stage D of issue #160 introduces a transactional rebuild boundary for editor
and runtime scene preview. The contract below is the target shape for the
follow-up implementation work; it is intentionally more precise than the
current `SceneAssetRuntimeBuild`, which only covers authored scene data through
`SceneInstance`.

## Problem

The editor currently performs rebuild phases against shared live state:

1. Materialize authored scene components into asset recipes.
2. Commit and resolve the asset graph.
3. Realize GPU resources through render caches and resolvers.
4. Instantiate the scene and rebuild render storage.

If any later step fails, the editor can report "keeping last valid scene" even
though earlier phases already mutated the asset library, resolver registrations,
or GPU caches that the last valid scene depends on. A failed rebuild must not
disturb the currently rendered scene.

## Bundle Ownership

`SceneRuntimeBundle` is the committed unit of scene runtime state. A complete
bundle owns every object whose lifetime must match the scene instance:

```cpp
struct SceneRuntimeBundle {
    SceneAssetData authored_scene;
    EngineAssetLibrary assets;
    SceneInstance instance;

    RenderResourceResolver render_resolver;
    RenderableGpuCache renderable_cache;
    RenderablePipelineCache pipeline_cache;
    RenderProgramPipelineCache render_program_cache;

    wz::scene::CompiledSceneStorage compiled_scene;
    wz::render::RenderIRStorage render_ir;
    wz::render::RenderFrameStorage render_frame;
    std::vector<SkyDrawCommand> sky_commands;

    uint64_t scene_hash = 0;
    std::string scene_hash_text;
    std::string status;
};
```

The exact member list may grow as existing editor-owned runtime state is folded
in, but the ownership rule is fixed: if a scene draw can refer to it, the
bundle owns it or holds a stable reference whose lifetime is outside scene
rebuilds, such as the `gpu::Device` and logger.

## Candidate/Commit Flow

Bundle rebuild is a candidate operation:

```cpp
SceneBundleBuildResult build_scene_runtime_bundle(
    gpu::Device& device,
    const SceneAssetData& authored,
    const SceneBundleBuildOptions& options);
```

`build_scene_runtime_bundle` copies `authored` into candidate state before any
materialization. All generated asset keys, resolved GPU handles, resolver
registrations, and render storage belong to the candidate bundle.

Commit is a move/swap of a complete candidate:

```cpp
if (candidate.ok()) {
    live_bundle = std::move(candidate.bundle);
}
```

Failure returns diagnostics and leaves `live_bundle` untouched. This is the
central invariant:

```text
A failed rebuild never mutates live authored_scene, live resolver
registrations, live GPU caches, live scene instance, or live render storage.
```

## Build Phases

The candidate builder performs the current editor rebuild work in this order:

1. Copy authored scene and reset materialized output keys on the copy.
2. Materialize authored components into the candidate asset library.
3. Commit and resolve the candidate asset graph.
4. Realize candidate renderables and render programs using candidate caches and
   the candidate render resolver.
5. Instantiate the candidate scene with candidate resource resolvers.
6. Build candidate compiled scene, render IR, render frame, sky commands, and
   any runtime side tables required by the renderer.

The live bundle is only replaced after every phase succeeds.

## Live Edit Overlay

Immediate editor interactions, such as mask-rule slider drags, are not bundle
rebuilds. They use a small live edit overlay attached to the committed bundle:

```cpp
struct SceneRuntimeLiveEdits {
    // keyed by authored node id or stable renderable/style identity
    std::vector<MeshStyleLiveEdit> mesh_style_edits;
};
```

The overlay may mutate resolver-owned style snapshots and mark small GPU
parameter buffers dirty. It must not:

- register asset nodes,
- call `EngineAssetLibrary::commit()` or `resolve_all()`,
- upload mesh buffers,
- compile mesh derived fields,
- replace the bundle's asset library, resolver, caches, or scene instance.

On slider release, the authored scene is updated and a normal candidate rebuild
is requested. If the rebuild succeeds, the committed bundle already contains
the authored values and the overlay entry is removed. If the rebuild fails, the
old bundle remains live and the overlay may either stay active for continued
editing or be reverted by the editor; it must not be partially committed.

## Live Edits Across Bundle Commit

An in-flight drag can overlap a candidate rebuild. The commit rule is:

- Bundle commit replaces the base scene runtime atomically.
- Live edit overlays are re-applied to the new bundle only when their target
  identity still resolves to the same authored node/style intent.
- If the target no longer exists or the render program/source field changed so
  the edit is no longer compatible, the overlay is dropped and the editor should
  end the drag against the committed authored value.

This makes slider drags responsive without letting a transient edit hold stale
GPU handles from an old bundle.

## D2 Instrumentation Contract

D2 must prove the live style path is immediate. During an active mask-rule drag,
instrumentation should report:

- zero mesh uploads,
- zero mesh derived-field compiles,
- zero asset graph commits/resolves caused by the drag tick,
- one or more small style/rule-buffer updates.

The counters should be queryable from tests or a debug diagnostic surface, and
the editor should use the live path for drag ticks and the candidate rebuild
path for release commits.

## Error Semantics

Build failures return a structured error with phase, message, and optional
asset key/context. Logging can still happen, but the result object is the API
contract so the editor can present one clear status while continuing to render
the previous bundle.

Device-lost remains terminal in the current design. Future recovery can reuse
this contract by building a fresh bundle against a fresh `gpu::Device`.
