// tests/engine/editor_runtime_control_tests.cpp
//
// EditorRuntimeControl is the cross-thread seam between the editor (owner
// thread) and the in-process engine runtime. These cover the live scene-edit
// channel device-free: posting node transforms, coalescing per node id, and
// draining them through an applier on the engine-thread side. The blocking
// bind handshake and the on-device apply (WozzitsApp_v1) are covered elsewhere.

#include <gtest/gtest.h>

#include <asset/draft.h>
#include <engine/app/editor_runtime.h>

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using wz::app::EditorRuntimeControl;
    using wz::app::SceneNodeCameraEdit;
    using wz::app::SceneNodePropertiesEdit;
    using wz::app::SceneNodeTransformEdit;

    SceneNodeTransformEdit make_edit(std::string id, float tx)
    {
        SceneNodeTransformEdit edit;
        edit.id = std::move(id);
        edit.transform.translation[0] = tx;
        return edit;
    }

    using wz::app::AssetGraphCompileResult;

    // A draft that is recognisably non-empty, so "the caller's authoring state
    // survived" is checkable rather than vacuous.
    wz::asset::AssetGraphDraft make_draft(size_t node_count)
    {
        wz::asset::AssetGraphDraft draft;
        draft.nodes.resize(node_count);
        draft.next_node_id =
            static_cast<wz::asset::AssetGraphDraftNodeId>(node_count + 1);
        return draft;
    }

    // Engine-thread side: keep servicing until the binder actually runs, so the
    // test exercises the claimed-the-draft window rather than racing past it.
    // `binder` may throw; the throw propagates out of service_* exactly as a
    // failing compile would, and is swallowed here the way the engine loop's
    // own catch does.
    void service_until_bound(
        EditorRuntimeControl& control,
        const std::function<
            AssetGraphCompileResult(wz::asset::AssetGraphDraft&)>& binder,
        const std::atomic_bool& bound)
    {
        while (!bound.load(std::memory_order_acquire)) {
            try {
                control.service_pending_asset_graph_bind(binder);
            }
            catch (const std::runtime_error&) {
                break;  // the binder threw — that IS the case under test
            }
            std::this_thread::yield();
        }
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

TEST(EditorRuntimeControl, CameraPostCoalescesByIdAndDrainsOnce)
{
    EditorRuntimeControl control;
    wz::engine::assets::SceneCameraAsset first;
    first.far_plane = 1000.f;
    wz::engine::assets::SceneCameraAsset second;
    second.far_plane = 2000.f;
    control.post_scene_node_camera(
        SceneNodeCameraEdit{ .node_id = "cam", .camera = first });
    control.post_scene_node_camera(
        SceneNodeCameraEdit{ .node_id = "cam", .camera = second });

    std::vector<SceneNodeCameraEdit> applied;
    control.service_pending_scene_node_cameras(
        [&applied](const SceneNodeCameraEdit& edit) { applied.push_back(edit); });

    ASSERT_EQ(applied.size(), 1u);                        // one node -> one apply
    EXPECT_FLOAT_EQ(applied[0].camera.far_plane, 2000.f); // latest wins

    std::vector<SceneNodeCameraEdit> again;
    control.service_pending_scene_node_cameras(
        [&again](const SceneNodeCameraEdit& edit) { again.push_back(edit); });
    EXPECT_TRUE(again.empty());
}

TEST(EditorRuntimeControl, RenderToTexturePostCoalescesByIdAndDrainsOnce)
{
    using wz::app::SceneNodeRenderToTextureEdit;

    EditorRuntimeControl control;
    wz::engine::assets::SceneRenderToTextureAsset first;
    first.target_node_id = 7u;
    wz::engine::assets::SceneRenderToTextureAsset second;
    second.target_node_id = 9u;
    second.also_draw_in_scene = true;

    control.post_scene_node_render_to_texture(
        SceneNodeRenderToTextureEdit{
            .node_id = "card", .render_to_texture = first });
    control.post_scene_node_render_to_texture(
        SceneNodeRenderToTextureEdit{
            .node_id = "card", .render_to_texture = second });

    std::vector<SceneNodeRenderToTextureEdit> applied;
    control.service_pending_scene_node_render_to_textures(
        [&applied](const SceneNodeRenderToTextureEdit& edit) {
            applied.push_back(edit);
        });

    // Dragging a switch or re-picking the target streams edits; only the latest
    // matters, so one node collapses to one apply.
    ASSERT_EQ(applied.size(), 1u);
    ASSERT_TRUE(applied[0].render_to_texture.target_node_id.has_value());
    EXPECT_EQ(*applied[0].render_to_texture.target_node_id, 9u);
    EXPECT_TRUE(applied[0].render_to_texture.also_draw_in_scene);

    std::vector<SceneNodeRenderToTextureEdit> again;
    control.service_pending_scene_node_render_to_textures(
        [&again](const SceneNodeRenderToTextureEdit& edit) {
            again.push_back(edit);
        });
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

// --- bind_asset_graph teardown windows -------------------------------------
//
// bind_asset_graph MOVES the caller's draft across to the engine thread, so a
// runtime that stops mid-handshake must hand it back. If it does not, the
// caller is left holding a moved-from draft and the next session save
// serializes that empty draft over the project's asset graph — the whole
// authored graph, gone with an OK return. These pin the invariant at each of
// the three teardown windows: before the claim, after the claim, and on the
// normal path.

TEST(EditorRuntimeControl, BindReturnsDraftWhenEngineStopsBeforeClaimingIt)
{
    EditorRuntimeControl control;

    wz::asset::AssetGraphDraft draft = make_draft(2);
    AssetGraphCompileResult result;
    std::thread caller([&] { result = control.bind_asset_graph(draft); });

    // The engine never services the request — it stops with the draft still
    // parked in the request slot.
    control.mark_finished();
    caller.join();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(draft.nodes.size(), 2u);
}

TEST(EditorRuntimeControl, BindReturnsDraftWhenBinderThrows)
{
    EditorRuntimeControl control;

    wz::asset::AssetGraphDraft draft = make_draft(3);
    AssetGraphCompileResult result;
    std::thread caller([&] { result = control.bind_asset_graph(draft); });

    // The engine claims the draft and then the compile blows up, destroying the
    // binder's frame — the draft exists nowhere else at that instant.
    std::atomic_bool bound{ false };
    service_until_bound(
        control,
        [&bound](wz::asset::AssetGraphDraft&) -> AssetGraphCompileResult {
            bound.store(true, std::memory_order_release);
            throw std::runtime_error("compile threw");
        },
        bound);
    control.mark_finished();
    caller.join();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(draft.nodes.size(), 3u)
        << "a torn bind must not consume the caller's authored graph";
}

TEST(EditorRuntimeControl, BindReturnsBoundDraftOnSuccess)
{
    EditorRuntimeControl control;

    wz::asset::AssetGraphDraft draft = make_draft(1);
    AssetGraphCompileResult result;
    std::thread caller([&] { result = control.bind_asset_graph(draft); });

    // The binder mutates the draft in place the way a real compile does; the
    // caller must get THAT draft back, not the one it handed over.
    std::atomic_bool bound{ false };
    service_until_bound(
        control,
        [&bound](wz::asset::AssetGraphDraft& claimed) {
            claimed.nodes.resize(4);
            bound.store(true, std::memory_order_release);
            AssetGraphCompileResult ok;
            ok.ok = true;
            return ok;
        },
        bound);
    caller.join();

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(draft.nodes.size(), 4u);
}
