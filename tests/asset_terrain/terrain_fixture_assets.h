#pragma once

#include <engine/assets/terrain/terrain.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace wz::engine::assets::test
{
    struct TerrainFixtureGroundTruth
    {
        std::string_view name;
        uint32_t vertex_count = 0;
        uint32_t triangle_count = 0;
        uint32_t visual_chunk_count = 0;
        float bounds_min[3]{ 0.0f, 0.0f, 0.0f };
        float bounds_max[3]{ 0.0f, 0.0f, 0.0f };

        // Nominal placeholders until the LOD compiler lands and can write
        // measured simplification errors back into this catalog.
        std::array<float, 4> max_geometric_error_by_lod{};
    };

    inline constexpr std::array<std::string_view, 10> kTerrainFixtureNames{
        "flat_plane",
        "stepped_plateau",
        "smooth_hemisphere",
        "noise_terrain",
        "thin_ridge",
        "sliver_triangles",
        "checkerboard_material",
        "large_coordinate",
        "camera_inside_chunk",
        "multi_chunk_seam",
    };

    namespace detail
    {
        inline wz::asset::AssetKey fixture_source_key(uint64_t id)
        {
            return wz::asset::AssetKey{
                .content_hash = { 0x7465727261696e00ull + id, id },
                .schema_hash = { 0x6669787475726500ull, id },
                .compiler_hash = { 0x67656e6572617465ull, id },
                .deps_hash = { 0x746573742d6f6e6cull, id },
            };
        }

        inline void append_vertex(
            TerrainAssetData& terrain,
            float x,
            float y,
            float z)
        {
            terrain.mesh_surface_points.push_back(x);
            terrain.mesh_surface_points.push_back(y);
            terrain.mesh_surface_points.push_back(z);
        }

        inline void append_triangle(
            TerrainAssetData& terrain,
            uint32_t a,
            uint32_t b,
            uint32_t c)
        {
            terrain.mesh_surface_indices.push_back(a);
            terrain.mesh_surface_indices.push_back(b);
            terrain.mesh_surface_indices.push_back(c);
            terrain.mesh_visual_indices.push_back(a);
            terrain.mesh_visual_indices.push_back(b);
            terrain.mesh_visual_indices.push_back(c);
        }

        inline void set_bounds(
            TerrainAssetData& terrain,
            const float min[3],
            const float max[3])
        {
            for (uint32_t i = 0; i < 3u; ++i) {
                terrain.bounds_min[i] = min[i];
                terrain.bounds_max[i] = max[i];
            }
        }

        inline TerrainVisualChunk make_chunk(
            const TerrainAssetData& terrain,
            uint32_t first_index,
            uint32_t index_count,
            const float min[3],
            const float max[3],
            const float* albedo = nullptr)
        {
            TerrainVisualChunk chunk{};
            for (uint32_t i = 0; i < 3u; ++i) {
                chunk.bounds_min[i] = min[i];
                chunk.bounds_max[i] = max[i];
                chunk.aggregate.albedo_mean[i] =
                    albedo ? albedo[i] : 1.0f;
            }
            chunk.first_index = first_index;
            chunk.index_count = index_count;
            chunk.aggregate.triangle_count = index_count / 3u;

            float mean = 0.0f;
            uint32_t referenced_vertices = 0u;
            const uint32_t end_index = first_index + index_count;
            for (uint32_t i = first_index; i < end_index; ++i) {
                const uint32_t vertex = terrain.mesh_visual_indices[i];
                const uint32_t point = vertex * 3u;
                if (point + 1u < terrain.mesh_surface_points.size()) {
                    mean += terrain.mesh_surface_points[point + 1u];
                    ++referenced_vertices;
                }
            }
            if (referenced_vertices > 0u) {
                mean /= static_cast<float>(referenced_vertices);
            }
            chunk.aggregate.mean_height = mean;
            return chunk;
        }

        inline TerrainAssetData make_base_mesh_fixture(uint64_t id)
        {
            TerrainAssetData terrain{};
            terrain.representation = TerrainRepresentationKind::MeshSurface;
            terrain.source_asset = fixture_source_key(id);
            terrain.mesh = terrain.source_asset;
            terrain.render_mode = TerrainRenderMode::DebugMesh;
            terrain.collision_mode = TerrainCollisionMode::MeshSurface;
            terrain.normal_source = TerrainNormalSource::DerivedGeometry;
            terrain.uv_source = TerrainUVSource::PlanarXZ;
            terrain.supports_height_query = false;
            terrain.supports_ray_query = true;
            terrain.supports_render_mesh = true;
            terrain.mesh_visual_chunk_count = 1u;
            return terrain;
        }

        inline void finalize_single_chunk(
            TerrainAssetData& terrain,
            const float min[3],
            const float max[3])
        {
            set_bounds(terrain, min, max);
            terrain.mesh_triangle_count =
                static_cast<uint32_t>(terrain.mesh_surface_indices.size() / 3u);
            terrain.mesh_accepted_surface_triangle_count =
                terrain.mesh_triangle_count;
            terrain.mesh_visual_chunks.clear();
            terrain.mesh_visual_chunks.push_back(
                make_chunk(
                    terrain,
                    0u,
                    static_cast<uint32_t>(terrain.mesh_visual_indices.size()),
                    min,
                    max));
        }

        inline TerrainAssetData make_grid_fixture(
            uint64_t id,
            uint32_t columns,
            uint32_t rows,
            float spacing,
            float (*height_fn)(uint32_t, uint32_t, float, float),
            const float offset[3])
        {
            TerrainAssetData terrain = make_base_mesh_fixture(id);
            for (uint32_t y = 0; y < rows; ++y) {
                for (uint32_t x = 0; x < columns; ++x) {
                    const float px = offset[0] + static_cast<float>(x) * spacing;
                    const float pz = offset[2] + static_cast<float>(y) * spacing;
                    append_vertex(
                        terrain,
                        px,
                        offset[1] + height_fn(x, y, px, pz),
                        pz);
                }
            }
            for (uint32_t y = 0; y + 1u < rows; ++y) {
                for (uint32_t x = 0; x + 1u < columns; ++x) {
                    const uint32_t a = y * columns + x;
                    const uint32_t b = y * columns + x + 1u;
                    const uint32_t c = (y + 1u) * columns + x;
                    const uint32_t d = (y + 1u) * columns + x + 1u;
                    append_triangle(terrain, a, c, b);
                    append_triangle(terrain, b, c, d);
                }
            }

            float min[3]{
                terrain.mesh_surface_points[0],
                terrain.mesh_surface_points[1],
                terrain.mesh_surface_points[2],
            };
            float max[3]{ min[0], min[1], min[2] };
            const uint32_t vertex_count =
                static_cast<uint32_t>(terrain.mesh_surface_points.size() / 3u);
            for (uint32_t i = 1; i < vertex_count; ++i) {
                for (uint32_t axis = 0; axis < 3u; ++axis) {
                    const float value =
                        terrain.mesh_surface_points[i * 3u + axis];
                    min[axis] = std::min(min[axis], value);
                    max[axis] = std::max(max[axis], value);
                }
            }
            finalize_single_chunk(terrain, min, max);
            return terrain;
        }

        inline float zero_height(uint32_t, uint32_t, float, float)
        {
            return 0.0f;
        }

        inline float plateau_height(uint32_t x, uint32_t y, float, float)
        {
            return (x >= 2u && x <= 4u && y >= 2u && y <= 4u) ? 2.0f : 0.0f;
        }

        inline float hemisphere_height(uint32_t, uint32_t, float x, float z)
        {
            const float cx = 2.0f;
            const float cz = 2.0f;
            const float dx = x - cx;
            const float dz = z - cz;
            const float r2 = dx * dx + dz * dz;
            return r2 >= 4.0f ? 0.0f : std::sqrt(4.0f - r2);
        }

        inline float noise_height(uint32_t x, uint32_t y, float, float)
        {
            return ((x * 17u + y * 31u + x * y * 7u) % 11u) * 0.125f;
        }

        inline float thin_ridge_height(uint32_t x, uint32_t, float, float)
        {
            return x == 3u ? 2.0f : 0.0f;
        }
    }

    inline TerrainAssetData load_fixture_terrain(std::string_view name)
    {
        const float origin[3]{ 0.0f, 0.0f, 0.0f };

        if (name == "flat_plane") {
            return detail::make_grid_fixture(
                1u,
                3u,
                3u,
                1.0f,
                detail::zero_height,
                origin);
        }
        if (name == "stepped_plateau") {
            return detail::make_grid_fixture(
                2u,
                6u,
                6u,
                1.0f,
                detail::plateau_height,
                origin);
        }
        if (name == "smooth_hemisphere") {
            return detail::make_grid_fixture(
                3u,
                5u,
                5u,
                1.0f,
                detail::hemisphere_height,
                origin);
        }
        if (name == "noise_terrain") {
            return detail::make_grid_fixture(
                4u,
                6u,
                6u,
                1.0f,
                detail::noise_height,
                origin);
        }
        if (name == "thin_ridge") {
            return detail::make_grid_fixture(
                5u,
                7u,
                3u,
                0.5f,
                detail::thin_ridge_height,
                origin);
        }
        if (name == "large_coordinate") {
            const float offset[3]{ 100000.0f, 0.0f, -100000.0f };
            return detail::make_grid_fixture(
                8u,
                3u,
                3u,
                10.0f,
                detail::zero_height,
                offset);
        }
        if (name == "camera_inside_chunk") {
            return detail::make_grid_fixture(
                9u,
                2u,
                2u,
                1000.0f,
                detail::zero_height,
                origin);
        }

        if (name == "sliver_triangles") {
            TerrainAssetData terrain = detail::make_base_mesh_fixture(6u);
            detail::append_vertex(terrain, 0.0f, 0.0f, 0.0f);
            detail::append_vertex(terrain, 100.0f, 0.0f, 0.001f);
            detail::append_vertex(terrain, 0.0f, 0.0f, 0.002f);
            detail::append_vertex(terrain, 100.0f, 0.0f, 0.003f);
            detail::append_triangle(terrain, 0u, 1u, 2u);
            detail::append_triangle(terrain, 1u, 3u, 2u);
            const float min[3]{ 0.0f, 0.0f, 0.0f };
            const float max[3]{ 100.0f, 0.0f, 0.003f };
            detail::finalize_single_chunk(terrain, min, max);
            return terrain;
        }

        if (name == "checkerboard_material") {
            TerrainAssetData terrain = detail::make_grid_fixture(
                7u,
                3u,
                3u,
                1.0f,
                detail::zero_height,
                origin);
            terrain.mesh_visual_chunks.clear();
            const float dark[3]{ 0.1f, 0.1f, 0.1f };
            const float light[3]{ 0.9f, 0.9f, 0.9f };
            const float min_a[3]{ 0.0f, 0.0f, 0.0f };
            const float max_a[3]{ 1.0f, 0.0f, 1.0f };
            const float min_b[3]{ 0.0f, 0.0f, 0.0f };
            const float max_b[3]{ 2.0f, 0.0f, 2.0f };
            terrain.mesh_visual_chunks.push_back(
                detail::make_chunk(terrain, 0u, 6u, min_a, max_a, dark));
            terrain.mesh_visual_chunks.push_back(
                detail::make_chunk(terrain, 6u, 18u, min_b, max_b, light));
            terrain.mesh_visual_chunk_count = 2u;
            return terrain;
        }

        if (name == "multi_chunk_seam") {
            TerrainAssetData terrain = detail::make_grid_fixture(
                10u,
                3u,
                2u,
                1.0f,
                detail::zero_height,
                origin);
            terrain.mesh_visual_chunks.clear();
            const float min_left[3]{ 0.0f, 0.0f, 0.0f };
            const float max_left[3]{ 1.0f, 0.0f, 1.0f };
            const float min_right[3]{ 1.0f, 0.0f, 0.0f };
            const float max_right[3]{ 2.0f, 0.0f, 1.0f };
            terrain.mesh_visual_chunks.push_back(
                detail::make_chunk(terrain, 0u, 6u, min_left, max_left));
            terrain.mesh_visual_chunks.push_back(
                detail::make_chunk(terrain, 6u, 6u, min_right, max_right));
            terrain.mesh_visual_chunk_count = 2u;
            return terrain;
        }

        return {};
    }

    inline TerrainFixtureGroundTruth fixture_ground_truth(std::string_view name)
    {
        if (name == "flat_plane") {
            return { name, 9u, 8u, 1u, { 0.0f, 0.0f, 0.0f },
                { 2.0f, 0.0f, 2.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        }
        if (name == "stepped_plateau") {
            return { name, 36u, 50u, 1u, { 0.0f, 0.0f, 0.0f },
                { 5.0f, 2.0f, 5.0f }, { 0.0f, 0.5f, 1.0f, 2.0f } };
        }
        if (name == "smooth_hemisphere") {
            return { name, 25u, 32u, 1u, { 0.0f, 0.0f, 0.0f },
                { 4.0f, 2.0f, 4.0f }, { 0.0f, 0.25f, 0.5f, 1.0f } };
        }
        if (name == "noise_terrain") {
            return { name, 36u, 50u, 1u, { 0.0f, 0.0f, 0.0f },
                { 5.0f, 1.25f, 5.0f }, { 0.0f, 0.625f, 1.0f, 1.25f } };
        }
        if (name == "thin_ridge") {
            return { name, 21u, 24u, 1u, { 0.0f, 0.0f, 0.0f },
                { 3.0f, 2.0f, 1.0f }, { 0.0f, 0.25f, 1.0f, 2.0f } };
        }
        if (name == "sliver_triangles") {
            return { name, 4u, 2u, 1u, { 0.0f, 0.0f, 0.0f },
                { 100.0f, 0.0f, 0.003f }, { 0.0f, 0.001f, 0.002f, 0.003f } };
        }
        if (name == "checkerboard_material") {
            return { name, 9u, 8u, 2u, { 0.0f, 0.0f, 0.0f },
                { 2.0f, 0.0f, 2.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        }
        if (name == "large_coordinate") {
            return { name, 9u, 8u, 1u, { 100000.0f, 0.0f, -100000.0f },
                { 100020.0f, 0.0f, -99980.0f },
                { 0.0f, 0.0f, 0.0f, 0.0f } };
        }
        if (name == "camera_inside_chunk") {
            return { name, 4u, 2u, 1u, { 0.0f, 0.0f, 0.0f },
                { 1000.0f, 0.0f, 1000.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        }
        if (name == "multi_chunk_seam") {
            return { name, 6u, 4u, 2u, { 0.0f, 0.0f, 0.0f },
                { 2.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        }
        return {};
    }
}
