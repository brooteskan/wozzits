#pragma once

// engine/rendering/rhi_scene_renderer.h
//
// RhiSceneRenderer — the reusable RHI scene render path (the "RPI tier"). It owns
// the RhiContext, pipeline cache, command recorder, and the realized-program /
// realized-renderable caches, and renders a scene's renderables (resolved from
// the asset system) into RHI draw packets via wozzits-rhi (DrawPacket /
// record_packet). Apps own the window + device and drive it via render_scene().
//
// The realize path is lifted from the scene editor's working RHI render path so
// the app and editor share one implementation (no legacy dx12 submit, no
// duplication).

#include <engine/rendering/rhi_context.h>
#include <engine/rendering/rhi_dx12_command_recorder.h>
#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_gpu_backend.h>

// For ClipmapLandscapeRenderSettings carried on a realized clipmap renderable.
#include <engine/assets/renderable/renderable.h>

#include <gpu/gpu.h>
#include <logging/logger.h>
#include <math/math_types.h>

#include <asset/types.h>

#include <wozzits/rhi/draw_list_tag.h>
#include <wozzits/rhi/draw_packet.h>
#include <wozzits/rhi/frame_graph.h>
#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/shader_resource_group.h>

#include <cstddef>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wz::engine::assets
{
    class EngineAssetLibrary;
    struct SceneNodeAsset;
    struct SceneRenderableConstantOverride;
}

namespace wz::engine::rendering
{
    class RhiSceneRenderer
    {
    public:
        RhiSceneRenderer(EngineGpuContext& gpu, wz::Logger& logger);

        RhiSceneRenderer(const RhiSceneRenderer&)            = delete;
        RhiSceneRenderer& operator=(const RhiSceneRenderer&) = delete;
        RhiSceneRenderer(RhiSceneRenderer&&)                 = delete;
        RhiSceneRenderer& operator=(RhiSceneRenderer&&)      = delete;

        void simulation_tick();

        // Render the scene's renderables. The caller owns the device frame
        // boundaries (begin/clear/end/present); this binds the current backbuffer
        // targets and records draw packets for every visible node that carries a
        // resolved renderable_asset key. Returns false if the recorder rejected a
        // draw. Renderables/programs are resolved from `assets` and cached.
        //
        // camera_world_pos is the camera's world-space position. Standard pull /
        // gpu_sparse renderables ignore it; a clipmap-landscape renderable uses
        // its XZ to center + snap the lattice and pack the per-draw clipmap view
        // transform. Pass {0,0,0} when there is no meaningful camera.
        //
        // world_transforms, when non-empty, is the per-node world matrix the
        // renderer draws with (index-aligned with `nodes`). The caller supplies
        // it so the drawn pose can come from the live simulation polytree (the
        // single source of truth) rather than the authored composition. When it
        // is empty (or its size doesn't match `nodes`), the renderer falls back
        // to composing the transforms itself via
        // compute_scene_node_world_transforms(nodes) — the pre-#221 behavior the
        // renderer-unit tests rely on.
        bool render_scene(
            std::span<const wz::engine::assets::SceneNodeAsset> nodes,
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos,
            std::span<const wz::math::Mat4> world_transforms = {});

        // Invalidate every realized cache after a wholesale asset-graph swap.
        // The caches (realized programs/renderables/registered shaders) are
        // keyed by the OUTGOING graph's AssetKeys, so on a swap they go stale —
        // the renderer would keep drawing the previous graph's GPU resources.
        // This deferred-releases the outgoing graph's pull buffers and clears
        // the caches
        // so the next render re-realizes against the new keys. A graph swap is a
        // rare, heavy editor action (replace-the-draft), so the flush this does
        // to make the release safe is acceptable here; it is NOT a per-frame path.
        void on_graph_changed();

        // GPU resources currently resident in the resource registry. Flat across
        // a graph swap once the new graph is realized (the outgoing graph's were
        // released): diagnostics + the rebind regression test read this.
        [[nodiscard]] std::size_t resident_gpu_resource_count() const
        {
            return gpu_.resources.resident_count();
        }

        // Render programs / shader modules currently registered in the rhi
        // context. Both drop to 0 on a graph swap (on_graph_changed clears them)
        // so the fixed-capacity registries don't grow across binds — the rebind
        // test asserts this.
        [[nodiscard]] std::size_t registered_program_count() const
        {
            return gpu_.programs.size();
        }
        [[nodiscard]] std::size_t registered_shader_count() const
        {
            return gpu_.shaders.size();
        }

