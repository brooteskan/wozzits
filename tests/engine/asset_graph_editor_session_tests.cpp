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
