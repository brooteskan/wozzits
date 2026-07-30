#include <gtest/gtest.h>

#include <engine/editor/asset_graph_editor_session.h>

#include <asset/draft.h>
#include <engine/assets/puppet_program.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/editor/asset_graph_snapshot.h>
#include <engine/assets/placement/placement.h>
#include <engine/assets/placement/placement_compilers.h>
#include <external/json/json_parser.h>
#include <logging/logger.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ranges>

namespace
{
    namespace fs = std::filesystem;

    constexpr wz::asset::SchemaID kSchema{ 0x1ull };

    wz::asset::AssetKey make_key(uint64_t value)
    {
        return wz::asset::AssetKey{
            .content_hash = { value, value + 1u },
            .schema_hash = { kSchema.value, 0u },
            .compiler_hash = { value + 2u, value + 3u },
        };
    }

    struct TempProjectRoot
    {
        fs::path root;

        TempProjectRoot()
        {
            root = fs::temp_directory_path()
                / ("wozzits_asset_graph_editor_session_tests_"
                   + std::to_string(
                       std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count()));
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        ~TempProjectRoot()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

    void write_text_file(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good()) << "failed to open " << path.string();
        file << text;
    }

    void write_project(const fs::path& project_root)
    {
        write_text_file(
            project_root / ".wozzits" / "project.json",
            R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "SessionTest",
  "asset_graph": "assets.graph.json"
})json");

        write_text_file(
            project_root / "assets.graph.json",
            R"json({
  "schema": "wozzits.scene_editor.assets.graph.v2",
  "nodes": [
    {
      "node_id": 1,
      "type": 7,
      "schema": "0x1",
      "stage": 0,
      "residency": 1,
      "kind": 0
    },
    {
      "node_id": 2,
      "type": 4,
      "schema": "0x1",
      "stage": 0,
      "residency": 1,
      "kind": 0,
      "deps": [
        {
          "from_node_id": 1,
          "to_input_port": 0
        }
      ]
    },
    {
      "node_id": 3,
      "type": 3,
      "schema": "0x1",
      "stage": 0,
      "residency": 1,
      "kind": 0
    },
    {
      "node_id": 4,
      "type": 7,
      "schema": "0x1",
      "stage": 0,
      "residency": 1,
      "kind": 0
    }
  ]
})json");
    }

    void write_valid_rewire_project(const fs::path& project_root)
    {
        write_text_file(
            project_root / ".wozzits" / "project.json",
            R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "SessionTest",
  "asset_graph": "assets.graph.json"
})json");

        write_text_file(
            project_root / "assets.graph.json",
            R"json({
  "schema": "wozzits.scene_editor.assets.graph.v2",
  "nodes": [
    {
      "node_id": 1,
      "type": 7,
      "schema": "0x1",
      "stage": 0,
      "residency": 1,
      "kind": 0
    },
    {
      "node_id": 2,
      "type": 4,
      "schema": "0x1",
      "stage": 0,
      "residency": 1,
      "kind": 0,
      "deps": [
        {
          "from_node_id": 1,
          "to_input_port": 0
        }
      ]
    },
    {
      "node_id": 4,
      "type": 7,
      "schema": "0x1",
      "stage": 0,
      "residency": 1,
      "kind": 0
    }
  ]
})json");
    }

    wz::asset::CompilerRegistry make_registry()
    {
        wz::asset::CompilerRegistry registry;
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kSchema,
            .output_type = wz::asset::AssetType::ShaderSource,
        });
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kSchema,
            .output_type = wz::asset::AssetType::Shader,
            .input_ports = {
                wz::asset::InputPort{
                    .name = "source_file",
                    .type = wz::asset::AssetType::ShaderSource,
                },
            },
        });
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kSchema,
            .output_type = wz::asset::AssetType::Material,
            .input_ports = {
                wz::asset::InputPort{
                    .name = "shader",
                    .type = wz::asset::AssetType::Shader,
                },
            },
        });
        return registry;
    }

    const wz::engine::editor::AssetGraphSnapshotNode* find_snapshot_node(
        const wz::engine::editor::AssetGraphSnapshot& snapshot,
        wz::asset::AssetGraphDraftNodeId id)
    {
        const auto it = std::ranges::find_if(
            snapshot.nodes,
            [id](const wz::engine::editor::AssetGraphSnapshotNode& node)
            {
                return node.id == id;
            });
        return it == snapshot.nodes.end() ? nullptr : &*it;
    }

    std::unique_ptr<wz::engine::editor::AssetGraphEditorSession>
    open_session(const fs::path& project_root, wz::asset::CompilerRegistry& registry)
    {
        auto opened = wz::engine::editor::open_asset_graph_editor_session(
            wz::engine::project::ProjectManifestLoadDesc{
                .project_root = project_root.string(),
            },
            registry);
        EXPECT_TRUE(opened.ok) << opened.error;
        return std::move(opened.session);
    }
}

