#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/terrain/terrain_visual_proxy.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

TEST(TerrainAssetModule, ResolvesHeightFieldTerrain)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_terrain_heightfield_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "terrain/height_gradient",
            .width = 8,
            .height = 4,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 2.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
        });
    ASSERT_TRUE(field.valid());

    TerrainFromHeightFieldDesc desc{};
    desc.name = "terrain/heightfield";
    desc.height_field = field;
    desc.size[0] = 64.0f;
    desc.size[1] = 32.0f;
    desc.vertical_scale = 10.0f;
    desc.base_height = -5.0f;

    const auto terrain = assets.terrains().create_from_height_field(desc);
    ASSERT_TRUE(terrain.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle = assets.terrains().get_terrain(terrain);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeTerrain);

    const auto* data = assets.terrains().get_terrain_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->representation, TerrainRepresentationKind::HeightField);
    EXPECT_EQ(data->height_field, field.output);
    EXPECT_EQ(data->resolution_x, 8u);
    EXPECT_EQ(data->resolution_y, 4u);
    EXPECT_EQ(data->height_samples.size(), 32u);
    EXPECT_FLOAT_EQ(data->size[0], 64.0f);
    EXPECT_FLOAT_EQ(data->size[1], 32.0f);
    EXPECT_FLOAT_EQ(data->min_height, -5.0f);
    EXPECT_FLOAT_EQ(data->max_height, 5.0f);
    EXPECT_TRUE(data->supports_height_query);
    EXPECT_FALSE(data->supports_ray_query);
}

TEST(TerrainAssetModule, ResolvesMeshTerrain)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_terrain_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "terrain/mesh_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto terrain =
        assets.terrains().create_from_mesh({
            .name = "terrain/mesh_surface",
            .mesh = mesh,
        });
    ASSERT_TRUE(terrain.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle = assets.terrains().get_terrain(terrain);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.terrains().get_terrain_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->representation, TerrainRepresentationKind::MeshSurface);
    EXPECT_EQ(data->mesh, mesh.output);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[1], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], 0.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[1], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 0.0f);
    EXPECT_EQ(
        data->mesh_height_policy,
        TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface);
    EXPECT_FLOAT_EQ(data->min_surface_normal_y, 0.2f);
    EXPECT_FALSE(data->include_backfaces);
    EXPECT_TRUE(data->mesh_has_source_normals);
    EXPECT_TRUE(data->mesh_has_source_uv0);
    EXPECT_EQ(data->normal_source, TerrainNormalSource::MeshVertexNormal);
    EXPECT_EQ(data->uv_source, TerrainUVSource::MeshUV0);
    EXPECT_EQ(data->mesh_triangle_count, 2u);
    EXPECT_EQ(data->mesh_accepted_surface_triangle_count, 0u);
    EXPECT_FALSE(data->supports_height_query);
    EXPECT_FALSE(data->supports_ray_query);
}

