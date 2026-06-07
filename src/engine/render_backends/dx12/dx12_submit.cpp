// file: src/engine/render_backends/dx12/dx12_submit.cpp

#include <d3d12.h>

#include <engine/render_backends/dx12/dx12_submit.h>
#include <gpu/dx12/dx12_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace wz::render::backend::dx12
{
    namespace
    {
        using wz::engine::assets::BuiltinRenderProgram;
        using wz::math::Mat4;

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

        bool draw_terrain_ref_mesh(
            wz::gpu::Device& device,
            ID3D12GraphicsCommandList* cmdList,
            const TerrainDrawRef& ref,
            const TerrainVisualInstance& instance,
            const RenderFrameView& frame,
            const wz::engine::rendering::RenderablePipelineCache& pipeline_cache,
            const wz::engine::rendering::RenderResourceResolver& resolver,
            const wz::engine::rendering::RenderProgramPipelineCache* render_program_cache)
        {
            if (ref.representation_kind
                != wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks)
            {
                return false;
            }

            const auto resolved =
                resolver.resolve_terrain_draw(instance.terrain_proxy_id, ref);
            if (!resolved || !is_terrain_surface_program(resolved->program))
                return false;

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
            if (!mesh || !mesh->vertex_buffer)
                return false;

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

            cmdList->SetGraphicsRootSignature(pl->root_sig);
            cmdList->SetPipelineState(pl->pso);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->SetGraphicsRoot32BitConstants(
                0,
                root_constant_count_for_program(resolved->program),
                constants,
                0);
            cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
            cmdList->IASetIndexBuffer(&mesh->index_view);
            cmdList->DrawIndexedInstanced(
                resolved->index_count,
                1,
                resolved->first_index,
                0,
                0);

            resolver.record_terrain_render_stats(
                1u,
                1u,
                resolved->source_triangle_count,
                resolved->index_count / 3u,
                0u,
                0u,
                resolved->lod_replacement_available ? 1u : 0u,
                resolved->lod_replacement_available
                    ? resolved->source_triangle_count
                    : 0u,
                resolved->lod_replacement_selected ? 1u : 0u,
                resolved->lod_replacement_selected
                    ? resolved->index_count / 3u
                    : 0u,
                0u,
                0u,
                resolved->terrain_target_pixels_per_triangle);
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

                draw_terrain_ref_mesh(
                    device,
                    cmdList,
                    ref,
                    instance,
                    frame,
                    pipeline_cache,
                    resolver,
                    render_program_cache);
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

        UINT root_constant_count_for_program(BuiltinRenderProgram program)
        {
            if (is_terrain_surface_program(program)) {
                return 48;
            }

            if (is_mesh_wireframe_program(program)
                || is_mesh_surface_program(program))
            {
                return 40;
            }

            return 32;
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

            const bool transparent =
                wz::engine::assets::is_mesh_render_style_transparent(style);
            const auto program = transparent
                ? BuiltinRenderProgram::MeshWireframeAlpha
                : style.depth_test || style.depth_write
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
            if (!transparent && mesh_wireframe_wants_prepass(style)) {
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
            write_mesh_wireframe_style_constants(constants, style);
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
                cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
                cmdList->IASetIndexBuffer(&mesh->index_view);
                cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);

                if (mesh_surface) {
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
            cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
            cmdList->IASetIndexBuffer(&mesh->index_view);
            cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);

            if (mesh_surface) {
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
            cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
            cmdList->IASetIndexBuffer(&mesh->index_view);
            cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);

            if (mesh_surface) {
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
