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
#include <chrono>
#include <functional>
#include <memory>
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

// A1-C6: the scene-mutation verbs are fire-and-forget -- they post and return OK
// long before the engine thread resolves the node id -- so when the target turns
// out to be gone the miss used to be a log line and a discarded bool, and the
// caller was told the edit succeeded. This is the channel that carries the real
// outcome back.
//
// Reporting from the APPLY site is what makes it free of false positives: there is
// no published id set that could go stale between the post and the apply, so a
// recorded drop always means the node genuinely was not there.
TEST(EditorRuntimeControl, DroppedEditsAreRecordedAndDrainedOnce)
{
    EditorRuntimeControl control;

    EXPECT_TRUE(control.take_dropped_edits().empty());

    control.post_scene_node_properties(
        SceneNodePropertiesEdit{ .id = "ghost", .name = "n", .visible = true });

    // The applier reports false the way WozzitsApp_v1 does for a missing node.
    control.service_pending_scene_node_properties(
        [&control](const SceneNodePropertiesEdit& edit) {
            control.record_dropped_edit("set_node_properties", edit.id);
        });

    const std::vector<std::string> drained = control.take_dropped_edits();
    ASSERT_EQ(drained.size(), 1u);
    EXPECT_NE(drained[0].find("set_node_properties"), std::string::npos)
        << drained[0];
    EXPECT_NE(drained[0].find("ghost"), std::string::npos) << drained[0];

    // TAKE semantics: a second drain is empty, so the host reports each drop once
    // instead of re-surfacing a level every refresh.
    EXPECT_TRUE(control.take_dropped_edits().empty());
}

// The record is bounded: dragging against a stale id posts one edit per frame, and
// an unbounded list would grow for as long as the drag lasts. Past the cap the
// entries stop accumulating but the COUNT is still reported, so the report never
// silently understates what happened.
TEST(EditorRuntimeControl, DroppedEditRecordIsBoundedButReportsTheOverflow)
{
    EditorRuntimeControl control;

    for (int i = 0; i < 100; ++i) {
        control.record_dropped_edit("set_node_transform", "ghost");
    }

    const std::vector<std::string> drained = control.take_dropped_edits();
    EXPECT_LT(drained.size(), 100u);
    EXPECT_NE(drained.back().find("more dropped edits not listed"),
        std::string::npos) << drained.back();

    EXPECT_TRUE(control.take_dropped_edits().empty());
}

// ─── Concurrent owners (issue #313, B4-C2, and the B4-T1 test gap) ──────────
// Every other threaded test in this file uses exactly ONE owner thread, which
// is why the seam's actual purpose went unpinned: the seven blocking handshakes
// share one mutex and one condition variable, a publish wakes EVERY waiter, and
// nothing in the payload says whose answer it is. Measured before the fix, at
// two concurrent callers: callers received each other's results, and callers
// hung forever with every request already serviced.
//
// The editor really does have more than one owner thread — it runs the graph
// bind on a .NET threadpool thread (Task.Run) so its UI stays live.
//
// THESE TESTS RUN MANY ROUNDS ON PURPOSE. A single round of four callers is not
// a regression pin: the first version of this test ran one round and PASSED
// with the fix neutered, because the wake order that exposes the bug is a race
// and Windows usually wakes the longest waiter first. Reverting the fix now
// fails these within a few rounds. If this ever gets "simplified" back to one
// round, it stops testing anything.
//
// A round that hangs is released through mark_finished() rather than hanging
// the suite: every handshake wait has `|| finished_`, so a stalled caller
// returns a typed failure and the assertions report a real failure.
namespace
{
    constexpr int kConcurrentOwners = 4;
    constexpr int kConcurrentRounds = 40;
    constexpr auto kOwnerDeadline = std::chrono::seconds(10);

    // Releases every blocked caller if they do not all return in time.
    struct OwnerWatchdog
    {
        EditorRuntimeControl& control;
        const std::atomic_int& finished;
        std::atomic_bool fired{ false };
        std::atomic_bool done{ false };
        std::thread thread;

