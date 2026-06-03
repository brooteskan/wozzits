#include "renderable_asset_module_test_support.h"

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

