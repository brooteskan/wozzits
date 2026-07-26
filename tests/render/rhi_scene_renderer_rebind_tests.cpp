// tests/render/rhi_scene_renderer_rebind_tests.cpp
//
// Regression coverage for issue #182: WozzitsApp_v1 must be linkable from the
// window_engine library (so the scene editor can compose it) AND must correctly
// rebind on a wholesale asset-graph swap — invalidating the renderer's realized
// caches and releasing the outgoing graph's GPU resources, rather than leaking
// them or drawing the previous graph.
//
// This is an on-device test: it creates a real window + DX12 device and renders
// a dedicated, self-contained fixture project (test_rebind_fixture) staged from
// tests/render/fixtures — a procedural cube mesh + the two bundled pull shaders,
// with NO external/heavy .glb. The fixture is separate from the editable sample
// projects so editor scratch work can never break these tests. Skipped if no
// device can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/assets/scene/asset_graph_json.h>
#include <engine/project/project_manifest.h>

#include <asset/draft.h>
#include <external/json/json_parser.h>
#include <file/filesystem.h>
#include <gpu/gpu.h>

#include <fstream>
#include <iterator>
#include <string>

namespace
{
    constexpr const char* kProjectRoot = "projects/test_rebind_fixture";

    std::string read_text_file(const wz::fs::Path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        return std::string(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }

    struct WozzitsAppFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = { "wozzits_app_v1_rebind_test", 256, 256, false, false };
            // The fixture is staged under "resources/projects/test_rebind_fixture"
            // next to the test exe; the manifest path is resolved against this
            // resource root (load_project_manifest only prefixes it when set).
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP() << "no GPU device — skipping on-device rebind test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        // begin/clear/render/end/present one frame; this is what realizes the
        // scene's renderables (ensure_renderable runs inside render_scene).
        void render_one_frame(wz::app::WozzitsApp_v1& app)
        {
            ASSERT_TRUE(wz::gpu::begin_frame(ctx.device));
            wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
            app.simulation_tick(wz::input::InputState{}, 0.0f);
            EXPECT_TRUE(app.render_scene());
            ASSERT_TRUE(wz::gpu::end_frame(ctx.device));
            wz::gpu::present(ctx.device, /*sync_interval*/ 0);
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
            };
        }

        // Parse a fresh AssetGraphDraft from the project's graph JSON — the same
        // input load_scene consumes, but built independently so we can feed it
        // to bind_asset_graph the way the editor re-binds an edited draft.
        bool load_graph_draft(wz::asset::AssetGraphDraft& out)
        {
            const auto project = load_test_project();
            if (!project.ok || project.manifest.asset_graph_path.empty()) {
                return false;
            }
            const std::string text =
                read_text_file(ctx.assets->files().resolve_path(
                    project.manifest.asset_graph_path));
            if (text.empty()) {
                return false;
            }
            const wz::json::JSONParseResult parsed =
                wz::json::parse_json_string(text);
            if (!parsed.ok || !parsed.document.root) {
                return false;
            }
            std::string error;
            return wz::engine::assets::load_asset_graph_draft_from_v2_json(
                *parsed.document.root, out, error);
        }
    };
}

// Criterion 1: WozzitsApp_v1 links + constructs against window_engine (this TU
// only links the library, not the wozzits_app_v1 executable).
TEST_F(WozzitsAppFixture, ConstructsAgainstLibrary)
{
    wz::app::WozzitsApp_v1 app(ctx);
    EXPECT_EQ(app.resident_gpu_resource_count(), 0u);
}

