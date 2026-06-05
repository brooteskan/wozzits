// src/engine/rendering/gpu_scene_render_resource_resolver.cpp

#include <engine/rendering/gpu_scene_render_resource_resolver.h>

#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/terrain_asset_module.h>
#include <gpu/mesh.h>

#include <algorithm>
#include <span>

namespace wz::engine::rendering
{
    namespace
    {
        wz::engine::assets::MeshData make_heightfield_preview_mesh(
            const wz::engine::assets::TerrainAssetData& terrain,
            const wz::engine::assets::ScalarFieldData& field)
        {
            wz::engine::assets::MeshData mesh{};

            if (field.width < 2 || field.height < 2 || field.values.empty()) {
                return mesh;
            }

            constexpr uint32_t kMaxPreviewResolution = 129;
            const uint32_t step_x = std::max(
                1u,
                (field.width + kMaxPreviewResolution - 1u)
                    / kMaxPreviewResolution);
            const uint32_t step_y = std::max(
                1u,
                (field.height + kMaxPreviewResolution - 1u)
                    / kMaxPreviewResolution);
            const uint32_t preview_w =
                1u + (field.width - 1u + step_x - 1u) / step_x;
            const uint32_t preview_h =
                1u + (field.height - 1u + step_y - 1u) / step_y;

            mesh.vertices.reserve(
                static_cast<size_t>(preview_w) * preview_h);
            mesh.indices.reserve(
                static_cast<size_t>(preview_w - 1u)
                * (preview_h - 1u)
                * 6u);

            for (uint32_t y = 0; y < preview_h; ++y) {
                const uint32_t src_y =
                    std::min(y * step_y, field.height - 1u);
                const float v = field.height > 1
                    ? static_cast<float>(src_y)
                        / static_cast<float>(field.height - 1u)
                    : 0.0f;

                for (uint32_t x = 0; x < preview_w; ++x) {
                    const uint32_t src_x =
                        std::min(x * step_x, field.width - 1u);
                    const float u = field.width > 1
                        ? static_cast<float>(src_x)
                            / static_cast<float>(field.width - 1u)
                        : 0.0f;
                    const size_t sample_index =
                        static_cast<size_t>(src_y) * field.width + src_x;
                    const float sample = sample_index < field.values.size()
                        ? field.values[sample_index]
                        : 0.0f;

                    wz::engine::assets::MeshVertex vertex{};
                    vertex.position[0] = terrain.origin[0] + u * terrain.size[0];
                    vertex.position[1] =
                        terrain.base_height + sample * terrain.vertical_scale;
                    vertex.position[2] = terrain.origin[1] + v * terrain.size[1];
                    vertex.normal[1] = 1.0f;
                    vertex.uv[0] = u;
                    vertex.uv[1] = v;
                    mesh.vertices.push_back(vertex);
                }
            }

            for (uint32_t y = 0; y + 1u < preview_h; ++y) {
                for (uint32_t x = 0; x + 1u < preview_w; ++x) {
                    const uint32_t i0 = y * preview_w + x;
                    const uint32_t i1 = i0 + 1u;
                    const uint32_t i2 = i0 + preview_w;
                    const uint32_t i3 = i2 + 1u;
                    mesh.indices.push_back(i0);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i3);
                }
            }

            return mesh;
        }

