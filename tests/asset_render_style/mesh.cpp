#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

TEST(MeshRenderStyleAssetModule, CreatesAndResolvesVectorWireframeStyle)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_mesh_render_style_vector_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    MeshRenderStyleData style{};
    style.wireframe.color[0] = 0.1f;
    style.wireframe.color[1] = 1.0f;
    style.wireframe.color[2] = 0.25f;
    style.wireframe.color[3] = 1.0f;
    style.wireframe.emissive_strength = 2.0f;
    style.depth_test = true;
    style.depth_write = false;
    style.double_sided = true;
    style.hidden_line_prepass = true;

    const MeshRenderStyleAsset asset =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/vector_green",
            .style = style,
        });
    ASSERT_TRUE(asset.valid());

    ASSERT_TRUE(assets.commit());
    const ResolveReport report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const MeshRenderStyleHandle handle =
        assets.mesh_render_styles().get_mesh_render_style(asset);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeMeshRenderStyle);

    const MeshRenderStyleData* data =
        assets.mesh_render_styles().get_mesh_render_style_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_TRUE(data->wireframe.enabled);
    EXPECT_FALSE(data->surface.enabled);
    EXPECT_FLOAT_EQ(data->wireframe.color[1], 1.0f);
    EXPECT_FLOAT_EQ(data->wireframe.emissive_strength, 2.0f);
    EXPECT_TRUE(data->depth_test);
    EXPECT_FALSE(data->depth_write);
    EXPECT_TRUE(data->double_sided);
    EXPECT_TRUE(data->hidden_line_prepass);
}

TEST(MeshRenderStyleAssetModule, SurfaceLayerParticipatesInIdentity)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_mesh_render_style_identity_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    MeshRenderStyleData surface{};
    surface.wireframe.enabled = false;
    surface.surface.enabled = true;
    surface.surface.color[3] = 0.35f;
    surface.depth_test = true;
    surface.depth_write = false;
    surface.double_sided = false;
    surface.hidden_line_prepass = false;

    MeshRenderStyleData depth_writing = surface;
    depth_writing.depth_write = true;

    const MeshRenderStyleAsset first =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/transparent_panel",
            .style = surface,
        });
    const MeshRenderStyleAsset duplicate =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/transparent_panel",
            .style = surface,
        });
    const MeshRenderStyleAsset changed =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/transparent_panel",
            .style = depth_writing,
        });

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(duplicate.valid());
    ASSERT_TRUE(changed.valid());
    EXPECT_TRUE(first.output == duplicate.output);
    EXPECT_FALSE(first.output == changed.output);

    ASSERT_TRUE(assets.commit());
    const ResolveReport report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);
}

TEST(MeshRenderStyleAssetModule, MaskStyleParticipatesInIdentity)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_mesh_render_style_mask_identity_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    MeshRenderStyleData mask_style{};
    mask_style.wireframe.enabled = false;
    mask_style.surface.enabled = true;
    mask_style.mask.enabled = true;
    mask_style.mask.rules = {
        MeshMaskRule{
            .input_channel_id = 0x3100u,
            .lo = 1.0f,
            .hi = 1.0f,
            .color = { 1.0f, 0.0f, 0.0f, 1.0f },
            .priority = 10,
        },
    };
    ASSERT_TRUE(mask_style.valid());

    MeshRenderStyleData changed = mask_style;
    changed.mask.rules[0].color[1] = 0.5f;

    MeshRenderStyleData invalid = mask_style;
    invalid.field_visualization.enabled = true;
    invalid.field_visualization.channel_id = 0x3100u;
    EXPECT_FALSE(invalid.valid());

    const MeshRenderStyleAsset first =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/face_mask",
            .style = mask_style,
        });
    const MeshRenderStyleAsset duplicate =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/face_mask",
            .style = mask_style,
        });
    const MeshRenderStyleAsset color_changed =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/face_mask",
            .style = changed,
        });

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(duplicate.valid());
    ASSERT_TRUE(color_changed.valid());
    EXPECT_TRUE(first.output == duplicate.output);
    EXPECT_FALSE(first.output == color_changed.output);

    ASSERT_TRUE(assets.commit());
    const ResolveReport report = assets.resolve_all();
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);
}
