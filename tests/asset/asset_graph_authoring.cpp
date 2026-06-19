#include <gtest/gtest.h>

#include <engine/assets/authoring/asset_graph_authoring.h>
#include <engine/assets/engine_asset_library_internal.h>  // internal::FileSourceDesc

#include <asset/compiler.h>
#include <asset/draft.h>
#include <asset/types.h>

#include <any>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace wz::asset;
    namespace authoring = wz::engine::assets::authoring;

    const SchemaID kFileSchema{ 0x1001u };
    const SchemaID kParamSchema{ 0x1002u };
    const SchemaID kConsumerSchema{ 0x1003u };

    // File-carrier compiler: declares a FilePath parameter, no input ports.
    AssetCompiler file_carrier_compiler()
    {
        AssetCompiler c{};
        c.input_schema = kFileSchema;
        c.output_type = AssetType::Mesh;
        c.parameters = {
            ParamDecl{ .name = "source_path", .type = ParamType::FilePath },
        };
        return c;
    }

    // Plain-parameter compiler: value params, no file path.
    AssetCompiler param_compiler()
    {
        AssetCompiler c{};
        c.input_schema = kParamSchema;
        c.output_type = AssetType::Material;
        c.parameters = {
            ParamDecl{ .name = "iterations", .type = ParamType::Int, .default_num = 4 },
            ParamDecl{ .name = "tau", .type = ParamType::Float, .default_num = 0.25 },
        };
        return c;
    }

    // Consumer compiler with one required Mesh input port.
    AssetCompiler consumer_compiler()
    {
        AssetCompiler c{};
        c.input_schema = kConsumerSchema;
        c.output_type = AssetType::Texture;
        c.input_ports = {
            InputPort{ .name = "mesh", .type = AssetType::Mesh },
        };
        return c;
    }

    authoring::GraphAuthoringContext make_ctx(const CompilerRegistry& registry)
    {
        return authoring::GraphAuthoringContext{
            registry,
            [](const wz::fs::Path& p) { return wz::fs::Path{ "ROOT/" } + p; },
        };
    }

    const AssetGraphDraftRegistration* find_registration(
        const std::vector<AssetGraphDraftRegistration>& registrations,
        AssetGraphDraftNodeId id)
    {
        for (const AssetGraphDraftRegistration& r : registrations) {
            if (r.draft_node == id) {
                return &r;
            }
        }
        return nullptr;
    }
}

TEST(AssetGraphAuthoring, FileCarrierNodeBakesFileSourceMeta)
{
    CompilerRegistry registry;
    registry.register_compiler(file_carrier_compiler());
    const auto ctx = make_ctx(registry);

    const AssetNode node = authoring::make_source_asset_node(
        ctx, kFileSchema, AssetType::Mesh, /*params=*/{}, wz::fs::Path{ "meshes/bunny.glb" });

    EXPECT_EQ(node.type, AssetType::Mesh);
    EXPECT_EQ(node.schema.value, kFileSchema.value);
    EXPECT_EQ(node.stage, AssetStage::Source);
    EXPECT_EQ(node.residency, ResidencyIntent::CompileOnly);
    EXPECT_EQ(node.kind, AssetNodeKind::Asset);

    const auto* file =
        std::any_cast<wz::engine::assets::internal::FileSourceDesc>(&node.meta);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->canonical_path, "meshes/bunny.glb");
    EXPECT_EQ(file->full_path, "ROOT/meshes/bunny.glb");
}

TEST(AssetGraphAuthoring, SourcePathParamOverridesSourceFile)
{
    CompilerRegistry registry;
    registry.register_compiler(file_carrier_compiler());
    const auto ctx = make_ctx(registry);

    ParamBlock params;
    params.values["source_path"] = std::string{ "meshes/override.glb" };

    const AssetNode node = authoring::make_source_asset_node(
        ctx, kFileSchema, AssetType::Mesh, params, wz::fs::Path{ "meshes/dragged.glb" });

    const auto* file =
        std::any_cast<wz::engine::assets::internal::FileSourceDesc>(&node.meta);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->canonical_path, "meshes/override.glb");
    EXPECT_EQ(file->full_path, "ROOT/meshes/override.glb");
}

TEST(AssetGraphAuthoring, FileCarrierTrimsQuotedWhitespacePaths)
{
    CompilerRegistry registry;
    registry.register_compiler(file_carrier_compiler());
    const auto ctx = make_ctx(registry);

    // via source_file: surrounding whitespace + quotes are stripped.
    {
        const AssetNode node = authoring::make_source_asset_node(
            ctx, kFileSchema, AssetType::Mesh, /*params=*/{},
            wz::fs::Path{ "  \"meshes/bunny.glb\"  " });
        const auto* file =
            std::any_cast<wz::engine::assets::internal::FileSourceDesc>(&node.meta);
        ASSERT_NE(file, nullptr);
        EXPECT_EQ(file->canonical_path, "meshes/bunny.glb");
        EXPECT_EQ(file->full_path, "ROOT/meshes/bunny.glb");
    }

    // via source_path param: also trimmed, and overrides source_file.
    {
        ParamBlock params;
        params.values["source_path"] = std::string{ "  'meshes/override.glb'  " };
        const AssetNode node = authoring::make_source_asset_node(
            ctx, kFileSchema, AssetType::Mesh, params,
            wz::fs::Path{ "meshes/dragged.glb" });
        const auto* file =
            std::any_cast<wz::engine::assets::internal::FileSourceDesc>(&node.meta);
        ASSERT_NE(file, nullptr);
        EXPECT_EQ(file->canonical_path, "meshes/override.glb");
        EXPECT_EQ(file->full_path, "ROOT/meshes/override.glb");
    }
}