        OwnerWatchdog(EditorRuntimeControl& c, const std::atomic_int& f)
            : control(c)
            , finished(f)
        {
            thread = std::thread([this] {
                const auto deadline =
                    std::chrono::steady_clock::now() + kOwnerDeadline;
                while (!done.load(std::memory_order_acquire)) {
                    if (finished.load(std::memory_order_acquire)
                        >= kConcurrentOwners)
                    {
                        return;
                    }
                    if (std::chrono::steady_clock::now() > deadline) {
                        fired.store(true, std::memory_order_release);
                        control.mark_finished();
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            });
        }

        ~OwnerWatchdog()
        {
            done.store(true, std::memory_order_release);
            if (thread.joinable()) {
                thread.join();
            }
        }
    };
}

TEST(EditorRuntimeControl, ConcurrentBindCallersEachGetTheirOwnDraftAndReport)
{
    for (int round = 0; round < kConcurrentRounds; ++round) {
        EditorRuntimeControl control;
        std::atomic_bool engine_running{ true };
        std::atomic_int finished{ 0 };

        // Engine thread: tag the report with the draft's size, so a caller can
        // tell WHOSE bind came back. The sleep widens the claimed-but-not-yet-
        // published window that a second caller used to be admitted into.
        std::thread engine([&] {
            while (engine_running.load(std::memory_order_acquire)) {
                control.service_pending_asset_graph_bind(
                    [](wz::asset::AssetGraphDraft& draft) {
                        std::this_thread::sleep_for(
                            std::chrono::microseconds(300));
                        AssetGraphCompileResult report;
                        report.ok = true;
                        report.diagnostics.push_back(
                            wz::asset::AssetGraphDraftValidationMessage{
                                .severity = wz::asset::
                                    AssetGraphDraftValidationSeverity::Info,
                                .node =
                                    wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE,
                                .message = std::to_string(draft.nodes.size()),
                            });
                        return report;
                    });
                std::this_thread::yield();
            }
        });

        // Unique, non-zero node count per caller: 2, 4, 6, 8.
        std::vector<wz::asset::AssetGraphDraft> drafts;
        std::vector<AssetGraphCompileResult> reports(kConcurrentOwners);
        for (int i = 0; i < kConcurrentOwners; ++i) {
            drafts.push_back(make_draft(static_cast<size_t>((i + 1) * 2)));
        }

        bool hung = false;
        {
            OwnerWatchdog watchdog(control, finished);
            std::vector<std::thread> owners;
            for (int i = 0; i < kConcurrentOwners; ++i) {
                owners.emplace_back([&, i] {
                    // Stagger so callers land at different points of each
                    // other's cycles rather than all piling on the entry gate.
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(i * 60));
                    reports[static_cast<size_t>(i)] = control.bind_asset_graph(
                        drafts[static_cast<size_t>(i)]);
                    finished.fetch_add(1, std::memory_order_acq_rel);
                });
            }
            for (std::thread& owner : owners) {
                owner.join();
            }
            hung = watchdog.fired.load(std::memory_order_acquire);
        }

        engine_running.store(false, std::memory_order_release);
        engine.join();

        ASSERT_FALSE(hung)
            << "round " << round
            << ": a caller never returned — a publish was consumed by the "
               "wrong waiter, or swallowed by a caller posting over it";

        for (int i = 0; i < kConcurrentOwners; ++i) {
            const size_t expected = static_cast<size_t>((i + 1) * 2);
            const auto& report = reports[static_cast<size_t>(i)];
            ASSERT_EQ(drafts[static_cast<size_t>(i)].nodes.size(), expected)
                << "round " << round << ", caller " << i
                << " got another caller's authored draft back";
            ASSERT_FALSE(report.diagnostics.empty())
                << "round " << round << ", caller " << i;
            ASSERT_EQ(report.diagnostics[0].message, std::to_string(expected))
                << "round " << round << ", caller " << i
                << " got the report for another caller's bind";
        }
    }
}

TEST(EditorRuntimeControl, ConcurrentAddChildCallersEachGetTheirOwnMintedId)
{
    // The same defect lived in all seven handshakes, so pin a second one: the
    // shape is shared, and a fix applied to only the bind would pass the test
    // above while leaving these six broken.
    for (int round = 0; round < kConcurrentRounds; ++round) {
        EditorRuntimeControl control;
        std::atomic_bool engine_running{ true };
        std::atomic_int finished{ 0 };

        std::thread engine([&] {
            while (engine_running.load(std::memory_order_acquire)) {
                control.service_pending_add_child(
                    [](const wz::scene::AuthoredEntityId& parent) {
                        std::this_thread::sleep_for(
                            std::chrono::microseconds(300));
                        return wz::engine::assets::SceneAddChildResult{
                            .ok = true,
                            .new_id = parent + "/child",
                            .error = {},
                        };
                    });
                std::this_thread::yield();
            }
        });

        std::vector<wz::engine::assets::SceneAddChildResult> results(
            kConcurrentOwners);
        bool hung = false;
        {
            OwnerWatchdog watchdog(control, finished);
            std::vector<std::thread> owners;
            for (int i = 0; i < kConcurrentOwners; ++i) {
                owners.emplace_back([&, i] {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(i * 60));
                    results[static_cast<size_t>(i)] =
                        control.add_child("P" + std::to_string(i));
                    finished.fetch_add(1, std::memory_order_acq_rel);
                });
            }
            for (std::thread& owner : owners) {
                owner.join();
            }
            hung = watchdog.fired.load(std::memory_order_acquire);
        }

        engine_running.store(false, std::memory_order_release);
        engine.join();

        ASSERT_FALSE(hung)
            << "round " << round << ": a caller never returned from add_child";

        for (int i = 0; i < kConcurrentOwners; ++i) {
            const auto& got = results[static_cast<size_t>(i)];
            ASSERT_TRUE(got.ok)
                << "round " << round << ", caller " << i << ": " << got.error;
            ASSERT_EQ(got.new_id, "P" + std::to_string(i) + "/child")
                << "round " << round << ", caller " << i
                << " got the id minted for another caller";
        }
    }
}

