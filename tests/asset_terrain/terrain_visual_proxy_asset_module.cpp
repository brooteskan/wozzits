#include <gtest/gtest.h>

#include "terrain_fixture_assets.h"

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/terrain/terrain_lod_seams.h>
#include <engine/assets/terrain/terrain_visual_proxy_compilers.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/render_resource_resolver.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace
{
    wz::fs::Path test_root(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }

    wz::engine::assets::TerrainVisualProxyData compile_cube_proxy(
        wz::engine::assets::EngineAssetLibrary& assets,
        wz::engine::assets::TerrainAsset* out_terrain = nullptr,
        wz::engine::assets::TerrainVisualProxyAsset* out_proxy_asset = nullptr,
        uint32_t visual_chunk_count = 1u)
    {
        using namespace wz::engine::assets;

        const auto mesh =
            assets.meshes().create_procedural_mesh({
                .name = "terrain/proxy_cube_mesh",
                .kind = ProceduralMeshKind::Cube,
            });
        EXPECT_TRUE(mesh.valid());

        const auto terrain =
            assets.terrains().create_from_mesh({
                .name = "terrain/proxy_cube_surface",
                .mesh = mesh,
                .min_surface_normal_y = 0.0f,
                .include_backfaces = true,
                .visual_chunk_count = visual_chunk_count,
            });
        EXPECT_TRUE(terrain.valid());

        const auto proxy =
            assets.terrain_visual_proxies().create_from_terrain({
                .name = "terrain/proxy_cube_visual",
                .terrain = terrain,
            });
        EXPECT_TRUE(proxy.valid());

        EXPECT_TRUE(assets.commit());
        const auto report = assets.resolve_all();
        EXPECT_TRUE(report.ok());

        const TerrainVisualProxyHandle handle =
            assets.terrain_visual_proxies().get_proxy(proxy);
        EXPECT_TRUE(handle.valid());
        const TerrainVisualProxyData* data =
            assets.terrain_visual_proxies().get_proxy_data(handle);
        EXPECT_NE(data, nullptr);

        if (out_terrain) {
            *out_terrain = terrain;
        }
        if (out_proxy_asset) {
            *out_proxy_asset = proxy;
        }
        return data ? *data : TerrainVisualProxyData{};
    }

    wz::asset::AssetKey test_key(uint64_t lo)
    {
        return wz::asset::AssetKey{
            .content_hash = { lo, lo + 1u },
            .schema_hash = { lo + 2u, lo + 3u },
            .compiler_hash = { lo + 4u, lo + 5u },
            .deps_hash = { lo + 6u, lo + 7u },
        };
    }

    float transition_parameter(
        wz::engine::assets::TerrainVisualProxyBoundaryEdge edge,
        const wz::engine::assets::TerrainVisualProxyTransitionVertex& vertex)
    {
        using wz::engine::assets::TerrainVisualProxyBoundaryEdge;
        switch (edge) {
        case TerrainVisualProxyBoundaryEdge::NegativeX:
        case TerrainVisualProxyBoundaryEdge::PositiveX:
            return vertex.position[2];
        case TerrainVisualProxyBoundaryEdge::NegativeZ:
        case TerrainVisualProxyBoundaryEdge::PositiveZ:
            return vertex.position[0];
        }
        return 0.0f;
    }

    float boundary_parameter(
        wz::engine::assets::TerrainVisualProxyBoundaryEdge edge,
        const wz::engine::assets::TerrainVisualProxyBoundaryPoint& point)
    {
        using wz::engine::assets::TerrainVisualProxyBoundaryEdge;
        switch (edge) {
        case TerrainVisualProxyBoundaryEdge::NegativeX:
        case TerrainVisualProxyBoundaryEdge::PositiveX:
            return point.position[2];
        case TerrainVisualProxyBoundaryEdge::NegativeZ:
        case TerrainVisualProxyBoundaryEdge::PositiveZ:
            return point.position[0];
        }
        return 0.0f;
    }

    float normal_delta(
        const wz::engine::assets::TerrainVisualProxySurfel& a,
        const wz::engine::assets::TerrainVisualProxySurfel& b)
    {
        return std::abs(a.normal[0] - b.normal[0])
            + std::abs(a.normal[1] - b.normal[1])
            + std::abs(a.normal[2] - b.normal[2]);
    }

    bool contains_parameter(const std::vector<float>& values, float parameter)
    {
        return std::any_of(
            values.begin(),
            values.end(),
            [parameter](float value) {
                return std::abs(value - parameter) <= 1e-5f;
            });
    }

    std::vector<float> transition_side_parameters(
        const wz::engine::assets::TerrainVisualProxyTransitionStrip& strip,
        uint8_t side)
    {
        std::vector<float> out;
        for (const auto& vertex : strip.vertices) {
            if (vertex.side == side) {
                out.push_back(transition_parameter(strip.edge, vertex));
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(
            std::unique(
                out.begin(),
                out.end(),
                [](float a, float b) {
                    return std::abs(a - b) <= 1e-5f;
                }),
            out.end());
        return out;
    }

    void expect_transition_sides_share_parameters(
        const wz::engine::assets::TerrainVisualProxyTransitionStrip& strip)
    {
        const std::vector<float> owner =
            transition_side_parameters(strip, 0u);
        const std::vector<float> neighbor =
            transition_side_parameters(strip, 1u);

        ASSERT_EQ(owner.size(), neighbor.size());
        for (size_t i = 0u; i < owner.size(); ++i) {
            EXPECT_NEAR(owner[i], neighbor[i], 1e-5f);
        }
    }

    const wz::engine::assets::TerrainVisualProxyLodRecord* find_lod(
        const wz::engine::assets::TerrainVisualProxyChunkRecord& chunk,
        wz::engine::assets::TerrainLodId lod_id)
    {
        for (const auto& lod : chunk.lods) {
            if (lod.lod_id == lod_id) {
                return &lod;
            }
        }
        return nullptr;
    }

    void expect_transition_covers_boundary_breakpoints(
        const wz::engine::assets::TerrainVisualProxyTransitionStrip& strip,
        const wz::engine::assets::TerrainVisualProxyLodRecord& lod,
        const wz::engine::assets::TerrainVisualProxyLodRecord& neighbor_lod)
    {
        using namespace wz::engine::assets;

        const TerrainVisualProxyBoundaryEdge neighbor_edge =
            opposite_terrain_boundary_edge(strip.edge);
        const auto* owner_points =
            terrain_lod_boundary_points(lod, strip.edge);
        const auto* neighbor_points =
            terrain_lod_boundary_points(neighbor_lod, neighbor_edge);
        ASSERT_NE(owner_points, nullptr);
        ASSERT_NE(neighbor_points, nullptr);
        ASSERT_FALSE(owner_points->empty());
        ASSERT_FALSE(neighbor_points->empty());

        const float min_parameter = std::max(
            boundary_parameter(strip.edge, owner_points->front()),
            boundary_parameter(neighbor_edge, neighbor_points->front()));
        const float max_parameter = std::min(
            boundary_parameter(strip.edge, owner_points->back()),
            boundary_parameter(neighbor_edge, neighbor_points->back()));

        const std::vector<float> owner_strip =
            transition_side_parameters(strip, 0u);
        const std::vector<float> neighbor_strip =
            transition_side_parameters(strip, 1u);

        for (const auto& point : *owner_points) {
            const float parameter = boundary_parameter(strip.edge, point);
            if (parameter >= min_parameter && parameter <= max_parameter) {
                EXPECT_TRUE(contains_parameter(owner_strip, parameter));
                EXPECT_TRUE(contains_parameter(neighbor_strip, parameter));
            }
        }
        for (const auto& point : *neighbor_points) {
            const float parameter = boundary_parameter(neighbor_edge, point);
            if (parameter >= min_parameter && parameter <= max_parameter) {
                EXPECT_TRUE(contains_parameter(owner_strip, parameter));
                EXPECT_TRUE(contains_parameter(neighbor_strip, parameter));
            }
        }
    }

    wz::engine::assets::TerrainAssetData make_two_chunk_lod_grid()
    {
        using namespace wz::engine::assets::test;

        const float origin[3]{ 0.0f, 0.0f, 0.0f };
        wz::engine::assets::TerrainAssetData terrain =
            detail::make_grid_fixture(
                9000u,
                5u,
                5u,
                1.0f,
                detail::plateau_height,
                origin);

        const float min_bottom[3]{ 0.0f, 0.0f, 0.0f };
        const float max_bottom[3]{ 4.0f, 2.0f, 2.0f };
        const float min_top[3]{ 0.0f, 0.0f, 2.0f };
        const float max_top[3]{ 4.0f, 2.0f, 4.0f };
        terrain.mesh_visual_chunks.clear();
        terrain.mesh_visual_chunks.push_back(
            detail::make_chunk(
                terrain,
                0u,
                48u,
                min_bottom,
                max_bottom));
        terrain.mesh_visual_chunks.push_back(
            detail::make_chunk(
                terrain,
                48u,
                48u,
                min_top,
                max_top));
        terrain.mesh_visual_chunk_count = 2u;
        return terrain;
    }
}

TEST(TerrainVisualProxyAssetModule, CompilesMultiLodProxyFromMeshTerrain)
{
    const wz::fs::Path root =
        test_root("wozzits_terrain_visual_proxy_compile_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    TerrainAsset terrain{};
    TerrainVisualProxyAsset proxy_asset{};
    const TerrainVisualProxyData proxy =
        compile_cube_proxy(assets, &terrain, &proxy_asset);

    ASSERT_TRUE(proxy.valid());
    EXPECT_EQ(proxy.source_asset_key, terrain.output);
    EXPECT_EQ(proxy.terrain_proxy_id.key, proxy_asset.output);
    EXPECT_EQ(
        proxy.primary_representation_kind,
        TerrainVisualRepresentationKind::MeshChunks);
    EXPECT_GT(proxy.chunk_count(), 0u);
    EXPECT_GT(proxy.lod_record_count(), proxy.chunk_count());
    EXPECT_GT(proxy.surfel_count(), 0u);
    EXPECT_EQ(proxy.transition_strip_count(), 0u);

    uint32_t combined_boundary_flags = TerrainVisualChunkBoundary_None;
    for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
        ASSERT_GE(chunk.lods.size(), 1u);
        ASSERT_GE(chunk.surfel_density_levels.size(), 3u);
        ASSERT_FALSE(chunk.surfels.empty());
        const TerrainVisualProxyLodRecord& lod = chunk.lods.front();
        EXPECT_EQ(lod.lod_id, TerrainLodId{ 0u });
        EXPECT_EQ(lod.representation_kind, TerrainVisualRepresentationKind::MeshChunks);
        EXPECT_TRUE(lod.bounds.valid());
        EXPECT_EQ(lod.index_count, chunk.triangle_count * 3u);
        EXPECT_EQ(lod.triangle_count, chunk.triangle_count);
        EXPECT_FLOAT_EQ(lod.conservative_geometric_error, 0.0f);
        EXPECT_GT(lod.boundary_ring.point_count(), 0u);
        uint32_t previous_triangles = lod.triangle_count;
        float previous_error = lod.conservative_geometric_error;
        for (size_t i = 1u; i < chunk.lods.size(); ++i) {
            const TerrainVisualProxyLodRecord& coarse = chunk.lods[i];
            EXPECT_EQ(coarse.lod_id, TerrainLodId{ static_cast<uint32_t>(i) });
            EXPECT_TRUE(coarse.bounds.valid());
            EXPECT_LE(coarse.triangle_count, previous_triangles);
            EXPECT_LE(coarse.index_count, previous_triangles * 3u);
            EXPECT_GE(coarse.conservative_geometric_error, previous_error);
            EXPECT_GT(coarse.boundary_ring.point_count(), 0u);
            previous_triangles = coarse.triangle_count;
            previous_error = coarse.conservative_geometric_error;
        }
        combined_boundary_flags |= chunk.boundary.boundary_flags;
    }
    EXPECT_GT(combined_boundary_flags, TerrainVisualChunkBoundary_None);
}

TEST(TerrainVisualProxyAssetModule, UsesProjectDiskCache)
{
    const wz::fs::Path root =
        test_root("wozzits_terrain_visual_proxy_disk_cache_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path cache_root =
        wz::fs::join(root, ".wozzits/cache");

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    auto resolve_proxy = [&]() -> TerrainVisualProxyData
    {
        EngineAssetLibrary assets{
            device,
            logger,
            root,
            EngineAssetCacheSettings{
                .root = cache_root,
                .enabled = true,
            },
        };
        return compile_cube_proxy(assets);
    };

    const TerrainVisualProxyData first = resolve_proxy();
    ASSERT_TRUE(first.valid());

    const wz::fs::Path cache_directory =
        wz::fs::join(
            wz::fs::join(cache_root, "assets"),
            "terrain_visual_proxy");
    const auto entries = wz::fs::list_directory(cache_directory);
    ASSERT_EQ(entries.error, wz::fs::FileError::None);
    EXPECT_FALSE(entries.value.empty());

    const TerrainVisualProxyData second = resolve_proxy();
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.source_asset_key, first.source_asset_key);
    EXPECT_EQ(second.terrain_proxy_id, first.terrain_proxy_id);
    EXPECT_EQ(second.chunk_count(), first.chunk_count());
    EXPECT_EQ(second.lod_record_count(), first.lod_record_count());
    EXPECT_EQ(second.transition_strip_count(), first.transition_strip_count());
    EXPECT_EQ(second.surfel_count(), first.surfel_count());
    ASSERT_FALSE(second.chunks.empty());
    ASSERT_EQ(second.chunks.front().lods.size(), first.chunks.front().lods.size());
    ASSERT_EQ(
        second.chunks.front().surfel_density_levels.size(),
        first.chunks.front().surfel_density_levels.size());
    ASSERT_EQ(
        second.chunks.front().surfels.size(),
        first.chunks.front().surfels.size());
    EXPECT_EQ(
        second.chunks.front().lods.front().triangle_count,
        first.chunks.front().lods.front().triangle_count);
    EXPECT_EQ(
        second.chunks.front().lods.back().triangle_count,
        first.chunks.front().lods.back().triangle_count);
    EXPECT_EQ(
        second.chunks.front().lods.back().boundary_ring.point_count(),
        first.chunks.front().lods.back().boundary_ring.point_count());
    EXPECT_FLOAT_EQ(
        second.chunks.front().lods.front()
            .source_region_aggregate.normal_variance,
        first.chunks.front().lods.front()
            .source_region_aggregate.normal_variance);
    EXPECT_FLOAT_EQ(
        second.chunks.front().lods.back()
            .lod_surface_aggregate.normal_variance,
        first.chunks.front().lods.back()
            .lod_surface_aggregate.normal_variance);
    EXPECT_FLOAT_EQ(
        second.chunks.front().lods.back()
            .lost_detail_aggregate.height_detail,
        first.chunks.front().lods.back()
            .lost_detail_aggregate.height_detail);
    EXPECT_FLOAT_EQ(
        second.chunks.front().surfel_density_levels.front().spacing,
        first.chunks.front().surfel_density_levels.front().spacing);
    EXPECT_FLOAT_EQ(
        second.chunks.front()
            .surfel_density_levels.front()
            .representative_radius,
        first.chunks.front()
            .surfel_density_levels.front()
            .representative_radius);
    EXPECT_FLOAT_EQ(
        second.chunks.front().surfels.front().radius,
        first.chunks.front().surfels.front().radius);
    EXPECT_FLOAT_EQ(
        second.chunks.front().surfels.front().roughness,
        first.chunks.front().surfels.front().roughness);
}

TEST(TerrainVisualProxyAssetModule, ComputesPrefilterAggregatesPerLod)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData terrain = load_fixture_terrain("noise_terrain");
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(2600u),
            test_key(2700u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_FALSE(proxy.chunks.empty());
    const TerrainVisualProxyChunkRecord& chunk = proxy.chunks.front();
    ASSERT_GE(chunk.lods.size(), 2u);

    const auto& source = chunk.lods.front().source_region_aggregate;
    EXPECT_GE(source.normal_variance, 0.0f);
    EXPECT_LE(source.height_range[0], source.height_range[1]);
    EXPECT_GE(source.roughness, 0.0f);
    ASSERT_FALSE(source.material_histogram.empty());

    float previous_lod_normal_variance =
        chunk.lods.front().lod_surface_aggregate.normal_variance;
    for (const TerrainVisualProxyLodRecord& lod : chunk.lods) {
        EXPECT_FLOAT_EQ(
            lod.source_region_aggregate.normal_variance,
            source.normal_variance);
        EXPECT_FLOAT_EQ(
            lod.source_region_aggregate.height_range[0],
            source.height_range[0]);
        EXPECT_FLOAT_EQ(
            lod.source_region_aggregate.height_range[1],
            source.height_range[1]);
        EXPECT_GE(lod.lod_surface_aggregate.normal_variance, 0.0f);
        EXPECT_LE(
            lod.lod_surface_aggregate.normal_variance,
            previous_lod_normal_variance + 1e-5f);
        EXPECT_GE(lod.lod_surface_aggregate.triangle_area_variance, 0.0f);
        EXPECT_GE(lod.lod_surface_aggregate.max_aspect_ratio, 0.0f);
        EXPECT_GE(lod.lost_detail_aggregate.normal_variance, 0.0f);
        EXPECT_GE(lod.lost_detail_aggregate.height_detail, 0.0f);
        EXPECT_NEAR(
            lod.lost_detail_aggregate.normal_variance,
            std::max(
                0.0f,
                lod.source_region_aggregate.normal_variance
                    - lod.lod_surface_aggregate.normal_variance),
            1e-5f);
        previous_lod_normal_variance =
            lod.lod_surface_aggregate.normal_variance;
    }
}

TEST(TerrainVisualProxyAssetModule, SmoothHemispherePrefilterAggregatesAreModerate)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData terrain = load_fixture_terrain("smooth_hemisphere");
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(2750u),
            test_key(2775u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_EQ(proxy.chunks.size(), 1u);
    const TerrainVisualProxyChunkRecord& chunk = proxy.chunks.front();
    ASSERT_GE(chunk.lods.size(), 2u);

    const float source_normal_variance =
        chunk.lods.front().source_region_aggregate.normal_variance;
    EXPECT_GT(source_normal_variance, 0.0f);
    EXPECT_LT(source_normal_variance, 1.0f);

    float previous_lod_normal_variance =
        chunk.lods.front().lod_surface_aggregate.normal_variance;
    for (const TerrainVisualProxyLodRecord& lod : chunk.lods) {
        EXPECT_FLOAT_EQ(
            lod.source_region_aggregate.normal_variance,
            source_normal_variance);
        EXPECT_LE(
            lod.lod_surface_aggregate.normal_variance,
            previous_lod_normal_variance + 1e-5f);
        EXPECT_GE(lod.lost_detail_aggregate.normal_variance, 0.0f);
        previous_lod_normal_variance =
            lod.lod_surface_aggregate.normal_variance;
    }
}

TEST(TerrainVisualProxyAssetModule, FlatPlaneHasNoLostPrefilterDetail)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData terrain = load_fixture_terrain("flat_plane");
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(2800u),
            test_key(2900u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_EQ(proxy.chunks.size(), 1u);
    for (const TerrainVisualProxyLodRecord& lod : proxy.chunks.front().lods) {
        EXPECT_NEAR(lod.source_region_aggregate.normal_variance, 0.0f, 1e-5f);
        EXPECT_NEAR(lod.lod_surface_aggregate.normal_variance, 0.0f, 1e-5f);
        EXPECT_NEAR(lod.lost_detail_aggregate.normal_variance, 0.0f, 1e-5f);
        EXPECT_NEAR(lod.lost_detail_aggregate.height_detail, 0.0f, 1e-5f);
    }
}

TEST(TerrainVisualProxyAssetModule, FlatPlaneCompilesFarFieldSurfels)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData terrain = load_fixture_terrain("flat_plane");
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(2920u),
            test_key(2940u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_EQ(proxy.chunks.size(), 1u);
    const TerrainVisualProxyChunkRecord& chunk = proxy.chunks.front();
    ASSERT_GE(chunk.surfel_density_levels.size(), 3u);
    ASSERT_GE(chunk.surfels.size(), 6u);

    uint32_t previous_count = UINT32_MAX;
    float previous_spacing = 0.0f;
    for (const TerrainVisualProxySurfelDensityLevel& level :
         chunk.surfel_density_levels)
    {
        EXPECT_TRUE(level.valid());
        EXPECT_LE(level.surfel_count, previous_count);
        EXPECT_GE(level.spacing, previous_spacing);
        EXPECT_LE(level.first_surfel + level.surfel_count, chunk.surfels.size());
        EXPECT_GT(level.representative_radius, 0.0f);
        const uint32_t grid_cells =
            std::max(
                1u,
                static_cast<uint32_t>(
                    std::sqrt(static_cast<float>(level.surfel_count))));
        const float cell_x =
            (chunk.bounds.max[0] - chunk.bounds.min[0])
            / static_cast<float>(grid_cells);
        const float cell_z =
            (chunk.bounds.max[2] - chunk.bounds.min[2])
            / static_cast<float>(grid_cells);
        const float cell_half_diagonal =
            std::sqrt(cell_x * cell_x + cell_z * cell_z) * 0.5f;
        EXPECT_GE(level.representative_radius, cell_half_diagonal - 1e-5f);
        for (uint32_t i = 0u; i < level.surfel_count; ++i) {
            const TerrainVisualProxySurfel& surfel =
                chunk.surfels[level.first_surfel + i];
            EXPECT_FLOAT_EQ(surfel.radius, level.representative_radius);
        }
        previous_count = level.surfel_count;
        previous_spacing = level.spacing;
    }

    for (const TerrainVisualProxySurfel& surfel : chunk.surfels) {
        EXPECT_TRUE(surfel.valid());
        EXPECT_NEAR(surfel.normal[0], 0.0f, 1e-5f);
        EXPECT_NEAR(surfel.normal[1], 1.0f, 1e-5f);
        EXPECT_NEAR(surfel.normal[2], 0.0f, 1e-5f);
        EXPECT_NEAR(surfel.roughness, 0.0f, 1e-5f);
        EXPECT_EQ(surfel.material_id, 0u);
    }
}

TEST(TerrainVisualProxyAssetModule, NoiseTerrainSurfelsCarryPrefilteredRoughness)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData flat_terrain = load_fixture_terrain("flat_plane");
    const TerrainAssetData noise_terrain = load_fixture_terrain("noise_terrain");
    ASSERT_TRUE(flat_terrain.valid());
    ASSERT_TRUE(noise_terrain.valid());
    const TerrainVisualProxyData flat_proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(2960u),
            test_key(2980u),
            flat_terrain);
    const TerrainVisualProxyData noise_proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(3020u),
            test_key(3040u),
            noise_terrain);

    ASSERT_TRUE(flat_proxy.valid());
    ASSERT_TRUE(noise_proxy.valid());
    ASSERT_FALSE(flat_proxy.chunks.front().surfels.empty());
    ASSERT_FALSE(noise_proxy.chunks.front().surfels.empty());
    EXPECT_GT(
        noise_proxy.chunks.front().surfels.front().roughness,
        flat_proxy.chunks.front().surfels.front().roughness);
}

