#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>



TEST(RenderableAssetModule, ResolvesMeshWireframeRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_tests");

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
            .name = "debug/cube",
            .kind = ProceduralMeshKind::Cube,
            });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/cube_wireframe",
            .mesh = mesh,
            });

    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeRenderable);

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[1], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[1], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 1.0f);
}

TEST(RenderableAssetModule, ResolvesDepthTestedMeshWireframeRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_depth_mesh_tests");

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
            .name = "debug/depth_cube",
            .kind = ProceduralMeshKind::Cube,
            });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/depth_cube_wireframe",
            .mesh = mesh,
            .program = BuiltinRenderProgram::MeshWireframeDepthDebug,
            .domain = RenderDomain::Debug,
            .policy_flags =
                RenderPolicy_Wireframe
                | RenderPolicy_DepthTest
                | RenderPolicy_DepthWrite,
            });

    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::Mesh);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthTest) != 0);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_DepthWrite) != 0);
}

TEST(RenderableAssetModule, MeshWireframeRenderableDomainParticipatesInIdentity)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_domain_identity_tests");

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
            .name = "debug/shared_quad",
            .kind = ProceduralMeshKind::Quad,
        });

    ASSERT_TRUE(mesh.valid());

    const auto debug_renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/shared_quad_wireframe",
            .mesh = mesh,
            .domain = RenderDomain::Debug,
        });

    const auto opaque_renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/shared_quad_wireframe",
            .mesh = mesh,
            .domain = RenderDomain::Opaque,
        });

    ASSERT_TRUE(debug_renderable.valid());
    ASSERT_TRUE(opaque_renderable.valid());
    EXPECT_FALSE(debug_renderable.output == opaque_renderable.output);

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto debug_handle =
        assets.renderables().get_renderable(debug_renderable);
    const auto opaque_handle =
        assets.renderables().get_renderable(opaque_renderable);

    ASSERT_TRUE(debug_handle.valid());
    ASSERT_TRUE(opaque_handle.valid());

    const auto* debug_data =
        assets.renderables().get_renderable_data(debug_handle);
    const auto* opaque_data =
        assets.renderables().get_renderable_data(opaque_handle);

    ASSERT_NE(debug_data, nullptr);
    ASSERT_NE(opaque_data, nullptr);
    EXPECT_EQ(debug_data->domain, RenderDomain::Debug);
    EXPECT_EQ(opaque_data->domain, RenderDomain::Opaque);
}

TEST(RenderableAssetModule, MeshWireframeRenderableBoundsComeFromMeshVertices)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_mesh_bounds_tests");

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
            .name = "debug/quad",
            .kind = ProceduralMeshKind::Quad,
        });

    ASSERT_TRUE(mesh.valid());

    const auto renderable =
        assets.renderables().create_mesh_wireframe({
            .name = "debug/quad_wireframe",
            .mesh = mesh,
        });

    ASSERT_TRUE(renderable.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[1], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], 0.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[1], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 0.0f);
}

TEST(RenderableAssetModule, ResolvesGaussianSplatDebugRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_splat_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto cloud =
        assets.gaussian_splats().create_procedural_cloud({
            .name = "debug/splat_sphere",
            .count = 64,
            .radius = 2.0f,
            .splat_scale = 1.0f,
            });

    ASSERT_TRUE(cloud.valid());

    const auto renderable =
        assets.renderables().create_gaussian_splat_debug({
            .name = "debug/splat_sphere_debug",
            .splat_cloud = cloud,
            });

    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeRenderable);

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->kind, RenderableKind::GaussianSplatCloud);
    EXPECT_EQ(data->program, BuiltinRenderProgram::GaussianSplatDebug);
    EXPECT_EQ(data->domain, RenderDomain::Splat);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_AlphaBlend) != 0);

    EXPECT_LE(data->bounds_min[0], -2.0f);
    EXPECT_GE(data->bounds_max[0], 2.0f);
}

TEST(RenderableAssetModule, ResolvesScalarFieldDebugRenderable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_renderable_scalar_field_tests");

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
            .name = "debug/scalar_gradient_debug",
            .scalar_field = field,
            });

    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.renderables().get_renderable(renderable);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeRenderable);

    const auto* data =
        assets.renderables().get_renderable_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());

    EXPECT_EQ(data->kind, RenderableKind::ScalarField);
    EXPECT_EQ(data->program, BuiltinRenderProgram::ScalarFieldDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_EQ(data->policy_flags, RenderPolicy_None);
    EXPECT_EQ(data->source_asset, field.output);
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

