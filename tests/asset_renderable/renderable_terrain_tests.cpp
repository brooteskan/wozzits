#include "renderable_asset_module_test_support.h"

TEST(RenderableAssetModule, ResolvesHeightFieldTerrainDebugRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_heightfield_terrain_tests");

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
            .width = 16,
            .height = 16,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 2.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
            });

    ASSERT_TRUE(field.valid());

    const auto terrain =
        assets.terrains().create_from_height_field({
            .name = "terrain/heightfield",
            .height_field = field,
            .origin = { -4.0f, -8.0f },
            .size = { 8.0f, 16.0f },
            .vertical_scale = 3.0f,
            .base_height = 1.0f,
            });

    ASSERT_TRUE(terrain.valid());

    const auto renderable =
        assets.renderables().create_terrain_debug({
            .name = "terrain/heightfield_debug",
            .terrain = terrain,
            });

    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::ScalarField);
    EXPECT_EQ(data->source_asset, field.output);
    EXPECT_EQ(data->companion_asset, terrain.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -4.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 4.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], -8.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 8.0f);
}

TEST(RenderableAssetModule, TerrainDebugEditorDepthFailureRequiresLibraryResetForCleanRecovery)
{
    using namespace wz::engine::assets;

    const auto make_root = [](const char* suffix) {
        return wz::fs::join(wz::fs::temp_directory_path(), suffix);
    };

    const auto count_failure =
        [](const ResolveReport& report,
           wz::asset::AssetKey key,
           wz::asset::ResolveError error) {
            return std::count_if(
                report.failures.begin(),
                report.failures.end(),
                [&](const ResolveFailure& failure) {
                    return failure.key == key && failure.error == error;
                });
        };

    wz::Logger logger;
    wz::gpu::Device device{};

    const wz::fs::Path bad_root =
        make_root("wozzits_renderable_terrain_editor_bad_depth_tests");
    ASSERT_EQ(wz::fs::create_directories(bad_root), wz::fs::FileError::None);

    {
        EngineAssetLibrary assets{
            device,
            logger,
            bad_root,
        };

        const auto field =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "editor/heightfield",
                .width = 16,
                .height = 16,
                .depth = 8,
                .generator = ScalarFieldGenerator::GradientX,
                .frequency = 1.0f,
                .amplitude = 1.0f,
                .format = ScalarFieldFormat::Float32,
                .domain_kind = ScalarFieldDomainKind::Spatial2D,
            });

        ASSERT_TRUE(field.valid());

        const auto terrain =
            assets.terrains().create_from_height_field({
                .name = "editor/terrain",
                .height_field = field,
                .origin = { 0.0f, 0.0f },
                .size = { 16.0f, 16.0f },
                .vertical_scale = 2.0f,
                .base_height = 0.0f,
            });

        ASSERT_TRUE(terrain.valid());

        const auto renderable =
            assets.renderables().create_terrain_debug({
                .name = "editor/terrain_debug",
                .terrain = terrain,
            });

        ASSERT_TRUE(renderable.valid());
        ASSERT_TRUE(assets.commit());

        const auto report = assets.resolve_all();
        EXPECT_FALSE(report.ok());
        EXPECT_EQ(report.resolved_count, 0u);
        EXPECT_EQ(count_failure(
            report,
            field.output,
            wz::asset::ResolveError::CompileFailed), 1);
        EXPECT_EQ(count_failure(
            report,
            terrain.output,
            wz::asset::ResolveError::DependencyFailed), 1);
        EXPECT_EQ(count_failure(
            report,
            renderable.output,
            wz::asset::ResolveError::DependencyFailed), 1);

        EXPECT_FALSE(assets.scalar_fields().get_scalar_field(field).valid());
        EXPECT_FALSE(assets.terrains().get_terrain(terrain).valid());
        EXPECT_FALSE(assets.renderables().get_renderable(renderable).valid());
        EXPECT_EQ(assets.system().find_compiled(field.output), nullptr);
        EXPECT_EQ(assets.system().find_compiled(terrain.output), nullptr);
        EXPECT_EQ(assets.system().find_compiled(renderable.output), nullptr);
    }

    const wz::fs::Path good_root =
        make_root("wozzits_renderable_terrain_editor_recovered_depth_tests");
    ASSERT_EQ(wz::fs::create_directories(good_root), wz::fs::FileError::None);

    {
        EngineAssetLibrary assets{
            device,
            logger,
            good_root,
        };

        const auto field =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "editor/heightfield",
                .width = 16,
                .height = 16,
                .depth = 1,
                .generator = ScalarFieldGenerator::GradientX,
                .frequency = 1.0f,
                .amplitude = 1.0f,
                .format = ScalarFieldFormat::Float32,
                .domain_kind = ScalarFieldDomainKind::Spatial2D,
            });

        ASSERT_TRUE(field.valid());

        const auto terrain =
            assets.terrains().create_from_height_field({
                .name = "editor/terrain",
                .height_field = field,
                .origin = { 0.0f, 0.0f },
                .size = { 16.0f, 16.0f },
                .vertical_scale = 2.0f,
                .base_height = 0.0f,
            });

        ASSERT_TRUE(terrain.valid());

        const auto renderable =
            assets.renderables().create_terrain_debug({
                .name = "editor/terrain_debug",
                .terrain = terrain,
            });

        ASSERT_TRUE(renderable.valid());
        ASSERT_TRUE(assets.commit());

        const auto report = assets.resolve_all();
        EXPECT_TRUE(report.ok());
        EXPECT_EQ(report.resolved_count, 3u);
        EXPECT_TRUE(assets.scalar_fields().get_scalar_field(field).valid());
        EXPECT_TRUE(assets.terrains().get_terrain(terrain).valid());
        EXPECT_TRUE(assets.renderables().get_renderable(renderable).valid());
    }
}