TEST(TerrainVisualProxyAssetModule, SmoothHemisphereSurfelsHaveVaryingNormals)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData terrain = load_fixture_terrain("smooth_hemisphere");
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(3060u),
            test_key(3080u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_EQ(proxy.chunks.size(), 1u);
    const auto& surfels = proxy.chunks.front().surfels;
    ASSERT_GE(surfels.size(), 4u);

    float max_delta = 0.0f;
    for (size_t i = 1u; i < surfels.size(); ++i) {
        max_delta = std::max(max_delta, normal_delta(surfels.front(), surfels[i]));
    }
    EXPECT_GT(max_delta, 0.05f);
}

TEST(TerrainVisualProxyAssetModule, ResamplesAndBlendsPrefilterAggregates)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData terrain = load_fixture_terrain("noise_terrain");
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(3000u),
            test_key(3100u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_FALSE(proxy.chunks.empty());
    ASSERT_GE(proxy.chunks.front().lods.size(), 2u);

    const TerrainVisualProxyPrefilterAggregates fine =
        terrain_visual_proxy_resample_aggregates(
            proxy,
            proxy.chunks.front().chunk_id,
            TerrainLodId{ 0u });
    const TerrainVisualProxyPrefilterAggregates coarse =
        terrain_visual_proxy_resample_aggregates(
            proxy,
            proxy.chunks.front().chunk_id,
            proxy.chunks.front().lods.back().lod_id);
    const TerrainVisualProxyPrefilterAggregates blended =
        terrain_visual_proxy_blend_aggregates(fine, coarse, 0.25f);

    EXPECT_FLOAT_EQ(
        fine.source_region.normal_variance,
        proxy.chunks.front().lods.front()
            .source_region_aggregate.normal_variance);
    EXPECT_GE(blended.lost_detail.normal_variance, 0.0f);
    EXPECT_LE(
        blended.lost_detail.normal_variance,
        std::max(
            fine.lost_detail.normal_variance,
            coarse.lost_detail.normal_variance)
            + 1e-5f);
    EXPECT_LE(
        std::abs(
            blended.lod_surface.normal_variance
            - fine.lod_surface.normal_variance),
        std::abs(
            coarse.lod_surface.normal_variance
            - fine.lod_surface.normal_variance)
            + 1e-5f);
}

