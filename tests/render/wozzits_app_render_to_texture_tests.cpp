// tests/render/wozzits_app_render_to_texture_tests.cpp
//
// Issue #287 end to end: an AUTHORED render-to-texture source fills its target.
//
// #281 part 1 made the target authorable, but who rendered into it was still
// app C++ -- an authored target with no code behind it was simply never written
// to, and a surface sampling it got whatever the acquire left. This drives the
// whole loop through the shipping frame (WozzitsApp_v1::render_scene) against a
// scene whose ONLY authoring is a render_to_texture component, and asserts both
// halves of what that component promises:
//
//   * the target texture receives the node's draw, and
//   * the node is dropped from the main pass, so art meant for a surface does
//     not also appear floating in the scene.
//
// The second is what makes the first meaningful: without it, a backbuffer with
// pixels in it would prove nothing about the offscreen pass.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/assets/scene/scene_asset_data.h>
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

    struct WozzitsAppRenderToTextureFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_render_to_texture_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device render-to-texture test";
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
                "render_to_texture.scene.json");
            return desc;
        }

        void render_one_frame(wz::app::WozzitsApp_v1& app)
        {
            ASSERT_TRUE(wz::gpu::begin_frame(ctx.device));
            wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
            app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
            EXPECT_TRUE(app.render_scene());
            ASSERT_TRUE(wz::gpu::end_frame(ctx.device));
        }
    };
}

TEST_F(WozzitsAppRenderToTextureFixture, AuthoredSourceFillsItsTargetAndLeavesTheScene)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(load_desc()));

    // The component's target anchor bridged to a resolved key -- the same
    // asset-graph-node-id -> AssetKey hop renderable_asset makes. Without this
    // the source renders nowhere and the rest of the test is vacuous.
    wz::asset::AssetKey target{};
    for (const wz::engine::assets::SceneNodeAsset& node :
         app.authored_scene_nodes())
    {
        if (node.render_to_texture) {
            target = node.render_to_texture->target;
        }
    }
    ASSERT_FALSE(target == wz::asset::AssetKey{})
        << "render_to_texture target did not resolve to a texture asset";

    render_one_frame(app);

    // The authored target got the draw.
    const wz::gpu::GPUHandle handle = app.texture_gpu_handle(target);
    ASSERT_TRUE(handle.valid())
        << "the authored render-target texture is not resident";

    std::vector<std::uint8_t> texels;
    ASSERT_TRUE(wz::gpu::dx12::internal::read_texture_rgba8_dx12(
        ctx.device, handle, texels));
    ASSERT_FALSE(texels.empty());
    std::size_t drawn = 0;
    for (std::size_t i = 0; i + 3 < texels.size(); i += 4) {
        if (texels[i] > 8u || texels[i + 1] > 8u || texels[i + 2] > 8u
            || texels[i + 3] > 8u)
        {
            ++drawn;
        }
    }
    EXPECT_GT(drawn, 0u)
        << "nothing was rendered into the authored render-target texture -- "
           "the component is inert (#287)";

    // ...and the source left the main pass: the scene's only drawable node is
    // the source, so the backbuffer must still be the clear colour.
    std::vector<std::uint8_t> backbuffer;
    ASSERT_TRUE(wz::gpu::dx12::internal::read_backbuffer_rgba8_dx12(
        ctx.device, backbuffer));
    std::size_t nonclear = 0;
    for (std::size_t i = 0; i + 3 < backbuffer.size(); i += 4) {
        const int dr = static_cast<int>(backbuffer[i]) - 26;
        const int dg = static_cast<int>(backbuffer[i + 1]) - 26;
        const int db = static_cast<int>(backbuffer[i + 2]) - 31;
        if (dr > 15 || dr < -15 || dg > 15 || dg < -15 || db > 15 || db < -15) {
            ++nonclear;
        }
    }
    EXPECT_EQ(nonclear, 0u)
        << "the render-to-texture source also drew into the main pass; with "
           "also_draw_in_scene off it must appear only on its target";

    wz::gpu::present(ctx.device, /*sync_interval*/ 0);
}