TEST(AssetGraphEditorSession, SnapshotContainsDeclaredPortsAndEdgeIds)
{
    TempProjectRoot temp;
    write_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);

    const wz::engine::editor::AssetGraphSnapshot snapshot =
        session->snapshot();

    ASSERT_EQ(snapshot.nodes.size(), 4u);
    const auto shader = std::ranges::find_if(
        snapshot.nodes,
        [](const wz::engine::editor::AssetGraphSnapshotNode& node)
        {
            return node.id == 2u;
        });
    ASSERT_NE(shader, snapshot.nodes.end());
    ASSERT_EQ(shader->input_ports.size(), 1u);
    EXPECT_EQ(shader->input_ports[0].index, 0u);
    EXPECT_EQ(shader->input_ports[0].name, "source_file");
    EXPECT_EQ(shader->input_ports[0].type, wz::asset::AssetType::ShaderSource);
    EXPECT_EQ(shader->input_ports[0].type_name, "Shader source");

    ASSERT_FALSE(snapshot.edges.empty());
    const auto& edge = snapshot.edges[0];
    EXPECT_EQ(edge.id, 1u);
    EXPECT_EQ(edge.from, 1u);
    EXPECT_EQ(edge.to, 2u);
    EXPECT_EQ(edge.to_input_port, 0u);
}

TEST(AssetGraphEditorSession, UnnamedNodeDisplayNameUsesSchemaLabel)
{
    // With no authored "name" param, a node's display name -- the card title
    // and inspector header -- is the schema's human display label (the same
    // label the asset browser/catalog shows), not the bare output type
    // ("mesh"). Guards both the fallback and the registered clipmap label.
    wz::asset::AssetGraphDraft draft;
    wz::asset::AssetGraphDraftNode node;
    node.id = 1u;
    node.state = wz::asset::AssetGraphDraftNodeState::Existing;
    node.node.type = wz::engine::assets::kAssetTypeMesh;
    node.node.schema =
        wz::engine::assets::kProceduralClipmapLatticeMeshSchema;
    draft.nodes.push_back(node);

    const auto parsed = wz::json::parse_json_string("{}");
    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    const wz::engine::editor::AssetGraphSnapshot snapshot =
        wz::engine::editor::build_asset_graph_snapshot(
            *parsed.document.root, draft, nullptr);

    const auto* out = find_snapshot_node(snapshot, 1u);
    ASSERT_NE(out, nullptr);

    const std::string expected(
        wz::engine::assets::schema_display_name_view(
            wz::engine::assets::kProceduralClipmapLatticeMeshSchema));
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(out->display_name, expected);
    // The schema label, not the bare output type name ("Mesh").
    EXPECT_NE(out->display_name, out->type_name);
}

