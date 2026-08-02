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
#include <engine/assets/puppet_program.h>
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

#include <algorithm>
#include <any>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <numeric>
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

    // 4 shader nodes (VS source/shader, PS source/shader) + ONE PROGRAM PER
    // BLEND VARIANT (#274), all sharing that single shader pair: 2 source->shader
    // edges + 2 shader->program edges per variant.
    const size_t variants = ea::kPuppetProgramBlendCount;
    EXPECT_EQ(draft.nodes.size(), before + 4u + variants);
    EXPECT_EQ(draft.edges.size(), 2u + 2u * variants);

    // Idempotent: a second call finds the existing programs and adds nothing.
    const wz::asset::AssetGraphDraftNodeId program_again =
        ea::inochi::ensure_shared_puppet_program_node(draft, ctx, logger);
    EXPECT_EQ(program_again, program);
    EXPECT_EQ(draft.nodes.size(), before + 4u + variants);
    EXPECT_EQ(draft.edges.size(), 2u + 2u * variants);

    // Every variant is present exactly once, and they are DISTINCT nodes -- a
    // count alone would pass if the authoring emitted three Normals.
    {
        std::vector<int64_t> blends;
        for (const wz::asset::AssetGraphDraftNode& n : draft.nodes) {
            if (n.state == wz::asset::AssetGraphDraftNodeState::Deleted
                || n.node.schema.value != ea::kPuppetProgramSchema.value)
            {
                continue;
            }
            const auto* params =
                std::any_cast<wz::asset::ParamBlock>(&n.node.meta);
            ASSERT_NE(params, nullptr)
                << "puppet program node carries no ParamBlock -- the compiler "
                   "must DECLARE blend_mode or every variant collapses to Normal";
            blends.push_back(params->get<int64_t>(
                ea::kPuppetProgramBlendParam, -1));
        }
        std::ranges::sort(blends);
        // Derived from kPuppetProgramBlendCount, not hand-listed: authoring
        // emits exactly one variant per PuppetProgramBlend, so a member added
        // to that enum must show up here rather than requiring this literal to
        // be edited (it was { 0, 1, 2, 3, 4 } until Additive landed, #316).
        std::vector<int64_t> expected(ea::kPuppetProgramBlendCount);
        std::iota(expected.begin(), expected.end(), 0);
        EXPECT_EQ(blends, expected);
    }

    const auto commit = assets.commit_asset_graph_draft(draft);
    ASSERT_TRUE(commit.success());

    const ea::ResolveReport resolve = assets.resolve_all();
    for (const ea::ResolveFailure& f : resolve.failures) {
        ADD_FAILURE() << "resolve failure: error=" << static_cast<int>(f.error);
    }
    ASSERT_TRUE(resolve.ok());

    // EVERY blend variant resolved to a compiled RenderProgram (their shared
    // shader pair compiled at the params-derived vs_5_1 / ps_5_1 targets), and
    // the variants are distinct ASSETS -- create_custom mixes blend_mode into
    // its key, so three variants must not dedup down to one.
    std::vector<wz::asset::AssetKey> variant_keys;
    for (const wz::asset::AssetGraphDraftNode& n : draft.nodes) {
        if (n.state == wz::asset::AssetGraphDraftNodeState::Deleted
            || n.node.schema.value != ea::kPuppetProgramSchema.value)
        {
            continue;
        }
        EXPECT_NE(assets.system().find_compiled(n.node.key), nullptr)
            << "an authored puppet program variant did not resolve";
        variant_keys.push_back(n.node.key);
    }
    ASSERT_EQ(variant_keys.size(), ea::kPuppetProgramBlendCount);
    std::ranges::sort(variant_keys, [](const auto& a, const auto& b) {
        return a.content_hash.lo < b.content_hash.lo;
    });
    EXPECT_EQ(
        std::ranges::adjacent_find(variant_keys), variant_keys.end())
        << "puppet program blend variants collapsed to the same asset key";
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

