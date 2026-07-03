// tests/render/wozzits_app_render_binding_tests.cpp
//
// On-device coverage for issue #213 increment 2: live authoring of a scene
// node's renderable BINDING (geometry + render program) via WozzitsApp_v1's
// set_node_geometry_asset / set_node_render_program. Reuses test_rebind_fixture's
// asset graph (node 9 = gpu_sparse_mesh geometry, node 10 = render program) and
// render_binding.scene.json (solo carries its own program; inherited carries
// only geometry and inherits the program from its parent group).
//
// The test exercises the live apply + re-assembly + the program-inheritance
// re-evaluation through resolved_renderable_node_count() (the count of scene
// nodes that resolved a renderable key). On-device: WozzitsApp_v1 owns a
// RhiSceneRenderer that needs a DX12 device; skipped if none can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/project/project_manifest.h>
#include <engine/rendering/rhi_scene_renderer.h>

#include <asset/draft.h>
#include <asset/system.h>
#include <external/json/json_parser.h>
#include <file/filesystem.h>
#include <gpu/gpu.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kProjectRoot = "projects/test_rebind_fixture";

    struct WozzitsAppRenderBindingFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_render_binding_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device render-binding test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        // Load test_rebind_fixture's graph paired with the binding scene that
        // sits beside it (render_binding.scene.json), not the fixture's own
        // pre-built-renderable scene.
        wz::app::WozzitsAppSceneLoadDesc binding_load_desc() const
        {
            const auto project = wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
            EXPECT_TRUE(project.ok) << project.error;

            wz::app::WozzitsAppSceneLoadDesc load_desc{};
            load_desc.asset_graph = project.manifest.asset_graph_path;
            load_desc.scene = wz::fs::join(
                wz::fs::parent_path(project.manifest.scene_path),
                "render_binding.scene.json");
            return load_desc;
        }

        // The #229 sibling scene: a node whose ingredients include semantic
        // bindings + a per-instance constant override, so the app synthesizes
        // a CUSTOM (0x70A) renderable (graph node 1 = cube Mesh, 16 = layout-
        // authored custom program, 17 = procedural scalar field).
        wz::app::WozzitsAppSceneLoadDesc custom_binding_load_desc() const
        {
            wz::app::WozzitsAppSceneLoadDesc load_desc = binding_load_desc();
            load_desc.scene = wz::fs::join(
                wz::fs::parent_path(load_desc.scene),
                "custom_binding.scene.json");
            return load_desc;
        }

        // Parse a fresh AssetGraphDraft from the project's graph JSON, the way
        // the rebind tests feed an edited draft back through bind_asset_graph.
        bool load_graph_draft(wz::asset::AssetGraphDraft& out)
        {
            const auto project = wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
            if (!project.ok || project.manifest.asset_graph_path.empty()) {
                return false;
            }
            std::ifstream file(
                ctx.assets->files().resolve_path(
                    project.manifest.asset_graph_path),
                std::ios::binary);
            if (!file) {
                return false;
            }
            const std::string text(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
            const wz::json::JSONParseResult parsed =
                wz::json::parse_json_string(text);
            if (!parsed.ok || !parsed.document.root) {
                return false;
            }
            std::string error;
            return wz::engine::assets::load_asset_graph_draft_from_v2_json(
                *parsed.document.root, out, error);
        }

        // begin/clear/render/end/present one frame — realizes the scene's
        // renderables (ensure_renderable runs inside render_scene).
        void render_one_frame(wz::app::WozzitsApp_v1& app)
        {
            ASSERT_TRUE(wz::gpu::begin_frame(ctx.device));
            wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
            app.simulation_tick(wz::input::InputState{}, 0.0f);
            EXPECT_TRUE(app.render_scene());
            ASSERT_TRUE(wz::gpu::end_frame(ctx.device));
            wz::gpu::present(ctx.device, /*sync_interval*/ 0);
        }
    };
}