TEST(AssetGraphEditorSession, SnapshotMarksKeyedNodesReady)
{
    TempProjectRoot temp;
    write_valid_rewire_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);

    auto& draft = session->draft();
    wz::asset::find_asset_graph_draft_node(draft, 1u)->node.key =
        make_key(0x10u);
    wz::asset::find_asset_graph_draft_node(draft, 2u)->node.key =
        make_key(0x20u);
    wz::asset::find_asset_graph_draft_node(draft, 4u)->node.key =
        make_key(0x40u);
    wz::asset::rebuild_asset_graph_draft_indexes(draft);

    const wz::engine::editor::AssetGraphSnapshot snapshot =
        session->snapshot();

    ASSERT_NE(find_snapshot_node(snapshot, 1u), nullptr);
    EXPECT_EQ(find_snapshot_node(snapshot, 1u)->compile_status, "ready");
    ASSERT_NE(find_snapshot_node(snapshot, 2u), nullptr);
    EXPECT_EQ(find_snapshot_node(snapshot, 2u)->compile_status, "ready");
    ASSERT_NE(find_snapshot_node(snapshot, 4u), nullptr);
    EXPECT_EQ(find_snapshot_node(snapshot, 4u)->compile_status, "ready");
}

TEST(AssetGraphEditorSession, SnapshotMarksInvalidatedNodesChanged)
{
    TempProjectRoot temp;
    write_valid_rewire_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);

    auto& draft = session->draft();
    wz::asset::find_asset_graph_draft_node(draft, 1u)->node.key =
        make_key(0x10u);
    wz::asset::find_asset_graph_draft_node(draft, 2u)->node.key =
        make_key(0x20u);
    wz::asset::find_asset_graph_draft_node(draft, 4u)->node.key =
        make_key(0x40u);

    wz::asset::AssetNode material{};
    material.key = make_key(0x30u);
    material.type = wz::asset::AssetType::Material;
    material.schema = kSchema;
    ASSERT_NE(
        wz::asset::add_asset_graph_draft_node_with_id(
            draft,
            material,
            3u,
            wz::asset::AssetGraphDraftNodeState::Existing),
        wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);
    ASSERT_NE(
        wz::asset::connect_asset_graph_draft_nodes(draft, 2u, 3u, 0u),
        wz::asset::INVALID_ASSET_GRAPH_DRAFT_EDGE);
    draft.dirty = false;
    wz::asset::rebuild_asset_graph_draft_indexes(draft);

    const auto check = session->connect(4u, 2u, 0u);
    ASSERT_TRUE(check.compatible) << check.message;

    const wz::engine::editor::AssetGraphSnapshot snapshot =
        session->snapshot();

    ASSERT_NE(find_snapshot_node(snapshot, 1u), nullptr);
    EXPECT_EQ(find_snapshot_node(snapshot, 1u)->compile_status, "ready");
    ASSERT_NE(find_snapshot_node(snapshot, 4u), nullptr);
    EXPECT_EQ(find_snapshot_node(snapshot, 4u)->compile_status, "ready");
    ASSERT_NE(find_snapshot_node(snapshot, 2u), nullptr);
    EXPECT_EQ(find_snapshot_node(snapshot, 2u)->compile_status, "changed");
    ASSERT_NE(find_snapshot_node(snapshot, 3u), nullptr);
    EXPECT_EQ(find_snapshot_node(snapshot, 3u)->compile_status, "changed");
}

TEST(AssetGraphEditorSession, SnapshotMarksDiagnosticNodesError)
{
    TempProjectRoot temp;
    write_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);

    auto& draft = session->draft();
    ASSERT_NE(
        wz::asset::connect_asset_graph_draft_nodes(draft, 1u, 3u, 0u),
        wz::asset::INVALID_ASSET_GRAPH_DRAFT_EDGE);
    EXPECT_FALSE(wz::asset::validate_asset_graph_draft(
        draft,
        registry,
        true));

    const wz::engine::editor::AssetGraphSnapshot snapshot =
        session->snapshot();

    const auto* material = find_snapshot_node(snapshot, 3u);
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(material->compile_status, "error");
    ASSERT_FALSE(material->diagnostics.empty());

    const auto diagnostic = std::ranges::find_if(
        material->diagnostics,
        [](const wz::engine::editor::AssetGraphSnapshotDiagnostic& item)
        {
            return item.code
                == wz::asset::AssetGraphDraftValidationCode::TypeMismatch;
        });
    ASSERT_NE(diagnostic, material->diagnostics.end());
    EXPECT_EQ(
        diagnostic->severity,
        wz::asset::AssetGraphDraftValidationSeverity::Error);
    EXPECT_EQ(diagnostic->severity_name, "error");
    EXPECT_EQ(diagnostic->code_name, "TypeMismatch");
    EXPECT_FALSE(diagnostic->message.empty());
}