TEST(TerrainAssetModule, MeshTerrainUsesProjectDiskCache)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_terrain_mesh_disk_cache_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path cache_root =
        wz::fs::join(root, ".wozzits/cache");

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    auto resolve_terrain = [&]() -> TerrainAssetData
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

        const auto mesh =
            assets.meshes().create_procedural_mesh({
                .name = "terrain/cache_mesh_cube",
                .kind = ProceduralMeshKind::Cube,
            });
        EXPECT_TRUE(mesh.valid());

        const auto terrain =
            assets.terrains().create_from_mesh({
                .name = "terrain/cache_mesh_surface",
                .mesh = mesh,
                .min_surface_normal_y = 0.0f,
                .include_backfaces = true,
                .visual_chunk_count = 512u,
            });
        EXPECT_TRUE(terrain.valid());

        EXPECT_TRUE(assets.commit());
        const auto report = assets.resolve_all();
        EXPECT_TRUE(report.ok());

        const auto handle = assets.terrains().get_terrain(terrain);
        EXPECT_TRUE(handle.valid());
        const TerrainAssetData* data =
            assets.terrains().get_terrain_data(handle);
        EXPECT_NE(data, nullptr);
        return data ? *data : TerrainAssetData{};
    };

    const TerrainAssetData first = resolve_terrain();
    ASSERT_TRUE(first.valid());
    EXPECT_EQ(first.representation, TerrainRepresentationKind::MeshSurface);
    EXPECT_EQ(first.mesh_visual_chunk_count, 512u);
    EXPECT_FALSE(first.mesh_visual_indices.empty());
    EXPECT_FALSE(first.mesh_visual_chunks.empty());

    const wz::fs::Path cache_directory =
        wz::fs::join(wz::fs::join(cache_root, "assets"), "mesh_terrain");
    const auto entries = wz::fs::list_directory(cache_directory);
    ASSERT_EQ(entries.error, wz::fs::FileError::None);
    EXPECT_FALSE(entries.value.empty());

    const TerrainAssetData second = resolve_terrain();
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.representation, first.representation);
    EXPECT_EQ(second.mesh_visual_chunk_count, first.mesh_visual_chunk_count);
    EXPECT_EQ(second.mesh_triangle_count, first.mesh_triangle_count);
    EXPECT_EQ(
        second.mesh_accepted_surface_triangle_count,
        first.mesh_accepted_surface_triangle_count);
    EXPECT_EQ(second.mesh_surface_points.size(), first.mesh_surface_points.size());
    EXPECT_EQ(second.mesh_surface_indices.size(), first.mesh_surface_indices.size());
    EXPECT_EQ(second.mesh_visual_indices.size(), first.mesh_visual_indices.size());
    EXPECT_EQ(second.mesh_visual_chunks.size(), first.mesh_visual_chunks.size());
    ASSERT_FALSE(second.mesh_visual_chunks.empty());
    EXPECT_EQ(
        second.mesh_visual_chunks.front().index_count,
        first.mesh_visual_chunks.front().index_count);
}

TEST(TerrainAssetModule, DuplicateMeshTerrainRegistrationReturnsSameAsset)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_terrain_duplicate_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "terrain/mesh_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto first =
        assets.terrains().create_from_mesh({
            .name = "terrain/mesh_surface",
            .mesh = mesh,
        });
    const auto second =
        assets.terrains().create_from_mesh({
            .name = "terrain/mesh_surface",
            .mesh = mesh,
        });

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.output, first.output);
}

TEST(TerrainAssetModule, MeshTerrainPolicyAndSourcePreferencesAffectAsset)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_terrain_mesh_policy_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "terrain/mesh_cube",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    const auto default_terrain =
        assets.terrains().create_from_mesh({
            .name = "terrain/mesh_surface",
            .mesh = mesh,
        });
    const auto backface_terrain =
        assets.terrains().create_from_mesh({
            .name = "terrain/mesh_surface",
            .mesh = mesh,
            .min_surface_normal_y = 0.5f,
            .include_backfaces = true,
            .preferred_normal_source = TerrainNormalSource::DerivedGeometry,
            .preferred_uv_source = TerrainUVSource::PlanarXZ,
        });

    ASSERT_TRUE(default_terrain.valid());
    ASSERT_TRUE(backface_terrain.valid());
    EXPECT_NE(default_terrain.output, backface_terrain.output);

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle = assets.terrains().get_terrain(backface_terrain);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.terrains().get_terrain_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->mesh_has_source_normals);
    EXPECT_FALSE(data->mesh_has_source_uv0);
    EXPECT_EQ(data->normal_source, TerrainNormalSource::DerivedGeometry);
    EXPECT_EQ(data->uv_source, TerrainUVSource::PlanarXZ);
    EXPECT_FLOAT_EQ(data->min_surface_normal_y, 0.5f);
    EXPECT_TRUE(data->include_backfaces);
    EXPECT_EQ(data->mesh_triangle_count, 12u);
    EXPECT_GT(data->mesh_accepted_surface_triangle_count, 0u);
}