// Criterion 2: binding a different graph twice renders the second graph with
// the previous graph's GPU resources released (resident count returns flat,
// never doubles).
TEST_F(WozzitsAppFixture, RebindReleasesOutgoingGraphResources)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)))
        << "test_mesh_001 scene failed to load/compile";

    // Realize the first graph's renderables.
    render_one_frame(app);
    const std::size_t resident_after_first = app.resident_gpu_resource_count();
    const std::size_t programs_after_first = app.registered_program_count();
    // Step 2: the renderer binds the asset-published resident pull buffers
    // (positions + indices + normals) by identity — it does NOT re-upload CPU
    // mesh data. The gpu_sparse_mesh compiler publishes a third pull buffer,
    // "pull_normals", for any mesh with has_normals (#259); the fixture's
    // procedural cube carries normals, so 3 resident pull buffers exist. Plus
    // the #229 fixture's procedural scalar field, published resident as an R32
    // texture at resolve (graph node 17, unreferenced by THIS scene), that is 4.
    // Plus the #287 fixture's render-target texture (graph node 20, also
    // unreferenced by THIS scene), resident at resolve like any other texture,
    // that is 5. A silent CPU-upload fallback would acquire duplicates under a
    // different identity, inflating the count past 5.
    EXPECT_EQ(resident_after_first, 5u)
        << "renderer should bind the 3 resident pull buffers (positions + "
           "indices + normals) + the resident field + render-target textures, "
           "not re-upload";
    EXPECT_GT(programs_after_first, 0u);
    // #192: the fixture's custom render program (schema 0x103) must come from the
    // asset compiler — which registers the rhi program under program_ref during
    // resolve — not the renderer's render-time bridge. Zero render-time bridges
    // proves the compiler-produced path is taken, the analog of
    // resident_after_first == 4 proving the resident-buffer path in #190.
    EXPECT_EQ(app.render_time_program_bridge_count(), 0u)
        << "custom render program was bridged at render time, not produced by "
           "the asset compiler";

    // Editor-style wholesale rebind: feed a freshly parsed draft to
    // bind_asset_graph. This must release the outgoing graph's GPU resources
    // and invalidate the renderer's realized caches.
    wz::asset::AssetGraphDraft draft;
    ASSERT_TRUE(load_graph_draft(draft)) << "could not reload graph draft";
    const wz::app::AssetGraphCompileResult bound = app.bind_asset_graph(draft);
    ASSERT_TRUE(bound.ok) << "rebind failed to compile the graph";

    // The scene was re-bridged to the new graph's keys (not left stale).
    EXPECT_EQ(app.resolved_renderable_node_count(), 1u)
        << "scene renderable was not re-bridged to the new graph";

    // Renderer-owned upload buffers were released + collected. The rebound
    // graph has already resolved its gpu_sparse_mesh asset (3 pull buffers:
    // positions + indices + normals, #259) + the #229 field texture + the #287
    // render-target texture, so those asset-published resources remain resident
    // before render consumes them.
    EXPECT_EQ(app.resident_gpu_resource_count(), 5u)
        << "rebind should retain only the resolved asset-published resources";

    // Render the rebound graph: it re-realizes against the new keys.
    render_one_frame(app);
    const std::size_t resident_after_rebind = app.resident_gpu_resource_count();
    EXPECT_EQ(resident_after_rebind, resident_after_first)
        << "rebind leaked resources (count grew) or failed to re-realize";
    // Programs/shaders are kept across a same-content rebind by the survivor-
    // preserving reconcile (not retired + re-registered), so registry occupancy
    // returns flat without depending on the compiler re-running on a cache hit.
    EXPECT_EQ(app.registered_program_count(), programs_after_first)
        << "rebind grew or emptied the render-program registry";
    // #193: with the reconcile keeping the compiler-produced program and the
    // shader asset producing its own rhi ShaderModule, the renderer binds the
    // program by find even after a same-content rebind — it never bridges at
    // render time. So render-time bridges stay 0 across BOTH the first render and
    // the rebind (the gap #192's first-render-only assertion left unguarded).
    EXPECT_EQ(app.render_time_program_bridge_count(), 0u)
        << "custom program was bridged at render time after a same-content "
           "rebind — the reconcile / shader-module path did not survive the swap";
}

// Criterion 2, missing-node case: when the replacement graph no longer contains
// the authored renderable node, the scene must STOP referencing the previous
// graph's key (it can otherwise still resolve via cached compiled payloads and
// draw stale geometry), and must draw nothing.
TEST_F(WozzitsAppFixture, RebindToGraphWithoutRenderableClearsStaleKey)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)))
        << "test_mesh_001 scene failed to load/compile";
    render_one_frame(app);
    ASSERT_GT(app.resident_gpu_resource_count(), 0u);
    ASSERT_EQ(app.resolved_renderable_node_count(), 1u)
        << "fixture precondition: test_mesh_001 has one bridged renderable";
    // The first graph realized at least one program + shader + descriptor table.
    ASSERT_GT(app.registered_program_count(), 0u);
    ASSERT_GT(app.registered_shader_count(), 0u);
    ASSERT_GT(app.cached_descriptor_table_count(), 0u);

    // Swap to a graph that does NOT contain the authored renderable node (an
    // empty graph is the simplest such replacement).
    wz::asset::AssetGraphDraft empty;
    const wz::app::AssetGraphCompileResult bound = app.bind_asset_graph(empty);
    ASSERT_TRUE(bound.ok) << "binding an empty graph should succeed";

    // The stale key must be cleared, not retained from the previous graph.
    EXPECT_EQ(app.resolved_renderable_node_count(), 0u)
        << "stale renderable key was not cleared on graph swap";

    // The outgoing graph's rhi programs + shaders must be retired, not leaked
    // into the fixed-capacity registries.
    EXPECT_EQ(app.registered_program_count(), 0u)
        << "render programs were not reclaimed on graph swap";
    EXPECT_EQ(app.registered_shader_count(), 0u)
        << "shader modules were not reclaimed on graph swap";
    EXPECT_EQ(app.cached_descriptor_table_count(), 0u)
        << "recorder SRV descriptor tables were not released on graph swap";

    render_one_frame(app);
    EXPECT_EQ(app.resident_gpu_resource_count(), 0u)
        << "nothing should be realized/drawn for the empty graph";
}
