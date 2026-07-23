// tests/render/rhi_puppet_authoring_render_tests.cpp
//
// On-device coverage for the Inochi2D puppet AUTHORING path (inochi S2c): the
// shared "Inochi shared assets" subgraph is authored into an AssetGraphDraft by
// ensure_shared_puppet_program_node (VS/PS source -> Shader -> PuppetProgram),
// committed, and resolved. Where rhi_puppet_render_tests builds the program via
// the typed create_custom API, these tests build it purely through the draft /
// authoring path -- the way the editor will -- and prove:
//   1. the routine authors a subgraph that commits (no duplicate-key rejection)
//      and RESOLVES, so the puppet-program compiler + the params-path shaders
//      (whose derived target is the SM 5.1 the space2 bindings need) all work;
//   2. a puppet renderable wired to that shared program node BY AN EXPLICIT EDGE
//      realizes and records on device, from the real Aka.inp.
//
// Both are skipped without a GPU device; test 2 also needs the Aka.inp fixture.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/inochi/puppet_authoring.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_scene_renderer.h>

#include <asset/draft.h>
#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <math/mat4.h>
#include <math/math_types.h>
#include <window/window2.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;
    namespace fs = std::filesystem;

    // A throwaway device + resource root for one test body. Returns false via
    // GTEST_SKIP semantics by leaving `device` invalid.
    struct DeviceHarness
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};

        bool ok() const { return device.valid(); }

        DeviceHarness()
        {
            wz::window::WindowDesc desc{};
            desc.title = "rhi_puppet_authoring_test";
            desc.width = 256;
            desc.height = 256;
            desc.resizable = false;
            window = wz::window::create_window(desc);
            if (!window.valid()) {
                return;
            }
            device = wz::gpu::create_device(window);
        }

        ~DeviceHarness()
        {
            if (device.valid()) {
                wz::gpu::destroy_device(device);
            }
            if (window.valid()) {
                wz::window::destroy_window(window);
            }
        }
    };

    std::string unique_temp_root(const char* tag)
    {
        const auto stamp = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path root =
            fs::temp_directory_path() / (std::string(tag) + std::to_string(stamp));
        fs::remove_all(root);
        return root.string();
    }

    // First non-deleted draft node carrying `schema`, or nullptr.
    const wz::asset::AssetGraphDraftNode* node_with_schema(
        const wz::asset::AssetGraphDraft& draft,
        wz::asset::SchemaID schema)
    {
        for (const wz::asset::AssetGraphDraftNode& node : draft.nodes) {
            if (node.state != wz::asset::AssetGraphDraftNodeState::Deleted
                && node.node.schema.value == schema.value)
            {
                return &node;
            }
        }
        return nullptr;
    }
}

// Test 1: the authoring routine yields a subgraph that commits + resolves, and is
// idempotent (a second call reuses the shared program node, adding nothing).
TEST(RhiPuppetAuthoring, SharedProgramSubgraphCommitsAndResolves)
{
    DeviceHarness harness;
    if (!harness.ok()) {
        GTEST_SKIP() << "no GPU device for the puppet authoring test";
    }

    wz::engine::rendering::EngineGpuContext gpu(harness.device);
    wz::Logger logger;
    ea::EngineAssetLibrary assets(gpu, logger, unique_temp_root("wozzits_puppet_authoring_"));

    wz::asset::AssetGraphDraft draft{};
    wz::asset::load_asset_graph_draft_from_registered_assets(
        draft, assets.system().registered_assets());

    const size_t before = draft.nodes.size();
    const auto ctx = assets.graph_authoring_context();

    const wz::asset::AssetGraphDraftNodeId program =
        ea::inochi::ensure_shared_puppet_program_node(draft, ctx, logger);
    ASSERT_NE(program, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);

    // 5 nodes (VS source/shader, PS source/shader, program) + 4 edges.
    EXPECT_EQ(draft.nodes.size(), before + 5u);
    EXPECT_EQ(draft.edges.size(), 4u);

    // Idempotent: a second call finds the existing program and adds nothing.
    const wz::asset::AssetGraphDraftNodeId program_again =
        ea::inochi::ensure_shared_puppet_program_node(draft, ctx, logger);
    EXPECT_EQ(program_again, program);
    EXPECT_EQ(draft.nodes.size(), before + 5u);
    EXPECT_EQ(draft.edges.size(), 4u);

    const auto commit = assets.commit_asset_graph_draft(draft);
    ASSERT_TRUE(commit.success());

    const ea::ResolveReport resolve = assets.resolve_all();
    for (const ea::ResolveFailure& f : resolve.failures) {
        ADD_FAILURE() << "resolve failure: error=" << static_cast<int>(f.error);
    }
    ASSERT_TRUE(resolve.ok());

    // The puppet program resolved to a compiled RenderProgram (its two shaders
    // compiled at the params-derived vs_5_1 / ps_5_1 targets).
    const wz::asset::AssetGraphDraftNode* program_node =
        node_with_schema(draft, ea::kPuppetProgramSchema);
    ASSERT_NE(program_node, nullptr);
    EXPECT_NE(assets.system().find_compiled(program_node->node.key), nullptr)
        << "authored puppet program did not resolve to a compiled asset";
}

