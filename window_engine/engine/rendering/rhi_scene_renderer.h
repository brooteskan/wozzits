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
        bool render_scene(
            std::span<const wz::engine::assets::SceneNodeAsset> nodes,
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos);

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
            // Lattice base_resolution recovered from the mesh level tags; sizes
            // the per-level geomorph band in the clipmap VS (issue #207).
            uint32_t clipmap_base_resolution = 8;

            // Gaussian-splat-cloud renderables (#208) bind the resident decoded
            // splat StructuredBuffer (at the SplatCloud semantic in object_srg,
            // asset-owned, never released) and record a non-indexed
            // DrawInstanced of 4 * splat_count vertices. is_splat_cloud gates the
            // per-frame SplatCloudDrawConstants packing in render_scene.
            bool is_splat_cloud = false;
            wz::engine::assets::GaussianSplatCloudRenderSettings splat_settings{};
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
}
