// tests/engine/editor_runtime_control_tests.cpp
//
// EditorRuntimeControl is the cross-thread seam between the editor (owner
// thread) and the in-process engine runtime. These cover the live scene-edit
// channel device-free: posting node transforms, coalescing per node id, and
// draining them through an applier on the engine-thread side. The blocking
// bind handshake and the on-device apply (WozzitsApp_v1) are covered elsewhere.

#include <gtest/gtest.h>

#include <engine/app/editor_runtime.h>

#include <string>
#include <vector>

namespace
{
    using wz::app::EditorRuntimeControl;
    using wz::app::SceneNodePropertiesEdit;
    using wz::app::SceneNodeTransformEdit;

    SceneNodeTransformEdit make_edit(std::string id, float tx)
    {
        SceneNodeTransformEdit edit;
        edit.id = std::move(id);
        edit.transform.translation[0] = tx;
        return edit;
    }

    // Drain the queue on the engine-thread side, recording what the applier saw.
    std::vector<SceneNodeTransformEdit> drain(EditorRuntimeControl& control)
    {
        std::vector<SceneNodeTransformEdit> applied;
        control.service_pending_scene_node_transforms(
            [&applied](const SceneNodeTransformEdit& edit) {
                applied.push_back(edit);
            });
        return applied;
    }
}

TEST(EditorRuntimeControl, ServiceAppliesPostedTransform)
{
    EditorRuntimeControl control;
    control.post_scene_node_transform(make_edit("node", 5.f));

    const auto applied = drain(control);
    ASSERT_EQ(applied.size(), 1u);
    EXPECT_EQ(applied[0].id, "node");
    EXPECT_FLOAT_EQ(applied[0].transform.translation[0], 5.f);
}

TEST(EditorRuntimeControl, DrainsExactlyOnce)
{
    EditorRuntimeControl control;
    control.post_scene_node_transform(make_edit("node", 5.f));

    EXPECT_EQ(drain(control).size(), 1u);
    // A second service finds an empty queue and applies nothing.
    EXPECT_TRUE(drain(control).empty());
}

TEST(EditorRuntimeControl, CoalescesByNodeIdLatestWins)
{
    EditorRuntimeControl control;
    control.post_scene_node_transform(make_edit("node", 1.f));
    control.post_scene_node_transform(make_edit("node", 2.f));
    control.post_scene_node_transform(make_edit("node", 3.f));

    const auto applied = drain(control);
    ASSERT_EQ(applied.size(), 1u);  // one node -> one apply
    EXPECT_FLOAT_EQ(applied[0].transform.translation[0], 3.f);  // latest wins
}

TEST(EditorRuntimeControl, DistinctNodesEachApplied)
{
    EditorRuntimeControl control;
    control.post_scene_node_transform(make_edit("a", 1.f));
    control.post_scene_node_transform(make_edit("b", 2.f));

    EXPECT_EQ(drain(control).size(), 2u);
}

TEST(EditorRuntimeControl, ServiceWithEmptyQueueDoesNothing)
{
    EditorRuntimeControl control;
    EXPECT_TRUE(drain(control).empty());
}

TEST(EditorRuntimeControl, PropertiesPostCoalescesByIdAndDrainsOnce)
{
    EditorRuntimeControl control;
    control.post_scene_node_properties(
        SceneNodePropertiesEdit{ .id = "n", .name = "a", .visible = true });
    control.post_scene_node_properties(
        SceneNodePropertiesEdit{ .id = "n", .name = "b", .visible = false });

    std::vector<SceneNodePropertiesEdit> applied;
    control.service_pending_scene_node_properties(
        [&applied](const SceneNodePropertiesEdit& edit) { applied.push_back(edit); });

    ASSERT_EQ(applied.size(), 1u);     // one node -> one apply
    EXPECT_EQ(applied[0].name, "b");   // latest wins
    EXPECT_FALSE(applied[0].visible);

    std::vector<SceneNodePropertiesEdit> again;
    control.service_pending_scene_node_properties(
        [&again](const SceneNodePropertiesEdit& edit) { again.push_back(edit); });
    EXPECT_TRUE(again.empty());
}