// Test 2: a puppet renderable wired to the authored shared program by an edge
// realizes + records on device from the real Aka.inp.
TEST(RhiPuppetAuthoring, AuthoredPuppetRenderableRealizesAndRecords)
{
    const fs::path fixture = fs::path(WZ_TEST_FIXTURE_DIR) / "inochi" / "Aka.inp";
    if (!fs::exists(fixture)) {
        GTEST_SKIP() << "no Aka.inp fixture for the puppet authoring render test";
    }

    DeviceHarness harness;
    if (!harness.ok()) {
        GTEST_SKIP() << "no GPU device for the puppet authoring render test";
    }

    wz::engine::rendering::EngineGpuContext gpu(harness.device);
    wz::Logger logger;
    ea::EngineAssetLibrary assets(gpu, logger, unique_temp_root("wozzits_puppet_authoring_render_"));

    wz::asset::AssetGraphDraft draft{};
    wz::asset::load_asset_graph_draft_from_registered_assets(
        draft, assets.system().registered_assets());

    const auto ctx = assets.graph_authoring_context();

    // Shared puppet program (VS/PS source -> shader -> program).
    const wz::asset::AssetGraphDraftNodeId program =
        ea::inochi::ensure_shared_puppet_program_node(draft, ctx, logger);
    ASSERT_NE(program, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);

    // Puppet from the Aka.inp raw file.
    const wz::asset::AssetGraphDraftNodeId inp_file =
        ea::authoring::add_source_asset_node(
            draft, ctx, ea::kRawFileSchema, ea::kAssetTypeRawFile,
            /*params=*/{}, wz::fs::Path{ fixture.string() });
    const wz::asset::AssetGraphDraftNodeId puppet =
        ea::authoring::add_source_asset_node(
            draft, ctx, ea::kPuppetFromFileSchema, ea::kAssetTypePuppet);
    ASSERT_NE(inp_file, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);
    ASSERT_NE(puppet, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);
    wz::asset::connect_asset_graph_draft_nodes(draft, inp_file, puppet, 0);

    // Puppet renderable: puppet (port 0) + shared program (port 1) by edge.
    const wz::asset::AssetGraphDraftNodeId renderable =
        ea::authoring::add_source_asset_node(
            draft, ctx, ea::kPuppetRhiRenderableSchema, ea::kAssetTypeRenderable);
    ASSERT_NE(renderable, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);
    wz::asset::connect_asset_graph_draft_nodes(draft, puppet, renderable, 0);
    wz::asset::connect_asset_graph_draft_nodes(draft, program, renderable, 1);

    const auto commit = assets.commit_asset_graph_draft(draft);
    ASSERT_TRUE(commit.success());
    const ea::ResolveReport resolve = assets.resolve_all();
    for (const ea::ResolveFailure& f : resolve.failures) {
        ADD_FAILURE() << "resolve failure: error=" << static_cast<int>(f.error);
    }
    ASSERT_TRUE(resolve.ok());

    // The renderable's materialized key (draft reloaded to Existing on commit).
    const wz::asset::AssetGraphDraftNode* renderable_node =
        node_with_schema(draft, ea::kPuppetRhiRenderableSchema);
    ASSERT_NE(renderable_node, nullptr);
    const wz::asset::AssetKey renderable_key = renderable_node->node.key;
    ASSERT_FALSE(renderable_key == wz::asset::AssetKey{});

    // Drive one device frame through the renderer.
    wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);

    ea::SceneNodeAsset node{};
    node.id = wz::scene::AuthoredEntityId{ 1 };
    node.name = "puppet";
    node.visible = true;
    node.renderable_asset = renderable_key;
    const std::vector<ea::SceneNodeAsset> nodes{ node };

    ASSERT_TRUE(wz::gpu::begin_frame(harness.device));
    wz::gpu::clear(harness.device, 0.1f, 0.1f, 0.12f, 1.0f);
    const bool recorded = renderer.render_scene(
        nodes, assets, wz::math::Mat4::identity(), wz::math::Vec3{ 0.0f, 0.0f, 0.0f });
    EXPECT_TRUE(recorded)
        << "authored puppet failed to realize or the recorder rejected the draws";
    ASSERT_TRUE(wz::gpu::end_frame(harness.device));
    wz::gpu::present(harness.device, /*sync_interval*/ 0);

    // Same structural proofs as the typed render test: the program came from the
    // asset compiler (no render-time bridge), and per-Part object SRGs bound.
    EXPECT_GT(renderer.registered_program_count(), 0u);
    EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u)
        << "authored puppet program was bridged at render time";
    EXPECT_GT(renderer.cached_descriptor_table_count(), 0u)
        << "authored puppet Part object SRGs did not bind descriptor tables";
}