TEST(RenderableAssetModule, ResolvesMeshTerrainDebugRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_terrain_tests");

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

    const auto renderable =
        assets.renderables().create_terrain_debug({
            .name = "terrain/mesh_surface_debug",
            .terrain = terrain,
            });

    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->source_asset, mesh.output);
    EXPECT_EQ(data->companion_asset, terrain.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
}

TEST(RenderableAssetModule, ResolvesMeshTerrainSurfaceRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_terrain_surface_tests");

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
            .name = "terrain/surface_mesh_quad",
            .kind = ProceduralMeshKind::Quad,
            });

    ASSERT_TRUE(mesh.valid());

    const auto terrain =
        assets.terrains().create_from_mesh({
            .name = "terrain/surface_mesh",
            .mesh = mesh,
            });

    ASSERT_TRUE(terrain.valid());

    const auto renderable =
        assets.renderables().create_terrain_surface({
            .name = "terrain/surface_mesh_renderable",
            .terrain = terrain,
            });

    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->source_asset, mesh.output);
    EXPECT_EQ(data->companion_asset, terrain.output);
    EXPECT_EQ(data->program, BuiltinRenderProgram::TerrainMeshSurface);
    EXPECT_EQ(data->domain, RenderDomain::Opaque);
    EXPECT_EQ(data->terrain_lighting.mode, TerrainLightingMode::SceneLights);
    EXPECT_EQ(data->policy_flags & RenderPolicy_Wireframe, 0u);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
}

TEST(RenderableAssetModule, TerrainSurfaceRenderableCarriesHDRILighting)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_terrain_hdri_lighting_tests");

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
            .name = "terrain/hdri_surface_mesh_quad",
            .kind = ProceduralMeshKind::Quad,
            });
    ASSERT_TRUE(mesh.valid());

    const auto terrain =
        assets.terrains().create_from_mesh({
            .name = "terrain/hdri_surface_mesh",
            .mesh = mesh,
            });
    ASSERT_TRUE(terrain.valid());

    TerrainLightingData lighting{};
    lighting.mode = TerrainLightingMode::HDRIEnvironment;
    lighting.environment_color[0] = 0.6f;
    lighting.environment_color[1] = 0.7f;
    lighting.environment_color[2] = 1.0f;
    lighting.environment_intensity = 0.8f;
    lighting.dominant_light_direction[0] = 0.0f;
    lighting.dominant_light_direction[1] = -0.5f;
    lighting.dominant_light_direction[2] = 0.8660254f;
    lighting.dominant_light_intensity = 2.0f;
    lighting.sky_visibility_strength = 0.75f;
    lighting.terrain_bounce_strength = 0.1f;

    const auto renderable =
        assets.renderables().create_terrain_surface({
            .name = "terrain/hdri_surface_mesh_renderable",
            .terrain = terrain,
            .lighting = lighting,
            });
    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(handle.valid());
    const auto* data =
        assets.renderables().get_renderable_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->terrain_lighting.mode, TerrainLightingMode::HDRIEnvironment);
    EXPECT_FLOAT_EQ(data->terrain_lighting.environment_color[1], 0.7f);
    EXPECT_FLOAT_EQ(data->terrain_lighting.environment_intensity, 0.8f);
    EXPECT_FLOAT_EQ(
        data->terrain_lighting.dominant_light_direction[2],
        0.8660254f);
    EXPECT_FLOAT_EQ(data->terrain_lighting.dominant_light_intensity, 2.0f);
    EXPECT_FLOAT_EQ(data->terrain_lighting.sky_visibility_strength, 0.75f);
    EXPECT_FLOAT_EQ(data->terrain_lighting.terrain_bounce_strength, 0.1f);
}

TEST(RenderableAssetModule, RejectsHeightFieldTerrainSurfaceRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_heightfield_terrain_surface_tests");

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
            .name = "terrain/heightfield_surface_source",
            .width = 2,
            .height = 2,
            .depth = 1,
            });

    ASSERT_TRUE(field.valid());

    const auto terrain =
        assets.terrains().create_from_height_field({
            .name = "terrain/heightfield_surface",
            .height_field = field,
            .size = { 1.0f, 1.0f },
            .vertical_scale = 1.0f,
            });

    ASSERT_TRUE(terrain.valid());

    const auto renderable =
        assets.renderables().create_terrain_surface({
            .name = "terrain/heightfield_surface_renderable",
            .terrain = terrain,
            });

    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());
    EXPECT_FALSE(assets.renderables().get_renderable(renderable).valid());
}

TEST(RenderableAssetModule, RejectsScalarFieldDebugRenderableWithInvalidSource)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_scalar_field_invalid_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto renderable =
        assets.renderables().create_scalar_field_debug({
            .name = "debug/invalid_scalar_field_debug",
            .scalar_field = ScalarFieldAsset{},
            });

    EXPECT_FALSE(renderable.valid());
}

TEST(RenderableAssetModule, RejectsScalarFieldDebugRenderableWithEmptyName)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_scalar_field_empty_name_tests");

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
            .name = "debug/scalar_gradient",
            .width = 16,
            .height = 16,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 1.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
            });

    ASSERT_TRUE(field.valid());

    const auto renderable =
        assets.renderables().create_scalar_field_debug({
            .name = "",
            .scalar_field = field,
            });

    EXPECT_FALSE(renderable.valid());
}

