#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
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
        uint32_t visual_chunk_count = 4u)
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
}

TEST(TerrainVisualProxyAssetModule, CompilesSingleLodProxyFromMeshTerrain)
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
    EXPECT_EQ(proxy.lod_record_count(), proxy.chunk_count());

    uint32_t combined_boundary_flags = TerrainVisualChunkBoundary_None;
    for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
        ASSERT_EQ(chunk.lods.size(), 1u);
        const TerrainVisualProxyLodRecord& lod = chunk.lods.front();
        EXPECT_EQ(lod.lod_id, TerrainLodId{ 0u });
        EXPECT_EQ(lod.representation_kind, TerrainVisualRepresentationKind::MeshChunks);
        EXPECT_EQ(lod.index_count, chunk.triangle_count * 3u);
        EXPECT_EQ(lod.triangle_count, chunk.triangle_count);
        EXPECT_FLOAT_EQ(lod.conservative_geometric_error, 0.0f);
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
    EXPECT_EQ(
        second.chunks.front().lods.front().triangle_count,
        first.chunks.front().lods.front().triangle_count);
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
        ASSERT_EQ(proxy_chunk.lods.size(), 1u);
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