// Issue #212: a draft carrying a resolve-failure validation message (the kind
// WozzitsApp_v1::bind_asset_graph appends after a failed resolve) must surface
// on the failing node as an Error diagnostic with the detailed message, and
// mark the node's compile_status "error". This is the snapshot half of the
// round trip; the message text is produced by the bind path from the
// ResolveFailure detail.
TEST(AssetGraphEditorSession, SnapshotSurfacesResolveFailureMessage)
{
    wz::asset::AssetGraphDraft draft;
    wz::asset::AssetGraphDraftNode node;
    node.id = 7u;
    node.state = wz::asset::AssetGraphDraftNodeState::Existing;
    node.node.type = wz::engine::assets::kAssetTypeMesh;
    node.node.schema =
        wz::engine::assets::kProceduralClipmapLatticeMeshSchema;
    node.node.key = make_key(0x70u);
    draft.nodes.push_back(node);
    wz::asset::rebuild_asset_graph_draft_indexes(draft);

    const std::string message =
        "asset resolve failed: CompileFailed: mesh source is invalid";
    draft.validation_messages.push_back(
        wz::asset::AssetGraphDraftValidationMessage{
            .severity = wz::asset::AssetGraphDraftValidationSeverity::Error,
            .node = 7u,
            .message = message,
        });

    const auto parsed = wz::json::parse_json_string("{}");
    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    const wz::engine::editor::AssetGraphSnapshot snapshot =
        wz::engine::editor::build_asset_graph_snapshot(
            *parsed.document.root, draft, nullptr);

    const auto* out = find_snapshot_node(snapshot, 7u);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->compile_status, "error");

    ASSERT_FALSE(out->diagnostics.empty());
    const auto diagnostic = std::ranges::find_if(
        out->diagnostics,
        [&message](const wz::engine::editor::AssetGraphSnapshotDiagnostic& item)
        {
            return item.message == message;
        });
    ASSERT_NE(diagnostic, out->diagnostics.end());
    EXPECT_EQ(
        diagnostic->severity,
        wz::asset::AssetGraphDraftValidationSeverity::Error);
    EXPECT_EQ(diagnostic->severity_name, "error");
}

TEST(AssetGraphEditorSession, ConnectionCheckRejectsTypeMismatchWithoutMutation)
{
    TempProjectRoot temp;
    write_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);

    const auto check = session->can_connect(1u, 3u, 0u);

    EXPECT_FALSE(check.compatible);
    EXPECT_EQ(
        check.status,
        wz::engine::editor::AssetGraphConnectionStatus::TypeMismatch);
    EXPECT_EQ(check.from_type, wz::asset::AssetType::ShaderSource);
    EXPECT_EQ(check.to_type, wz::asset::AssetType::Shader);
    EXPECT_FALSE(session->dirty());
    EXPECT_EQ(session->draft().edges.size(), 1u);
}

TEST(AssetGraphEditorSession, ConnectReplacesExistingSingleInputEdge)
{
    TempProjectRoot temp;
    write_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);

    const auto check = session->connect(4u, 2u, 0u);

    EXPECT_TRUE(check.compatible) << check.message;
    EXPECT_TRUE(check.replaces_existing);
    ASSERT_EQ(session->draft().edges.size(), 1u);
    EXPECT_EQ(session->draft().edges[0].from, 4u);
    EXPECT_EQ(session->draft().edges[0].to, 2u);
    EXPECT_TRUE(session->dirty());
}

