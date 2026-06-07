#include "terrain_fixture_assets.h"

#include <gtest/gtest.h>

#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <string_view>

namespace
{
    uint32_t vertex_count(const wz::engine::assets::TerrainAssetData& terrain)
    {
        return static_cast<uint32_t>(terrain.mesh_surface_points.size() / 3u);
    }

    uint32_t triangle_count(const wz::engine::assets::TerrainAssetData& terrain)
    {
        return static_cast<uint32_t>(terrain.mesh_surface_indices.size() / 3u);
    }

    size_t approximate_fixture_bytes(
        const wz::engine::assets::TerrainAssetData& terrain)
    {
        return terrain.mesh_surface_points.size() * sizeof(float)
            + terrain.mesh_surface_indices.size() * sizeof(uint32_t)
            + terrain.mesh_visual_indices.size() * sizeof(uint32_t)
            + terrain.mesh_visual_chunks.size()
                * sizeof(wz::engine::assets::TerrainVisualChunk)
            + terrain.height_samples.size() * sizeof(float);
    }

    void expect_chunk_matches_referenced_vertices(
        const wz::engine::assets::TerrainAssetData& terrain,
        const wz::engine::assets::TerrainVisualChunk& chunk,
        std::string_view fixture_name,
        uint32_t chunk_index)
    {
        ASSERT_GT(chunk.index_count, 0u)
            << fixture_name << " chunk " << chunk_index;
        ASSERT_LE(
            chunk.first_index + chunk.index_count,
            terrain.mesh_visual_indices.size())
            << fixture_name << " chunk " << chunk_index;

        float min[3]{
            terrain.mesh_surface_points[
                terrain.mesh_visual_indices[chunk.first_index] * 3u],
            terrain.mesh_surface_points[
                terrain.mesh_visual_indices[chunk.first_index] * 3u + 1u],
            terrain.mesh_surface_points[
                terrain.mesh_visual_indices[chunk.first_index] * 3u + 2u],
        };
        float max[3]{ min[0], min[1], min[2] };
        float mean_height = 0.0f;

        for (uint32_t i = 0; i < chunk.index_count; ++i) {
            const uint32_t vertex =
                terrain.mesh_visual_indices[chunk.first_index + i];
            const uint32_t point = vertex * 3u;
            ASSERT_LT(point + 2u, terrain.mesh_surface_points.size())
                << fixture_name << " chunk " << chunk_index;

            for (uint32_t axis = 0; axis < 3u; ++axis) {
                const float value = terrain.mesh_surface_points[point + axis];
                min[axis] = std::min(min[axis], value);
                max[axis] = std::max(max[axis], value);
            }
            mean_height += terrain.mesh_surface_points[point + 1u];
        }
        mean_height /= static_cast<float>(chunk.index_count);

        for (uint32_t axis = 0; axis < 3u; ++axis) {
            EXPECT_FLOAT_EQ(chunk.bounds_min[axis], min[axis])
                << fixture_name << " chunk " << chunk_index;
            EXPECT_FLOAT_EQ(chunk.bounds_max[axis], max[axis])
                << fixture_name << " chunk " << chunk_index;
        }
        EXPECT_FLOAT_EQ(chunk.aggregate.mean_height, mean_height)
            << fixture_name << " chunk " << chunk_index;
    }
}

TEST(TerrainFixtureAssets, CatalogContainsExpectedFixtures)
{
    using namespace wz::engine::assets::test;

    ASSERT_EQ(kTerrainFixtureNames.size(), 10u);
    EXPECT_EQ(kTerrainFixtureNames[0], "flat_plane");
    EXPECT_EQ(kTerrainFixtureNames[9], "multi_chunk_seam");
}