TEST(TerrainVisualProxyAssetModule, FixtureLodChainsAreMonotonic)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    uint64_t key_seed = 100u;
    for (const std::string_view name : kTerrainFixtureNames) {
        const TerrainAssetData terrain = load_fixture_terrain(name);
        ASSERT_TRUE(terrain.valid()) << name;

        const TerrainVisualProxyData proxy =
            internal::compile_terrain_visual_proxy_for_tests(
                test_key(key_seed),
                test_key(key_seed + 20u),
                terrain);
        key_seed += 100u;

        ASSERT_TRUE(proxy.valid()) << name;
        ASSERT_EQ(proxy.chunk_count(), terrain.mesh_visual_chunks.size()) << name;
        for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
            ASSERT_GE(chunk.lods.size(), 1u) << name;
            uint32_t previous_triangles = chunk.lods.front().triangle_count;
            float previous_error =
                chunk.lods.front().conservative_geometric_error;
            EXPECT_FLOAT_EQ(previous_error, 0.0f) << name;
            EXPECT_GT(chunk.lods.front().boundary_ring.point_count(), 0u)
                << name;
            for (size_t i = 1u; i < chunk.lods.size(); ++i) {
                const TerrainVisualProxyLodRecord& lod = chunk.lods[i];
                EXPECT_LE(lod.triangle_count, previous_triangles) << name;
                EXPECT_GE(lod.conservative_geometric_error, previous_error)
                    << name;
                EXPECT_TRUE(lod.bounds.valid()) << name;
                EXPECT_GT(lod.boundary_ring.point_count(), 0u) << name;
                previous_triangles = lod.triangle_count;
                previous_error = lod.conservative_geometric_error;
            }
        }
    }
}