        // Programs the renderer bridged at render time (the find-then-fallback
        // fallback path) instead of binding one the asset compiler produced. The
        // rebind test asserts this is 0 after the first render of the migrated
        // custom program — proving it came from the compiler, the way resident ==
        // 2 proved the resident-buffer path in #190. Cumulative since construction.
        [[nodiscard]] std::size_t render_time_program_bridge_count() const
        {
            return render_time_program_bridges_;
        }

        // SRV descriptor tables cached by the command recorder. Resets to 0 on a
        // graph swap (on_graph_changed releases them) so descriptor-heap ranges
        // don't leak across binds.
        [[nodiscard]] std::size_t cached_descriptor_table_count() const
        {
            return recorder_.cached_descriptor_table_count();
        }

    private:
        struct RealizedProgram
        {
            wz::asset::AssetKey program_key{};
            wz::asset::AssetKey vertex_shader_key{};
            wz::asset::AssetKey pixel_shader_key{};
            wz::rhi::Tag        program{};
        };
        struct RealizedRenderable
        {
            wz::asset::AssetKey         renderable_key{};
            wz::rhi::Tag                program{};
            wz::rhi::GpuResourceHandle  positions{};
            wz::rhi::GpuResourceHandle  indices{};
            wz::rhi::ShaderResourceGroup object_srg{};
            wz::rhi::DrawPacket         packet{};
            // True only for the CPU-upload fallback path, where the renderer
            // acquired these pull buffers and must release them. Resident
            // (asset-published) buffers are owned by the asset library; the
            // renderer binds but never releases them.
            bool                        owns_buffers = false;

            // Clipmap-landscape renderables (recipe carries height_texture_key)
            // displace the lattice in the VS by sampling a resident height
            // texture, and pack a per-frame view transform into the draw's root
            // constants instead of the per-node MVP. is_clipmap gates that
            // packing in render_scene; the settings + heightmap dims feed
            // compute_clipmap_view. The height texture is asset-owned (bound into
            // object_srg at the scalar_field_texture semantic, never released).
            bool is_clipmap = false;
            wz::engine::assets::ClipmapLandscapeRenderSettings clipmap_settings{};
            uint32_t heightmap_width = 1;
            uint32_t heightmap_height = 1;
            // Lattice base_resolution + level_count recovered from the mesh level
            // tags. base_resolution sizes the per-level geomorph band (#207);
            // together they give the lattice's unitless grid extent
            // (clipmap_lattice_grid_extent), which divides the mesh's world width
            // to recover the finest cell world size c0 (the lattice mesh is now
            // WORLD-SIZED — its positions bake cell_size in — so c0 comes from the
            // mesh, not the node scale).
            uint32_t clipmap_base_resolution = 8;
            uint32_t clipmap_level_count = 1;
            // World-space width of the lattice mesh along X (max.x - min.x),
            // captured at realize time. c0 = clipmap_mesh_width_x /
            // clipmap_lattice_grid_extent({level_count, base_resolution}); see
            // compute_clipmap_placement.
            float clipmap_mesh_width_x = 0.0f;

            // Gaussian-splat-cloud renderables (#208) bind the resident decoded
            // splat StructuredBuffer (at the SplatCloud semantic in object_srg,
            // asset-owned, never released) and record a non-indexed
            // DrawInstanced of 4 * splat_count vertices. is_splat_cloud gates the
            // per-frame SplatCloudDrawConstants packing in render_scene.
            bool is_splat_cloud = false;
            wz::engine::assets::GaussianSplatCloudRenderSettings splat_settings{};

            // Baked mesh-render-style shading (issue #195 slice A). When
            // has_style is set, render_scene packs MeshStyleDrawConstants (MVP +
            // 12 floats of style) into this renderable's root constants instead of
            // the plain 16-float MVP, and the program declares the "mesh_style"
            // root constant (binding_layout preset 4). Only the CPU pull-mesh path
            // sets it; clipmap/splat/gpu_sparse take their own pack branches.
            bool has_style = false;
            wz::engine::assets::MeshRenderStyleShading style{};

            // Custom renderable recipes (issue #228). is_custom gates the
            // generic per-frame constants pack: custom_constants is the full
            // root-constant block as a byte template — authored tail values
            // baked in at realize time — and render_scene re-packs only the
            // head packer named by custom_constants_head (Mvp16 /
            // WorldViewProjCamera36) in place each frame. The recipe's extra
            // resource bindings were walked into object_srg at realize time;
            // they are asset-owned (bound, never released).
            bool is_custom = false;
            wz::engine::assets::RenderBindingConstantsHead
                custom_constants_head =
                    wz::engine::assets::RenderBindingConstantsHead::None;
            std::vector<uint8_t> custom_constants;
            // The recipe's declared tail-field table (name → baked offset /
            // width; #229). Per-instance scene-node constant overrides merge
            // against it by name at pack time — into the PACKET's copy of the
            // block, never this shared template, so two nodes drawing the
            // same recipe can carry different override values.
            std::vector<wz::engine::assets::RhiRenderableConstant>
                custom_fields;
        };

