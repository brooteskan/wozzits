#pragma once

// wz/scene/scene_compiler.h

#include <scene/compile/compiled_scene.h>
#include <scene/compile/scene_node_class.h>
#include <scene/scene_graph.h>
#include <graph/static_polytree.h>
#include <cfloat>

namespace wz::scene {

    // ─── update_view() ───────────────────────────────────────────────────────────
    //
    // Rebuilds only the view-dependent portion of CompiledScene.
    // Rewrites view_buffer — splat_depths updated in-place,
    // transparent and particle primitives rebuilt with new depth values.
    // stable_buffer and metadata_buffer are never touched.
    //
    // Call every frame the camera moves. O(transparent + particles + splats).

    void update_view(CompiledSceneStorage& storage, const ViewData& view);

    // ─── compile() ───────────────────────────────────────────────────────────────
    //
    // Full compile. Builds stable_buffer (opaque, splats, lights),
    // metadata_buffer (node↔output mapping), and view_buffer (transparent,
    // particles, splat_depths). Calls update_view() for initial depth values.
    // Returns a view into storage.

    CompiledSceneView compile(
        CompiledSceneStorage&                 storage,
        const SceneGraph&                     g,
        std::span<const RenderableDescriptor> descs,
        std::span<const LightRecord>          lights,
        const ViewData&                       view);

    // ─── update_compiled_transforms() ────────────────────────────────────────────
    //
    // Patches compiled records in-place after world matrices have changed.
    // Assumes the SceneGraph world matrices are already current — the caller is
    // responsible for running propagate_all(), update_static(), or equivalent
    // before calling this function.
    //
    // Uses metadata.node_to_output to jump directly from each dirty node to its
    // compiled output slot. Cost is O(dirty_nodes.size()) for partial updates,
    // not O(scene_size). This is the intended use of the node_to_output map.
    //
    // dirty_nodes — list of scene-graph nodes whose world matrices changed.
    //   Empty span (default): all compiled outputs are treated as dirty and patched.
    //     Walks the compact source-node spans; cost is O(compiled_output_count).
    //   Non-empty span: only the listed nodes are patched via node_to_output lookup.
    //     Cost is O(dirty_nodes.size()), independent of total scene size.
    //     Nodes not in the list keep their existing compiled state unchanged.
    //
    // view_changed — whether the camera moved this frame.
    //   true  (default): calls update_view() after patching, refreshing depths for
    //     ALL compiled records regardless of which nodes were dirty.
    //   false: computes view-dependent depths inline for each patched node only.
    //     Nodes not in dirty_nodes keep their existing depths. Correct when the
    //     camera is stationary and only transform-dirty nodes need depth refreshed.
    //     Callers must also update storage.scene.view if they rely on the stored
    //     ViewData — update_view() is the only other code that writes it.
    //
    // Does not touch metadata_buffer. stable_buffer and view_buffer are mutated
    // in-place; no reallocation occurs.

    void update_compiled_transforms(
        CompiledSceneStorage&                 storage,
        const SceneGraph&                            g,
        std::span<const RenderableDescriptor>        descs,
        const ViewData&                              view,
        std::span<const wz::core::graph::NodeHandle> dirty_nodes = {},
        bool                                         view_changed = true);

    // ─── update_compiled_descriptors() ──────────────────────────────────────────
    //
    // Patches compiled records in-place when mesh/material handles or splat
    // appearance data have changed but scene structure is unchanged.
    //
    // Updates all records in every reverse source-node span:
    //   opaque      — mesh and material handles
    //   transparent — mesh and material handles
    //   splats      — scale, rotation, color, opacity from splat_data
    //   particles   — mesh and material handles
    //   lights      — no-op (lights have no scene-graph association)
    //
    // Does not touch world matrices, bounds, or view-dependent depths.
    // Does not touch metadata_buffer or stable_buffer layout — no reallocation.
    // Callers that also need depths refreshed must call update_view() separately.

    void update_compiled_descriptors(
        CompiledSceneStorage&                 storage,
        std::span<const RenderableDescriptor> descs);

    // ─── refresh_compiled_scene() ────────────────────────────────────────────────
    //
    // Routes to the cheapest update path based on which axes are dirty.
    //
    // Structural changes (Topology, Geometry, Visibility, Metadata,
    // Lighting, Animation, Constraint, Procedural):
    //     compile() — full rebuild of all three buffers.
    //
    // Transform or Bounds (no structural changes):
    //     update_compiled_transforms() — in-place patch of world/bounds, then
    //     update_view(). stable_buffer and metadata_buffer are not reallocated.
    //     Assumes world matrices are already current in the SceneGraph.
    //     dirty_nodes narrows the transform patch when provided; empty keeps the
    //     existing "all compiled outputs dirty" behavior.
    //     If Material is also dirty, update_compiled_descriptors() is called
    //     after the transform patch (before update_view()).
    //
    // Material only (handles/appearance changed, structure and transforms stable):
    //     update_compiled_descriptors() — in-place patch of mesh/material/splat
    //     appearance data, then update_view().
    //
    // View only (camera moved, scene unchanged):
    //     update_view() — only view_buffer updated.
    //
    // None:
    //     no-op — returns current storage.scene unchanged.

    CompiledSceneView refresh_compiled_scene(
        CompiledSceneStorage&                 storage,
        const SceneGraph&                            g,
        std::span<const RenderableDescriptor>        descs,
        std::span<const LightRecord>                 lights,
        const ViewData&                              view,
        SceneDirtyBits                               dirty,
        std::span<const wz::core::graph::NodeHandle> dirty_nodes = {});

} // namespace wz::scene