// Pure (device-free): which program variant a Part's authored blend mode draws
// through (#274). The fallbacks matter as much as the hits -- masks and the
// destination-reading modes have no fixed-function variant and MUST land on
// Normal rather than on whatever the enum happens to sit next to.
TEST(PuppetProgramVariants, MapsPartBlendToVariant)
{
    using ea::PuppetProgramBlend;
    namespace ino = wz::engine::assets::inochi;

    EXPECT_EQ(
        ea::puppet_program_blend_for_part(ino::BlendMode::Multiply),
        PuppetProgramBlend::Multiply);
    EXPECT_EQ(
        ea::puppet_program_blend_for_part(ino::BlendMode::Screen),
        PuppetProgramBlend::Screen);
    EXPECT_EQ(
        ea::puppet_program_blend_for_part(ino::BlendMode::Normal),
        PuppetProgramBlend::Normal);

    // The two LOWER-relative modes (#299). They clip against what has already
    // been drawn, which reads like a backdrop sample -- but both are
    // destination-ALPHA operations, so they are ordinary PSO variants.
    EXPECT_EQ(
        ea::puppet_program_blend_for_part(ino::BlendMode::ClipToLower),
        PuppetProgramBlend::ClipToLower);
    EXPECT_EQ(
        ea::puppet_program_blend_for_part(ino::BlendMode::SliceFromLower),
        PuppetProgramBlend::SliceFromLower);

    // What is genuinely left needs destination COLOUR, which no fixed-function
    // blend can express; those still fall back to Normal rather than to
    // whatever the enum happens to sit next to.
    for (const ino::BlendMode unsupported : {
             ino::BlendMode::Overlay,
             ino::BlendMode::SoftLight,
             ino::BlendMode::ColorBurn,
             ino::BlendMode::Difference,
             ino::BlendMode::Unknown })
    {
        EXPECT_EQ(
            ea::puppet_program_blend_for_part(unsupported),
            PuppetProgramBlend::Normal)
            << "unsupported blend " << static_cast<int>(unsupported)
            << " must fall back to Normal";
    }

    // Normal is PREMULTIPLIED, not AlphaBlend -- the atlas and pixel shader
    // work in premultiplied space (#277), so AlphaBlend would double-scale.
    EXPECT_EQ(
        ea::rhi_blend_for(PuppetProgramBlend::Normal),
        wz::rhi::BlendMode::PremultipliedAlpha);
    EXPECT_EQ(
        ea::rhi_blend_for(PuppetProgramBlend::Multiply),
        wz::rhi::BlendMode::Multiply);
    EXPECT_EQ(
        ea::rhi_blend_for(PuppetProgramBlend::Screen),
        wz::rhi::BlendMode::Screen);
    // ClipToLower is Porter-Duff source-atop; SliceFromLower is its cutting
    // counterpart. Named for the general operation in rhi, since the engine
    // consumes them as general capability rather than as Inochi modes.
    EXPECT_EQ(
        ea::rhi_blend_for(PuppetProgramBlend::ClipToLower),
        wz::rhi::BlendMode::SourceAtop);
    EXPECT_EQ(
        ea::rhi_blend_for(PuppetProgramBlend::SliceFromLower),
        wz::rhi::BlendMode::SliceFromDestination);
}

// puppet_program_variants on an EMPTY system: no siblings to find, so the base
// is returned for every blend. This is the pre-#274 graph case, and it is what
// makes the renderer degrade to "draw everything Normal" instead of failing.
TEST(PuppetProgramVariants, FallsBackToBaseWhenNoSiblings)
{
    wz::asset::AssetSystem system{ wz::asset::CompilerRegistry{} };
    ea::RenderProgramTable programs_table;
    const ea::RenderProgramAssetModule programs{ system, programs_table };
    wz::asset::AssetKey base{};
    base.content_hash.lo = 0x1234;

    const ea::PuppetProgramVariants v =
        ea::puppet_program_variants(system, programs, base);
    for (std::size_t i = 0; i < ea::kPuppetProgramBlendCount; ++i) {
        EXPECT_EQ(v.key_for(static_cast<ea::PuppetProgramBlend>(i)), base);
    }

    // An empty base stays empty rather than manufacturing a key.
    const ea::PuppetProgramVariants none =
        ea::puppet_program_variants(system, programs, wz::asset::AssetKey{});
    EXPECT_EQ(
        none.key_for(ea::PuppetProgramBlend::Normal), wz::asset::AssetKey{});
}