        const RealizedProgram* realize_program(
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& program_key);
        RealizedRenderable* ensure_renderable(
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& renderable_key);
        // Verify the shader's rhi ShaderModule (produced by the shader compiler
        // under shader_ref(key)) is registered. The renderer never compiles
        // shaders; this just gates the render-time program fallback (#193).
        bool ensure_shader_module(const wz::asset::AssetKey& shader_key);

        // Pack a camera-follow terrain draw-constant block (the 32-dword
        // ClipmapDrawConstants: view_projection + per-level snap / world→uv /
        // vertical / texel-extent) into `out`, from the node transform + the
        // realized clipmap settings/dims + the live camera. Shared by the
        // bespoke clipmap (0x708) branch and the generic custom-renderable
        // (0x70A) CameraSnappedTerrain head case (issue #233) so both stay
        // byte-identical.
        void pack_camera_snapped_terrain_constants(
            const RealizedRenderable& realized,
            const wz::math::Mat4& node_world,
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos,
            std::vector<uint8_t>& out);

        EngineGpuContext&           gpu_;
        wz::Logger&                 logger_;
        RhiContext                  ctx_;
        RhiDx12PipelineCache        cache_;
        RhiDx12CommandRecorder      recorder_;
        wz::rhi::DrawListTag        forward_{};

        std::unordered_map<wz::asset::AssetKey, bool, wz::asset::AssetKeyHash>
            registered_shaders_;
        std::unordered_map<
            wz::asset::AssetKey, RealizedProgram, wz::asset::AssetKeyHash>
            realized_programs_;
        std::unordered_map<
            wz::asset::AssetKey, RealizedRenderable, wz::asset::AssetKeyHash>
            realized_renderables_;
        // Renderables that failed to realize (unresolved/broken graph). Logged
        // once and then skipped each frame so the render loop never re-attempts
        // or re-logs per frame (which floods the editor log sink). Cleared on a
        // graph swap (on_graph_changed) so a fixed graph re-realizes.
        std::unordered_set<wz::asset::AssetKey, wz::asset::AssetKeyHash>
            failed_renderables_;

        // One-shot guard so the clipmap world-placement diagnostic (footprint vs
        // node scale, for aligning a terrain-stick collision) logs once, not per
        // frame.
        bool clipmap_placement_logged_ = false;

        // See render_time_program_bridge_count().
        std::size_t render_time_program_bridges_ = 0;
    };

    // Compose each scene node's world transform from its local TRS and its
    // parent chain (world = parent_world * local), so renderable children
    // inherit their parents' transforms. Returns one matrix per node,
    // index-aligned with `nodes`. Self-contained: it walks parent_id within
    // `nodes` only and does NOT consult the ECS polytree or the legacy
    // compile_scene path. A node with no parent, a dangling parent id, or a
    // parent cycle resolves to its own local TRS. RhiSceneRenderer::render_scene
    // consumes this so nesting a node moves its children.
    std::vector<wz::math::Mat4> compute_scene_node_world_transforms(
        std::span<const wz::engine::assets::SceneNodeAsset> nodes);

    // Effective (inherited) visibility per node: 1 iff the node AND every
    // ancestor is visible, so hiding a parent hides its whole subtree (e.g. a
    // scene-source host hiding its grafted children). Index-aligned with
    // `nodes`; same self-contained parent_id walk as the world transforms (a
    // dangling/cyclic parent falls back to the node's own visibility).
    std::vector<std::uint8_t> compute_scene_node_effective_visibility(
        std::span<const wz::engine::assets::SceneNodeAsset> nodes);

    // Merge a scene node's per-instance constant overrides (issue #229) into a
    // packed custom root-constant block. `fields` is the recipe's declared
    // tail-field table (every layout field with its baked dword offset/width);
    // each override matches a field by name and its value[0..dwords) replaces
    // the field's bytes in `block`. An override naming no declared field is
    // ignored (the node may momentarily disagree with a re-authored layout —
    // authoring feedback lives at compile/bridge time, not per frame), and
    // writes are clamped to the block. This runs against the DRAW PACKET's
    // copy of the block each frame, which is what makes an override edit a
    // pack-time change: no asset-graph recompile, no re-key.
    void merge_renderable_constant_overrides(
        std::span<uint8_t> block,
        std::span<const wz::engine::assets::RhiRenderableConstant> fields,
        std::span<const wz::engine::assets::SceneRenderableConstantOverride>
            overrides) noexcept;
}
