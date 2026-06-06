// src/engine/rendering/gpu_scene_render_resource_resolver.cpp

#include <engine/rendering/gpu_scene_render_resource_resolver.h>

#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/terrain_asset_module.h>
#include <gpu/gaussian_splat.h>
#include <gpu/mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

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
            const uint32_t source_vertex_count =
                static_cast<uint32_t>(mesh.vertices.size());
            const uint32_t detail_index_count =
                static_cast<uint32_t>(mesh.indices.size());
            const uint32_t detail_triangle_count = detail_index_count / 3u;

            constexpr uint32_t kMinReplacementSourceTriangles = 96u;
            constexpr uint32_t kMaxReplacementTriangles = 256u;

            struct ClusterAccum
            {
                double position[3]{};
                double normal[3]{};
                double uv[2]{};
                uint32_t count = 0;
                uint32_t vertex_index = UINT32_MAX;
            };

            struct EdgeRef
            {
                uint32_t a = 0;
                uint32_t b = 0;
                uint32_t triangle = 0;
                uint32_t chunk = UINT32_MAX;
            };

            std::vector<uint32_t> triangle_chunks(
                detail_triangle_count,
                UINT32_MAX);
            for (uint32_t chunk_index = 0;
                chunk_index < chunks.size();
                ++chunk_index)
            {
                const auto& chunk = chunks[chunk_index];
                const uint32_t begin = chunk.first_index;
                const uint32_t end = std::min(
                    detail_index_count,
                    chunk.first_index + chunk.index_count);
                for (uint32_t i = begin; i + 2u < end; i += 3u) {
                    triangle_chunks[i / 3u] = chunk_index;
                }
            }

            const auto push_edge =
                [](std::vector<EdgeRef>& edges,
                   uint32_t a,
                   uint32_t b,
                   uint32_t triangle,
                   uint32_t chunk) {
                    if (a > b) {
                        std::swap(a, b);
                    }
                    edges.push_back(EdgeRef{ a, b, triangle, chunk });
                };

            std::vector<EdgeRef> edges;
            edges.reserve(static_cast<size_t>(detail_triangle_count) * 3u);
            for (uint32_t tri = 0; tri < detail_triangle_count; ++tri) {
                const uint32_t chunk = triangle_chunks[tri];
                if (chunk == UINT32_MAX) {
                    continue;
                }
                const uint32_t base = tri * 3u;
                const uint32_t ia = mesh.indices[base + 0u];
                const uint32_t ib = mesh.indices[base + 1u];
                const uint32_t ic = mesh.indices[base + 2u];
                if (ia >= source_vertex_count
                    || ib >= source_vertex_count
                    || ic >= source_vertex_count)
                {
                    continue;
                }
                push_edge(edges, ia, ib, tri, chunk);
                push_edge(edges, ib, ic, tri, chunk);
                push_edge(edges, ic, ia, tri, chunk);
            }
            std::sort(
                edges.begin(),
                edges.end(),
                [](const EdgeRef& lhs, const EdgeRef& rhs) {
                    if (lhs.a != rhs.a) {
                        return lhs.a < rhs.a;
                    }
                    if (lhs.b != rhs.b) {
                        return lhs.b < rhs.b;
                    }
                    return lhs.chunk < rhs.chunk;
                });

            std::vector<uint8_t> fixed_vertices(source_vertex_count, 0u);
            for (size_t i = 0; i < edges.size();) {
                size_t end = i + 1u;
                bool crosses_chunk = false;
                while (end < edges.size()
                    && edges[end].a == edges[i].a
                    && edges[end].b == edges[i].b)
                {
                    crosses_chunk =
                        crosses_chunk || edges[end].chunk != edges[i].chunk;
                    ++end;
                }

                if (crosses_chunk || end - i == 1u) {
                    fixed_vertices[edges[i].a] = 1u;
                    fixed_vertices[edges[i].b] = 1u;
                }
                i = end;
            }

            const auto cluster_for_vertex =
                [](const wz::engine::assets::TerrainVisualChunk& chunk,
                   const wz::engine::assets::MeshVertex& vertex,
                   uint32_t grid_vertices) -> uint32_t {
                    const float extent_x =
                        chunk.bounds_max[0] - chunk.bounds_min[0];
                    const float extent_z =
                        chunk.bounds_max[2] - chunk.bounds_min[2];
                    if (extent_x <= 1e-6f || extent_z <= 1e-6f) {
                        return UINT32_MAX;
                    }

                    const float ux = std::clamp(
                        (vertex.position[0] - chunk.bounds_min[0]) / extent_x,
                        0.0f,
                        1.0f);
                    const float uz = std::clamp(
                        (vertex.position[2] - chunk.bounds_min[2]) / extent_z,
                        0.0f,
                        1.0f);
                    const uint32_t max_grid = grid_vertices - 1u;
                    const uint32_t gx = std::min(
                        max_grid,
                        static_cast<uint32_t>(
                            ux * static_cast<float>(max_grid) + 0.5f));
                    const uint32_t gz = std::min(
                        max_grid,
                        static_cast<uint32_t>(
                            uz * static_cast<float>(max_grid) + 0.5f));
                    return gz * grid_vertices + gx;
                };

            const auto triangle_area_and_normal =
                [](const wz::engine::assets::MeshData& mesh_data,
                   uint32_t ia,
                   uint32_t ib,
                   uint32_t ic,
                   float normal[3]) -> float {
                    if (ia >= mesh_data.vertices.size()
                        || ib >= mesh_data.vertices.size()
                        || ic >= mesh_data.vertices.size())
                    {
                        normal[0] = 0.0f;
                        normal[1] = 1.0f;
                        normal[2] = 0.0f;
                        return 0.0f;
                    }

                    const auto& a = mesh_data.vertices[ia];
                    const auto& b = mesh_data.vertices[ib];
                    const auto& c = mesh_data.vertices[ic];
                    const float abx = b.position[0] - a.position[0];
                    const float aby = b.position[1] - a.position[1];
                    const float abz = b.position[2] - a.position[2];
                    const float acx = c.position[0] - a.position[0];
                    const float acy = c.position[1] - a.position[1];
                    const float acz = c.position[2] - a.position[2];
                    normal[0] = aby * acz - abz * acy;
                    normal[1] = abz * acx - abx * acz;
                    normal[2] = abx * acy - aby * acx;
                    const float len = std::sqrt(
                        normal[0] * normal[0]
                        + normal[1] * normal[1]
                        + normal[2] * normal[2]);
                    if (len <= 1e-8f) {
                        normal[0] = 0.0f;
                        normal[1] = 1.0f;
                        normal[2] = 0.0f;
                        return 0.0f;
                    }
                    const float inv_len = 1.0f / len;
                    normal[0] *= inv_len;
                    normal[1] *= inv_len;
                    normal[2] *= inv_len;
                    return 0.5f * len;
                };

            for (auto& chunk : chunks) {
                chunk.replacement_first_index = 0;
                chunk.replacement_index_count = 0;

                const uint32_t source_triangles = chunk.triangle_count();
                if (source_triangles < kMinReplacementSourceTriangles) {
                    continue;
                }
                const float extent_x = chunk.bounds_max[0] - chunk.bounds_min[0];
                const float extent_z = chunk.bounds_max[2] - chunk.bounds_min[2];
                if (extent_x <= 1e-6f || extent_z <= 1e-6f) {
                    continue;
                }

                const uint32_t desired_triangles = std::clamp(
                    source_triangles / 32u,
                    16u,
                    kMaxReplacementTriangles);
                const uint32_t grid_cells = std::clamp(
                    static_cast<uint32_t>(
                        std::sqrt(
                            static_cast<float>(desired_triangles) * 0.5f))
                        + 1u,
                    3u,
                    24u);
                const uint32_t grid_vertices = grid_cells + 1u;
                std::vector<ClusterAccum> clusters(
                    static_cast<size_t>(grid_vertices) * grid_vertices);

                for (uint32_t i = 0; i + 2u < chunk.index_count; i += 3u) {
                    const uint32_t base = chunk.first_index + i;
                    if (base + 2u >= mesh.indices.size()) {
                        continue;
                    }

                    const uint32_t ids[3]{
                        mesh.indices[base + 0u],
                        mesh.indices[base + 1u],
                        mesh.indices[base + 2u],
                    };
                    for (uint32_t id : ids) {
                        if (id >= source_vertex_count
                            || fixed_vertices[id] != 0u)
                        {
                            continue;
                        }
                        const auto& vertex = mesh.vertices[id];
                        const uint32_t cluster_id = cluster_for_vertex(
                            chunk,
                            vertex,
                            grid_vertices);
                        if (cluster_id >= clusters.size()) {
                            continue;
                        }

                        ClusterAccum& accum = clusters[cluster_id];
                        for (int axis = 0; axis < 3; ++axis) {
                            accum.position[axis] += vertex.position[axis];
                            accum.normal[axis] += vertex.normal[axis];
                        }
                        accum.uv[0] += vertex.uv[0];
                        accum.uv[1] += vertex.uv[1];
                        ++accum.count;
                    }
                }

                for (ClusterAccum& accum : clusters) {
                    if (accum.count == 0) {
                        continue;
                    }

                    const double inv_count =
                        1.0 / static_cast<double>(accum.count);
                    wz::engine::assets::MeshVertex vertex{};
                    for (int axis = 0; axis < 3; ++axis) {
                        vertex.position[axis] =
                            static_cast<float>(
                                accum.position[axis] * inv_count);
                        vertex.normal[axis] =
                            static_cast<float>(
                                accum.normal[axis] * inv_count);
                    }
                    const float normal_len = std::sqrt(
                        vertex.normal[0] * vertex.normal[0]
                        + vertex.normal[1] * vertex.normal[1]
                        + vertex.normal[2] * vertex.normal[2]);
                    if (normal_len > 1e-6f) {
                        vertex.normal[0] /= normal_len;
                        vertex.normal[1] /= normal_len;
                        vertex.normal[2] /= normal_len;
                    }
                    else {
                        vertex.normal[0] = 0.0f;
                        vertex.normal[1] = 1.0f;
                        vertex.normal[2] = 0.0f;
                    }
                    vertex.uv[0] = static_cast<float>(accum.uv[0] * inv_count);
                    vertex.uv[1] = static_cast<float>(accum.uv[1] * inv_count);
                    accum.vertex_index =
                        static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(vertex);
                }

                const uint32_t replacement_first =
                    static_cast<uint32_t>(mesh.indices.size());
                std::vector<std::array<uint32_t, 3>> emitted_keys;
                emitted_keys.reserve(desired_triangles);

                for (uint32_t i = 0; i + 2u < chunk.index_count; i += 3u) {
                    const uint32_t base = chunk.first_index + i;
                    if (base + 2u >= mesh.indices.size()) {
                        continue;
                    }

                    uint32_t replacement[3]{};
                    bool valid = true;
                    for (uint32_t corner = 0; corner < 3u; ++corner) {
                        const uint32_t source_id = mesh.indices[base + corner];
                        if (source_id >= source_vertex_count) {
                            valid = false;
                            break;
                        }
                        if (fixed_vertices[source_id] != 0u) {
                            replacement[corner] = source_id;
                            continue;
                        }
                        const uint32_t cluster_id = cluster_for_vertex(
                            chunk,
                            mesh.vertices[source_id],
                            grid_vertices);
                        if (cluster_id >= clusters.size()
                            || clusters[cluster_id].vertex_index == UINT32_MAX)
                        {
                            valid = false;
                            break;
                        }
                        replacement[corner] =
                            clusters[cluster_id].vertex_index;
                    }
                    if (!valid) {
                        mesh.indices.push_back(mesh.indices[base + 0u]);
                        mesh.indices.push_back(mesh.indices[base + 1u]);
                        mesh.indices.push_back(mesh.indices[base + 2u]);
                        continue;
                    }

                    if (replacement[0] == replacement[1]
                        || replacement[1] == replacement[2]
                        || replacement[2] == replacement[0])
                    {
                        mesh.indices.push_back(mesh.indices[base + 0u]);
                        mesh.indices.push_back(mesh.indices[base + 1u]);
                        mesh.indices.push_back(mesh.indices[base + 2u]);
                        continue;
                    }

                    float source_normal[3]{};
                    const float source_area = triangle_area_and_normal(
                        mesh,
                        mesh.indices[base + 0u],
                        mesh.indices[base + 1u],
                        mesh.indices[base + 2u],
                        source_normal);
                    float replacement_normal[3]{};
                    const float replacement_area = triangle_area_and_normal(
                        mesh,
                        replacement[0],
                        replacement[1],
                        replacement[2],
                        replacement_normal);
                    const float normal_dot =
                        source_normal[0] * replacement_normal[0]
                        + source_normal[1] * replacement_normal[1]
                        + source_normal[2] * replacement_normal[2];
                    if (replacement_area <= source_area * 0.01f
                        || normal_dot <= 0.0f)
                    {
                        mesh.indices.push_back(mesh.indices[base + 0u]);
                        mesh.indices.push_back(mesh.indices[base + 1u]);
                        mesh.indices.push_back(mesh.indices[base + 2u]);
                        continue;
                    }

                    std::array<uint32_t, 3> key{
                        replacement[0],
                        replacement[1],
                        replacement[2],
                    };
                    std::sort(key.begin(), key.end());
                    const bool already_emitted = std::find(
                        emitted_keys.begin(),
                        emitted_keys.end(),
                        key) != emitted_keys.end();
                    if (already_emitted) {
                        mesh.indices.push_back(mesh.indices[base + 0u]);
                        mesh.indices.push_back(mesh.indices[base + 1u]);
                        mesh.indices.push_back(mesh.indices[base + 2u]);
                        continue;
                    }
                    emitted_keys.push_back(key);

                    mesh.indices.push_back(replacement[0]);
                    mesh.indices.push_back(replacement[1]);
                    mesh.indices.push_back(replacement[2]);
                }

                const uint32_t replacement_count =
                    static_cast<uint32_t>(mesh.indices.size())
                    - replacement_first;
                if (replacement_count >= 3u
                    && replacement_count < chunk.index_count)
                {
                    chunk.replacement_first_index = replacement_first;
                    chunk.replacement_index_count = replacement_count;
                }
                else {
                    mesh.indices.resize(replacement_first);
                }
            }

            mesh.has_normals = true;
            mesh.has_uv0 = true;
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

                if (cache_) {
                    const PreparedRenderable cached =
                        cache_->find_mesh_data(
                            renderable.companion_asset,
                            renderable.program,
                            renderable.render_program,
                            renderable.domain,
                            renderable.policy_flags);
                    if (cached.valid()) {
                        if (const auto* cached_chunks =
                                cache_->find_terrain_mesh_chunks(
                                    renderable.companion_asset))
                        {
                            terrain_chunks_storage = *cached_chunks;
                            cached_mesh = cached.gpu_resource;
                        }
                    }

                    if (!cached_mesh.valid()) {
                        preview_mesh = make_terrain_surface_mesh(
                            *terrain_data,
                            *source_mesh,
                            terrain_chunks_storage);
                        if (!preview_mesh.valid()) {
                            return false;
                        }

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
                        cache_->add_terrain_mesh_chunks(
                            renderable.companion_asset,
                            terrain_chunks_storage);
                    }
                }
                else {
                    preview_mesh = make_terrain_surface_mesh(
                        *terrain_data,
                        *source_mesh,
                        terrain_chunks_storage);
                    if (!preview_mesh.valid()) {
                        return false;
                    }
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
                renderable.terrain_target_pixels_per_triangle,
                renderable.mesh_style,
                terrain_chunks);

        descriptor.mesh = scene_mesh;
        descriptor.material = wz::scene::INVALID_MATERIAL;

        return true;
    }
}