// Authoring a node's geometry/program binding live re-assembles its renderable,
// and editing an ancestor's program cascades to inheriting descendants.
TEST_F(WozzitsAppRenderBindingFixture, SetNodeBindingLiveReassembles)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(binding_load_desc()));

    // Both bound nodes assemble: solo (own program) + inherited (program
    // inherited from group).
    EXPECT_EQ(app.resolved_renderable_node_count(), 2u);

    // Clear solo's geometry -> it stops drawing.
    EXPECT_TRUE(app.set_node_geometry_asset("solo", 0));
    EXPECT_EQ(app.resolved_renderable_node_count(), 1u);

    // Re-add solo's geometry (graph node 9) -> draws again.
    EXPECT_TRUE(app.set_node_geometry_asset(
        "solo", static_cast<wz::asset::AssetGraphDraftNodeId>(9)));
    EXPECT_EQ(app.resolved_renderable_node_count(), 2u);

    // Clear the group's render program -> inherited loses its (inherited)
    // program and stops drawing; solo (own program) is unaffected.
    EXPECT_TRUE(app.set_node_render_program("group", 0));
    EXPECT_EQ(app.resolved_renderable_node_count(), 1u);

    // Give inherited its own program (graph node 10) -> draws again.
    EXPECT_TRUE(app.set_node_render_program(
        "inherited", static_cast<wz::asset::AssetGraphDraftNodeId>(10)));
    EXPECT_EQ(app.resolved_renderable_node_count(), 2u);

    // A missing node is a logged no-op, not a crash.
    EXPECT_FALSE(app.set_node_geometry_asset(
        "no_such_node", static_cast<wz::asset::AssetGraphDraftNodeId>(9)));
}

// #221: this scene has no behaviors/motion/terrain/constraints. It now DOES get
// a live polytree (rebuild_behavior_scene materializes one for every loaded
// scene so a transform edit has somewhere to land) but ZERO behavior bindings,
// and scene_world_transforms() must still be exactly the authored composition
// (compute_scene_node_world_transforms → the polytree's own composed world) —
// same hierarchical result the renderer used to compute internally. 'inherited'
// sits at local origin under 'group' (translation +150 x), so its world X is
// 150; 'solo' is a root at -150. Proves the parent chain composes.
TEST_F(WozzitsAppRenderBindingFixture, NoSimSceneWorldTransformsAreAuthoredComposition)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(binding_load_desc()));

    // A static scene carries no behavior bindings (even though it now has a
    // polytree). The count reads behaviors.size(), so it is still 0.
    ASSERT_EQ(app.active_behavior_binding_count(), 0u);

    // Root 'solo' draws at its own local translation.
    const std::optional<wz::math::Mat4> solo = app.node_world_transform("solo");
    ASSERT_TRUE(solo.has_value());
    EXPECT_FLOAT_EQ(solo->m[12], -150.0f);  // world-space X translation

    // 'inherited' (local origin) inherits 'group's +150 X — the parent-chain
    // composition the standalone compute_scene_node_world_transforms produces.
    const std::optional<wz::math::Mat4> inherited =
        app.node_world_transform("inherited");
    ASSERT_TRUE(inherited.has_value());
    EXPECT_FLOAT_EQ(inherited->m[12], 150.0f);
}

// #221 single edit seam on a STATIC scene: a scene with no behaviors/motion/
// terrain now still comes up with a live polytree (active_behavior_binding_count
// stays 0), renders the authored composition, and set_node_transform lands in
// the polytree — NOT scene_nodes_. Proof: after editing 'solo', its render world
// transform reflects the edit while its stored_node_local_translation (read
// straight from scene_nodes_, never derived) is unchanged. This is the seam
// writing the polytree, which the derivation persistence (Phase 1) then covers.
TEST_F(WozzitsAppRenderBindingFixture, StaticSceneEditLandsInPolytreeNotSceneNodes)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(binding_load_desc()));

    // No behavior bindings — this is the static path — but a polytree exists, so
    // the edit has somewhere to land.
    ASSERT_EQ(app.active_behavior_binding_count(), 0u);

    // 'solo' is a root authored at X = -150.
    const auto stored_before = app.stored_node_local_translation("solo");
    ASSERT_TRUE(stored_before.has_value());
    EXPECT_FLOAT_EQ(stored_before->x, -150.0f);

    // Edit 'solo' to a new pose.
    wz::engine::assets::AuthoredTransform edited{};
    edited.translation[0] = 42.0f;
    edited.translation[1] = 7.0f;
    edited.translation[2] = -3.0f;
    edited.rotation_quat[3] = 1.0f;
    edited.scale[0] = 1.0f;
    edited.scale[1] = 1.0f;
    edited.scale[2] = 1.0f;
    ASSERT_TRUE(app.set_node_transform("solo", edited));

    // The edit is visible in the RENDER world transform (read from the polytree).
    const std::optional<wz::math::Mat4> world = app.node_world_transform("solo");
    ASSERT_TRUE(world.has_value());
    EXPECT_FLOAT_EQ(world->m[12], 42.0f);
    EXPECT_FLOAT_EQ(world->m[13], 7.0f);
    EXPECT_FLOAT_EQ(world->m[14], -3.0f);

    // But the STORED authored transform in scene_nodes_ is UNCHANGED — the seam
    // wrote the polytree, not scene_nodes_ (Phase 1's derive-on-save covers
    // persistence). This is the core Phase-2 contract.
    const auto stored_after = app.stored_node_local_translation("solo");
    ASSERT_TRUE(stored_after.has_value());
    EXPECT_FLOAT_EQ(stored_after->x, -150.0f)
        << "set_node_transform mutated scene_nodes_ instead of the polytree";

    // A missing node is still rejected (existence is resolved before the seam).
    EXPECT_FALSE(app.set_node_transform("no_such_node", edited));
}

