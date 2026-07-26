// tests/render/wozzits_app_composite_material_tests.cpp
//
// Issues #285 / #288 end to end, through the shipping frame: an AUTHORED
// composite material is filled by the engine and sampled by a surface, and the
// composite pass is SKIPPED once nothing feeding it has changed.
//
// The fixture scene carries the whole chain and no C++ knows any of it:
//   art_source  -- a render_to_texture source filling render target node 27
//   node 28     -- a composite material: clear to red, place node 27 over it
//   surface     -- binds node 28 at material_albedo (program node 26, whose
//                  layout has the texture row a material needs)

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/texture/texture.h>
#include <engine/assets/texture_asset_module.h>
#include <engine/project/project_manifest.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <gpu/dx12/dx12_internal.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kProjectRoot = "projects/test_rebind_fixture";

    struct WozzitsAppCompositeMaterialFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_composite_material_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device composite test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        wz::app::WozzitsAppSceneLoadDesc load_desc() const
        {
            const auto project = wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
            EXPECT_TRUE(project.ok) << project.error;

            wz::app::WozzitsAppSceneLoadDesc desc{};
            desc.asset_graph = project.manifest.asset_graph_path;
            desc.scene = wz::fs::join(
                wz::fs::parent_path(project.manifest.scene_path),
                "composite_material.scene.json");
            return desc;
        }

        void render_one_frame(wz::app::WozzitsApp_v1& app)
        {
            ASSERT_TRUE(wz::gpu::begin_frame(ctx.device));
            wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
            app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
            EXPECT_TRUE(app.render_scene());
            ASSERT_TRUE(wz::gpu::end_frame(ctx.device));
            wz::gpu::present(ctx.device, /*sync_interval*/ 0);
        }

        // The composite material the scene's surface binds, found the way the
        // driver finds it: ask the renderables what they bind.
        wz::asset::AssetKey bound_composite_material(
            wz::app::WozzitsApp_v1& app) const
        {
            for (const wz::engine::assets::SceneNodeAsset& node :
                 app.authored_scene_nodes())
            {
                if (!node.renderable_asset.has_value()) {
                    continue;
                }
                const auto* recipe =
                    ctx.assets->renderables().get_rhi_renderable_recipe(
                        wz::engine::assets::RenderableAsset{
                            .output = *node.renderable_asset });
                if (!recipe) {
                    continue;
                }
                for (const auto& binding : recipe->bindings) {
                    const auto* compiled =
                        ctx.assets->system().find_compiled(binding.key);
                    if (!compiled) {
                        continue;
                    }
                    const auto* texture =
                        ctx.assets->textures().get_texture_data(
                            wz::engine::assets::TextureHandle{
                                compiled->handle });
                    if (texture && texture->is_composite) {
                        return binding.key;
                    }
                }
            }
            return {};
        }
    };
}

TEST_F(WozzitsAppCompositeMaterialFixture, AuthoredMaterialIsCompositedAndBound)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(load_desc()));

    const wz::asset::AssetKey material = bound_composite_material(app);
    ASSERT_FALSE(material == wz::asset::AssetKey{})
        << "no surface in the scene binds an authored composite material";

    // The authored recipe reached the compiled asset.
    const auto* compiled = ctx.assets->system().find_compiled(material);
    ASSERT_NE(compiled, nullptr);
    const wz::engine::assets::TextureData* recipe =
        ctx.assets->textures().get_texture_data(
            wz::engine::assets::TextureHandle{ compiled->handle });
    ASSERT_NE(recipe, nullptr);
    EXPECT_TRUE(recipe->is_composite);
    EXPECT_FLOAT_EQ(recipe->base_colour[0], 1.0f);
    EXPECT_FLOAT_EQ(recipe->base_colour[1], 0.0f);
    ASSERT_EQ(recipe->composite_layers.size(), 1u);
    EXPECT_FALSE(recipe->composite_layers[0].source == wz::asset::AssetKey{})
        << "the layer's source port did not resolve";

    render_one_frame(app);

    // The material carries the authored BASE COLOUR (red), which is the half a
    // "did anything draw" check would miss: an unrun composite leaves the target
    // as-acquired, which is not red.
    const wz::gpu::GPUHandle target = app.texture_gpu_handle(material);
    ASSERT_TRUE(target.valid());
    std::vector<std::uint8_t> texels;
    ASSERT_TRUE(wz::gpu::dx12::internal::read_texture_rgba8_dx12(
        ctx.device, target, texels));
    ASSERT_FALSE(texels.empty());

    std::size_t red = 0;
    for (std::size_t i = 0; i + 3 < texels.size(); i += 4) {
        if (texels[i] > 200u && texels[i + 1] < 60u && texels[i + 2] < 60u) {
            ++red;
        }
    }
    EXPECT_GT(red, 0u)
        << "the authored base colour never reached the material texture";
}

// #288: a target nothing feeds and a material nothing changed cost ZERO passes.
// The scene is static -- a fixed camera over an unmoving cube -- so after the
// first frame every redraw would produce the same texels.
TEST_F(WozzitsAppCompositeMaterialFixture, StaticSceneStopsRefreshingAndCompositing)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(load_desc()));

    render_one_frame(app);

    // The first frame must do the work: there is no previous state to compare
    // against, and the target starts as-acquired.
    const std::size_t targets_after_first = app.render_target_pass_count();
    const std::size_t composites_after_first = app.composite_pass_count();
    EXPECT_EQ(targets_after_first, 1u)
        << "the authored render target was not filled on the first frame";
    EXPECT_EQ(composites_after_first, 1u)
        << "the authored material was not composited on the first frame";

    for (int i = 0; i < 5; ++i) {
        render_one_frame(app);
    }

    EXPECT_EQ(app.render_target_pass_count(), targets_after_first)
        << "an unchanged render-to-texture source was refreshed anyway";
    EXPECT_EQ(app.composite_pass_count(), composites_after_first)
        << "an unchanged composite material was rebuilt anyway (#288)";

    // The skip must not be a one-way latch: MOVING the source has to bring both
    // passes back. Otherwise a gate that never re-fires looks identical to a
    // working one in a static test.
    wz::engine::assets::AuthoredTransform moved{};
    moved.translation[0] = 10.0f;
    moved.rotation_quat[3] = 1.0f;
    moved.scale[0] = moved.scale[1] = moved.scale[2] = 200.0f;
    ASSERT_TRUE(app.set_node_transform("art_source", moved));
    render_one_frame(app);

    EXPECT_EQ(app.render_target_pass_count(), targets_after_first + 1u)
        << "moving the source did not refresh its target";
    EXPECT_EQ(app.composite_pass_count(), composites_after_first + 1u)
        << "a refreshed layer source did not rebuild the material";

    // ...and it settles again once the move is done.
    for (int i = 0; i < 3; ++i) {
        render_one_frame(app);
    }
    EXPECT_EQ(app.render_target_pass_count(), targets_after_first + 1u);
    EXPECT_EQ(app.composite_pass_count(), composites_after_first + 1u);
}
