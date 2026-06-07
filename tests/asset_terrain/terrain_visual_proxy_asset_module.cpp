#include <gtest/gtest.h>

#include "terrain_fixture_assets.h"

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/terrain/terrain_visual_proxy_compilers.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/render_resource_resolver.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

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

    uint32_t combined_boundary_flags = TerrainVisualChunkBoundary_None;
    for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
        ASSERT_GE(chunk.lods.size(), 1u);
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
    ASSERT_FALSE(second.chunks.empty());
    ASSERT_EQ(second.chunks.front().lods.size(), first.chunks.front().lods.size());
    EXPECT_EQ(
        second.chunks.front().lods.front().triangle_count,
        first.chunks.front().lods.front().triangle_count);
    EXPECT_EQ(
        second.chunks.front().lods.back().triangle_count,
        first.chunks.front().lods.back().triangle_count);
    EXPECT_EQ(
        second.chunks.front().lods.back().boundary_ring.point_count(),
        first.chunks.front().lods.back().boundary_ring.point_count());
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
            lod.source_region_aggregate.mean_height;
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
        BuiltinRenderProgram::TerrainMeshSurface,
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