TEST(AssetGraphEditorSession, RewireInvalidatesConsumerKeyForCompile)
{
    TempProjectRoot temp;
    write_valid_rewire_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);

    auto& draft = session->draft();
    ASSERT_NE(
        wz::asset::find_asset_graph_draft_node(draft, 1u),
        nullptr);
    ASSERT_NE(
        wz::asset::find_asset_graph_draft_node(draft, 2u),
        nullptr);
    ASSERT_NE(
        wz::asset::find_asset_graph_draft_node(draft, 4u),
        nullptr);

    wz::asset::find_asset_graph_draft_node(draft, 1u)->node.key =
        make_key(0x10u);
    wz::asset::find_asset_graph_draft_node(draft, 2u)->node.key =
        make_key(0x20u);
    wz::asset::find_asset_graph_draft_node(draft, 4u)->node.key =
        make_key(0x40u);
    const wz::asset::AssetKey old_consumer_key =
        wz::asset::find_asset_graph_draft_node(draft, 2u)->node.key;
    draft.dirty = false;
    wz::asset::rebuild_asset_graph_draft_indexes(draft);

    const auto check = session->connect(4u, 2u, 0u);

    EXPECT_TRUE(check.compatible) << check.message;
    const wz::asset::AssetGraphDraftNode* consumer =
        wz::asset::find_asset_graph_draft_node(draft, 2u);
    ASSERT_NE(consumer, nullptr);
    EXPECT_EQ(
        consumer->state,
        wz::asset::AssetGraphDraftNodeState::Modified);
    EXPECT_EQ(consumer->node.key, wz::asset::AssetKey{});

    ASSERT_TRUE(wz::asset::materialize_asset_graph_draft_keys(draft, registry));
    consumer = wz::asset::find_asset_graph_draft_node(draft, 2u);
    ASSERT_NE(consumer, nullptr);
    EXPECT_NE(consumer->node.key, wz::asset::AssetKey{});
    EXPECT_NE(consumer->node.key, old_consumer_key);
}

TEST(AssetGraphEditorSession, DisconnectEdgeRemovesByDraftEdgeId)
{
    TempProjectRoot temp;
    write_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);
    ASSERT_EQ(session->draft().edges.size(), 1u);

    EXPECT_TRUE(session->disconnect_edge(1u));

    EXPECT_TRUE(session->draft().edges.empty());
    EXPECT_TRUE(session->dirty());
    EXPECT_FALSE(session->disconnect_edge(1u));
}

TEST(AssetGraphEditorSession, CanReconnectAfterDisconnectingEdge)
{
    TempProjectRoot temp;
    write_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_TRUE(session);
    ASSERT_EQ(session->draft().edges.size(), 1u);

    EXPECT_TRUE(session->disconnect_edge(1u));

    const auto check = session->connect(1u, 2u, 0u);

    EXPECT_TRUE(check.compatible) << check.message;
    EXPECT_FALSE(check.replaces_existing);
    ASSERT_EQ(session->draft().edges.size(), 1u);
    EXPECT_EQ(session->draft().edges[0].from, 1u);
    EXPECT_EQ(session->draft().edges[0].to, 2u);
    EXPECT_EQ(session->draft().edges[0].to_input_port, 0u);
}

namespace
{
    const wz::engine::editor::AssetGraphSnapshotParam* find_snapshot_param(
        const wz::engine::editor::AssetGraphSnapshotNode& node,
        std::string_view name)
    {
        const auto it = std::ranges::find_if(
            node.params,
            [name](const wz::engine::editor::AssetGraphSnapshotParam& param)
            {
                return param.name == name;
            });
        return it == node.params.end() ? nullptr : &*it;
    }
}