// #229 live bind: a node carrying semantic bindings + a constant override
// assembles a CUSTOM (0x70A) renderable — the binding bridged to the field
// node's key with the publisher's variant, the constant table carrying the
// declared 'tint' field at its baked offset with a ZERO default (the
// per-instance override never enters the synthesized asset) — and the scene
// draws through it on device.
TEST_F(WozzitsAppRenderBindingFixture, CustomBindingSynthesizesAndDraws)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(custom_binding_load_desc()));

    EXPECT_EQ(app.resolved_renderable_node_count(), 1u);

    const std::optional<wz::asset::AssetKey> key =
        app.node_renderable_asset("bound");
    ASSERT_TRUE(key.has_value());

    const wz::engine::assets::RhiRenderableRecipe* recipe =
        ctx.assets->renderables().get_rhi_renderable_recipe(
            wz::engine::assets::RenderableAsset{ .output = *key });
    ASSERT_NE(recipe, nullptr)
        << "synthesized custom renderable did not compile";
    EXPECT_TRUE(recipe->custom);
    ASSERT_EQ(recipe->bindings.size(), 1u);
    EXPECT_EQ(recipe->bindings[0].semantic, "scalar_field_texture");
    EXPECT_EQ(recipe->bindings[0].variant, "field_texture");
    EXPECT_FALSE(recipe->bindings[0].key == wz::asset::AssetKey{});
    // The full declared-field table (#229), zero-defaulted: the node's
    // per-instance 'tint' override merges at pack time, never at compile.
    ASSERT_EQ(recipe->constants.size(), 1u);
    EXPECT_EQ(recipe->constants[0].name, "tint");
    EXPECT_EQ(recipe->constants[0].offset_dwords, 16u);
    EXPECT_EQ(recipe->constants[0].dwords, 4u);
    EXPECT_FLOAT_EQ(recipe->constants[0].value[0], 0.0f);
    EXPECT_FLOAT_EQ(recipe->constants[0].value[3], 0.0f);

    render_one_frame(app);
}

// #229 instance-edit contract: editing a constant override goes through
// set_node_renderable_constant and changes NOTHING on the asset side — same
// renderable key (no re-key), no new GPU resources (no recompile) — while the
// next frame still renders. Removing the LAST override (with no bindings this
// scene keeps one binding, so form never flips) is covered by the seam's
// upsert/remove semantics.
TEST_F(WozzitsAppRenderBindingFixture, ConstantOverrideEditsWithoutRecompile)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(custom_binding_load_desc()));
    render_one_frame(app);

    const std::optional<wz::asset::AssetKey> key_before =
        app.node_renderable_asset("bound");
    ASSERT_TRUE(key_before.has_value());
    const std::size_t resident_before = app.resident_gpu_resource_count();

    const float edited[4] = { 0.9f, 0.1f, 0.4f, 1.0f };
    EXPECT_TRUE(app.set_node_renderable_constant("bound", "tint", edited));

    const std::optional<wz::asset::AssetKey> key_after =
        app.node_renderable_asset("bound");
    ASSERT_TRUE(key_after.has_value());
    EXPECT_TRUE(*key_before == *key_after)
        << "a constant override edit re-keyed the synthesized renderable";

    render_one_frame(app);
    EXPECT_EQ(app.resident_gpu_resource_count(), resident_before)
        << "a constant override edit created GPU resources (recompiled)";

    // Guarded no-ops: missing node / empty name.
    EXPECT_FALSE(app.set_node_renderable_constant("no_such_node", "tint", edited));
    EXPECT_FALSE(app.set_node_renderable_constant("bound", "", edited));
}