TEST(TerrainVisualProxyData, DefinesStableCpuReadableLodVocabulary)
{
    using namespace wz::engine::assets;

    const wz::asset::AssetKey source_key{
        .content_hash = { 1, 2 },
        .schema_hash = { 3, 4 },
        .compiler_hash = { 5, 6 },
        .deps_hash = { 7, 8 },
    };
    const wz::asset::AssetKey proxy_key{
        .content_hash = { 11, 12 },
        .schema_hash = { 13, 14 },
        .compiler_hash = { 15, 16 },
        .deps_hash = { 17, 18 },
    };

    TerrainVisualProxyLodRecord lod0{};
    lod0.lod_id = TerrainLodId{ 0 };
    lod0.representation_id = TerrainRepresentationId{ 10 };
    lod0.representation_kind = TerrainVisualRepresentationKind::MeshChunks;
    lod0.bounds.min[0] = -1.0f;
    lod0.bounds.min[1] = 0.0f;
    lod0.bounds.min[2] = -2.0f;
    lod0.bounds.max[0] = 1.0f;
    lod0.bounds.max[1] = 3.0f;
    lod0.bounds.max[2] = 2.0f;
    lod0.first_index = 0;
    lod0.index_count = 300;
    lod0.first_vertex = 0;
    lod0.vertex_count = 120;
    lod0.triangle_count = 100;
    lod0.conservative_geometric_error = 0.0f;
    lod0.source_region_aggregate.normal_variance = 0.4f;
    lod0.source_region_aggregate.height_range[0] = 1.0f;
    lod0.source_region_aggregate.height_range[1] = 7.0f;
    lod0.source_region_aggregate.roughness = 2.0f;
    lod0.lod_surface_aggregate.normal_variance = 0.25f;
    lod0.lod_surface_aggregate.height_range[0] = 1.5f;
    lod0.lod_surface_aggregate.height_range[1] = 6.5f;
    lod0.lost_detail_aggregate.normal_variance = 0.15f;
    lod0.lost_detail_aggregate.height_detail = 1.0f;
    lod0.source_region_aggregate.material_histogram.push_back(
        TerrainMaterialCoverage{ .material_id = 3, .coverage = 0.75f });

    TerrainVisualProxyLodRecord lod1 = lod0;
    lod1.lod_id = TerrainLodId{ 1 };
    lod1.first_index = 300;
    lod1.index_count = 48;
    lod1.vertex_count = 24;
    lod1.triangle_count = 16;
    lod1.conservative_geometric_error = 0.5f;

    TerrainVisualProxyChunkRecord chunk{};
    chunk.chunk_id = TerrainChunkId{ 42 };
    chunk.representation_id = TerrainRepresentationId{ 10 };
    chunk.representation_kind = TerrainVisualRepresentationKind::MeshChunks;
    chunk.bounds.min[0] = -1.0f;
    chunk.bounds.min[1] = 0.0f;
    chunk.bounds.min[2] = -2.0f;
    chunk.bounds.max[0] = 1.0f;
    chunk.bounds.max[1] = 3.0f;
    chunk.bounds.max[2] = 2.0f;
    chunk.first_triangle = 0;
    chunk.triangle_count = 100;
    chunk.first_vertex = 0;
    chunk.vertex_count = 120;
    chunk.aggregate.normal_mean[1] = 1.0f;
    chunk.boundary.boundary_flags =
        TerrainVisualChunkBoundary_NegativeX
        | TerrainVisualChunkBoundary_PositiveZ;
    chunk.boundary.positive_x_neighbor = TerrainChunkId{ 43 };
    chunk.lods = { lod0, lod1 };

    TerrainVisualProxyData proxy{};
    proxy.compiler_version = 23;
    proxy.source_asset_key = source_key;
    proxy.simplification_settings_hash = { 101, 202 };
    proxy.terrain_proxy_id = TerrainProxyId{ proxy_key };
    proxy.bounds = chunk.bounds;
    proxy.chunks.push_back(chunk);

    ASSERT_TRUE(proxy.valid());
    EXPECT_EQ(proxy.schema_version, kTerrainVisualProxySchemaVersion);
    EXPECT_EQ(proxy.source_asset_key, source_key);
    EXPECT_EQ(proxy.terrain_proxy_id.key, proxy_key);
    EXPECT_EQ(proxy.chunk_count(), 1u);
    EXPECT_EQ(proxy.lod_record_count(), 2u);

    const TerrainVisualProxyChunkRecord& stored_chunk = proxy.chunks.front();
    EXPECT_EQ(stored_chunk.chunk_id, TerrainChunkId{ 42 });
    EXPECT_EQ(stored_chunk.lods[1].lod_id, TerrainLodId{ 1 });
    EXPECT_EQ(
        stored_chunk.lods[1].representation_id,
        TerrainRepresentationId{ 10 });
    EXPECT_EQ(stored_chunk.lods[1].triangle_count, 16u);
    EXPECT_FLOAT_EQ(
        stored_chunk.lods[1].conservative_geometric_error,
        0.5f);
    EXPECT_FLOAT_EQ(
        stored_chunk.lods[0].source_region_aggregate.roughness,
        2.0f);
    EXPECT_FLOAT_EQ(
        stored_chunk.lods[0].lod_surface_aggregate.normal_variance,
        0.25f);
    EXPECT_FLOAT_EQ(
        stored_chunk.lods[0].lost_detail_aggregate.height_detail,
        1.0f);
    ASSERT_EQ(
        stored_chunk.lods[0].source_region_aggregate.material_histogram.size(),
        1u);
    EXPECT_FLOAT_EQ(
        stored_chunk.lods[0]
            .source_region_aggregate
            .material_histogram[0]
            .coverage,
        0.75f);
}