// ─── Shutdown interlock (issue #313, B4-C12) ───────────────────────────────
// wz_host_runtime_stop joins the engine thread and then deletes the runtime,
// taking this object with it. join() only proves the ENGINE thread is gone; a
// different owner thread can still be unwinding out of a blocking verb, and
// that is the normal shape — the editor runs the graph bind on a .NET
// threadpool thread while the UI thread is the one that calls stop.
//
// NOTE ON WHAT IS AND IS NOT ASSERTABLE HERE. wait_for_callers_to_exit()
// guarantees no thread is still INSIDE the control when it returns; it says
// nothing about statements a caller thread runs afterwards. A first draft of
// these tests asserted "the caller's own done-flag is set by the time the drain
// returns", which races legitimately: the flag is stored after the CallerScope
// has already been released. What is pinned below instead is the property the
// delete actually depends on — the drain terminates, and every caller leaves
// through the not-running path with its authored state intact.

TEST(EditorRuntimeControl, BeginCloseReleasesAParkedCallerSoTheDrainTerminates)
{
    // A caller parked in a blocking verb with NOTHING servicing it — the state
    // wz_host_runtime_stop finds after join() when the editor's threadpool
    // thread is mid-bind. begin_close() is what releases it; without that the
    // drain below would never return.
    auto control = std::make_unique<EditorRuntimeControl>();

    AssetGraphCompileResult report;
    wz::asset::AssetGraphDraft draft = make_draft(3);
    std::thread caller([&] { report = control->bind_asset_graph(draft); });

    // Wait for the caller to be provably parked in the bind. There IS a handshake
    // to poll now (bind_request_pending): the sleep this replaces silently
    // exercised the "caller never arrived" path whenever 50ms was not enough, and
    // that path does not test what this test is named for.
    while (!control->bind_request_pending()) {
        std::this_thread::yield();
    }

    control->begin_close();
    control->wait_for_callers_to_exit();
    caller.join();

    EXPECT_FALSE(report.ok)
        << "the parked caller must leave through the not-running path";
    EXPECT_EQ(draft.nodes.size(), 3u)
        << "and must get its authored draft back, not a moved-from husk";

    // Stands in for wz_host_runtime_stop's `delete runtime`: after the drain,
    // destroying the control must be safe.
    control.reset();
}

