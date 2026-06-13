// file: src/engine/render_backends/dx12/dx12_submit.cpp

#include <d3d12.h>

#include <engine/render_backends/dx12/dx12_submit.h>
#include <gpu/mesh_field_visualization.h>
#include <gpu/dx12/dx12_internal.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace wz::engine::render_backend::dx12
{
    // Backend implementation works in the render layer's vocabulary throughout.
    using namespace wz::render;

    namespace
    {
        using wz::engine::assets::BuiltinRenderProgram;
        using wz::math::Mat4;
        using SubmitClock = std::chrono::steady_clock;

        double elapsed_us(
            SubmitClock::time_point a,
            SubmitClock::time_point b)
        {
            return std::chrono::duration<double, std::micro>(b - a).count();
        }

        wz::engine::assets::TerrainVisualRepresentationKind
        engine_terrain_representation_kind(
            wz::scene::TerrainVisualRepresentationKind kind) noexcept
        {
            switch (kind) {
            case wz::scene::TerrainVisualRepresentationKind::MeshChunks:
                return wz::engine::assets::TerrainVisualRepresentationKind
                    ::MeshChunks;
            case wz::scene::TerrainVisualRepresentationKind::GridTiles:
                return wz::engine::assets::TerrainVisualRepresentationKind
                    ::GridTiles;
            case wz::scene::TerrainVisualRepresentationKind::SurfelCloud:
                return wz::engine::assets::TerrainVisualRepresentationKind
                    ::SurfelCloud;
            }
            return wz::engine::assets::TerrainVisualRepresentationKind
                ::MeshChunks;
        }

        struct TerrainLightingConstants
        {
            float light_position[4]{ 0.0f, 8.0f, 0.0f, 0.0f };
            float light_direction[4]{ 0.35f, 0.8f, 0.45f, 0.0f };
            float light_color_intensity[4]{ 1.0f, 1.0f, 1.0f, 0.75f };
            // ambient_rgb, light_type
            float lighting_params[4]{ 0.25f, 0.25f, 0.25f, 0.0f };
        };

        void normalize3(float v[4])
        {
            const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (len > 1e-6f) {
                v[0] /= len;
                v[1] /= len;
                v[2] /= len;
            }
        }

        TerrainLightingConstants terrain_lighting_from_scene(
            std::span<const wz::scene::LightRecord> lights)
        {
            TerrainLightingConstants out{};
            const wz::scene::LightRecord* direct = nullptr;
            const wz::scene::LightRecord* ambient = nullptr;

            for (const auto& light : lights) {
                if (!ambient && light.type == wz::scene::LightType::Ambient) {
                    ambient = &light;
                }
                if (!direct && light.type == wz::scene::LightType::Directional) {
                    direct = &light;
                }
            }
            if (!direct) {
                for (const auto& light : lights) {
                    if (light.type == wz::scene::LightType::Point
                        || light.type == wz::scene::LightType::Spot)
                    {
                        direct = &light;
                        break;
                    }
                }
            }

            if (ambient) {
                out.lighting_params[0] =
                    (std::max)(0.0f, ambient->color.x * ambient->intensity);
                out.lighting_params[1] =
                    (std::max)(0.0f, ambient->color.y * ambient->intensity);
                out.lighting_params[2] =
                    (std::max)(0.0f, ambient->color.z * ambient->intensity);
            }

            if (direct) {
                out.light_position[0] = direct->position.x;
                out.light_position[1] = direct->position.y;
                out.light_position[2] = direct->position.z;
                out.light_position[3] = (std::max)(0.0f, direct->range);

                out.light_direction[0] = direct->direction.x;
                out.light_direction[1] = direct->direction.y;
                out.light_direction[2] = direct->direction.z;
                normalize3(out.light_direction);

                out.light_color_intensity[0] = direct->color.x;
                out.light_color_intensity[1] = direct->color.y;
                out.light_color_intensity[2] = direct->color.z;
                out.light_color_intensity[3] =
                    (std::max)(0.0f, direct->intensity);

                switch (direct->type) {
                case wz::scene::LightType::Directional:
                    out.lighting_params[3] = 0.0f;
                    break;
                case wz::scene::LightType::Point:
                    out.lighting_params[3] = 1.0f;
                    break;
                case wz::scene::LightType::Spot:
                    out.lighting_params[3] = 2.0f;
                    break;
                case wz::scene::LightType::Ambient:
                    out.lighting_params[3] = 0.0f;
                    break;
                }
            }

            return out;
        }

        TerrainLightingConstants terrain_lighting_from_renderable(
            const wz::engine::assets::TerrainLightingData& lighting,
            std::span<const wz::scene::LightRecord> lights)
        {
            if (lighting.mode
                != wz::engine::assets::TerrainLightingMode::HDRIEnvironment)
            {
                return terrain_lighting_from_scene(lights);
            }

            TerrainLightingConstants out{};
            out.light_position[0] = lighting.sky_visibility_strength;
            out.light_position[1] = lighting.terrain_bounce_strength;
            out.light_position[2] = 0.0f;
            out.light_position[3] = 0.0f;

            out.light_direction[0] = lighting.dominant_light_direction[0];
            out.light_direction[1] = lighting.dominant_light_direction[1];
            out.light_direction[2] = lighting.dominant_light_direction[2];
            normalize3(out.light_direction);

            out.light_color_intensity[0] = lighting.dominant_light_color[0];
            out.light_color_intensity[1] = lighting.dominant_light_color[1];
            out.light_color_intensity[2] = lighting.dominant_light_color[2];
            out.light_color_intensity[3] =
                (std::max)(0.0f, lighting.dominant_light_intensity);

            out.lighting_params[0] =
                (std::max)(0.0f, lighting.environment_color[0])
                * (std::max)(0.0f, lighting.environment_intensity);
            out.lighting_params[1] =
                (std::max)(0.0f, lighting.environment_color[1])
                * (std::max)(0.0f, lighting.environment_intensity);
            out.lighting_params[2] =
                (std::max)(0.0f, lighting.environment_color[2])
                * (std::max)(0.0f, lighting.environment_intensity);
            out.lighting_params[3] = -1.0f;

            return out;
        }

        void write_mesh_layer_style_constants(
            float constants[40],
            const wz::engine::assets::MeshRenderLayerStyle& layer,
            float alpha)
        {
            constants[32] = layer.color[0];
            constants[33] = layer.color[1];
            constants[34] = layer.color[2];
            constants[35] = (std::clamp)(alpha, 0.0f, 1.0f);
            constants[36] = (std::max)(0.0f, layer.emissive_strength);
            constants[37] = 0.0f;
            constants[38] = 0.0f;
            constants[39] = 0.0f;
        }

        void write_mesh_wireframe_style_constants(
            float constants[40],
            const wz::engine::assets::MeshRenderStyleData& style)
        {
            write_mesh_layer_style_constants(constants, style.wireframe, style.alpha);
        }

        void write_mesh_surface_style_constants(
            float constants[40],
            const wz::engine::assets::MeshRenderStyleData& style)
        {
            write_mesh_layer_style_constants(constants, style.surface, style.alpha);
        }

        void write_mesh_field_heatmap_style_constants(
            float constants[40],
            const wz::engine::assets::MeshRenderStyleData& style)
        {
            write_mesh_layer_style_constants(constants, style.surface, style.alpha);
            constants[36] = static_cast<float>(
                static_cast<uint32_t>(
                    style.field_visualization.palette));
            constants[37] = style.field_visualization.value_min;
            constants[38] = style.field_visualization.value_max;
            constants[39] = (std::max)(
                style.field_visualization.gamma,
                0.0001f);
        }

        void write_mesh_mask_style_constants(
            float constants[48],
            const wz::engine::assets::MeshRenderStyleData& style,
            uint32_t enabled_rule_count,
            uint32_t element_count)
        {
            write_mesh_layer_style_constants(
                constants,
                style.surface,
                style.alpha);
            constants[36] = static_cast<float>(enabled_rule_count);
            constants[37] = static_cast<float>(
                static_cast<uint32_t>(style.mask.overlap_mode));
            constants[38] = static_cast<float>(element_count);
            constants[39] = style.mask.show_unmatched ? 1.0f : 0.0f;
            constants[40] = style.mask.unmatched_color[0];
            constants[41] = style.mask.unmatched_color[1];
            constants[42] = style.mask.unmatched_color[2];
            constants[43] = style.mask.unmatched_color[3];
            constants[44] =
                (std::max)(0.0f, style.surface.emissive_strength);
            constants[45] = style.mask.domain
                    == wz::engine::assets::MeshMaskDomain::Vertex
                ? 1.0f
                : 0.0f;
            constants[46] = 0.0f;
            constants[47] = 0.0f;
        }

        struct MeshMaskRuleGpu
        {
            float color[4]{};
            float lo = 0.0f;
            float hi = 0.0f;
            uint32_t value_offset = 0u;
            int32_t priority = 0;
        };

        static_assert(sizeof(MeshMaskRuleGpu) == 32u);

        struct MeshMaskRuleBufferCacheEntry
        {
            void* device_impl = nullptr;
            uint64_t signature = 0u;
            wz::gpu::GPUHandle handle{};
            uint32_t rule_count = 0u;
        };

        std::vector<MeshMaskRuleBufferCacheEntry> g_mesh_mask_rule_buffers;

        struct MeshMaskDummyBuffers
        {
            void* device_impl = nullptr;
            wz::gpu::GPUHandle field{};
            wz::gpu::GPUHandle rules{};
        };

        std::vector<MeshMaskDummyBuffers> g_mesh_mask_dummy_buffers;

        uint64_t fnv_mix_bytes(
            uint64_t hash,
            const void* data,
            size_t size) noexcept
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0u; i < size; ++i) {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::vector<uint32_t> mesh_mask_channel_ids(
            const wz::engine::assets::MeshMaskRenderStyleData& mask)
        {
            std::vector<uint32_t> channels;
            channels.reserve(mask.rules.size());
            for (const wz::engine::assets::MeshMaskRule& rule : mask.rules) {
                if (rule.enabled && rule.input_channel_id != 0u) {
                    channels.push_back(rule.input_channel_id);
                }
            }
            std::sort(channels.begin(), channels.end());
            channels.erase(
                std::unique(channels.begin(), channels.end()),
                channels.end());
            return channels;
        }

        std::vector<MeshMaskRuleGpu> pack_mesh_mask_rules(
            const wz::engine::assets::MeshMaskRenderStyleData& mask,
            uint32_t element_count)
        {
            struct RuleWithChannel
            {
                const wz::engine::assets::MeshMaskRule* rule = nullptr;
                uint32_t channel_index = 0u;
            };

            const std::vector<uint32_t> channels = mesh_mask_channel_ids(mask);
            std::vector<RuleWithChannel> sorted_rules;
            sorted_rules.reserve(mask.rules.size());
            for (const wz::engine::assets::MeshMaskRule& rule : mask.rules) {
                if (!rule.enabled || rule.input_channel_id == 0u) {
                    continue;
                }
                const auto found = std::find(
                    channels.begin(),
                    channels.end(),
                    rule.input_channel_id);
                if (found == channels.end()) {
                    continue;
                }
                sorted_rules.push_back(RuleWithChannel{
                    .rule = &rule,
                    .channel_index = static_cast<uint32_t>(
                        std::distance(channels.begin(), found)),
                });
            }

            std::stable_sort(
                sorted_rules.begin(),
                sorted_rules.end(),
                [](const RuleWithChannel& a, const RuleWithChannel& b)
                {
                    return a.rule->priority < b.rule->priority;
                });

            std::vector<MeshMaskRuleGpu> packed;
            packed.reserve(
                (std::min)(
                    sorted_rules.size(),
                    static_cast<size_t>(
                        wz::engine::assets::kMaxMeshMaskRules)));
            for (const RuleWithChannel& item : sorted_rules) {
                if (packed.size()
                    >= wz::engine::assets::kMaxMeshMaskRules)
                {
                    break;
                }
                MeshMaskRuleGpu out{};
                for (int i = 0; i < 4; ++i) {
                    out.color[i] = item.rule->color[i];
                }
                out.lo = item.rule->lo;
                out.hi = item.rule->hi;
                out.value_offset = item.channel_index * element_count;
                out.priority = item.rule->priority;
                packed.push_back(out);
            }
            return packed;
        }

        wz::gpu::GPUHandle ensure_mesh_mask_rule_buffer(
            wz::gpu::Device& device,
            const wz::engine::assets::MeshMaskRenderStyleData& mask,
            uint32_t element_count,
            uint32_t& out_rule_count)
        {
            out_rule_count = 0u;
            if (!device.valid() || element_count == 0u || !mask.enabled) {
                return {};
            }

            const std::vector<MeshMaskRuleGpu> packed =
                pack_mesh_mask_rules(mask, element_count);
            if (packed.empty()) {
                return {};
            }

            out_rule_count = static_cast<uint32_t>(packed.size());
            uint64_t signature = 14695981039346656037ull;
            signature = fnv_mix_bytes(
                signature,
                &element_count,
                sizeof(element_count));
            signature = fnv_mix_bytes(
                signature,
                packed.data(),
                packed.size() * sizeof(MeshMaskRuleGpu));

            for (MeshMaskRuleBufferCacheEntry& entry :
                 g_mesh_mask_rule_buffers)
            {
                if (entry.device_impl != device.impl) {
                    continue;
                }
                if (entry.signature == signature && entry.handle.valid()) {
                    out_rule_count = entry.rule_count;
                    return entry.handle;
                }

                if (entry.handle.valid()
                    && entry.rule_count == out_rule_count
                    && wz::gpu::update_mesh_field_visualization_values(
                        device,
                        entry.handle,
                        reinterpret_cast<const std::byte*>(packed.data()),
                        static_cast<uint64_t>(
                            packed.size() * sizeof(MeshMaskRuleGpu)),
                        static_cast<uint32_t>(packed.size()),
                        sizeof(MeshMaskRuleGpu)))
                {
                    entry.signature = signature;
                    return entry.handle;
                }
            }

            const wz::gpu::GPUHandle handle =
                wz::gpu::upload_mesh_field_visualization_values(
                    device,
                    reinterpret_cast<const std::byte*>(packed.data()),
                    static_cast<uint64_t>(
                        packed.size() * sizeof(MeshMaskRuleGpu)),
                    static_cast<uint32_t>(packed.size()),
                    sizeof(MeshMaskRuleGpu));
            if (!handle.valid()) {
                out_rule_count = 0u;
                return {};
            }

            g_mesh_mask_rule_buffers.push_back({
                .device_impl = device.impl,
                .signature = signature,
                .handle = handle,
                .rule_count = out_rule_count,
            });
            return handle;
        }

        bool ensure_mesh_mask_dummy_buffers(
            wz::gpu::Device& device,
            wz::gpu::GPUHandle& out_field,
            wz::gpu::GPUHandle& out_rules)
        {
            out_field = {};
            out_rules = {};
            if (!device.valid()) {
                return false;
            }

            for (const MeshMaskDummyBuffers& entry :
                 g_mesh_mask_dummy_buffers)
            {
                if (entry.device_impl == device.impl
                    && entry.field.valid()
                    && entry.rules.valid())
                {
                    out_field = entry.field;
                    out_rules = entry.rules;
                    return true;
                }
            }

            const float dummy_field = 0.0f;
            const wz::gpu::GPUHandle field =
                wz::gpu::upload_mesh_field_visualization_values(
                    device,
                    reinterpret_cast<const std::byte*>(&dummy_field),
                    sizeof(dummy_field),
                    1u,
                    sizeof(dummy_field));

            MeshMaskRuleGpu dummy_rule{};
            const wz::gpu::GPUHandle rules =
                wz::gpu::upload_mesh_field_visualization_values(
                    device,
                    reinterpret_cast<const std::byte*>(&dummy_rule),
                    sizeof(dummy_rule),
                    1u,
                    sizeof(dummy_rule));

            if (!field.valid() || !rules.valid()) {
                return false;
            }

            g_mesh_mask_dummy_buffers.push_back({
                .device_impl = device.impl,
                .field = field,
                .rules = rules,
            });
            out_field = field;
            out_rules = rules;
            return true;
        }

        bool mesh_wireframe_wants_prepass(
            const wz::engine::assets::MeshRenderStyleData& style)
        {
            return style.depth_write;
        }

        bool is_terrain_surface_program(BuiltinRenderProgram program)
        {
            return program == BuiltinRenderProgram::TerrainMeshSurface;
        }

        UINT root_constant_count_for_program(BuiltinRenderProgram program);

        struct TerrainSubmitCpuProfile
        {
            double total_us = 0.0;
            double resolve_us = 0.0;
            double resource_us = 0.0;
            double constants_us = 0.0;
            double bind_us = 0.0;
            double draw_us = 0.0;
            double stats_us = 0.0;
            uint64_t surfel_draw_calls = 0;
            uint64_t mesh_draw_calls = 0;
            uint64_t fallback_mesh_draw_calls = 0;
            uint64_t surfel_fallbacks = 0;

            // Surfel failure diagnostics
            uint64_t surfel_fail_resolve = 0;
            uint64_t surfel_fail_pipeline = 0;
            uint64_t surfel_fail_cloud = 0;
            uint64_t surfel_fail_splats = 0;
        };

        struct TerrainMeshDrawBindings
        {
            const wz::gpu::dx12::internal::DX12GraphicsPipeline* pipeline =
                nullptr;
            const wz::gpu::dx12::internal::DX12MeshResource* mesh = nullptr;
            size_t terrain_instance_index =
                (std::numeric_limits<size_t>::max)();

            void invalidate() noexcept
            {
                pipeline = nullptr;
                mesh = nullptr;
                terrain_instance_index =
                    (std::numeric_limits<size_t>::max)();
            }
        };

        bool draw_terrain_ref_mesh(
            wz::gpu::Device& device,
            ID3D12GraphicsCommandList* cmdList,
            const TerrainDrawRef& ref,
            const TerrainVisualInstance& instance,
            const RenderFrameView& frame,
            const wz::engine::rendering::RenderablePipelineCache& pipeline_cache,
            const wz::engine::rendering::RenderResourceResolver& resolver,
            const wz::engine::rendering::RenderProgramPipelineCache* render_program_cache,
            TerrainMeshDrawBindings& bindings,
            TerrainSubmitCpuProfile& profile,
            bool mesh_fallback = false)
        {
            if (ref.representation_kind
                != wz::scene::TerrainVisualRepresentationKind::MeshChunks)
            {
                if (!mesh_fallback)
                    return false;
            }

            const auto resolve_t0 = SubmitClock::now();
            const auto resolved =
                mesh_fallback
                ? resolver.resolve_terrain_draw_mesh_fallback(
                    instance.terrain_proxy_id,
                    ref)
                : resolver.resolve_terrain_draw(instance.terrain_proxy_id, ref);
            const auto resolve_t1 = SubmitClock::now();
            profile.resolve_us += elapsed_us(resolve_t0, resolve_t1);
            if (!resolved || !is_terrain_surface_program(resolved->program))
                return false;

            const auto resource_t0 = SubmitClock::now();
            wz::gpu::GPUHandle pipeline_handle;
            if (render_program_cache && resolved->render_program.valid()) {
                pipeline_handle =
                    render_program_cache->get(resolved->render_program);
            }
            else {
                pipeline_handle = pipeline_cache.get(resolved->program);
            }

            const auto* pl =
                wz::gpu::dx12::internal::get_graphics_pipeline(
                    device,
                    pipeline_handle);
            if (!pl || !pl->valid())
                return false;

            const auto* mesh = wz::gpu::dx12::internal::get_mesh(
                device,
                resolved->gpu_resource);
            const auto resource_t1 = SubmitClock::now();
            profile.resource_us += elapsed_us(resource_t0, resource_t1);
            if (!mesh || !mesh->vertex_buffer)
                return false;

            const auto bind_t0 = SubmitClock::now();

            if (bindings.pipeline != pl) {
                cmdList->SetGraphicsRootSignature(pl->root_sig);
                cmdList->SetPipelineState(pl->pso);
                cmdList->IASetPrimitiveTopology(
                    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                bindings.pipeline = pl;
                bindings.mesh = nullptr;
                bindings.terrain_instance_index =
                    (std::numeric_limits<size_t>::max)();
            }

            if (bindings.terrain_instance_index
                != ref.terrain_instance_index)
            {
                const auto constants_t0 = SubmitClock::now();
                float constants[48] = {};
                for (int i = 0; i < 16; ++i)
                    constants[i] = instance.world.m[i];
                for (int i = 0; i < 16; ++i)
                    constants[16 + i] = frame.view.view_projection.m[i];

                const TerrainLightingConstants lighting =
                    terrain_lighting_from_renderable(
                        resolved->terrain_lighting,
                        frame.lights);
                for (int i = 0; i < 4; ++i) {
                    constants[32 + i] = lighting.light_position[i];
                    constants[36 + i] = lighting.light_direction[i];
                    constants[40 + i] = lighting.light_color_intensity[i];
                    constants[44 + i] = lighting.lighting_params[i];
                }
                const auto constants_t1 = SubmitClock::now();
                profile.constants_us +=
                    elapsed_us(constants_t0, constants_t1);

                cmdList->SetGraphicsRoot32BitConstants(
                    0,
                    root_constant_count_for_program(resolved->program),
                    constants,
                    0);
                bindings.terrain_instance_index =
                    ref.terrain_instance_index;
            }

            if (bindings.mesh != mesh) {
                cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
                cmdList->IASetIndexBuffer(&mesh->index_view);
                bindings.mesh = mesh;
            }

            const auto bind_t1 = SubmitClock::now();
            profile.bind_us += elapsed_us(bind_t0, bind_t1);

            const auto draw_t0 = SubmitClock::now();
            cmdList->DrawIndexedInstanced(
                resolved->index_count,
                1,
                resolved->first_index,
                0,
                0);
            const auto draw_t1 = SubmitClock::now();
            profile.draw_us += elapsed_us(draw_t0, draw_t1);
            ++profile.mesh_draw_calls;
            if (mesh_fallback)
                ++profile.fallback_mesh_draw_calls;

            const auto stats_t0 = SubmitClock::now();
            resolver.record_terrain_render_stats(
                0u,
                1u,
                0u,
                resolved->index_count / 3u,
                0u,
                0u,
                resolved->lod_replacement_available
                    && !resolved->transition_selected ? 1u : 0u,
                resolved->lod_replacement_available
                    && !resolved->transition_selected
                    ? resolved->source_triangle_count
                    : 0u,
                resolved->lod_replacement_selected
                    && !resolved->transition_selected ? 1u : 0u,
                resolved->lod_replacement_selected
                    && !resolved->transition_selected
                    ? resolved->index_count / 3u
                    : 0u,
                0u,
                0u,
                resolved->terrain_target_pixels_per_triangle,
                0.0f,
                0.0f,
                0.0,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                mesh_fallback ? 0u : ref.lod_id.value,
                mesh_fallback
                    ? wz::engine::assets::TerrainVisualRepresentationKind
                        ::MeshChunks
                    : engine_terrain_representation_kind(
                        ref.representation_kind),
                1u);
            const auto stats_t1 = SubmitClock::now();
            profile.stats_us += elapsed_us(stats_t0, stats_t1);
            return true;
        }

        bool record_terrain_ref_surfel_stats(
            const TerrainDrawRef& ref,
            const wz::engine::rendering::ResolvedTerrainDrawResource& resolved,
            const wz::engine::rendering::RenderResourceResolver& resolver,
            uint64_t submitted_draw_calls)
        {
            if (ref.representation_kind
                != wz::scene::TerrainVisualRepresentationKind
                    ::SurfelCloud)
            {
                return false;
            }

            if (!resolved.far_splat_selected)
                return false;

            resolver.record_terrain_render_stats(
                0u,
                1u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                1u,
                resolved.far_splat_count,
                resolved.terrain_target_pixels_per_triangle,
                0.0f,
                0.0f,
                0.0,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                0u,
                ref.lod_id.value,
                engine_terrain_representation_kind(ref.representation_kind),
                submitted_draw_calls);
            return true;
        }

        struct TerrainSurfelDrawBindings
        {
            const wz::gpu::dx12::internal::DX12GraphicsPipeline* pipeline =
                nullptr;
            const wz::gpu::dx12::internal::DX12GaussianSplatCloudResource*
                cloud = nullptr;
            size_t terrain_instance_index =
                (std::numeric_limits<size_t>::max)();
            bool pipeline_bound = false;

            void invalidate() noexcept
            {
                pipeline_bound = false;
                pipeline = nullptr;
                cloud = nullptr;
                terrain_instance_index =
                    (std::numeric_limits<size_t>::max)();
            }
        };

        bool draw_terrain_ref_surfel(
            wz::gpu::Device& device,
            ID3D12GraphicsCommandList* cmdList,
            const TerrainDrawRef& ref,
            const TerrainVisualInstance& instance,
            const RenderFrameView& frame,
            const wz::gpu::dx12::internal::DX12GraphicsPipeline* pl,
            const wz::engine::rendering::RenderResourceResolver& resolver,
            TerrainSurfelDrawBindings& bindings,
            TerrainSubmitCpuProfile& profile,
            std::optional<wz::engine::rendering::ResolvedTerrainDrawResource>*
                out_resolved = nullptr)
        {
            if (ref.representation_kind
                != wz::scene::TerrainVisualRepresentationKind
                    ::SurfelCloud)
            {
                return false;
            }

            const auto resolve_t0 = SubmitClock::now();
            const auto resolved =
                resolver.resolve_terrain_draw(instance.terrain_proxy_id, ref);
            const auto resolve_t1 = SubmitClock::now();
            profile.resolve_us += elapsed_us(resolve_t0, resolve_t1);
            if (out_resolved)
                *out_resolved = resolved;
            if (!resolved || !resolved->far_splat_selected) {
                ++profile.surfel_fail_resolve;
                return false;
            }

            if (!pl || !pl->valid()) {
                ++profile.surfel_fail_pipeline;
                return false;
            }

            const auto resource_t0 = SubmitClock::now();
            const auto* cloud = wz::gpu::dx12::internal::get_gaussian_splat_cloud(
                device,
                resolved->gpu_resource);
            const auto resource_t1 = SubmitClock::now();
            profile.resource_us += elapsed_us(resource_t0, resource_t1);
            if (!cloud || !cloud->valid_for_vertex_instanced()) {
                ++profile.surfel_fail_cloud;
                return false;
            }

            const uint32_t available_splats =
                resolved->first_splat < cloud->splat_count
                ? cloud->splat_count - resolved->first_splat
                : 0u;
            const uint32_t draw_splats =
                (std::min)(resolved->far_splat_count, available_splats);
            if (draw_splats == 0u) {
                ++profile.surfel_fail_splats;
                return false;
            }

            const auto bind_t0 = SubmitClock::now();
            if (!bindings.pipeline_bound || bindings.pipeline != pl) {
                cmdList->SetGraphicsRootSignature(pl->root_sig);
                cmdList->SetPipelineState(pl->pso);
                cmdList->IASetPrimitiveTopology(
                    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                bindings.pipeline = pl;
                bindings.cloud = nullptr;
                bindings.terrain_instance_index =
                    (std::numeric_limits<size_t>::max)();
                bindings.pipeline_bound = true;
            }

            if (bindings.terrain_instance_index
                != ref.terrain_instance_index)
            {
                const auto constants_t0 = SubmitClock::now();
                const float vp_w = frame.view.terrain_lod.viewport_width;
                const float vp_h = frame.view.terrain_lod.viewport_height;

                float constants[36] = {};
                for (int i = 0; i < 16; ++i)
                    constants[i] = instance.world.m[i];
                for (int i = 0; i < 16; ++i)
                    constants[16 + i] = frame.view.view_projection.m[i];
                constants[32] = vp_w;
                constants[33] = vp_h;
                constants[34] = 0.55f;
                constants[35] = 0.0f;

                cmdList->SetGraphicsRoot32BitConstants(0, 36, constants, 0);
                bindings.terrain_instance_index =
                    ref.terrain_instance_index;
                const auto constants_t1 = SubmitClock::now();
                profile.constants_us +=
                    elapsed_us(constants_t0, constants_t1);
            }

            if (bindings.cloud != cloud) {
                cmdList->IASetVertexBuffers(0, 1, &cloud->vertex_view);
                bindings.cloud = cloud;
            }
            const auto bind_t1 = SubmitClock::now();
            profile.bind_us += elapsed_us(bind_t0, bind_t1);

            const auto draw_t0 = SubmitClock::now();
            cmdList->DrawInstanced(
                4,
                draw_splats,
                0,
                resolved->first_splat);
            const auto draw_t1 = SubmitClock::now();
            profile.draw_us += elapsed_us(draw_t0, draw_t1);
            ++profile.surfel_draw_calls;

            const auto stats_t0 = SubmitClock::now();
            record_terrain_ref_surfel_stats(ref, *resolved, resolver, 1u);
            const auto stats_t1 = SubmitClock::now();
            profile.stats_us += elapsed_us(stats_t0, stats_t1);
            return true;
        }

        void submit_terrain_refs(
            wz::gpu::Device& device,
            const RenderFrameView& frame,
            const wz::engine::rendering::RenderResourceResolver& resolver,
            const wz::engine::rendering::RenderablePipelineCache& pipeline_cache,
            const wz::engine::rendering::RenderProgramPipelineCache* render_program_cache)
        {
            auto* cmdList =
                wz::gpu::dx12::internal::get_command_list(device);
            TerrainSubmitCpuProfile profile{};
            const auto total_t0 = SubmitClock::now();

            for (const TerrainVisualInstance& instance :
                frame.terrain_instances)
            {
                if (!instance.visible)
                    continue;

                const auto diagnostics =
                    resolver.resolve_terrain_proxy_diagnostics(
                        instance.terrain_proxy_id);
                if (diagnostics)
                    resolver.record_terrain_source_totals(
                        diagnostics->proxy_chunks,
                        diagnostics->source_triangles);
            }

            TerrainSurfelDrawBindings surfel_bindings{};
            TerrainMeshDrawBindings mesh_bindings{};

            for (const TerrainDrawRef& ref : frame.terrain) {
                if (ref.terrain_instance_index
                    >= frame.terrain_instances.size())
                {
                    continue;
                }

                const TerrainVisualInstance& instance =
                    frame.terrain_instances[ref.terrain_instance_index];
                if (!instance.visible)
                    continue;

                if (ref.kind == TerrainDrawRefKind::ChunkLod) {
                    resolver.record_terrain_visible_chunks(1u);
                }

                std::optional<
                    wz::engine::rendering::ResolvedTerrainDrawResource>
                    surfel_resolved;

                if (ref.representation_kind
                    == wz::scene::TerrainVisualRepresentationKind
                        ::SurfelCloud)
                {
                    const auto resolve_t0 = SubmitClock::now();
                    surfel_resolved = resolver.resolve_terrain_draw(
                        instance.terrain_proxy_id,
                        ref);
                    const auto resolve_t1 = SubmitClock::now();
                    profile.resolve_us += elapsed_us(resolve_t0, resolve_t1);
                    if (surfel_resolved
                        && surfel_resolved->far_splat_selected)
                    {
                        record_terrain_ref_surfel_stats(
                            ref,
                            *surfel_resolved,
                            resolver,
                            0u);
                    }
                    ++profile.surfel_fallbacks;
                    draw_terrain_ref_mesh(
                        device,
                        cmdList,
                        ref,
                        instance,
                        frame,
                        pipeline_cache,
                        resolver,
                        render_program_cache,
                        mesh_bindings,
                        profile,
                        true);
                    surfel_bindings.invalidate();
                    continue;
                }

                draw_terrain_ref_mesh(
                    device,
                    cmdList,
                    ref,
                    instance,
                    frame,
                    pipeline_cache,
                    resolver,
                    render_program_cache,
                    mesh_bindings,
                    profile);
                surfel_bindings.invalidate();
            }
            const auto total_t1 = SubmitClock::now();
            profile.total_us = elapsed_us(total_t0, total_t1);
            resolver.record_terrain_submit_cpu_profile(
                profile.total_us,
                profile.resolve_us,
                profile.resource_us,
                profile.constants_us,
                profile.bind_us,
                profile.draw_us,
                profile.stats_us,
                profile.surfel_draw_calls,
                profile.mesh_draw_calls,
                profile.fallback_mesh_draw_calls,
                profile.surfel_fallbacks);

            // Periodic diagnostic: log surfel failure breakdown
            {
                static uint64_t diag_frame = 0;
                if ((diag_frame++ % 300) == 0
                    && (profile.surfel_fallbacks > 0
                        || profile.surfel_draw_calls > 0))
                {
                    std::cerr
                        << "[terrain_submit] surfel="
                        << profile.surfel_draw_calls
                        << " mesh=" << profile.mesh_draw_calls
                        << " fallback=" << profile.fallback_mesh_draw_calls
                        << " fail:resolve="
                        << profile.surfel_fail_resolve
                        << " fail:pipeline="
                        << profile.surfel_fail_pipeline
                        << " fail:cloud="
                        << profile.surfel_fail_cloud
                        << " fail:splats="
                        << profile.surfel_fail_splats
                        << " surfel_pipeline=disabled"
                        << "\n";
                }
            }
        }

        bool is_mesh_wireframe_program(BuiltinRenderProgram program)
        {
            return program == BuiltinRenderProgram::MeshWireframeDebug
                || program == BuiltinRenderProgram::MeshWireframeDepthDebug
                || program == BuiltinRenderProgram::MeshWireframeAlpha;
        }

        bool is_mesh_surface_program(BuiltinRenderProgram program)
        {
            return program == BuiltinRenderProgram::MeshSurface
                || program == BuiltinRenderProgram::MeshSurfaceAlpha;
        }

        bool is_mesh_field_heatmap_program(BuiltinRenderProgram program)
        {
            return program == BuiltinRenderProgram::MeshFieldHeatmap;
        }

        bool is_mesh_mask_program(BuiltinRenderProgram program)
        {
            return program == BuiltinRenderProgram::MeshMaskStyle;
        }

        UINT root_constant_count_for_program(BuiltinRenderProgram program)
        {
            if (is_terrain_surface_program(program)) {
                return 48;
            }

            if (is_mesh_mask_program(program)) {
                return 48;
            }

            if (is_mesh_wireframe_program(program)
                || is_mesh_surface_program(program)
                || is_mesh_field_heatmap_program(program))
            {
                return 40;
            }

            return 32;
        }

        bool bind_mesh_field_heatmap_resource(
            wz::gpu::Device& device,
            ID3D12GraphicsCommandList* cmdList,
            const wz::engine::rendering::ResolvedRenderableResource& resolved,
            uint32_t root_parameter_index)
        {
            const auto* field =
                wz::gpu::dx12::internal::get_mesh_field_visualization(
                    device,
                    resolved.mesh_field_visualization_resource);
            if (!field || !field->valid()) {
                return false;
            }

            auto* srv_heap =
                wz::gpu::dx12::internal::get_srv_cbv_uav_heap(device);
            if (!srv_heap) {
                return false;
            }

            cmdList->SetDescriptorHeaps(1, &srv_heap);
            cmdList->SetGraphicsRootDescriptorTable(
                root_parameter_index,
                field->srv_table.gpu_at(0));
            return true;
        }

        bool bind_builtin_mesh_field_heatmap_resource(
            wz::gpu::Device& device,
            ID3D12GraphicsCommandList* cmdList,
            const wz::engine::rendering::ResolvedRenderableResource& resolved)
        {
            if (!is_mesh_field_heatmap_program(resolved.program)) {
                return true;
            }

            return bind_mesh_field_heatmap_resource(
                device,
                cmdList,
                resolved,
                1u);
        }

        uint32_t mesh_mask_element_count(
            wz::gpu::Device& device,
            const wz::engine::rendering::ResolvedRenderableResource& resolved)
        {
            const auto* field =
                wz::gpu::dx12::internal::get_mesh_field_visualization(
                    device,
                    resolved.mesh_field_visualization_resource);
            if (!field || !field->valid()) {
                return 0u;
            }

            const std::vector<uint32_t> channels =
                mesh_mask_channel_ids(resolved.mesh_style.mask);
            if (channels.empty()
                || field->element_count
                    < static_cast<uint32_t>(channels.size()))
            {
                return 0u;
            }
            return field->element_count / static_cast<uint32_t>(channels.size());
        }

        bool prepare_builtin_mesh_mask_resources(
            wz::gpu::Device& device,
            const wz::engine::rendering::ResolvedRenderableResource& resolved,
            uint32_t fallback_element_count,
            uint32_t& out_rule_count,
            uint32_t& out_element_count)
        {
            out_rule_count = 0u;
            out_element_count = 0u;
            if (!is_mesh_mask_program(resolved.program)) {
                return true;
            }

            out_element_count = mesh_mask_element_count(device, resolved);
            if (out_element_count > 0u) {
                const wz::gpu::GPUHandle rules =
                    ensure_mesh_mask_rule_buffer(
                        device,
                        resolved.mesh_style.mask,
                        out_element_count,
                        out_rule_count);
                if (rules.valid() && out_rule_count > 0u) {
                    return true;
                }
            }

            if (!resolved.mesh_style.mask.show_unmatched
                || fallback_element_count == 0u)
            {
                out_rule_count = 0u;
                out_element_count = 0u;
                return false;
            }

            wz::gpu::GPUHandle dummy_field{};
            wz::gpu::GPUHandle dummy_rules{};
            if (!ensure_mesh_mask_dummy_buffers(
                    device,
                    dummy_field,
                    dummy_rules))
            {
                out_rule_count = 0u;
                out_element_count = 0u;
                return false;
            }

            out_rule_count = 0u;
            out_element_count = fallback_element_count;
            return true;
        }

        bool bind_builtin_mesh_mask_resources(
            wz::gpu::Device& device,
            ID3D12GraphicsCommandList* cmdList,
            const wz::engine::rendering::ResolvedRenderableResource& resolved,
            uint32_t fallback_element_count,
            uint32_t& out_rule_count,
            uint32_t& out_element_count)
        {
            out_rule_count = 0u;
            out_element_count = 0u;
            if (!is_mesh_mask_program(resolved.program)) {
                return true;
            }

            const auto* field =
                wz::gpu::dx12::internal::get_mesh_field_visualization(
                    device,
                    resolved.mesh_field_visualization_resource);

            const std::vector<uint32_t> channels =
                mesh_mask_channel_ids(resolved.mesh_style.mask);
            wz::gpu::GPUHandle field_handle =
                resolved.mesh_field_visualization_resource;
            wz::gpu::GPUHandle rules_handle{};

            if (field && field->valid() && !channels.empty()
                && field->element_count
                    >= static_cast<uint32_t>(channels.size()))
            {
                out_element_count =
                    field->element_count
                    / static_cast<uint32_t>(channels.size());
            }

            if (out_element_count > 0u) {
                rules_handle = ensure_mesh_mask_rule_buffer(
                    device,
                    resolved.mesh_style.mask,
                    out_element_count,
                    out_rule_count);
            }

            if (!rules_handle.valid() || out_rule_count == 0u) {
                if (!resolved.mesh_style.mask.show_unmatched
                    || fallback_element_count == 0u)
                {
                    return false;
                }

                wz::gpu::GPUHandle dummy_field{};
                wz::gpu::GPUHandle dummy_rules{};
                if (!ensure_mesh_mask_dummy_buffers(
                        device,
                        dummy_field,
                        dummy_rules))
                {
                    return false;
                }
                field_handle = dummy_field;
                rules_handle = dummy_rules;
                out_rule_count = 0u;
                out_element_count = fallback_element_count;
            }

            const auto* rules =
                wz::gpu::dx12::internal::get_mesh_field_visualization(
                    device,
                    rules_handle);
            if (!rules || !rules->valid()) {
                return false;
            }
            field = wz::gpu::dx12::internal::get_mesh_field_visualization(
                device,
                field_handle);
            if (!field || !field->valid()) {
                return false;
            }

            auto* srv_heap =
                wz::gpu::dx12::internal::get_srv_cbv_uav_heap(device);
            if (!srv_heap) {
                return false;
            }

            cmdList->SetDescriptorHeaps(1, &srv_heap);
            cmdList->SetGraphicsRootDescriptorTable(
                1u,
                field->srv_table.gpu_at(0));
            cmdList->SetGraphicsRootDescriptorTable(
                2u,
                rules->srv_table.gpu_at(0));
            return true;
        }

        bool bind_custom_render_program_descriptor_resources(
            wz::gpu::Device& device,
            ID3D12GraphicsCommandList* cmdList,
            const wz::engine::rendering::ResolvedRenderableResource& resolved,
            const wz::engine::rendering::RenderProgramPipelineCache*
                render_program_cache)
        {
            if (!render_program_cache || !resolved.render_program.valid()) {
                return true;
            }

            const auto* layout =
                render_program_cache->get_binding_layout(
                    resolved.render_program);
            if (!layout) {
                return false;
            }

            for (const auto& descriptor : layout->descriptors) {
                switch (descriptor.semantic) {
                case wz::engine::assets::DescriptorSemantic
                    ::MeshFieldVisualization:
                    if (descriptor.descriptor_table_offset != 0u) {
                        return false;
                    }
                    if (!bind_mesh_field_heatmap_resource(
                            device,
                            cmdList,
                            resolved,
                            descriptor.root_parameter_index))
                    {
                        return false;
                    }
                    break;
                case wz::engine::assets::DescriptorSemantic::MeshMaskRules:
                {
                    const uint32_t element_count =
                        mesh_mask_element_count(device, resolved);
                    uint32_t rule_count = 0u;
                    const wz::gpu::GPUHandle rules_handle =
                        ensure_mesh_mask_rule_buffer(
                            device,
                            resolved.mesh_style.mask,
                            element_count,
                            rule_count);
                    if (!rules_handle.valid() || rule_count == 0u) {
                        return false;
                    }

                    const auto* rules =
                        wz::gpu::dx12::internal::get_mesh_field_visualization(
                            device,
                            rules_handle);
                    if (!rules || !rules->valid()) {
                        return false;
                    }

                    auto* srv_heap =
                        wz::gpu::dx12::internal::get_srv_cbv_uav_heap(device);
                    if (!srv_heap) {
                        return false;
                    }

                    cmdList->SetDescriptorHeaps(1, &srv_heap);
                    cmdList->SetGraphicsRootDescriptorTable(
                        descriptor.root_parameter_index,
                        rules->srv_table.gpu_at(0));
                    break;
                }
                default:
                    return false;
                }
            }

            return true;
        }

        void draw_mesh_surface_wireframe_overlay(
            wz::gpu::Device& device,
            const wz::engine::rendering::RenderablePipelineCache& pipeline_cache,
            const wz::gpu::dx12::internal::DX12MeshResource& mesh,
            float constants[48],
            const wz::engine::assets::MeshRenderStyleData& style)
        {
            if (!style.wireframe.enabled) {
                return;
            }

            wz::engine::assets::MeshRenderStyleData overlay_style = style;
            if (std::isfinite(overlay_style.wireframe.color[3])) {
                overlay_style.alpha = (std::clamp)(
                    overlay_style.wireframe.color[3],
                    0.0f,
                    1.0f);
            }
            if (overlay_style.alpha <= 0.0f) {
                return;
            }
            const bool transparent =
                wz::engine::assets::is_mesh_render_style_transparent(
                    overlay_style);
            const auto program =
                transparent
                    ? BuiltinRenderProgram::MeshWireframeAlpha
                : overlay_style.depth_test || overlay_style.depth_write
                    ? BuiltinRenderProgram::MeshWireframeDepthDebug
                    : BuiltinRenderProgram::MeshWireframeDebug;
            const auto pipeline_handle = pipeline_cache.get(program);
            const auto* overlay =
                wz::gpu::dx12::internal::get_graphics_pipeline(
                    device,
                    pipeline_handle);
            if (!overlay || !overlay->valid()) {
                return;
            }

            auto* cmdList =
                wz::gpu::dx12::internal::get_command_list(device);
            if (!transparent && mesh_wireframe_wants_prepass(overlay_style)) {
                const auto prepass_handle = pipeline_cache.get(
                    BuiltinRenderProgram::MeshDepthPrepassDebug);
                const auto* prepass =
                    wz::gpu::dx12::internal::get_graphics_pipeline(
                        device,
                        prepass_handle);
                if (prepass && prepass->valid()) {
                    cmdList->SetGraphicsRootSignature(prepass->root_sig);
                    cmdList->SetPipelineState(prepass->pso);
                    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    cmdList->SetGraphicsRoot32BitConstants(0, 40, constants, 0);
                    cmdList->IASetVertexBuffers(0, 1, &mesh.vertex_view);
                    cmdList->IASetIndexBuffer(&mesh.index_view);
                    cmdList->DrawIndexedInstanced(mesh.index_count, 1, 0, 0, 0);
                }
            }
            write_mesh_wireframe_style_constants(constants, overlay_style);
            cmdList->SetGraphicsRootSignature(overlay->root_sig);
            cmdList->SetPipelineState(overlay->pso);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->SetGraphicsRoot32BitConstants(0, 40, constants, 0);
            cmdList->IASetVertexBuffers(0, 1, &mesh.vertex_view);
            cmdList->IASetIndexBuffer(&mesh.index_view);
            cmdList->DrawIndexedInstanced(mesh.index_count, 1, 0, 0, 0);
        }

        void submit_mesh_commands(
            wz::gpu::Device& device,
            std::span<const DrawCommand> commands,
            const RenderFrameView& frame,
            const wz::engine::rendering::RenderResourceResolver& resolver,
            const wz::engine::rendering::RenderablePipelineCache& pipeline_cache,
            const wz::engine::rendering::RenderProgramPipelineCache* render_program_cache)
        {
            auto* cmdList =
                wz::gpu::dx12::internal::get_command_list(device);

            for (const DrawCommand& dc : commands)
            {
                if (dc.kind != DrawCommandKind::Mesh)
                    continue;
                if (dc.mesh == INVALID_MESH)
                    continue;

                const auto resolved = resolver.resolve_mesh(dc.mesh);
                if (!resolved)
                    continue;

                wz::gpu::GPUHandle pipeline_handle;
                if (render_program_cache && resolved->render_program.valid()) {
                    pipeline_handle =
                        render_program_cache->get(resolved->render_program);
                }
                else {
                    pipeline_handle = pipeline_cache.get(resolved->program);
                }

                const auto* pl =
                    wz::gpu::dx12::internal::get_graphics_pipeline(
                        device,
                        pipeline_handle);
                if (!pl || !pl->valid())
                    continue;

                const auto* mesh = wz::gpu::dx12::internal::get_mesh(
                    device,
                    resolved->gpu_resource);
                if (!mesh || !mesh->vertex_buffer)
                    continue;

                float constants[48] = {};
                for (int i = 0; i < 16; ++i) constants[i] = dc.world.m[i];
                for (int i = 0; i < 16; ++i) {
                    constants[16 + i] = frame.view.view_projection.m[i];
                }

                if (is_terrain_surface_program(resolved->program))
                    continue;
                const bool mesh_wireframe =
                    is_mesh_wireframe_program(resolved->program);
                const bool mesh_surface =
                    is_mesh_surface_program(resolved->program);
                const bool mesh_field_heatmap =
                    is_mesh_field_heatmap_program(resolved->program);
                const bool mesh_mask =
                    is_mesh_mask_program(resolved->program);
                uint32_t mesh_mask_rule_count = 0u;
                uint32_t mesh_mask_elements = 0u;
                if (mesh_mask) {
                    const uint32_t fallback_element_count =
                        resolved->mesh_style.mask.domain
                                == wz::engine::assets::MeshMaskDomain::Vertex
                            ? mesh->vertex_count
                            : mesh->index_count / 3u;
                    if (!prepare_builtin_mesh_mask_resources(
                            device,
                            *resolved,
                            fallback_element_count,
                            mesh_mask_rule_count,
                            mesh_mask_elements))
                    {
                        continue;
                    }
                }

                if (mesh_wireframe) {
                    write_mesh_wireframe_style_constants(
                        constants,
                        resolved->mesh_style);
                }
                else if (mesh_mask) {
                    write_mesh_mask_style_constants(
                        constants,
                        resolved->mesh_style,
                        mesh_mask_rule_count,
                        mesh_mask_elements);
                }
                else if (mesh_surface) {
                    write_mesh_surface_style_constants(
                        constants,
                        resolved->mesh_style);
                }
                else if (mesh_field_heatmap) {
                    write_mesh_field_heatmap_style_constants(
                        constants,
                        resolved->mesh_style);
                }

                if (resolved->program == BuiltinRenderProgram::MeshWireframeDepthDebug
                    && mesh_wireframe_wants_prepass(resolved->mesh_style))
                {
                    const auto prepass_handle = pipeline_cache.get(
                        BuiltinRenderProgram::MeshDepthPrepassDebug);
                    const auto* prepass =
                        wz::gpu::dx12::internal::get_graphics_pipeline(
                            device,
                            prepass_handle);

                    if (prepass && prepass->valid()) {
                        cmdList->SetGraphicsRootSignature(prepass->root_sig);
                        cmdList->SetPipelineState(prepass->pso);
                        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        cmdList->SetGraphicsRoot32BitConstants(0, 40, constants, 0);
                        cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
                        cmdList->IASetIndexBuffer(&mesh->index_view);
                        cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
                    }
                }

                cmdList->SetGraphicsRootSignature(pl->root_sig);
                cmdList->SetPipelineState(pl->pso);
                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                cmdList->SetGraphicsRoot32BitConstants(
                    0,
                    root_constant_count_for_program(resolved->program),
                    constants,
                    0);
                if (!bind_custom_render_program_descriptor_resources(
                        device,
                        cmdList,
                        *resolved,
                        render_program_cache))
                {
                    continue;
                }
                if (!resolved->render_program.valid()
                    && !bind_builtin_mesh_field_heatmap_resource(
                        device,
                        cmdList,
                        *resolved))
                {
                    continue;
                }
                if (!resolved->render_program.valid()) {
                    uint32_t bound_rule_count = 0u;
                    uint32_t bound_element_count = 0u;
                    const uint32_t fallback_element_count =
                        resolved->mesh_style.mask.domain
                                == wz::engine::assets::MeshMaskDomain::Vertex
                            ? mesh->vertex_count
                            : mesh->index_count / 3u;
                    if (!bind_builtin_mesh_mask_resources(
                            device,
                            cmdList,
                            *resolved,
                            fallback_element_count,
                            bound_rule_count,
                            bound_element_count))
                    {
                        continue;
                    }
                }
                cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
                cmdList->IASetIndexBuffer(&mesh->index_view);
                cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);

                if (mesh_surface || mesh_field_heatmap || mesh_mask) {
                    draw_mesh_surface_wireframe_overlay(
                        device,
                        pipeline_cache,
                        *mesh,
                        constants,
                        resolved->mesh_style);
                }
            }
        }

        void submit_sky_pass(
            wz::gpu::Device& device,
            const Mat4& view,
            std::span<const SkyDrawCommand> sky,
            const wz::engine::rendering::RenderablePipelineCache* pipeline_cache)
        {
            auto* cmdList =
                wz::gpu::dx12::internal::get_command_list(device);
            static bool logged_scalar_missing = false;
            static bool logged_scalar_bound = false;
            static bool logged_texture_missing = false;
            static bool logged_texture_bound = false;
            static bool logged_vector_missing = false;
            static bool logged_vector_bound = false;

            for (const SkyDrawCommand& dc : sky)
            {
                if (dc.visual_kind != SkyVisualKind::SolidColor
                    && dc.visual_kind != SkyVisualKind::DirectionDebug
                    && dc.visual_kind != SkyVisualKind::Gradient
                    && dc.visual_kind != SkyVisualKind::EquirectangularTexture
                    && dc.visual_kind != SkyVisualKind::ScalarField
                    && dc.visual_kind != SkyVisualKind::VectorField)
                {
                    continue;
                }

                const bool needs_scalar_field =
                    dc.visual_kind == SkyVisualKind::ScalarField;
                const bool needs_texture =
                    dc.visual_kind == SkyVisualKind::EquirectangularTexture;
                const bool needs_vector_field =
                    dc.visual_kind == SkyVisualKind::VectorField;
                const auto* field_texture = needs_texture
                    ? wz::gpu::dx12::internal::get_scalar_field_texture(
                        device,
                        wz::gpu::GPUHandle{
                            .id = dc.texture_handle,
                            .epoch = 1,
                            .type = wz::gpu::GPUResourceType::Texture,
                        })
                    : (needs_scalar_field
                    ? wz::gpu::dx12::internal::get_scalar_field_texture(
                        device,
                        wz::gpu::GPUHandle{
                            .id = dc.scalar_field_handle,
                            .epoch = 1,
                            .type = wz::gpu::GPUResourceType::Texture,
                        })
                    : (needs_vector_field
                        ? wz::gpu::dx12::internal::get_scalar_field_texture(
                            device,
                            wz::gpu::GPUHandle{
                                .id = dc.vector_field_handle,
                                .epoch = 1,
                                .type = wz::gpu::GPUResourceType::Texture,
                            })
                        : nullptr));
                if (needs_texture && !field_texture) {
                    if (!logged_texture_missing) {
                        OutputDebugStringA(
                            "[scene_editor] sky texture skipped: texture lookup failed\n");
                        logged_texture_missing = true;
                    }
                    continue;
                }
                if (needs_scalar_field && !field_texture) {
                    if (!logged_scalar_missing) {
                        OutputDebugStringA(
                            "[scene_editor] sky scalar skipped: texture lookup failed\n");
                        logged_scalar_missing = true;
                    }
                    continue;
                }
                if (needs_vector_field && !field_texture) {
                    if (!logged_vector_missing) {
                        OutputDebugStringA(
                            "[scene_editor] sky vector skipped: texture lookup failed\n");
                        logged_vector_missing = true;
                    }
                    continue;
                }

                const float exposure = (std::max)(0.0f, dc.exposure);
                if (pipeline_cache)
                {
                    const auto pipeline_handle = pipeline_cache->get(
                        wz::engine::assets::BuiltinRenderProgram::SkySurface);
                    const auto* pl = wz::gpu::dx12::internal::get_graphics_pipeline(
                        device,
                        pipeline_handle);

                    if (pl && pl->valid())
                    {
                        const float constants[28] = {
                            dc.solid_color.x,
                            dc.solid_color.y,
                            dc.solid_color.z,
                            exposure,
                            dc.gradient_top_color.x,
                            dc.gradient_top_color.y,
                            dc.gradient_top_color.z,
                            0.0f,
                            dc.gradient_bottom_color.x,
                            dc.gradient_bottom_color.y,
                            dc.gradient_bottom_color.z,
                            0.0f,
                            static_cast<float>(static_cast<uint32_t>(dc.visual_kind)),
                            static_cast<float>(static_cast<uint32_t>(dc.projection)),
                            dc.rotation_x_radians,
                            dc.rotation_y_radians,
                            dc.rotation_z_radians,
                            view.m[0],
                            view.m[4],
                            view.m[8],
                            view.m[1],
                            view.m[5],
                            view.m[9],
                            0.0f,
                            view.m[2],
                            view.m[6],
                            view.m[10],
                            0.0f,
                        };

                        cmdList->SetGraphicsRootSignature(pl->root_sig);
                        cmdList->SetPipelineState(pl->pso);
                        cmdList->IASetPrimitiveTopology(
                            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        if (needs_texture || needs_scalar_field || needs_vector_field) {
                            ID3D12DescriptorHeap* heap =
                                wz::gpu::dx12::internal
                                    ::get_scalar_field_srv_heap(device);
                            if (!heap) {
                                continue;
                            }
                            cmdList->SetDescriptorHeaps(1, &heap);
                            cmdList->SetGraphicsRootDescriptorTable(
                                1,
                                field_texture->srv_gpu);
                            if (needs_texture && !logged_texture_bound) {
                                OutputDebugStringA(
                                    "[scene_editor] sky texture bound\n");
                                logged_texture_bound = true;
                            }
                            if (needs_scalar_field && !logged_scalar_bound) {
                                OutputDebugStringA(
                                    "[scene_editor] sky scalar texture bound\n");
                                logged_scalar_bound = true;
                            }
                            if (needs_vector_field && !logged_vector_bound) {
                                OutputDebugStringA(
                                    "[scene_editor] sky vector texture bound\n");
                                logged_vector_bound = true;
                            }
                        }
                        cmdList->SetGraphicsRoot32BitConstants(
                            0,
                            28,
                            constants,
                            0);
                        cmdList->DrawInstanced(3, 1, 0, 0);
                        break;
                    }
                }

                if (dc.visual_kind != SkyVisualKind::SolidColor) {
                    continue;
                }

                const float color[4] = {
                    (std::clamp)(dc.solid_color.x * exposure, 0.0f, 1.0f),
                    (std::clamp)(dc.solid_color.y * exposure, 0.0f, 1.0f),
                    (std::clamp)(dc.solid_color.z * exposure, 0.0f, 1.0f),
                    1.0f,
                };
                const auto rtv =
                    wz::gpu::dx12::internal::get_current_rtv(device);
                cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
                break;
            }
        }
    }

    Context* create(
    wz::gpu::Device& device,
    const TrianglePipelineDesc& tri_desc)
{
    assert(tri_desc.valid());

    ID3D12Device* dev = wz::gpu::dx12::internal::get_device(device);

    Context* ctx = new Context();
    ctx->device = &device;

    ctx->root_sig =
        wz::gpu::dx12::internal::create_empty_root_signature(dev);

    ctx->pso = wz::gpu::dx12::internal::create_triangle_pso(
        device,
        ctx->root_sig,
        tri_desc.vertex_shader,
        tri_desc.pixel_shader
    );



        char buf[128];
        sprintf_s(buf, "  PSO created: %p\n", ctx->pso);
        OutputDebugStringA(buf);
        assert(ctx->pso);

        struct Vertex { float x, y, z; };

        Vertex tri[3] =
        {
            {  0.0f,  0.5f, 0.0f },
            {  0.5f, -0.5f, 0.0f },
            { -0.5f, -0.5f, 0.0f }
        };

        const UINT vb_size = sizeof(tri);

        // ────── vertex buffer ──────
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC vb_desc = {};
        vb_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vb_desc.Width = vb_size;
        vb_desc.Height = 1;
        vb_desc.DepthOrArraySize = 1;
        vb_desc.MipLevels = 1;
        vb_desc.SampleDesc.Count = 1;
        vb_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = dev->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &vb_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&ctx->vertex_buffer)
        );
        assert(SUCCEEDED(hr));

        void* mapped = nullptr;
        ctx->vertex_buffer->Map(0, nullptr, &mapped);
        memcpy(mapped, tri, vb_size);
        ctx->vertex_buffer->Unmap(0, nullptr);

        ctx->vb_view.BufferLocation = ctx->vertex_buffer->GetGPUVirtualAddress();
        ctx->vb_view.StrideInBytes = sizeof(Vertex);
        ctx->vb_view.SizeInBytes = vb_size;

        // ────── mesh table ──────
        ctx->mesh_table.resize(1);

        GpuMesh mesh{};
        mesh.vertex_buffer = ctx->vertex_buffer;
        mesh.index_buffer = nullptr;          // IMPORTANT: no IB yet
        mesh.vb_view = ctx->vb_view;
        mesh.ib_view = {};                    // unused
        mesh.index_count = 3;

        ctx->mesh_table[0] = mesh;

        assert(ctx->root_sig);
        assert(ctx->pso);
        assert(ctx->vertex_buffer);


        return ctx;
    }

    void submit(Context* ctx, const RenderFrameView& frame)
    {
        assert(ctx);
        assert(ctx->device);
        assert(ctx->device->impl);

        //{
        //    char buf[128];
        //    sprintf_s(
        //        buf,
        //        "RenderFrame submit: commands=%zu\n",
        //        frame.commands.size()
        //    );
        //    OutputDebugStringA(buf);
        //}

        auto* cmdList =
            wz::gpu::dx12::internal::get_command_list(*ctx->device);

        submit_sky_pass(*ctx->device, frame.view.view, frame.sky, nullptr);

        cmdList->SetGraphicsRootSignature(ctx->root_sig);
        cmdList->SetPipelineState(ctx->pso);

        cmdList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        );

        struct
        {
            Mat4 world;
            Mat4 view_proj;
            float style_color[4];
            float style_params[4];
        } data;

        for (const DrawCommand& dc : frame.opaque)
        {


            if (dc.mesh >= ctx->mesh_table.size())
                continue;

            const auto& mesh = ctx->mesh_table[dc.mesh];

            cmdList->IASetVertexBuffers(0, 1, &mesh.vb_view);

            data.world = dc.world;
            data.view_proj = frame.view.view_projection;
            data.style_color[0] = 0.0f;
            data.style_color[1] = 1.0f;
            data.style_color[2] = 0.15f;
            data.style_color[3] = 1.0f;
            data.style_params[0] = 1.0f;
            data.style_params[1] = 0.0f;
            data.style_params[2] = 0.0f;
            data.style_params[3] = 0.0f;

            cmdList->SetGraphicsRoot32BitConstants(
                0,
                40,
                &data,
                0
            );

            if (mesh.index_buffer)
            {
                cmdList->IASetIndexBuffer(&mesh.ib_view);
                cmdList->DrawIndexedInstanced(mesh.index_count, 1, 0, 0, 0);
            }
            else
            {
                OutputDebugStringA("Drawing opaque debug mesh\n");
                cmdList->DrawInstanced(mesh.index_count, 1, 0, 0);
            }
        }
    }

    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver)
    {
        resolver.reset_terrain_render_stats();
        auto* cmdList = wz::gpu::dx12::internal::get_command_list(device);

        submit_sky_pass(device, frame.view.view, frame.sky, nullptr);

        // ── Opaque mesh pass (resolver path) ──────────────────────────────────

        if (!frame.opaque.empty())
        {
            const auto mesh_pipeline =
                wz::gpu::dx12::internal::get_mesh_wireframe_pipeline(device);

            if (mesh_pipeline.valid())
            {
                cmdList->SetGraphicsRootSignature(mesh_pipeline.root_sig);
                cmdList->SetPipelineState(mesh_pipeline.pso);
                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                float constants[40] = {};

                for (const DrawCommand& dc : frame.opaque)
                {
                    if (dc.kind != DrawCommandKind::Mesh)
                        continue;
                    if (dc.mesh == INVALID_MESH)
                        continue;

                    const auto resolved = resolver.resolve_mesh(dc.mesh);
                    if (!resolved)
                        continue;

                    const auto* mesh =
                        wz::gpu::dx12::internal::get_mesh(device, resolved->gpu_resource);
                    if (!mesh || !mesh->vertex_buffer)
                        continue;

                    for (int i = 0; i < 16; ++i) {
                        constants[i] = dc.world.m[i];
                        constants[16 + i] = frame.view.view_projection.m[i];
                    }
                    write_mesh_wireframe_style_constants(
                        constants,
                        resolved->mesh_style);

                    cmdList->SetGraphicsRoot32BitConstants(0, 40, constants, 0);
                    cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
                    cmdList->IASetIndexBuffer(&mesh->index_view);
                    cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
                }
            }
        }

        // ── Splat pass (resolver path) ────────────────────────────────────────

        if (frame.splats.empty())
            return;

        const auto pipeline =
            wz::gpu::dx12::internal::get_gaussian_splat_debug_pipeline(device);

        if (!pipeline.valid())
            return;

        cmdList->SetGraphicsRootSignature(pipeline.root_sig);
        cmdList->SetPipelineState(pipeline.pso);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        const float vp_w = static_cast<float>(
            wz::gpu::dx12::internal::get_width(device));
        const float vp_h = static_cast<float>(
            wz::gpu::dx12::internal::get_height(device));

        for (const DrawCommand& dc : frame.splats)
        {
            if (dc.kind != DrawCommandKind::GaussianSplats)
                continue;
            if (dc.splats_buffer == INVALID_SPLAT)
                continue;

            const auto resolved = resolver.resolve_splats(dc.splats_buffer);
            if (!resolved)
                continue;

            const auto* cloud =
                wz::gpu::dx12::internal::get_gaussian_splat_cloud(
                    device, resolved->gpu_resource);
            if (!cloud || !cloud->vertex_buffer)
                continue;

            // world[16], view_proj[16], viewport_and_size[4] — matches
            // the gaussian splat debug root signature (36 x 32-bit constants).
            float constants[36] = {};
            for (int i = 0; i < 16; ++i) constants[i]      = dc.world.m[i];
            for (int i = 0; i < 16; ++i) constants[16 + i] =
                frame.view.view_projection.m[i];
            constants[32] = vp_w;
            constants[33] = vp_h;
            constants[34] = 8.0f;  // base splat size in pixels
            constants[35] = 0.0f;

            cmdList->SetGraphicsRoot32BitConstants(0, 36, constants, 0);
            cmdList->IASetVertexBuffers(0, 1, &cloud->vertex_view);
            cmdList->DrawInstanced(4, cloud->splat_count, 0, 0);
        }

    }

    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver,
                const wz::engine::rendering::RenderablePipelineCache& pipeline_cache)
    {
        resolver.reset_terrain_render_stats();
        auto* cmdList = wz::gpu::dx12::internal::get_command_list(device);

        submit_sky_pass(device, frame.view.view, frame.sky, &pipeline_cache);

        // ── Opaque mesh pass ──────────────────────────────────────────────────

        for (const DrawCommand& dc : frame.opaque)
        {
            if (dc.kind != DrawCommandKind::Mesh)
                continue;
            if (dc.mesh == INVALID_MESH)
                continue;

            const auto resolved = resolver.resolve_mesh(dc.mesh);
            if (!resolved)
                continue;

            const auto pipeline_handle = pipeline_cache.get(resolved->program);
            const auto* pl = wz::gpu::dx12::internal::get_graphics_pipeline(
                device, pipeline_handle);
            if (!pl || !pl->valid())
                continue;

            const auto* mesh = wz::gpu::dx12::internal::get_mesh(
                device, resolved->gpu_resource);
            if (!mesh || !mesh->vertex_buffer)
                continue;

            float constants[48] = {};
            for (int i = 0; i < 16; ++i) constants[i] = dc.world.m[i];
            for (int i = 0; i < 16; ++i) {
                constants[16 + i] = frame.view.view_projection.m[i];
            }

            if (is_terrain_surface_program(resolved->program))
                continue;
            const bool mesh_wireframe =
                is_mesh_wireframe_program(resolved->program);
            const bool mesh_surface =
                is_mesh_surface_program(resolved->program);
            const bool mesh_field_heatmap =
                is_mesh_field_heatmap_program(resolved->program);
            const bool mesh_mask =
                is_mesh_mask_program(resolved->program);
            uint32_t mesh_mask_rule_count = 0u;
            uint32_t mesh_mask_elements = 0u;
            if (mesh_mask) {
                const uint32_t fallback_element_count =
                    resolved->mesh_style.mask.domain
                            == wz::engine::assets::MeshMaskDomain::Vertex
                        ? mesh->vertex_count
                        : mesh->index_count / 3u;
                if (!prepare_builtin_mesh_mask_resources(
                        device,
                        *resolved,
                        fallback_element_count,
                        mesh_mask_rule_count,
                        mesh_mask_elements))
                {
                    continue;
                }
            }
            if (mesh_wireframe) {
                write_mesh_wireframe_style_constants(
                    constants,
                    resolved->mesh_style);
            }
            else if (mesh_surface) {
                write_mesh_surface_style_constants(
                    constants,
                    resolved->mesh_style);
            }
            else if (mesh_mask) {
                write_mesh_mask_style_constants(
                    constants,
                    resolved->mesh_style,
                    mesh_mask_rule_count,
                    mesh_mask_elements);
            }
            else if (mesh_field_heatmap) {
                write_mesh_field_heatmap_style_constants(
                    constants,
                    resolved->mesh_style);
            }

            if (resolved->program
                == BuiltinRenderProgram::MeshWireframeDepthDebug
                && mesh_wireframe_wants_prepass(resolved->mesh_style))
            {
                const auto prepass_handle = pipeline_cache.get(
                    BuiltinRenderProgram::MeshDepthPrepassDebug);
                const auto* prepass =
                    wz::gpu::dx12::internal::get_graphics_pipeline(
                        device,
                        prepass_handle);

                if (prepass && prepass->valid()) {
                    cmdList->SetGraphicsRootSignature(prepass->root_sig);
                    cmdList->SetPipelineState(prepass->pso);
                    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    cmdList->SetGraphicsRoot32BitConstants(0, 40, constants, 0);
                    cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
                    cmdList->IASetIndexBuffer(&mesh->index_view);
                    cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
                }
            }

            cmdList->SetGraphicsRootSignature(pl->root_sig);
            cmdList->SetPipelineState(pl->pso);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->SetGraphicsRoot32BitConstants(
                0,
                root_constant_count_for_program(resolved->program),
                constants,
                0);
            if (!bind_builtin_mesh_field_heatmap_resource(
                    device,
                    cmdList,
                    *resolved))
            {
                continue;
            }
            uint32_t bound_rule_count = 0u;
            uint32_t bound_element_count = 0u;
            const uint32_t fallback_element_count =
                resolved->mesh_style.mask.domain
                        == wz::engine::assets::MeshMaskDomain::Vertex
                    ? mesh->vertex_count
                    : mesh->index_count / 3u;
            if (!bind_builtin_mesh_mask_resources(
                    device,
                    cmdList,
                    *resolved,
                    fallback_element_count,
                    bound_rule_count,
                    bound_element_count))
            {
                continue;
            }
            cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
            cmdList->IASetIndexBuffer(&mesh->index_view);
            cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);

            if (mesh_surface || mesh_field_heatmap || mesh_mask) {
                draw_mesh_surface_wireframe_overlay(
                    device,
                    pipeline_cache,
                    *mesh,
                    constants,
                    resolved->mesh_style);
            }
        }

        // ── Splat pass ────────────────────────────────────────────────────────

        submit_terrain_refs(
            device,
            frame,
            resolver,
            pipeline_cache,
            nullptr);

        const float vp_w = static_cast<float>(
            wz::gpu::dx12::internal::get_width(device));
        const float vp_h = static_cast<float>(
            wz::gpu::dx12::internal::get_height(device));

        for (const DrawCommand& dc : frame.splats)
        {
            if (dc.kind != DrawCommandKind::GaussianSplats)
                continue;
            if (dc.splats_buffer == INVALID_SPLAT)
                continue;

            const auto resolved = resolver.resolve_splats(dc.splats_buffer);
            if (!resolved)
                continue;

            const auto pipeline_handle = pipeline_cache.get(resolved->program);
            const auto* pl = wz::gpu::dx12::internal::get_graphics_pipeline(
                device, pipeline_handle);
            if (!pl || !pl->valid())
                continue;

            const auto* cloud = wz::gpu::dx12::internal::get_gaussian_splat_cloud(
                device, resolved->gpu_resource);
            if (!cloud || !cloud->vertex_buffer)
                continue;

            float constants[36] = {};
            for (int i = 0; i < 16; ++i) constants[i]      = dc.world.m[i];
            for (int i = 0; i < 16; ++i) constants[16 + i] =
                frame.view.view_projection.m[i];
            constants[32] = vp_w;
            constants[33] = vp_h;
            constants[34] = 0.01f;
            constants[35] = 0.0f;

            cmdList->SetGraphicsRootSignature(pl->root_sig);
            cmdList->SetPipelineState(pl->pso);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            cmdList->SetGraphicsRoot32BitConstants(0, 36, constants, 0);
            cmdList->IASetVertexBuffers(0, 1, &cloud->vertex_view);
            cmdList->DrawInstanced(4, cloud->splat_count, 0, 0);
        }

        submit_mesh_commands(
            device,
            frame.transparent,
            frame,
            resolver,
            pipeline_cache,
            nullptr);
    }

    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver,
                const wz::engine::rendering::RenderablePipelineCache& pipeline_cache,
                const wz::engine::rendering::RenderProgramPipelineCache& render_program_cache)
    {
        resolver.reset_terrain_render_stats();
        auto* cmdList = wz::gpu::dx12::internal::get_command_list(device);

        submit_sky_pass(device, frame.view.view, frame.sky, &pipeline_cache);

        const float vp_w = static_cast<float>(
            wz::gpu::dx12::internal::get_width(device));
        const float vp_h = static_cast<float>(
            wz::gpu::dx12::internal::get_height(device));

        auto resolve_pipeline = [&](
            const wz::engine::rendering::ResolvedRenderableResource& resolved)
            -> const wz::gpu::dx12::internal::DX12GraphicsPipeline*
        {
            wz::gpu::GPUHandle pipeline_handle;
            if (resolved.render_program.valid())
                pipeline_handle = render_program_cache.get(resolved.render_program);
            else
                pipeline_handle = pipeline_cache.get(resolved.program);

            return wz::gpu::dx12::internal::get_graphics_pipeline(
                device, pipeline_handle);
        };

        // ── Opaque mesh pass ──────────────────────────────────────────────────

        for (const DrawCommand& dc : frame.opaque)
        {
            if (dc.kind != DrawCommandKind::Mesh) continue;
            if (dc.mesh == INVALID_MESH)          continue;

            const auto resolved = resolver.resolve_mesh(dc.mesh);
            if (!resolved) continue;

            const auto* pl = resolve_pipeline(*resolved);
            if (!pl || !pl->valid()) continue;

            const auto* mesh = wz::gpu::dx12::internal::get_mesh(
                device, resolved->gpu_resource);
            if (!mesh || !mesh->vertex_buffer) continue;

            float constants[48] = {};
            for (int i = 0; i < 16; ++i) constants[i] = dc.world.m[i];
            for (int i = 0; i < 16; ++i) {
                constants[16 + i] = frame.view.view_projection.m[i];
            }

            if (is_terrain_surface_program(resolved->program))
                continue;
            const bool mesh_wireframe =
                is_mesh_wireframe_program(resolved->program);
            const bool mesh_surface =
                is_mesh_surface_program(resolved->program);
            const bool mesh_field_heatmap =
                is_mesh_field_heatmap_program(resolved->program);
            const bool mesh_mask =
                is_mesh_mask_program(resolved->program);
            uint32_t mesh_mask_rule_count = 0u;
            uint32_t mesh_mask_elements = 0u;
            if (mesh_mask) {
                const uint32_t fallback_element_count =
                    resolved->mesh_style.mask.domain
                            == wz::engine::assets::MeshMaskDomain::Vertex
                        ? mesh->vertex_count
                        : mesh->index_count / 3u;
                if (!prepare_builtin_mesh_mask_resources(
                        device,
                        *resolved,
                        fallback_element_count,
                        mesh_mask_rule_count,
                        mesh_mask_elements))
                {
                    continue;
                }
            }
            if (mesh_wireframe) {
                write_mesh_wireframe_style_constants(
                    constants,
                    resolved->mesh_style);
            }
            else if (mesh_surface) {
                write_mesh_surface_style_constants(
                    constants,
                    resolved->mesh_style);
            }
            else if (mesh_mask) {
                write_mesh_mask_style_constants(
                    constants,
                    resolved->mesh_style,
                    mesh_mask_rule_count,
                    mesh_mask_elements);
            }
            else if (mesh_field_heatmap) {
                write_mesh_field_heatmap_style_constants(
                    constants,
                    resolved->mesh_style);
            }

            if (resolved->program
                == BuiltinRenderProgram::MeshWireframeDepthDebug
                && mesh_wireframe_wants_prepass(resolved->mesh_style))
            {
                const auto prepass_handle = pipeline_cache.get(
                    BuiltinRenderProgram::MeshDepthPrepassDebug);
                const auto* prepass =
                    wz::gpu::dx12::internal::get_graphics_pipeline(
                        device,
                        prepass_handle);

                if (prepass && prepass->valid()) {
                    cmdList->SetGraphicsRootSignature(prepass->root_sig);
                    cmdList->SetPipelineState(prepass->pso);
                    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    cmdList->SetGraphicsRoot32BitConstants(0, 40, constants, 0);
                    cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
                    cmdList->IASetIndexBuffer(&mesh->index_view);
                    cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
                }
            }

            cmdList->SetGraphicsRootSignature(pl->root_sig);
            cmdList->SetPipelineState(pl->pso);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->SetGraphicsRoot32BitConstants(
                0,
                root_constant_count_for_program(resolved->program),
                constants,
                0);
            if (!bind_custom_render_program_descriptor_resources(
                    device,
                    cmdList,
                    *resolved,
                    &render_program_cache))
            {
                continue;
            }
            if (!resolved->render_program.valid()
                && !bind_builtin_mesh_field_heatmap_resource(
                    device,
                    cmdList,
                    *resolved))
            {
                continue;
            }
            if (!resolved->render_program.valid()) {
                uint32_t bound_rule_count = 0u;
                uint32_t bound_element_count = 0u;
                const uint32_t fallback_element_count =
                    resolved->mesh_style.mask.domain
                            == wz::engine::assets::MeshMaskDomain::Vertex
                        ? mesh->vertex_count
                        : mesh->index_count / 3u;
                if (!bind_builtin_mesh_mask_resources(
                        device,
                        cmdList,
                        *resolved,
                        fallback_element_count,
                        bound_rule_count,
                        bound_element_count))
                {
                    continue;
                }
            }
            cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
            cmdList->IASetIndexBuffer(&mesh->index_view);
            cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);

            if (mesh_surface || mesh_field_heatmap || mesh_mask) {
                draw_mesh_surface_wireframe_overlay(
                    device,
                    pipeline_cache,
                    *mesh,
                    constants,
                    resolved->mesh_style);
            }
        }

        // ── Splat pass ────────────────────────────────────────────────────────

        submit_terrain_refs(
            device,
            frame,
            resolver,
            pipeline_cache,
            &render_program_cache);

        for (const DrawCommand& dc : frame.splats)
        {
            if (dc.kind != DrawCommandKind::GaussianSplats) continue;
            if (dc.splats_buffer == INVALID_SPLAT)          continue;

            const auto resolved = resolver.resolve_splats(dc.splats_buffer);
            if (!resolved) continue;

            const auto* pl = resolve_pipeline(*resolved);
            if (!pl || !pl->valid()) continue;

            const auto* cloud = wz::gpu::dx12::internal::get_gaussian_splat_cloud(
                device, resolved->gpu_resource);
            if (!cloud) continue;

            // Constants buffer sized to fit all splat programs:
            //   PullDebug                       — reads [0..35]   (36 dwords)
            //   NeighborhoodColorBlend         — reads [0..47]   (48 dwords)
            //   GaussianSplatTerrainCoverageDebug — reads [0..59] (60 dwords)
            // The actual count pushed is driven by `value_count`, so each
            // program sees only its declared range.  Slot meanings beyond
            // [0..35]:
            //   [36..39] NeighborhoodColorBlend.lod_params0
            //   [40..43] NeighborhoodColorBlend.lod_params1
            //   [44..47] NeighborhoodColorBlend.lod_pad
            //   [48..51] coverage_params0 (mode, threshold, opacity_scale, kernel_mode)
            //   [52..55] coverage_params1 (radius_scale, inner_r, outer_r, gaussian_falloff)
            //   [56..59] coverage_params2 (min_screen_radius_px, _, _, _)
            float constants[60] = {};
            for (int i = 0; i < 16; ++i) constants[i]      = dc.world.m[i];
            for (int i = 0; i < 16; ++i) constants[16 + i] =
                frame.view.view_projection.m[i];
            constants[32] = vp_w;
            constants[33] = vp_h;
            constants[34] = 0.01f;
            constants[35] = 0.0f;

            // LOD slots (consumed by NeighborhoodColorBlend; ignored by
            // PullDebug). [36..39] = mode, strength, near, far.
            // [40..43] = stride_ratio, max_stride, use_confidence, pad.
            // [44..47] reserved/pad.
            {
                const auto& lod = wz::gpu::dx12::internal::get_lod_settings(device);
                constants[36] = static_cast<float>(static_cast<uint32_t>(lod.mode));
                constants[37] = lod.strength;
                constants[38] = lod.near_distance;
                constants[39] = lod.far_distance;

                const uint32_t total = cloud->splat_count;
                const uint32_t rendered =
                    dc.splat_instance_count > 0
                        ? dc.splat_instance_count
                        : total;
                const float stride_ratio = (total > 0)
                    ? static_cast<float>(rendered) / static_cast<float>(total)
                    : 1.0f;
                constants[40] = stride_ratio;
                constants[41] = lod.max_stride_for_blend;
                constants[42] = lod.use_confidence ? 1.0f : 0.0f;
                constants[43] = 0.0f;
            }

            // Coverage slots [48..59] (consumed by
            // GaussianSplatTerrainCoverageDebug; ignored by others).
            {
                const auto& cov = wz::gpu::dx12::internal::get_coverage_settings(device);
                constants[48] = static_cast<float>(static_cast<uint32_t>(cov.mode));
                constants[49] = cov.threshold;
                constants[50] = cov.opacity_scale;
                constants[51] = static_cast<float>(static_cast<uint32_t>(cov.kernel_mode));

                constants[52] = cov.radius_scale;
                constants[53] = cov.inner_radius;
                constants[54] = cov.outer_radius;
                constants[55] = cov.gaussian_falloff;

                constants[56] = cov.min_screen_radius_px;
                constants[57] = static_cast<float>(
                    static_cast<uint32_t>(cov.debug_view));
                constants[58] = 0.0f;
                constants[59] = 0.0f;
            }

            // Determine binding model.  Default to SplatVertexInstanced when no
            // render-program handle is attached.  If a valid handle is present but
            // absent from the cache, the pipeline was never realized — skip rather
            // than silently misrouting to a wrong binding path.
            wz::engine::assets::RenderBindingModel binding_model =
                wz::engine::assets::RenderBindingModel::SplatVertexInstanced;

            if (resolved->render_program.valid())
            {
                const auto maybe = render_program_cache.get_binding_model(
                    resolved->render_program);
                if (!maybe.has_value())
                    continue;   // valid handle, but pipeline was never realized
                binding_model = *maybe;
            }

            if (binding_model == wz::engine::assets::RenderBindingModel::SplatPull)
            {
                if (!cloud->valid_for_splat_pull()) continue;

                auto* srv_heap =
                    wz::gpu::dx12::internal::get_srv_cbv_uav_heap(device);
                if (!srv_heap) continue;

                const auto* layout = render_program_cache.get_binding_layout(
                    resolved->render_program);
                if (!layout || !layout->valid()) continue;

                // Upload externally-computed sorted indices if provided.
                // Empty span = keep existing t1 buffer (identity or prior sort).
                if (!dc.sorted_splat_indices.empty())
                    wz::gpu::dx12::internal::update_sorted_indices(
                        *cloud, dc.sorted_splat_indices);

                // Use the visible instance count from the DrawCommand when
                // available (back-to-front sorted subset); fall back to the
                // full cloud count for identity/legacy draws.
                const uint32_t instance_count =
                    dc.splat_instance_count > 0
                        ? dc.splat_instance_count
                        : cloud->splat_count;

                cmdList->SetDescriptorHeaps(1, &srv_heap);
                cmdList->SetGraphicsRootSignature(pl->root_sig);
                cmdList->SetPipelineState(pl->pso);
                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

                // Root constants — iterate declared bindings; no hardcoded index.
                for (const auto& rc : layout->root_constants)
                    cmdList->SetGraphicsRoot32BitConstants(
                        rc.root_parameter_index, rc.value_count, constants, 0);

                // Descriptor tables — one SetGraphicsRootDescriptorTable per
                // visibility group; heap_start_slot offsets into srv_table.
                for (const auto& dt : layout->desc_tables)
                    cmdList->SetGraphicsRootDescriptorTable(
                        dt.root_parameter_index,
                        cloud->srv_table.gpu_at(dt.heap_start_slot));

                cmdList->DrawInstanced(4, instance_count, 0, 0);
            }
            else
            {
                if (!cloud->valid_for_vertex_instanced()) continue;

                cmdList->SetGraphicsRootSignature(pl->root_sig);
                cmdList->SetPipelineState(pl->pso);
                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                cmdList->SetGraphicsRoot32BitConstants(0, 36, constants, 0);
                cmdList->IASetVertexBuffers(0, 1, &cloud->vertex_view);
                cmdList->DrawInstanced(4, cloud->splat_count, 0, 0);
            }
        }

        submit_mesh_commands(
            device,
            frame.transparent,
            frame,
            resolver,
            pipeline_cache,
            &render_program_cache);
    }

    void destroy(Context* ctx)
    {
        if (!ctx) return;

        OutputDebugStringA("dx12::destroy called\n");

        if (ctx->vertex_buffer)
        {
            OutputDebugStringA("  releasing vertex_buffer\n");
            ctx->vertex_buffer->Release();
            ctx->vertex_buffer = nullptr;
        }

        if (ctx->pso)
        {
            char buf[128];
            sprintf_s(buf, "  releasing pso: %p\n", ctx->pso);
            OutputDebugStringA(buf);
            ctx->pso->Release();
            ctx->pso = nullptr;

        }

        if (ctx->root_sig)
        {
            OutputDebugStringA("  releasing root_sig\n");
            ctx->root_sig->Release();
            ctx->root_sig = nullptr;
        }

        delete ctx;
    }
}