// #218 Phase 3 regression: editing a newly-added Placement node's params through
// the editor session must PERSIST and MERGE — a second edit must not wipe the
// first back to defaults. This pins the engine half of the "placement params
// reset automatically" report: a param-only asset type (no input ports) gets a
// ParamBlock meta on add, set_node_param merges into it, and the live snapshot
// (built from the session draft) reflects the authored values, not the schema
// defaults. If this passes, any remaining reset is in the editor client / a
// stale wozzits_abi.dll, not the engine.
TEST(AssetGraphEditorSession, PlacementNodeParamEditsPersistAndMerge)
{
    TempProjectRoot temp;
    write_project(temp.root);

    wz::Logger logger;
    wz::engine::assets::PlacementTable placement_table;
    auto registry = make_registry();
    wz::engine::assets::internal::register_placement_compilers(
        registry, logger, placement_table);

    auto session = open_session(temp.root, registry);
    ASSERT_NE(session, nullptr);

    const wz::asset::AssetGraphDraftNodeId node_id = session->add_node(
        wz::engine::assets::kPlacementSchema,
        wz::engine::assets::kAssetTypePlacement);
    ASSERT_NE(node_id, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);

    // First edit: one float param.
    ASSERT_TRUE(session->set_node_param(node_id, "extent_x", "4096"));
    {
        const auto snapshot = session->snapshot();
        const auto* node = find_snapshot_node(snapshot, node_id);
        ASSERT_NE(node, nullptr);
        const auto* extent_x = find_snapshot_param(*node, "extent_x");
        const auto* extent_y = find_snapshot_param(*node, "extent_y");
        ASSERT_NE(extent_x, nullptr);
        ASSERT_NE(extent_y, nullptr);
        EXPECT_DOUBLE_EQ(std::stod(extent_x->value), 4096.0); // edit persisted
        EXPECT_DOUBLE_EQ(std::stod(extent_y->value), 1.0);    // sibling default,
                                                              // NOT reset
    }

    // Second edit: a different param must MERGE, leaving the first intact.
    ASSERT_TRUE(session->set_node_param(node_id, "extent_y", "300"));
    {
        const auto snapshot = session->snapshot();
        const auto* node = find_snapshot_node(snapshot, node_id);
        ASSERT_NE(node, nullptr);
        EXPECT_DOUBLE_EQ(
            std::stod(find_snapshot_param(*node, "extent_x")->value), 4096.0);
        EXPECT_DOUBLE_EQ(
            std::stod(find_snapshot_param(*node, "extent_y")->value), 300.0);
    }
}

// The "Add Inochi shared assets" editor action (item 6): the session authors the
// shared puppet-program subgraph and stages the embedded shaders into the
// project. Device-free structural coverage (full resolve+render is covered by
// render/rhi_puppet_authoring_render_tests); make_registry() lacks the puppet
// compilers, so the nodes are bare but still authored and the shaders staged.
TEST(AssetGraphEditorSession, AddInochiSharedAssetsAuthorsSubgraphAndStagesShaders)
{
    TempProjectRoot temp;
    write_project(temp.root);
    auto registry = make_registry();
    auto session = open_session(temp.root, registry);
    ASSERT_NE(session, nullptr);

    const size_t nodes_before = session->draft().nodes.size();
    const size_t edges_before = session->draft().edges.size();

    const wz::asset::AssetGraphDraftNodeId program =
        session->add_inochi_shared_assets();
    ASSERT_NE(program, wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE);

    // 4 shader nodes (VS source/shader, PS source/shader) + one program per
    // blend variant (#274), sharing that pair: 2 source->shader edges + 2
    // shader->program edges per variant.
    const size_t variants = wz::engine::assets::kPuppetProgramBlendCount;
    EXPECT_EQ(session->draft().nodes.size(), nodes_before + 4u + variants);
    EXPECT_EQ(session->draft().edges.size(), edges_before + 2u + 2u * variants);

    // The embedded puppet shaders were staged into <project>/shaders/puppet/.
    EXPECT_TRUE(fs::exists(temp.root / "shaders" / "puppet" / "puppet_vs.hlsl"));
    EXPECT_TRUE(fs::exists(temp.root / "shaders" / "puppet" / "puppet_ps.hlsl"));

    // Idempotent: a second call returns the same program node, adds nothing.
    const wz::asset::AssetGraphDraftNodeId program_again =
        session->add_inochi_shared_assets();
    EXPECT_EQ(program_again, program);
    EXPECT_EQ(session->draft().nodes.size(), nodes_before + 4u + variants);
    EXPECT_EQ(session->draft().edges.size(), edges_before + 2u + 2u * variants);
}