TEST(AssetGraphAuthoring, ParamCompilerIgnoresSourceFile)
{
    CompilerRegistry registry;
    registry.register_compiler(param_compiler());
    const auto ctx = make_ctx(registry);

    // A non-file-carrier compiler handed a source_file keeps its ParamBlock meta
    // and never receives FileSourceDesc meta it could not compile from.
    const AssetNode node = authoring::make_source_asset_node(
        ctx, kParamSchema, AssetType::Material, /*params=*/{},
        wz::fs::Path{ "meshes/should_be_ignored.glb" });

    EXPECT_EQ(node.residency, ResidencyIntent::EditorResident);
    EXPECT_EQ(
        std::any_cast<wz::engine::assets::internal::FileSourceDesc>(&node.meta),
        nullptr);
    const auto* block = std::any_cast<ParamBlock>(&node.meta);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(std::get<int64_t>(block->values.at("iterations")), 4);
}

TEST(AssetGraphAuthoring, ParamNodeFillsDefaultsAsMeta)
{
    CompilerRegistry registry;
    registry.register_compiler(param_compiler());
    const auto ctx = make_ctx(registry);

    const AssetNode node =
        authoring::make_source_asset_node(ctx, kParamSchema, AssetType::Material);

    EXPECT_EQ(node.residency, ResidencyIntent::EditorResident);
    const auto* block = std::any_cast<ParamBlock>(&node.meta);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(std::get<int64_t>(block->values.at("iterations")), 4);
    EXPECT_DOUBLE_EQ(std::get<double>(block->values.at("tau")), 0.25);
}

TEST(AssetGraphAuthoring, UnknownCompilerYieldsBareSourceNode)
{
    CompilerRegistry registry;  // empty
    const auto ctx = make_ctx(registry);

    const AssetNode node =
        authoring::make_source_asset_node(ctx, kParamSchema, AssetType::Material);

    EXPECT_EQ(node.stage, AssetStage::Source);
    EXPECT_EQ(node.residency, ResidencyIntent::EditorResident);
    EXPECT_FALSE(node.meta.has_value());
}

TEST(AssetGraphAuthoring, AddSourceNodeCreatesCreatedDraftNode)
{
    CompilerRegistry registry;
    registry.register_compiler(param_compiler());
    const auto ctx = make_ctx(registry);

    AssetGraphDraft draft;
    const auto id =
        authoring::add_source_asset_node(draft, ctx, kParamSchema, AssetType::Material);
    ASSERT_NE(id, INVALID_ASSET_GRAPH_DRAFT_NODE);

    const AssetGraphDraftNode* n = find_asset_graph_draft_node(draft, id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->state, AssetGraphDraftNodeState::Created);
    EXPECT_EQ(n->node.type, AssetType::Material);
}

TEST(AssetGraphAuthoring, UnknownTypeIsRejected)
{
    CompilerRegistry registry;
    const auto ctx = make_ctx(registry);

    AssetGraphDraft draft;
    const auto id =
        authoring::add_source_asset_node(draft, ctx, kParamSchema, AssetType::Unknown);
    EXPECT_EQ(id, INVALID_ASSET_GRAPH_DRAFT_NODE);
    EXPECT_TRUE(draft.nodes.empty());
}

// End-to-end over the live draft: nodes built by the moved authoring verb feed
// the existing draft.h pipeline (connect -> materialize -> registrations) with
// the consumer's dependency key pointing at the provider's materialized key.
TEST(AssetGraphAuthoring, LiveDraftAddConnectMaterialize)
{
    CompilerRegistry registry;
    registry.register_compiler(file_carrier_compiler());  // Mesh provider
    registry.register_compiler(consumer_compiler());      // Texture consumer (Mesh input)
    const auto ctx = make_ctx(registry);

    AssetGraphDraft draft;
    const auto mesh = authoring::add_source_asset_node(
        draft, ctx, kFileSchema, AssetType::Mesh, {}, wz::fs::Path{ "meshes/bunny.glb" });
    const auto consumer =
        authoring::add_source_asset_node(draft, ctx, kConsumerSchema, AssetType::Texture);
    ASSERT_NE(mesh, INVALID_ASSET_GRAPH_DRAFT_NODE);
    ASSERT_NE(consumer, INVALID_ASSET_GRAPH_DRAFT_NODE);

    const auto edge =
        connect_asset_graph_draft_nodes(draft, mesh, consumer, /*to_input_port=*/0);
    ASSERT_NE(edge, INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry));

    const auto registrations = asset_graph_draft_to_registrations(draft, &registry);
    const auto* mesh_reg = find_registration(registrations, mesh);
    const auto* consumer_reg = find_registration(registrations, consumer);
    ASSERT_NE(mesh_reg, nullptr);
    ASSERT_NE(consumer_reg, nullptr);

    EXPECT_TRUE(asset_graph_draft_key_valid(mesh_reg->node.key));
    ASSERT_EQ(consumer_reg->dep_keys.size(), 1u);
    EXPECT_EQ(consumer_reg->dep_keys[0], mesh_reg->node.key);
}