        wz::engine::assets::MeshData make_terrain_surface_mesh(
            const wz::engine::assets::TerrainAssetData& terrain,
            const wz::engine::assets::MeshData& source,
            std::vector<wz::engine::assets::TerrainVisualChunk>& chunks)
        {
            wz::engine::assets::MeshData mesh = source;
            if (!terrain.mesh_visual_indices.empty()) {
                mesh.indices = terrain.mesh_visual_indices;
            }
            chunks = terrain.mesh_visual_chunks;
            return mesh;
        }
    }

    GpuSceneRenderResourceResolver::GpuSceneRenderResourceResolver(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        RenderResourceResolver& render_resolver,
        RenderableGpuCache* cache)
        : device_(device)
        , assets_(assets)
        , render_resolver_(render_resolver)
        , cache_(cache)
    {
    }

    bool GpuSceneRenderResourceResolver::realize_renderable_descriptor(
        const wz::engine::assets::RenderableAssetData& renderable,
        wz::scene::RenderableDescriptor& descriptor) const
    {
        wz::engine::assets::MeshData preview_mesh{};
        const wz::engine::assets::MeshData* mesh_data = nullptr;
        wz::gpu::GPUHandle cached_mesh{};

        const bool is_terrain_surface =
            renderable.kind == wz::engine::assets::RenderableKind::Mesh
            && renderable.program
                == wz::engine::assets::BuiltinRenderProgram::TerrainMeshSurface
            && !(renderable.companion_asset == wz::asset::AssetKey{});

        const wz::engine::assets::TerrainAssetData* terrain_data = nullptr;
        std::vector<wz::engine::assets::TerrainVisualChunk> terrain_chunks_storage;

        if (renderable.kind == wz::engine::assets::RenderableKind::Mesh) {
            if (is_terrain_surface) {
                const wz::engine::assets::TerrainAsset terrain_asset{
                    .output = renderable.companion_asset,
                };
                const wz::engine::assets::TerrainHandle terrain_handle =
                    assets_.terrains().get_terrain(terrain_asset);
                if (!terrain_handle.valid()) {
                    return false;
                }

                terrain_data =
                    assets_.terrains().get_terrain_data(terrain_handle);
                if (!terrain_data || !terrain_data->valid()) {
                    return false;
                }

                const wz::engine::assets::MeshAsset mesh_asset{
                    .output = renderable.source_asset,
                };
                const wz::engine::assets::MeshHandle mesh_handle =
                    assets_.meshes().get_mesh(mesh_asset);
                if (!mesh_handle.valid()) {
                    return false;
                }

                const wz::engine::assets::MeshData* source_mesh =
                    assets_.meshes().get_mesh_data(mesh_handle);
                if (!source_mesh || !source_mesh->valid()) {
                    return false;
                }

                preview_mesh = make_terrain_surface_mesh(
                    *terrain_data,
                    *source_mesh,
                    terrain_chunks_storage);
                if (!preview_mesh.valid()) {
                    return false;
                }

                if (cache_) {
                    const PreparedRenderable prepared =
                        cache_->realize_mesh_data(
                            device_,
                            renderable.companion_asset,
                            preview_mesh,
                            renderable.program,
                            renderable.render_program,
                            renderable.domain,
                            renderable.policy_flags);
                    if (!prepared.valid()) {
                        return false;
                    }
                    cached_mesh = prepared.gpu_resource;
                }
                else {
                    mesh_data = &preview_mesh;
                }
            }
            else if (cache_) {
                const PreparedRenderable prepared =
                    cache_->realize_data(device_, assets_, renderable);
                if (!prepared.valid()) {
                    return false;
                }
                cached_mesh = prepared.gpu_resource;
            }
            else {
                const wz::engine::assets::MeshAsset mesh_asset{
                    .output = renderable.source_asset,
                };

                const wz::engine::assets::MeshHandle mesh_handle =
                    assets_.meshes().get_mesh(mesh_asset);

                if (!mesh_handle.valid())
                    return false;

                mesh_data = assets_.meshes().get_mesh_data(mesh_handle);

                if (!mesh_data || !mesh_data->valid())
                    return false;
            }
        }
        else if (renderable.kind
            == wz::engine::assets::RenderableKind::ScalarField)
        {
            if (renderable.companion_asset == wz::asset::AssetKey{})
                return false;

            const wz::engine::assets::TerrainAsset terrain_asset{
                .output = renderable.companion_asset,
            };
            const wz::engine::assets::TerrainHandle terrain_handle =
                assets_.terrains().get_terrain(terrain_asset);
            if (!terrain_handle.valid())
                return false;

            const wz::engine::assets::TerrainAssetData* terrain =
                assets_.terrains().get_terrain_data(terrain_handle);
            if (!terrain || !terrain->valid())
                return false;

            const wz::engine::assets::ScalarFieldAsset field_asset{
                .output = renderable.source_asset,
            };
            const wz::engine::assets::ScalarFieldHandle field_handle =
                assets_.scalar_fields().get_scalar_field(field_asset);
            if (!field_handle.valid())
                return false;

            const wz::engine::assets::ScalarFieldData* field =
                assets_.scalar_fields().get_scalar_field_data(field_handle);
            if (!field || !field->valid())
                return false;

            preview_mesh = make_heightfield_preview_mesh(*terrain, *field);
            if (!preview_mesh.valid())
                return false;

            mesh_data = &preview_mesh;
        }
        else {
            return false;
        }

        const wz::gpu::GPUHandle gpu_mesh = cached_mesh.valid()
            ? cached_mesh
            : wz::gpu::upload_mesh(device_, *mesh_data);

        if (!gpu_mesh.valid())
            return false;

        std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks{};
        if (terrain_data) {
            terrain_chunks = terrain_chunks_storage;
        }

        const wz::scene::MeshHandle scene_mesh =
            render_resolver_.register_mesh(
                gpu_mesh,
                renderable.program,
                renderable.render_program,
                renderable.terrain_lighting,
                renderable.mesh_style,
                terrain_chunks);

        descriptor.mesh = scene_mesh;
        descriptor.material = wz::scene::INVALID_MATERIAL;

        return true;
    }
}