TEST(EditorRuntimeControl, ACallerArrivingAfterBeginCloseIsRefusedNotBlocked)
{
    // The other half: once closing, a verb must return its not-running failure
    // immediately rather than parking on a cv nobody will notify again. If this
    // regressed the test would hang rather than fail, which is itself the
    // signal.
    EditorRuntimeControl control;
    control.begin_close();

    wz::asset::AssetGraphDraft draft = make_draft(4);
    const auto report = control.bind_asset_graph(draft);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(draft.nodes.size(), 4u)
        << "a refusal must not cost the caller its graph";

    const auto added = control.add_child("parent");
    EXPECT_FALSE(added.ok);

    control.wait_for_callers_to_exit();  // nobody inside: returns immediately
}

TEST(EditorRuntimeControl, DestroyingRightAfterTheDrainIsSafeUnderConcurrentCallers)
{
    // Stress rather than a crisp assertion, and worth being honest about that:
    // the failure this guards is a use-after-free, which shows up as a crash or
    // a debug-CRT heap assert rather than a failed EXPECT. Several owner threads
    // call while the owner closes and destroys underneath them, many rounds. The
    // countable part (every caller returned) is checked after join, where it is
    // race-free.
    constexpr int kCallers = 4;
    for (int round = 0; round < 25; ++round) {
        auto control = std::make_unique<EditorRuntimeControl>();
        std::atomic_int returned{ 0 };

        std::vector<std::thread> callers;
        for (int i = 0; i < kCallers; ++i) {
            callers.emplace_back([&, i] {
                if (i % 2 == 0) {
                    wz::asset::AssetGraphDraft d = make_draft(2);
                    auto r = control->bind_asset_graph(d);
                    (void)r;
                }
                else {
                    auto r = control->add_child("p");
                    (void)r;
                }
                returned.fetch_add(1, std::memory_order_acq_rel);
            });
        }

        // Every caller must be INSIDE before the owner closes. The interlock
        // covers calls already in flight; it cannot cover a call that has not
        // started yet, and neither can any handle-based C API (that is the
        // host's use-after-free, not ours).
        //
        // Waited on rather than slept for: with a 50ms sleep, a scheduling hiccup
        // meant some callers had not entered yet, so the round tested a smaller
        // set than it claimed and still passed. active_caller_count is exactly
        // "how many are inside", which is the precondition this test needs.
        while (control->active_caller_count() < kCallers) {
            std::this_thread::yield();
        }

        control->begin_close();
        control->wait_for_callers_to_exit();
        // Destroy while the caller threads are still winding down. This is the
        // point of the test: the drain must have made it safe.
        control.reset();

        for (std::thread& t : callers) {
            t.join();
        }
        ASSERT_EQ(returned.load(std::memory_order_acquire), kCallers)
            << "round " << round << ": a caller never returned";
    }
}

// ─── Save handshake (#300 layer 1 / #299 A1-C20) ───────────────────────────

TEST(EditorRuntimeControl, SaveSceneBlockingReturnsTheEnginesFileError)
{
    // The result the old fire-and-forget save could not deliver: a caller blocks
    // in save_scene_blocking, the engine services it, and the caller wakes with
    // exactly the FileError the engine produced. Serviced via the #305 step 4b
    // split -- claim, then publish -- as the runtime loop does.
    EditorRuntimeControl control;
    bool serviced = false;
    wz::fs::FileError error = wz::fs::FileError::None;

    std::thread caller([&] {
        const auto outcome = control.save_scene_blocking();
        serviced = outcome.serviced;
        error = outcome.value;
    });

    while (!control.begin_pending_save()) {
        std::this_thread::yield();
    }
    control.complete_pending_save(wz::fs::FileError::PermissionDenied);
    caller.join();

    EXPECT_TRUE(serviced);
    EXPECT_EQ(error, wz::fs::FileError::PermissionDenied);
}