// #229 bridge reset: deleting the bound graph node (the scalar field) and
// re-binding clears the stale resolved key, so the re-synthesized custom
// renderable fails compile with the unbound-source reason and the node stops
// drawing.
TEST_F(WozzitsAppRenderBindingFixture, DeletingBoundGraphNodeStopsDraw)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(custom_binding_load_desc()));

    const std::optional<wz::asset::AssetKey> key_before =
        app.node_renderable_asset("bound");
    ASSERT_TRUE(key_before.has_value());
    ASSERT_NE(
        ctx.assets->renderables().get_rhi_renderable_recipe(
            wz::engine::assets::RenderableAsset{ .output = *key_before }),
        nullptr);

    // Delete the field node (17) from a fresh draft and re-bind — the same
    // path an editor delete takes.
    wz::asset::AssetGraphDraft draft{};
    ASSERT_TRUE(load_graph_draft(draft));
    ASSERT_TRUE(wz::asset::remove_asset_graph_draft_node(
        draft, static_cast<wz::asset::AssetGraphDraftNodeId>(17)));
    const wz::app::AssetGraphCompileResult bound = app.bind_asset_graph(draft);
    // The draft (minus the field node) is still a VALID graph; only the
    // synthesized renderable fails resolve, which bind reports as a warning.
    EXPECT_TRUE(bound.ok);

    // The re-assembled renderable re-keyed (the binding row lost its source)
    // and its compile FAILED with the reason naming the unbound binding — so
    // the node no longer draws through a recipe.
    const std::optional<wz::asset::AssetKey> key_after =
        app.node_renderable_asset("bound");
    ASSERT_TRUE(key_after.has_value());
    EXPECT_FALSE(*key_after == *key_before);
    EXPECT_EQ(
        ctx.assets->renderables().get_rhi_renderable_recipe(
            wz::engine::assets::RenderableAsset{ .output = *key_after }),
        nullptr)
        << "deleting the bound graph node left a live recipe";

    const auto state = ctx.assets->system().node_resolve_state(*key_after);
    ASSERT_TRUE(state.has_value());
    EXPECT_NE(
        state->detail.find("has no connected source"), std::string::npos)
        << "detail was: " << state->detail;

    render_one_frame(app);  // still renders cleanly with the node dark
}

// #229 pack-time merge, byte-level: overrides land at each field's baked
// offset/width in the packed block, unknown names are ignored, and writes are
// clamped to the block. Pure CPU — this is the renderer helper the per-frame
// custom branch runs against the packet's copy of the constants.
TEST(MergeRenderableConstantOverrides, WritesFieldsAtBakedOffsets)
{
    namespace ea = wz::engine::assets;

    // 20-dword block: 16-dword head + tint (float4 @16). A second narrow
    // field would overflow — declared at the end to prove clamping.
    std::vector<uint8_t> block(20u * 4u, uint8_t{ 0 });
    const std::vector<ea::RhiRenderableConstant> fields = {
        { .name = "tint", .offset_dwords = 16u, .dwords = 4u },
        { .name = "overflowing", .offset_dwords = 19u, .dwords = 4u },
    };

    const std::vector<ea::SceneRenderableConstantOverride> overrides = {
        { .name = "tint", .value = { 0.25f, 0.5f, 0.75f, 1.0f } },
        { .name = "unknown_field", .value = { 9.0f, 9.0f, 9.0f, 9.0f } },
        { .name = "overflowing", .value = { 8.0f, 8.0f, 8.0f, 8.0f } },
    };

    wz::engine::rendering::merge_renderable_constant_overrides(
        block, fields, overrides);

    float tint[4] = {};
    std::memcpy(tint, block.data() + 16u * 4u, sizeof(tint));
    EXPECT_FLOAT_EQ(tint[0], 0.25f);
    EXPECT_FLOAT_EQ(tint[1], 0.5f);
    EXPECT_FLOAT_EQ(tint[2], 0.75f);
    EXPECT_FLOAT_EQ(tint[3], 1.0f);

    // The overflowing write was skipped whole (clamped, not truncated): the
    // head bytes before tint and the final dword keep their prior values.
    float last = -1.0f;
    std::memcpy(&last, block.data() + 19u * 4u, sizeof(last));
    EXPECT_FLOAT_EQ(last, 1.0f);  // tint.w, untouched by 'overflowing'
    for (size_t i = 0; i < 16u * 4u; ++i) {
        EXPECT_EQ(block[i], uint8_t{ 0 }) << "head byte " << i << " written";
    }
}