TEST(TerrainVisualProxyData, RejectsMissingVersionIdentityOrLods)
{
    using namespace wz::engine::assets;

    const wz::asset::AssetKey source_key{
        .content_hash = { 1, 0 },
        .schema_hash = { 2, 0 },
        .compiler_hash = { 3, 0 },
        .deps_hash = { 4, 0 },
    };
    const wz::asset::AssetKey proxy_key{
        .content_hash = { 5, 0 },
        .schema_hash = { 6, 0 },
        .compiler_hash = { 7, 0 },
        .deps_hash = { 8, 0 },
    };

    TerrainVisualProxyLodRecord lod{};
    lod.bounds.max[0] = 1.0f;
    lod.bounds.max[1] = 1.0f;
    lod.bounds.max[2] = 1.0f;
    lod.triangle_count = 4;
    lod.vertex_count = 6;

    TerrainVisualProxyChunkRecord chunk{};
    chunk.bounds.max[0] = 1.0f;
    chunk.bounds.max[1] = 1.0f;
    chunk.bounds.max[2] = 1.0f;
    chunk.triangle_count = 4;
    chunk.vertex_count = 6;
    chunk.lods.push_back(lod);

    TerrainVisualProxyData proxy{};
    proxy.compiler_version = 1;
    proxy.source_asset_key = source_key;
    proxy.terrain_proxy_id = TerrainProxyId{ proxy_key };
    proxy.bounds = chunk.bounds;
    proxy.chunks.push_back(chunk);
    ASSERT_TRUE(proxy.valid());

    TerrainVisualProxyData stale_schema = proxy;
    stale_schema.schema_version = kTerrainVisualProxySchemaVersion + 1u;
    EXPECT_FALSE(stale_schema.valid());

    TerrainVisualProxyData missing_source = proxy;
    missing_source.source_asset_key = {};
    EXPECT_FALSE(missing_source.valid());

    TerrainVisualProxyData missing_identity = proxy;
    missing_identity.terrain_proxy_id = {};
    EXPECT_FALSE(missing_identity.valid());

    TerrainVisualProxyData missing_lods = proxy;
    missing_lods.chunks.front().lods.clear();
    EXPECT_FALSE(missing_lods.valid());
}