TEST(EditorRuntimeControl, AsyncSavePublishesOffThreadAndReportsBackToTheFrameThread)
{
    // #305 step 4b: the caller blocks in save_scene_blocking; the engine claims
    // the save (begin_pending_save) and hands the write to ANOTHER thread (the IO
    // lane), which publishes the FileError to the parked caller AND posts an
    // AsyncSaveResult the frame thread drains to finalize -- restoring the dirty
    // flags a failed write cleared.
    EditorRuntimeControl control;
    bool serviced = false;
    wz::fs::FileError caller_error = wz::fs::FileError::None;

    std::thread caller([&] {
        const auto outcome = control.save_scene_blocking();
        serviced = outcome.serviced;
        caller_error = outcome.value;
    });

    // Engine (frame) thread claims the pending save.
    while (!control.begin_pending_save()) {
        std::this_thread::yield();
    }
    // The "IO lane": finish the write and report back on a separate thread.
    std::thread io([&] {
        control.complete_pending_save(wz::fs::FileError::PermissionDenied);
        control.post_async_save_result(
            { wz::fs::FileError::PermissionDenied,
              /*restore_scene_dirty=*/true,
              /*restore_camera_dirty=*/false });
    });
    io.join();
    caller.join();

    EXPECT_TRUE(serviced);
    EXPECT_EQ(caller_error, wz::fs::FileError::PermissionDenied);

    // The frame thread drains exactly one result carrying what finalize needs.
    std::vector<wz::app::AsyncSaveResult> got;
    control.drain_async_save_results(
        [&](const wz::app::AsyncSaveResult& r) { got.push_back(r); });
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].err, wz::fs::FileError::PermissionDenied);
    EXPECT_TRUE(got[0].restore_scene_dirty);
    EXPECT_FALSE(got[0].restore_camera_dirty);
}

// The ordering guarantee the frame loop's in-claim drain rests on: once a SECOND
// save is claimable, the FIRST save's result is already drainable.
//
// The frame loop drains finished saves, then claims a newly requested one -- and
// those two steps are not atomic against the IO lane, so save #1 can land in the
// gap between them. What makes that survivable is the closure posting its result
// BEFORE waking the caller: the caller cannot return (and so cannot request save
// #2, which is what makes a claim possible) until after the post. Draining again
// inside the claim therefore always sees it. Reverse those two and the guarantee
// is gone -- save #2 gets prepared while #1's dirty-flag restore is still in
// flight, and a failed #1 makes prepare take its "nothing changed" early-out and
// answer None for edits that were never written.
//
// Scope, stated honestly: this pins the OBSERVABLE end state -- a result posted
// by the lane is drainable by the time the next save is claimed -- not the
// ordering that guarantees it. Inverting the closure to complete-then-post was
// tried here and still passes: losing the race would require the caller to wake,
// return, request save #2, and the frame thread to claim and drain it, all before
// the lane executes its very next function call. The window is real but not
// reachable from a test. The frame-loop sequencing that consumes this
// (run_project_runtime's drain / claim / drain) needs a live app and has no
// harness here either.
TEST(EditorRuntimeControl, ASecondSaveIsClaimableOnlyOnceTheFirstResultIsDrainable)
{
    EditorRuntimeControl control;

    // ── save #1 ──────────────────────────────────────────────────────────────
    std::thread caller1([&] { (void)control.save_scene_blocking(); });
    while (!control.begin_pending_save()) {
        std::this_thread::yield();
    }
    // The "IO lane", in the order the production closure uses: post the result,
    // THEN wake the caller.
    std::thread io1([&] {
        control.post_async_save_result(
            { wz::fs::FileError::PermissionDenied,
              /*restore_scene_dirty=*/true,
              /*restore_camera_dirty=*/false });
        control.complete_pending_save(wz::fs::FileError::PermissionDenied);
    });

    // Join the CALLER, not the IO thread. Joining io1 here would serialize the
    // whole lane and the assert below would hold under either ordering, testing
    // nothing. The caller returns purely on the publish, which is what the real
    // owner thread does -- so if the post did not already happen-before that
    // publish, the drain below races it and comes up empty.
    caller1.join();

    // ── save #2, requested the instant #1's caller returned ──────────────────
    std::thread caller2([&] { (void)control.save_scene_blocking(); });
    while (!control.begin_pending_save()) {
        std::this_thread::yield();
    }

    // #2 is claimed. #1's result must ALREADY be here -- this is the drain the
    // frame loop does inside the claim, before it prepares #2's snapshot.
    std::vector<wz::app::AsyncSaveResult> got;
    control.drain_async_save_results(
        [&](const wz::app::AsyncSaveResult& r) { got.push_back(r); });
    ASSERT_EQ(got.size(), 1u)
        << "save #1's result must be drainable before save #2 can be prepared";
    EXPECT_EQ(got[0].err, wz::fs::FileError::PermissionDenied);
    EXPECT_TRUE(got[0].restore_scene_dirty)
        << "the restore a failed #1 owes the scene must reach the frame thread "
           "before #2's snapshot is taken";

    control.complete_pending_save(wz::fs::FileError::None);
    caller2.join();
    io1.join();
}

