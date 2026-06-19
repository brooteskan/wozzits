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
#include <engine/rendering/rhi_gpu_backend.h>

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
        RhiSceneRenderer(wz::gpu::Device& device, wz::Logger& logger);

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
        bool render_scene(
            std::span<const wz::engine::assets::SceneNodeAsset> nodes,
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::math::Mat4& view_projection);

        // Invalidate every realized cache after a wholesale asset-graph swap.
        // The caches (realized programs/renderables/registered shaders) are
        // keyed by the OUTGOING graph's AssetKeys, so on a swap they go stale —
        // the renderer would keep drawing the previous graph's GPU resources.
        // This deferred-releases the outgoing graph's pull buffers (the only
        // GpuResourceRegistry resources the renderer owns) and clears the caches
        // so the next render re-realizes against the new keys. A graph swap is a
        // rare, heavy editor action (replace-the-draft), so the flush this does
        // to make the release safe is acceptable here; it is NOT a per-frame path.
        void on_graph_changed();

        // GPU resources currently resident in the resource registry. Flat across
        // a graph swap once the new graph is realized (the outgoing graph's were
        // released): diagnostics + the rebind regression test read this.
        [[nodiscard]] std::size_t resident_gpu_resource_count() const
        {
            return ctx_.resources.resident_count();
        }

        // Render programs / shader modules currently registered in the rhi
        // context. Both drop to 0 on a graph swap (on_graph_changed clears them)
        // so the fixed-capacity registries don't grow across binds — the rebind
        // test asserts this.
        [[nodiscard]] std::size_t registered_program_count() const
        {
            return ctx_.programs.size();
        }
        [[nodiscard]] std::size_t registered_shader_count() const
        {
            return ctx_.shaders.size();
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
        };

        const RealizedProgram* realize_program(
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& program_key);
        RealizedRenderable* ensure_renderable(
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& renderable_key);
        bool register_shader_from_source(
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& shader_key,
            wz::rhi::ShaderStage stage,
            const char* target);

        wz::gpu::Device&            device_;
        wz::Logger&                 logger_;
        EngineGpuBackend            backend_;
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
    };
}