TEST(EditorRuntimeControl, ReorderPostCoalescesByIdAndDrainsOnce)
{
    using wz::app::SceneNodeReorderEdit;

    EditorRuntimeControl control;
    control.post_scene_node_reorder(
        SceneNodeReorderEdit{ .id = "n", .before_id = "a" });
    control.post_scene_node_reorder(
        SceneNodeReorderEdit{ .id = "n", .before_id = "b" });  // latest dest
    control.post_scene_node_reorder(
        SceneNodeReorderEdit{ .id = "m", .before_id = {} });   // distinct node

    std::vector<SceneNodeReorderEdit> applied;
    control.service_pending_scene_node_reorders(
        [&applied](const SceneNodeReorderEdit& edit) {
            applied.push_back(edit);
        });

    ASSERT_EQ(applied.size(), 2u);  // n coalesced to one, m distinct
    EXPECT_EQ(applied[0].id, "n");
    EXPECT_EQ(applied[0].before_id, "b");  // latest destination wins
    EXPECT_EQ(applied[1].id, "m");
    EXPECT_TRUE(applied[1].before_id.empty());  // empty => move to end

    std::vector<SceneNodeReorderEdit> again;
    control.service_pending_scene_node_reorders(
        [&again](const SceneNodeReorderEdit& edit) { again.push_back(edit); });
    EXPECT_TRUE(again.empty());
}

// Custom-renderable ingredient edits (issue #229/#230): NOT coalesced — an
// upsert then a remove of the same semantic must both land, in order — and
// each queue drains exactly once.
TEST(EditorRuntimeControl, RenderableBindingEditsApplyInOrderNotCoalesced)
{
    using wz::app::SceneNodeRenderableBindingEdit;

    EditorRuntimeControl control;
    control.post_scene_node_renderable_binding(
        SceneNodeRenderableBindingEdit{
            .node_id = "n",
            .semantic = "scalar_field_texture",
            .asset_graph_node_id = 17u,
        });
    control.post_scene_node_renderable_binding(
        SceneNodeRenderableBindingEdit{
            .node_id = "n",
            .semantic = "scalar_field_texture",
            .asset_graph_node_id = 0u,  // remove — must land AFTER the upsert
        });

    std::vector<SceneNodeRenderableBindingEdit> applied;
    control.service_pending_scene_node_renderable_bindings(
        [&applied](const SceneNodeRenderableBindingEdit& edit) {
            applied.push_back(edit);
        });

    ASSERT_EQ(applied.size(), 2u);
    EXPECT_EQ(applied[0].semantic, "scalar_field_texture");
    EXPECT_EQ(applied[0].asset_graph_node_id, 17u);
    EXPECT_EQ(applied[1].asset_graph_node_id, 0u);

    std::vector<SceneNodeRenderableBindingEdit> again;
    control.service_pending_scene_node_renderable_bindings(
        [&again](const SceneNodeRenderableBindingEdit& edit) {
            again.push_back(edit);
        });
    EXPECT_TRUE(again.empty());
}

TEST(EditorRuntimeControl, RenderableParamEditsCarryValueAndClear)
{
    using wz::app::SceneNodeRenderableParamEdit;

    EditorRuntimeControl control;
    control.post_scene_node_renderable_param(
        SceneNodeRenderableParamEdit{
            .node_id = "n",
            .name = "tint",
            .clear = false,
            .value = { 0.2f, 0.7f, 0.3f, 1.0f },
        });
    control.post_scene_node_renderable_param(
        SceneNodeRenderableParamEdit{
            .node_id = "n",
            .name = "tint",
            .clear = true,  // remove — must land AFTER the upsert
        });

    std::vector<SceneNodeRenderableParamEdit> applied;
    control.service_pending_scene_node_renderable_params(
        [&applied](const SceneNodeRenderableParamEdit& edit) {
            applied.push_back(edit);
        });

    ASSERT_EQ(applied.size(), 2u);
    EXPECT_EQ(applied[0].name, "tint");
    EXPECT_FALSE(applied[0].clear);
    EXPECT_FLOAT_EQ(applied[0].value[0], 0.2f);
    EXPECT_FLOAT_EQ(applied[0].value[1], 0.7f);
    EXPECT_FLOAT_EQ(applied[0].value[2], 0.3f);
    EXPECT_FLOAT_EQ(applied[0].value[3], 1.0f);
    EXPECT_TRUE(applied[1].clear);

    std::vector<SceneNodeRenderableParamEdit> again;
    control.service_pending_scene_node_renderable_params(
        [&again](const SceneNodeRenderableParamEdit& edit) {
            again.push_back(edit);
        });
    EXPECT_TRUE(again.empty());
}