// ─── The out-of-band dirty restore ─────────────────────────────────────────
//
// post_async_save_result allocates (Mailbox -> vector push_back), so the IO
// closure has to guard it -- and the guard DROPS the result on bad_alloc. That
// was reasoned about as survivable, but it is not: with the restore lost,
// document_.dirty() stays cleared from prepare's optimistic clear, so the retry
// takes the "nothing changed" early-out and reports success having written
// nothing, and the teardown save skips for the same reason. The two failures are
// correlated -- whatever exhausted the heap for the post is what failed the save.
//
// request_dirty_restore is the allocation-free duplicate: a bitmask fetch_or,
// noexcept by construction, that the frame thread takes alongside the mailbox
// drain. These tests drive the lane WITHOUT the mailbox post at all, which is
// exactly the state an OOM leaves behind.

TEST(EditorRuntimeControl, DirtyRestoreSurvivesALostMailboxPost)
{
    EditorRuntimeControl control;

    std::thread caller([&] { (void)control.save_scene_blocking(); });
    while (!control.begin_pending_save()) {
        std::this_thread::yield();
    }

    // The IO lane after a FAILED write, with the mailbox post lost to bad_alloc:
    // the restore signal, then the publish. No post_async_save_result.
    std::thread io([&] {
        control.request_dirty_restore(/*scene=*/true, /*camera=*/true);
        control.complete_pending_save(wz::fs::FileError::PermissionDenied);
    });
    io.join();
    caller.join();

    // Nothing in the mailbox -- this is the state that used to lose the edits.
    int mailbox_results = 0;
    control.drain_async_save_results(
        [&](const wz::app::AsyncSaveResult&) { ++mailbox_results; });
    EXPECT_EQ(mailbox_results, 0);

    // The restore is still there, and says which flags the frame thread owes.
    const unsigned restore = control.take_dirty_restore();
    EXPECT_NE(restore & EditorRuntimeControl::kRestoreSceneDirty, 0u)
        << "a failed save's scene-dirty restore was lost with the mailbox post";
    EXPECT_NE(restore & EditorRuntimeControl::kRestoreCameraDirty, 0u);
}

TEST(EditorRuntimeControl, DirtyRestoreIsTakenOnceAndAccumulates)
{
    EditorRuntimeControl control;

    // Nothing pending: an empty take must not claim a restore that never
    // happened, or every frame would re-dirty a cleanly saved scene.
    EXPECT_EQ(control.take_dirty_restore(), 0u);

    // Two failed saves before the frame thread gets a turn: the second must not
    // erase the first's bits (fetch_or, not store). Scene-only then camera-only,
    // so a store would leave exactly one bit set.
    control.request_dirty_restore(/*scene=*/true, /*camera=*/false);
    control.request_dirty_restore(/*scene=*/false, /*camera=*/true);

    const unsigned restore = control.take_dirty_restore();
    EXPECT_NE(restore & EditorRuntimeControl::kRestoreSceneDirty, 0u);
    EXPECT_NE(restore & EditorRuntimeControl::kRestoreCameraDirty, 0u);

    // Sticky, but not permanent: taken bits are cleared, so the NEXT frame does
    // not re-restore and mark a saved scene dirty forever.
    EXPECT_EQ(control.take_dirty_restore(), 0u);

    // A successful save asks for nothing and must leave no trace.
    control.request_dirty_restore(/*scene=*/false, /*camera=*/false);
    EXPECT_EQ(control.take_dirty_restore(), 0u);
}

TEST(EditorRuntimeControl, BeginCloseReleasesAParkedSaveCaller)
{
    // The A1-C20 teardown case: a save posted with nothing servicing it (the
    // viewport is coming down) must not hang, and must report serviced=false so
    // the editor does not treat a save that never happened as success.
    auto control = std::make_unique<EditorRuntimeControl>();

    bool serviced = true;
    std::thread caller(
        [&] { serviced = control->save_scene_blocking().serviced; });

    // Provably parked in the save before the close, rather than a 50ms guess --
    // otherwise begin_close() can land before the caller ever posts, and the test
    // passes through the "refused on arrival" path instead of the
    // "released while parked" one it is named for.
    while (!control->save_request_pending()) {
        std::this_thread::yield();
    }

    control->begin_close();
    control->wait_for_callers_to_exit();
    caller.join();

    EXPECT_FALSE(serviced)
        << "a save the engine never ran must not report success";
    control.reset();  // stands in for `delete runtime`; safe after the drain
}