TEST(TerrainVisualProxyAssetModule, FlatPlaneReachesTwoTriangleZeroErrorLod)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData terrain = load_fixture_terrain("flat_plane");
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(1000u),
            test_key(1100u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_EQ(proxy.chunks.size(), 1u);
    ASSERT_GE(proxy.chunks.front().lods.size(), 2u);
    const TerrainVisualProxyLodRecord& coarsest =
        proxy.chunks.front().lods.back();
    EXPECT_LE(coarsest.triangle_count, 2u);
    EXPECT_FLOAT_EQ(coarsest.conservative_geometric_error, 0.0f);
}

TEST(TerrainVisualProxyAssetModule, MultiChunkSeamCarriesNeighborAndLodRings)
{
    using namespace wz::engine::assets;
    using namespace wz::engine::assets::test;

    const TerrainAssetData terrain = load_fixture_terrain("multi_chunk_seam");
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(2000u),
            test_key(2100u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_EQ(proxy.chunks.size(), 2u);
    EXPECT_EQ(
        proxy.chunks[0].boundary.positive_x_neighbor,
        TerrainChunkId{ 1u });
    EXPECT_EQ(
        proxy.chunks[1].boundary.negative_x_neighbor,
        TerrainChunkId{ 0u });

    for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
        for (const TerrainVisualProxyLodRecord& lod : chunk.lods) {
            EXPECT_GT(lod.boundary_ring.point_count(), 0u);
        }
    }
}

