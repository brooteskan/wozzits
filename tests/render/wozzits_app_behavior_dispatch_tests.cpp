// tests/render/wozzits_app_behavior_dispatch_tests.cpp
//
// Coverage for the WozzitsApp_v1 behavior runtime: a project whose scene binds a
// node to a project behavior-module DLL must, inside the shared runtime, load +
// register that DLL, dispatch frame.update to the binding, and apply the
// produced transform command — with the result visible on the authored scene
// node (what the renderer draws from).
//
// The fixture (behavior_test_fixture) is self-contained and separate from the
// editable sample projects: an empty asset graph + a scene with one "mover" node
// bound to the module "move_up_on_frame", whose DLL (behavior_module_test_plugin)
// CMake stages into the fixture's behavior_module_folder. The module writes an
// add-local-translation of (0, +1, 0) on every frame.update.
//
// This is an on-device test (WozzitsApp_v1 owns a RhiSceneRenderer that needs a
// DX12 device). Skipped if no device can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/project/project_manifest.h>

#include <gpu/gpu.h>
#include <input/input.h>

namespace
{
    constexpr const char* kProjectRoot = "projects/behavior_test_fixture";

    struct WozzitsAppBehaviorFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_behavior_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device behavior test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        wz::engine::project::ProjectManifestLoadResult load_test_project() const
        {
            return wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
        }

        wz::app::WozzitsAppSceneLoadDesc scene_load_desc(
            const wz::engine::project::ProjectManifest& project) const
        {
            return wz::app::WozzitsAppSceneLoadDesc{
                .asset_graph = project.asset_graph_path,
                .scene = project.scene_path,
                .behavior_module_folder = project.behavior_module_folder,
            };
        }
    };
}

// load_scene must load the project behavior DLL, register its module, and
// materialize the scene's one behavior binding.
TEST_F(WozzitsAppBehaviorFixture, LoadSceneMaterializesProjectBehavior)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_FALSE(project.manifest.behavior_module_folder.empty())
        << "fixture manifest must point at the staged behavior module folder";

    // The empty asset graph compiles fine; load_scene returns true overall.
    EXPECT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "the scene's one behavior binding was not materialized";
}

// A simulation_tick must dispatch frame.update to the binding and apply the
// resulting add-local-translation command, moving the authored node up.
TEST_F(WozzitsAppBehaviorFixture, FrameUpdateDispatchAppliesTransformCommand)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);

    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("mover");
    ASSERT_TRUE(before.has_value()) << "mover node missing from scene";
    EXPECT_FLOAT_EQ(before->y, 0.0f);

    // One tick: behavior writes ADD_LOCAL_TRANSLATION (0,+1,0); apply moves the
    // node and the change is written back to the authored scene node.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);

    const std::optional<wz::math::Vec3> after_one =
        app.node_local_translation("mover");
    ASSERT_TRUE(after_one.has_value());
    EXPECT_FLOAT_EQ(after_one->y, 1.0f)
        << "frame.update transform command did not apply to scene_nodes_";

    // It is per-frame: a second tick advances it again.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_two =
        app.node_local_translation("mover");
    ASSERT_TRUE(after_two.has_value());
    EXPECT_FLOAT_EQ(after_two->y, 2.0f)
        << "behaviors did not dispatch on the second frame";
}