// ─── Frame delta (issue #313, B4-S2 and B4-C9) ─────────────────────────────
// The loop's timing rules, extracted so they can be tested without a device.
// Two defects lived here: the delta was an UNSIGNED tick subtraction with no
// monotonicity guard (the engine's other frame loop has carried that guard for
// a long time), and it was handed unclamped to motion integration and terrain
// constraints while the renderer clamped its own copy one line away -- so a
// stall teleported physics while animation correctly held still.

namespace
{
    constexpr uint64_t kTicksPerSecond = 10'000'000;  // 100ns ticks, like QPC
    using wz::app::compute_frame_delta;
    using wz::app::kMaxFrameSeconds;
}

TEST(FrameDelta, AnOrdinaryFrameMeasuresTheRealInterval)
{
    const auto frame = compute_frame_delta(
        1'000'000, 1'000'000 + kTicksPerSecond / 60, kTicksPerSecond);
    EXPECT_NEAR(frame.dt, 1.0f / 60.0f, 1.0e-6f);
    EXPECT_EQ(frame.now, 1'000'000u + kTicksPerSecond / 60);
}

TEST(FrameDelta, AClockThatDidNotAdvanceYieldsATinyPositiveDeltaNotAHugeOne)
{
    // B4-S2. Tick is unsigned: without the guard `sampled - last` wraps to
    // ~1.8e19 ticks and dt becomes astronomically large.
    const auto frame = compute_frame_delta(5'000, 5'000, kTicksPerSecond);
    EXPECT_GT(frame.dt, 0.0f);
    EXPECT_LT(frame.dt, 1.0e-3f);
    EXPECT_EQ(frame.now, 5'001u) << "the corrected tick must move forward, or "
                                    "the next frame repeats the same fault";
}

TEST(FrameDelta, AClockThatWentBACKWARDSDoesNotProduceAGiantDelta)
{
    // QPC can retrograde slightly across a core switch on some hardware.
    const auto frame = compute_frame_delta(9'000, 8'000, kTicksPerSecond);
    EXPECT_GT(frame.dt, 0.0f);
    EXPECT_LT(frame.dt, 1.0e-3f);
    EXPECT_EQ(frame.now, 9'001u);
}

TEST(FrameDelta, AnUnboundedPumpStallIsClampedBeforeAnyoneSeesIt)
{
    // B4-C9 / B4-C7. The Win32 modal move/size loop blocks the engine thread's
    // only pump for as long as the user holds the window border, so the next
    // frame's raw delta is user-controlled. Ten seconds of it must not reach
    // motion integration.
    const auto frame =
        compute_frame_delta(0, 10 * kTicksPerSecond, kTicksPerSecond);
    EXPECT_FLOAT_EQ(frame.dt, kMaxFrameSeconds);
    EXPECT_EQ(frame.now, 10u * kTicksPerSecond)
        << "the clamp bounds the DELTA, not the clock: time itself still moved";
}

TEST(FrameDelta, TheClampMatchesTheRenderersOwnLimit)
{
    // The renderer keeps an identical bound as defence in depth. If these two
    // ever diverge, animation and simulation resume at different rates after a
    // stall -- the exact split B4-C9 reported.
    EXPECT_FLOAT_EQ(kMaxFrameSeconds, 0.25f);
}

TEST(FrameDelta, ADeltaJustUnderTheBoundIsNotClamped)
{
    const auto frame = compute_frame_delta(
        0, static_cast<wz::time::Tick>(0.24 * kTicksPerSecond),
        kTicksPerSecond);
    EXPECT_NEAR(frame.dt, 0.24f, 1.0e-4f);
}

TEST(FrameDelta, AZeroTickRateYieldsZeroRatherThanADivisionByZero)
{
    const auto frame = compute_frame_delta(0, 1'000, 0);
    EXPECT_FLOAT_EQ(frame.dt, 0.0f);
}