TEST(TerrainVisualProxyAssetModule, MultiChunkGridPrecomputesMixedLodTransitions)
{
    using namespace wz::engine::assets;

    const TerrainAssetData terrain = make_two_chunk_lod_grid();
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(2200u),
            test_key(2300u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_EQ(proxy.chunks.size(), 2u);
    EXPECT_GT(proxy.transition_strip_count(), 0u);
    uint32_t max_mixed_lod_pairs = 0u;
    for (const TerrainVisualProxyLodRecord& lod : proxy.chunks[0].lods) {
        for (const TerrainVisualProxyLodRecord& neighbor_lod :
             proxy.chunks[1].lods)
        {
            if (lod.lod_id != neighbor_lod.lod_id) {
                ++max_mixed_lod_pairs;
            }
        }
    }
    EXPECT_LE(proxy.transition_strip_count(), max_mixed_lod_pairs);

    bool saw_mixed_lod_strip = false;
    bool saw_non_flat_strip = false;
    bool saw_gap_covered_by_strip = false;
    for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
        for (const TerrainVisualProxyTransitionStrip& strip :
             chunk.transition_strips)
        {
            EXPECT_TRUE(strip.valid());
            EXPECT_LT(strip.chunk_id.value, strip.neighbor_chunk_id.value);
            EXPECT_NE(strip.lod_id, strip.neighbor_lod_id);
            ASSERT_GE(strip.vertices.size(), 3u);
            EXPECT_GT(strip.triangle_count(), 0u);
            EXPECT_LE(strip.triangle_count(), strip.vertices.size() - 2u);
            expect_transition_sides_share_parameters(strip);
            saw_non_flat_strip |= std::any_of(
                strip.vertices.begin(),
                strip.vertices.end(),
                [](const TerrainVisualProxyTransitionVertex& vertex) {
                    return vertex.position[1] > 0.0f;
                });

            ASSERT_LT(strip.chunk_id.value, proxy.chunks.size());
            ASSERT_LT(strip.neighbor_chunk_id.value, proxy.chunks.size());
            const TerrainVisualProxyLodRecord* lod =
                find_lod(proxy.chunks[strip.chunk_id.value], strip.lod_id);
            const TerrainVisualProxyLodRecord* neighbor_lod =
                find_lod(
                    proxy.chunks[strip.neighbor_chunk_id.value],
                    strip.neighbor_lod_id);
            ASSERT_NE(lod, nullptr);
            ASSERT_NE(neighbor_lod, nullptr);

            const TerrainLodSeamReport seam =
                analyze_terrain_lod_seam(
                    proxy,
                    TerrainLodSeamEndpoint{
                        .chunk_id = strip.chunk_id,
                        .lod_id = strip.lod_id,
                        .edge = strip.edge,
                    },
                    TerrainLodSeamEndpoint{
                        .chunk_id = strip.neighbor_chunk_id,
                        .lod_id = strip.neighbor_lod_id,
                        .edge = opposite_terrain_boundary_edge(strip.edge),
                    },
                    0.001f);
            if (seam.valid() && seam.gap_exceeds_tolerance) {
                expect_transition_covers_boundary_breakpoints(
                    strip,
                    *lod,
                    *neighbor_lod);
                saw_gap_covered_by_strip = true;
            }

            if (strip.lod_id == TerrainLodId{ 0u }
                && strip.neighbor_lod_id != TerrainLodId{ 0u })
            {
                saw_mixed_lod_strip = true;
            }
        }
    }
    EXPECT_TRUE(saw_mixed_lod_strip);
    EXPECT_TRUE(saw_non_flat_strip);
    EXPECT_TRUE(saw_gap_covered_by_strip);
}

