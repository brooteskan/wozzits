#include <gtest/gtest.h>

#include <engine/editor/asset_graph_editor_session.h>

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
  "schema": "wozzits.asset_graph.v2",
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
  "schema": "wozzits.asset_graph.v2",
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