TEST(TerrainFixtureAssets, FixturesMatchGroundTruth)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    for (const std::string_view name : kTerrainFixtureNames) {
        const TerrainAssetData terrain = load_fixture_terrain(name);
        const TerrainFixtureGroundTruth truth = fixture_ground_truth(name);

        ASSERT_TRUE(terrain.valid()) << name;
        EXPECT_EQ(terrain.representation, TerrainRepresentationKind::MeshSurface)
            << name;
        EXPECT_TRUE(terrain.supports_render_mesh) << name;
        EXPECT_TRUE(terrain.supports_ray_query) << name;
        EXPECT_FALSE(terrain.supports_height_query) << name;
        EXPECT_EQ(vertex_count(terrain), truth.vertex_count) << name;
        EXPECT_EQ(triangle_count(terrain), truth.triangle_count) << name;
        EXPECT_EQ(terrain.mesh_triangle_count, truth.triangle_count) << name;
        EXPECT_EQ(
            terrain.mesh_accepted_surface_triangle_count,
            truth.triangle_count) << name;
        EXPECT_EQ(terrain.mesh_visual_chunks.size(), truth.visual_chunk_count)
            << name;
        EXPECT_EQ(terrain.mesh_visual_chunk_count, truth.visual_chunk_count)
            << name;
        EXPECT_EQ(terrain.mesh_visual_indices.size(),
            terrain.mesh_surface_indices.size()) << name;
        EXPECT_LT(approximate_fixture_bytes(terrain), 100u * 1024u) << name;

        for (uint32_t chunk = 0u;
             chunk < static_cast<uint32_t>(terrain.mesh_visual_chunks.size());
             ++chunk)
        {
            expect_chunk_matches_referenced_vertices(
                terrain,
                terrain.mesh_visual_chunks[chunk],
                name,
                chunk);
        }

        for (uint32_t axis = 0; axis < 3u; ++axis) {
            EXPECT_FLOAT_EQ(terrain.bounds_min[axis], truth.bounds_min[axis])
                << name;
            EXPECT_FLOAT_EQ(terrain.bounds_max[axis], truth.bounds_max[axis])
                << name;
        }

        for (uint32_t lod = 1; lod < truth.max_geometric_error_by_lod.size();
             ++lod)
        {
            EXPECT_LE(
                truth.max_geometric_error_by_lod[lod - 1u],
                truth.max_geometric_error_by_lod[lod]) << name;
        }
    }
}

TEST(TerrainFixtureAssets, FixturesRoundTripThroughTerrainAssetTable)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    TerrainAssetTable table;

    for (const std::string_view name : kTerrainFixtureNames) {
        const TerrainFixtureGroundTruth truth = fixture_ground_truth(name);
        const wz::asset::ResourceHandle handle =
            table.add(load_fixture_terrain(name));

        ASSERT_TRUE(handle.valid()) << name;
        EXPECT_EQ(handle.type, kAssetTypeTerrain) << name;

        const TerrainAssetData* terrain = table.get(handle);
        ASSERT_NE(terrain, nullptr) << name;
        EXPECT_TRUE(terrain->valid()) << name;
        EXPECT_EQ(vertex_count(*terrain), truth.vertex_count) << name;
        EXPECT_EQ(triangle_count(*terrain), truth.triangle_count) << name;
    }
}

TEST(TerrainFixtureAssets, PathologicalFixturesExposeExpectedSignals)
{
    using namespace wz::engine::assets::test;

    const auto sliver = load_fixture_terrain("sliver_triangles");
    ASSERT_TRUE(sliver.valid());
    EXPECT_GT(
        sliver.bounds_max[0] - sliver.bounds_min[0],
        10000.0f * (sliver.bounds_max[2] - sliver.bounds_min[2]));

    const auto large = load_fixture_terrain("large_coordinate");
    ASSERT_TRUE(large.valid());
    EXPECT_GE(large.bounds_min[0], 100000.0f);
    EXPECT_LE(large.bounds_min[2], -100000.0f);

    const auto checker = load_fixture_terrain("checkerboard_material");
    ASSERT_EQ(checker.mesh_visual_chunks.size(), 2u);
    EXPECT_LT(
        checker.mesh_visual_chunks[0].aggregate.albedo_mean[0],
        checker.mesh_visual_chunks[1].aggregate.albedo_mean[0]);

    const auto seam = load_fixture_terrain("multi_chunk_seam");
    ASSERT_EQ(seam.mesh_visual_chunks.size(), 2u);
    EXPECT_FLOAT_EQ(
        seam.mesh_visual_chunks[0].bounds_max[0],
        seam.mesh_visual_chunks[1].bounds_min[0]);
}

TEST(TerrainFixtureAssets, UnknownFixtureReturnsInvalidTerrain)
{
    const auto terrain =
        wz::engine::assets::test::load_fixture_terrain("missing_fixture");
    EXPECT_FALSE(terrain.valid());
}