TEST(TerrainVisualProxyAssetModule, TransitionStripOverheadStaysBounded)
{
    using namespace wz::engine::assets;

    const TerrainAssetData terrain = make_two_chunk_lod_grid();
    ASSERT_TRUE(terrain.valid());
    const TerrainVisualProxyData proxy =
        internal::compile_terrain_visual_proxy_for_tests(
            test_key(2400u),
            test_key(2500u),
            terrain);

    ASSERT_TRUE(proxy.valid());
    ASSERT_GT(proxy.transition_strip_count(), 0u);

    uint64_t lod_boundary_points = 0u;
    uint64_t transition_vertices = 0u;
    uint64_t transition_indices = 0u;
    uint64_t transition_triangles = 0u;
    for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
        for (const TerrainVisualProxyLodRecord& lod : chunk.lods) {
            lod_boundary_points += lod.boundary_ring.point_count();
        }
        for (const TerrainVisualProxyTransitionStrip& strip :
             chunk.transition_strips)
        {
            transition_vertices += strip.vertices.size();
            transition_indices += strip.indices.size();
            transition_triangles += strip.triangle_count();
        }
    }

    const uint64_t source_triangles =
        terrain.mesh_visual_indices.empty()
            ? terrain.mesh_surface_indices.size() / 3u
            : terrain.mesh_visual_indices.size() / 3u;

    EXPECT_LE(
        proxy.transition_strip_count(),
        proxy.lod_record_count() * proxy.lod_record_count());
    EXPECT_LE(transition_vertices, lod_boundary_points * 2u);
    EXPECT_LE(transition_indices, transition_vertices * 6u);
    EXPECT_LE(transition_triangles, source_triangles * 2u);
}

