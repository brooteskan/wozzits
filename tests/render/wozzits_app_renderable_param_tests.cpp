// tests/render/wozzits_app_renderable_param_tests.cpp
//
// Coverage for the SET_RENDERABLE_PARAM behavior verb (issue #232) in the
// WozzitsApp_v1 shared runtime: a behavior animates an authored renderable
// CONSTANT at runtime — the audio SET_GAIN pattern applied to looks.
//
// The fixture (renderable_param_fixture) carries a "tinted" node with an
// authored renderable_constants "tint" override ([0.2, 0.7, 0.3, 1.0]) bound to
// pulse_tint_on_frame, which emits SET_RENDERABLE_PARAM("tint", 0.9, 0.1, 0.4)
// every frame.update. It reuses the behavior fixture's staged module DLL
// (behavior_module_test_plugin).
//
// The verb flows plugin ABI -> from_abi_command_kind -> the host command-pass
// drain: resolve the entity to its authored node, resolve the name hash against
// the node's declared/overridden constants, and write the per-instance override
// (the #229 seam) — no re-key, no recompile. Only x/y/z are carried; the host
// preserves the fourth component (alpha). The override lives on the stable
// authored node, so it survives a structural behavior-scene rebuild.
//
// On-device test (WozzitsApp_v1 owns a RhiSceneRenderer needing a DX12 device).
// Skipped if no device can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/project/project_manifest.h>

#include <gpu/gpu.h>
#include <input/input.h>

#include <array>

namespace
{
    constexpr const char* kProjectRoot = "projects/renderable_param_fixture";

    struct WozzitsAppRenderableParamFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_renderable_param_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device renderable-param test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        wz::app::WozzitsAppSceneLoadDesc scene_load_desc() const
        {
            const auto project = wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
            EXPECT_TRUE(project.ok) << project.error;
            return wz::app::WozzitsAppSceneLoadDesc{
                .asset_graph = project.manifest.asset_graph_path,
                .scene = project.manifest.scene_path,
                .behavior_module_folder =
                    project.manifest.behavior_module_folder,
            };
        }
    };
}

// One tick: the bound pulse_tint behavior emits SET_RENDERABLE_PARAM, and the
// host drain writes the node's "tint" override to the pulsed r/g/b — with the
// authored alpha preserved (the command carries only three components).
TEST_F(WozzitsAppRenderableParamFixture, BehaviorPulsesAuthoredConstant)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(scene_load_desc()));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u)
        << "the scene's one pulse_tint behavior binding was not materialized";

    // The authored override parsed onto the node, untouched before dispatch.
    const auto before = app.node_renderable_constant("tinted", "tint");
    ASSERT_TRUE(before.has_value()) << "authored 'tint' override missing";
    EXPECT_FLOAT_EQ((*before)[0], 0.2f);
    EXPECT_FLOAT_EQ((*before)[1], 0.7f);
    EXPECT_FLOAT_EQ((*before)[2], 0.3f);
    EXPECT_FLOAT_EQ((*before)[3], 1.0f);

    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);

    const auto after = app.node_renderable_constant("tinted", "tint");
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ((*after)[0], 0.9f);
    EXPECT_FLOAT_EQ((*after)[1], 0.1f);
    EXPECT_FLOAT_EQ((*after)[2], 0.4f);
    // Alpha was NOT carried by the command — the host preserved the prior 1.0.
    EXPECT_FLOAT_EQ((*after)[3], 1.0f)
        << "the fourth component was not preserved";
}

// The override lives on the stable authored node, so a structural
// behavior-scene rebuild (add_child) leaves it intact — no re-tick needed to
// re-establish it, unlike runtime-only state.
TEST_F(WozzitsAppRenderableParamFixture, OverrideSurvivesStructuralRebuild)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(scene_load_desc()));

    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const auto pulsed = app.node_renderable_constant("tinted", "tint");
    ASSERT_TRUE(pulsed.has_value());
    EXPECT_FLOAT_EQ((*pulsed)[0], 0.9f);

    // A structural edit rebuilds the behavior runtime. The renderable_constants
    // override is authored scene-node state (keyed by the stable node id), NOT
    // runtime-only state, so it must survive the rebuild WITHOUT another tick.
    const auto added = app.add_child_node("root");
    ASSERT_TRUE(added.ok) << added.error;

    const auto after_rebuild = app.node_renderable_constant("tinted", "tint");
    ASSERT_TRUE(after_rebuild.has_value())
        << "the override was lost in the rebuild";
    EXPECT_FLOAT_EQ((*after_rebuild)[0], 0.9f);
    EXPECT_FLOAT_EQ((*after_rebuild)[1], 0.1f);
    EXPECT_FLOAT_EQ((*after_rebuild)[2], 0.4f);
    EXPECT_FLOAT_EQ((*after_rebuild)[3], 1.0f);
}

// An unknown constant name is a logged no-op: nothing on the node changes.
// (Driven directly here — the fixture behavior only ever pulses "tint".)
TEST_F(WozzitsAppRenderableParamFixture, UnknownConstantNameIsNoOp)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(scene_load_desc()));

    // No override exists for a name the node never declared.
    EXPECT_FALSE(
        app.node_renderable_constant("tinted", "not_a_field").has_value());

    // The direct seam rejects an empty name and a missing node (the drain's
    // name-hash resolution is the behavior-facing analog of these guards).
    const float v[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    EXPECT_FALSE(app.set_node_renderable_constant("no_such_node", "tint", v));
    EXPECT_FALSE(app.set_node_renderable_constant("tinted", "", v));
}