TEST(TerrainVisualProxyAssetModule, ModifiedSourceTerrainUsesDistinctCacheKey)
{
    const wz::fs::Path root =
        test_root("wozzits_terrain_visual_proxy_cache_invalidation_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path cache_root =
        wz::fs::join(root, ".wozzits/cache");

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    auto resolve_proxy =
        [&](uint32_t visual_chunk_count,
            TerrainAsset& terrain,
            TerrainVisualProxyAsset& proxy_asset) -> TerrainVisualProxyData
    {
        EngineAssetLibrary assets{
            device,
            logger,
            root,
            EngineAssetCacheSettings{
                .root = cache_root,
                .enabled = true,
            },
        };
        return compile_cube_proxy(
            assets,
            &terrain,
            &proxy_asset,
            visual_chunk_count);
    };

    TerrainAsset first_terrain{};
    TerrainVisualProxyAsset first_proxy_asset{};
    const TerrainVisualProxyData first =
        resolve_proxy(4u, first_terrain, first_proxy_asset);
    ASSERT_TRUE(first.valid());

    TerrainAsset second_terrain{};
    TerrainVisualProxyAsset second_proxy_asset{};
    const TerrainVisualProxyData second =
        resolve_proxy(6u, second_terrain, second_proxy_asset);
    ASSERT_TRUE(second.valid());

    EXPECT_NE(second_terrain.output, first_terrain.output);
    EXPECT_NE(second_proxy_asset.output, first_proxy_asset.output);
    EXPECT_EQ(first.source_asset_key, first_terrain.output);
    EXPECT_EQ(second.source_asset_key, second_terrain.output);
    EXPECT_EQ(first.terrain_proxy_id.key, first_proxy_asset.output);
    EXPECT_EQ(second.terrain_proxy_id.key, second_proxy_asset.output);

    const wz::fs::Path cache_directory =
        wz::fs::join(
            wz::fs::join(cache_root, "assets"),
            "terrain_visual_proxy");
    const auto entries = wz::fs::list_directory(cache_directory);
    ASSERT_EQ(entries.error, wz::fs::FileError::None);
    EXPECT_GE(entries.value.size(), 2u);
}

TEST(TerrainVisualProxyAssetModule, RegistersCompiledProxyWithRenderResolver)
{
    const wz::fs::Path root =
        test_root("wozzits_terrain_visual_proxy_resolver_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const TerrainVisualProxyData proxy = compile_cube_proxy(assets);
    ASSERT_TRUE(proxy.valid());

    std::vector<TerrainVisualChunk> chunks;
    chunks.reserve(proxy.chunks.size());
    for (const TerrainVisualProxyChunkRecord& proxy_chunk : proxy.chunks) {
        ASSERT_GE(proxy_chunk.lods.size(), 1u);
        const TerrainVisualProxyLodRecord& lod = proxy_chunk.lods.front();

        TerrainVisualChunk chunk{};
        for (uint32_t axis = 0; axis < 3u; ++axis) {
            chunk.bounds_min[axis] = proxy_chunk.bounds.min[axis];
            chunk.bounds_max[axis] = proxy_chunk.bounds.max[axis];
        }
        chunk.first_index = lod.first_index;
        chunk.index_count = lod.index_count;
        chunk.aggregate.triangle_count = lod.triangle_count;
        chunk.aggregate.mean_height =
            (lod.source_region_aggregate.height_range[0]
             + lod.source_region_aggregate.height_range[1])
            * 0.5f;
        chunks.push_back(chunk);
    }

    wz::engine::rendering::RenderResourceResolver resolver;
    const wz::gpu::GPUHandle gpu_mesh{
        .id = 9u,
        .epoch = 1u,
        .type = wz::gpu::GPUResourceType::Mesh,
    };

    ASSERT_TRUE(resolver.register_terrain_proxy(
        proxy.terrain_proxy_id,
        gpu_mesh,
        // The program value is opaque to the resolver here -- the assertions
        // below are about proxy chunk diagnostics, not about the program.
        BuiltinRenderProgram::TerrainSurfelSurface,
        {},
        {},
        0.0f,
        {},
        std::span<const TerrainVisualChunk>(chunks.data(), chunks.size())));

    const auto diagnostics =
        resolver.resolve_terrain_proxy_diagnostics(proxy.terrain_proxy_id);
    ASSERT_TRUE(diagnostics.has_value());
    EXPECT_EQ(diagnostics->proxy_chunks, proxy.chunk_count());
    EXPECT_GT(diagnostics->source_triangles, 0u);
}
