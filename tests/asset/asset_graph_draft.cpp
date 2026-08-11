#include <gtest/gtest.h>

#include <asset/draft.h>
#include <asset/system.h>
#include <engine/assets/engine_asset_key_factory.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/key_factories/hlsl_shader.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <graph/static_dag.h>
#include <gpu/shader_types.h>

#include <any>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>

namespace
{
    using namespace wz::asset;
    namespace fs = std::filesystem;

    SchemaID schema(uint64_t value)
    {
        return SchemaID{ value };
    }

    AssetKey make_key(uint64_t content_lo)
    {
        AssetKey key{};
        key.content_hash = { content_lo, 0xabcdu };
        return key;
    }

    AssetNode make_node(
        AssetType type,
        SchemaID schema_id,
        uint64_t content_lo = 0)
    {
        AssetNode node{};
        node.type = type;
        node.schema = schema_id;
        node.stage = AssetStage::Source;
        if (content_lo != 0) {
            node.key = make_key(content_lo);
        }
        return node;
    }

    const AssetGraphDraftRegistration* find_registration(
        const std::vector<AssetGraphDraftRegistration>& registrations,
        AssetGraphDraftNodeId id)
    {
        for (const AssetGraphDraftRegistration& registration : registrations) {
            if (registration.draft_node == id) {
                return &registration;
            }
        }
        return nullptr;
    }

    bool has_validation_code(
        const AssetGraphDraft& draft,
        AssetGraphDraftValidationCode code)
    {
        return std::any_of(
            draft.validation_messages.begin(),
            draft.validation_messages.end(),
            [code](const AssetGraphDraftValidationMessage& message)
            {
                return message.code == code;
            });
    }

    CompilerRegistry make_draft_test_registry()
    {
        CompilerRegistry registry{};
        registry.register_compiler(AssetCompiler{
            .input_schema = schema(10),
            .output_type = AssetType::Mesh,
        });
        registry.register_compiler(AssetCompiler{
            .input_schema = schema(11),
            .output_type = AssetType::Texture,
        });
        registry.register_compiler(AssetCompiler{
            .input_schema = schema(12),
            .output_type = AssetType::Shader,
        });
        registry.register_compiler(AssetCompiler{
            .input_schema = schema(20),
            .output_type = AssetType::Material,
            .input_ports = {
                InputPort{ "mesh", AssetType::Mesh },
                InputPort{
                    "texture",
                    AssetType::Texture,
                    InputPortRequirement::Optional,
                },
                InputPort{
                    "shaders",
                    AssetType::Shader,
                    InputPortRequirement::Optional,
                    InputPortArity::Many,
                },
            },
        });
        return registry;
    }

    uint32_t validation_count(
        const AssetGraphDraft& draft,
        AssetGraphDraftValidationCode code)
    {
        return static_cast<uint32_t>(
            std::count_if(
                draft.validation_messages.begin(),
                draft.validation_messages.end(),
                [code](const AssetGraphDraftValidationMessage& message)
                {
                    return message.code == code;
                }));
    }

    struct TypedDesc
    {
        int value = 0;
    };

    struct TempDraftFile
    {
        fs::path root;
        fs::path path;

        explicit TempDraftFile(std::string_view filename)
        {
            const auto unique =
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count();
            root = fs::temp_directory_path()
                / ("wozzits_asset_graph_draft_" + std::to_string(unique));
            path = root / std::string(filename);
            fs::create_directories(path.parent_path());
        }

        ~TempDraftFile()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

    void write_text(const fs::path& path, std::string_view text)
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "failed to open " << path.string();
        out << text;
    }

    void register_test_file_carrier(
        CompilerRegistry& registry,
        SchemaID schema_id,
        AssetType type)
    {
        registry.register_compiler(AssetCompiler{
            .input_schema = schema_id,
            .output_type = type,
            .parameters = {
                ParamDecl{
                    .name = "source_path",
                    .type = ParamType::FilePath,
                    .label = "Path",
                },
            },
        });
    }

    CompilerRegistry make_engine_draft_key_test_registry()
    {
        CompilerRegistry registry{};
        register_test_file_carrier(
            registry,
            wz::engine::assets::kHLSLFileSchema,
            AssetType::ShaderSource);
        registry.register_compiler(AssetCompiler{
            .input_schema = wz::engine::assets::kHLSLShaderSchema,
            .output_type = AssetType::Shader,
            .input_ports = {
                InputPort{ "source_file", AssetType::ShaderSource },
            },
            .parameters = {
                ParamDecl{
                    .name = "stage",
                    .type = ParamType::Enum,
                    .label = "Stage",
                    .default_num = 0.0,
                },
                ParamDecl{
                    .name = "entry",
                    .type = ParamType::String,
                    .label = "Entry",
                    .default_str = "main",
                },
                ParamDecl{
                    .name = "target",
                    .type = ParamType::String,
                    .label = "Target",
                    .default_str = "vs_5_1",
                },
            },
        });
        return registry;
    }
}

TEST(AssetGraphDraft, ValidationDisplayNamesMatchEditorSnapshotVocabulary)
{
    EXPECT_EQ(
        asset_graph_draft_validation_severity_name(
            AssetGraphDraftValidationSeverity::Info),
        "info");
    EXPECT_EQ(
        asset_graph_draft_validation_severity_name(
            AssetGraphDraftValidationSeverity::Warning),
        "warning");
    EXPECT_EQ(
        asset_graph_draft_validation_severity_name(
            AssetGraphDraftValidationSeverity::Error),
        "error");

    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::None),
        "None");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::UnknownCompiler),
        "UnknownCompiler");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::MissingRequiredInput),
        "MissingRequiredInput");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::InvalidInputPort),
        "InvalidInputPort");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::DuplicateInputPort),
        "DuplicateInputPort");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::TypeMismatch),
        "TypeMismatch");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::MissingNode),
        "MissingNode");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::MissingAssetKey),
        "MissingAssetKey");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::SelfDependency),
        "SelfDependency");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::Cycle),
        "Cycle");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::DuplicateAssetKey),
        "DuplicateAssetKey");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::TypedMetaConflict),
        "TypedMetaConflict");
    EXPECT_EQ(
        asset_graph_draft_validation_code_name(
            AssetGraphDraftValidationCode::AmbiguousManyPortBoundary),
        "AmbiguousManyPortBoundary");
}

TEST(AssetGraphDraft, InputPortMetadataDefaultsToRequiredSingle)
{
    const InputPort required{ "src", AssetType::Mesh };

    EXPECT_TRUE(input_port_required(required));
    EXPECT_FALSE(input_port_optional(required));
    EXPECT_FALSE(input_port_many(required));

    const InputPort optional{
        "maybe_texture",
        AssetType::Texture,
        InputPortRequirement::Optional,
    };

    EXPECT_FALSE(input_port_required(optional));
    EXPECT_TRUE(input_port_optional(optional));
    EXPECT_FALSE(input_port_many(optional));

    const InputPort many{
        "layers",
        AssetType::Shader,
        InputPortRequirement::Optional,
        InputPortArity::Many,
    };

    EXPECT_TRUE(input_port_optional(many));
    EXPECT_TRUE(input_port_many(many));
}

TEST(AssetGraphDraft, AddNodeAssignsStableIdsAndIndexesNonEmptyKeys)
{
    AssetGraphDraft draft{};

    const AssetGraphDraftNodeId provisional =
        add_asset_graph_draft_node(
            draft,
            make_node(AssetType::Mesh, schema(1)));
    const AssetGraphDraftNodeId keyed =
        add_asset_graph_draft_node(
            draft,
            make_node(AssetType::Texture, schema(2), 0x20),
            AssetGraphDraftNodeState::Existing);

    EXPECT_EQ(provisional, 1u);
    EXPECT_EQ(keyed, 2u);
    EXPECT_EQ(draft.next_node_id, 3u);
    EXPECT_TRUE(draft.dirty);

    ASSERT_NE(find_asset_graph_draft_node(draft, provisional), nullptr);
    ASSERT_NE(find_asset_graph_draft_node(draft, keyed), nullptr);
    EXPECT_EQ(draft.node_by_key.find(AssetKey{}), draft.node_by_key.end());
    ASSERT_NE(draft.node_by_key.find(make_key(0x20)), draft.node_by_key.end());
    EXPECT_EQ(draft.node_by_key.at(make_key(0x20)), keyed);

    ASSERT_TRUE(remove_asset_graph_draft_node(draft, keyed));
    const AssetGraphDraftNode* deleted =
        find_asset_graph_draft_node(draft, keyed);
    ASSERT_NE(deleted, nullptr);
    EXPECT_EQ(deleted->state, AssetGraphDraftNodeState::Deleted);
    EXPECT_EQ(draft.node_by_key.find(make_key(0x20)), draft.node_by_key.end());
}

TEST(AssetGraphDraft, AddNodeWithIdPreservesExplicitIdAndAdvancesNextId)
{
    AssetGraphDraft draft{};

    const AssetGraphDraftNodeId explicit_id =
        add_asset_graph_draft_node_with_id(
            draft,
            make_node(AssetType::Mesh, schema(10), 0x10),
            42u,
            AssetGraphDraftNodeState::Existing);

    EXPECT_EQ(explicit_id, 42u);
    EXPECT_EQ(draft.next_node_id, 43u);
    ASSERT_NE(find_asset_graph_draft_node(draft, 42u), nullptr);
    EXPECT_EQ(draft.node_by_key.at(make_key(0x10)), 42u);

    const AssetGraphDraftNodeId next_id =
        add_asset_graph_draft_node(
            draft,
            make_node(AssetType::Texture, schema(11), 0x11));
    EXPECT_EQ(next_id, 43u);
    EXPECT_EQ(draft.next_node_id, 44u);
}

TEST(AssetGraphDraft, AddNodeWithIdRejectsInvalidDuplicateAndDeletedIds)
{
    AssetGraphDraft draft{};

    ASSERT_EQ(
        add_asset_graph_draft_node_with_id(
            draft,
            make_node(AssetType::Mesh, schema(10)),
            5u),
        5u);

    EXPECT_EQ(
        add_asset_graph_draft_node_with_id(
            draft,
            make_node(AssetType::Texture, schema(11)),
            5u),
        INVALID_ASSET_GRAPH_DRAFT_NODE);
    EXPECT_EQ(
        add_asset_graph_draft_node_with_id(
            draft,
            make_node(AssetType::Texture, schema(11)),
            INVALID_ASSET_GRAPH_DRAFT_NODE),
        INVALID_ASSET_GRAPH_DRAFT_NODE);
    EXPECT_EQ(
        add_asset_graph_draft_node_with_id(
            draft,
            make_node(AssetType::Texture, schema(11)),
            6u,
            AssetGraphDraftNodeState::Deleted),
        INVALID_ASSET_GRAPH_DRAFT_NODE);

    EXPECT_EQ(draft.nodes.size(), 1u);
    EXPECT_EQ(draft.next_node_id, 6u);
}

TEST(AssetGraphDraft, RemovedExplicitIdIsNotReused)
{
    AssetGraphDraft draft{};

    ASSERT_EQ(
        add_asset_graph_draft_node_with_id(
            draft,
            make_node(AssetType::Mesh, schema(10)),
            7u),
        7u);
    ASSERT_TRUE(remove_asset_graph_draft_node(draft, 7u));

    const AssetGraphDraftNodeId next_id =
        add_asset_graph_draft_node(
            draft,
            make_node(AssetType::Texture, schema(11)));

    EXPECT_EQ(next_id, 8u);
    EXPECT_EQ(draft.next_node_id, 9u);
    ASSERT_NE(find_asset_graph_draft_node(draft, 7u), nullptr);
    EXPECT_EQ(
        find_asset_graph_draft_node(draft, 7u)->state,
        AssetGraphDraftNodeState::Deleted);
}

TEST(AssetGraphDraft, SetParamsInvalidatesExistingKeyAndMarksModified)
{
    AssetGraphDraft draft{};
    const AssetGraphDraftNodeId node_id =
        add_asset_graph_draft_node(
            draft,
            make_node(AssetType::ShaderSource, schema(7), 0x77),
            AssetGraphDraftNodeState::Existing);

    ParamBlock params{};
    params.values["source_path"] = std::string("assets/shaders/test.hlsl");

    ASSERT_TRUE(set_asset_graph_draft_node_params(
        draft,
        node_id,
        std::move(params)));

    const AssetGraphDraftNode* node =
        find_asset_graph_draft_node(draft, node_id);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->state, AssetGraphDraftNodeState::Modified);
    EXPECT_EQ(node->node.key, AssetKey{});
    EXPECT_EQ(draft.node_by_key.find(make_key(0x77)), draft.node_by_key.end());

    const auto* stored =
        std::any_cast<ParamBlock>(&node->node.meta);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(
        stored->get<std::string>("source_path", {}),
        "assets/shaders/test.hlsl");
}

TEST(AssetGraphDraft, SetSchemaOnCreatedNodeInvalidatesKeyButKeepsCreatedState)
{
    AssetGraphDraft draft{};
    const AssetGraphDraftNodeId node_id =
        add_asset_graph_draft_node(
            draft,
            make_node(AssetType::Shader, schema(1), 0x10));

    ASSERT_TRUE(set_asset_graph_draft_node_type_schema(
        draft,
        node_id,
        AssetType::Material,
        schema(2)));

    const AssetGraphDraftNode* node =
        find_asset_graph_draft_node(draft, node_id);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->state, AssetGraphDraftNodeState::Created);
    EXPECT_EQ(node->node.type, AssetType::Material);
    EXPECT_EQ(node->node.schema, schema(2));
    EXPECT_EQ(node->node.key, AssetKey{});
}

TEST(AssetGraphDraft, RemoveNodeDeletesAllIncidentEdgesOnly)
{
    AssetGraphDraft draft{};
    const auto a = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(1), 0x1));
    const auto b = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(2), 0x2));
    const auto c = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Shader, schema(3), 0x3));
    const auto d = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(4), 0x4));

    const auto keep = connect_asset_graph_draft_nodes(draft, a, d, 0);
    ASSERT_NE(connect_asset_graph_draft_nodes(draft, a, b, 0),
              INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(connect_asset_graph_draft_nodes(draft, b, c, 0),
              INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(remove_asset_graph_draft_node(draft, b));

    ASSERT_EQ(draft.edges.size(), 1u);
    EXPECT_EQ(draft.edges.front().id, keep);
    EXPECT_EQ(draft.edges.front().from, a);
    EXPECT_EQ(draft.edges.front().to, d);
}

TEST(AssetGraphDraft, ConnectAndDisconnectOperateOnEdgesAndInputPorts)
{
    AssetGraphDraft draft{};
    const auto a = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(1), 0x1));
    const auto b = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(2), 0x2));
    const auto c = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(3), 0x3));

    EXPECT_EQ(
        connect_asset_graph_draft_nodes(
            draft,
            a,
            INVALID_ASSET_GRAPH_DRAFT_NODE,
            0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    const auto edge_a = connect_asset_graph_draft_nodes(draft, a, c, 0);
    const auto edge_b = connect_asset_graph_draft_nodes(draft, b, c, 1);
    ASSERT_NE(edge_a, INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(edge_b, INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(disconnect_asset_graph_draft_edge(draft, edge_a));
    EXPECT_FALSE(disconnect_asset_graph_draft_edge(draft, edge_a));
    ASSERT_EQ(draft.edges.size(), 1u);
    EXPECT_EQ(draft.edges.front().id, edge_b);

    const auto edge_c = connect_asset_graph_draft_nodes(draft, a, c, 1);
    ASSERT_NE(edge_c, INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_EQ(draft.edges.size(), 2u);

    EXPECT_TRUE(disconnect_asset_graph_draft_input(draft, c, 1));
    EXPECT_TRUE(draft.edges.empty());
    EXPECT_FALSE(disconnect_asset_graph_draft_input(draft, c, 1));
}

TEST(AssetGraphDraft, SnapshotBuildsAcyclicGraphAndStoresHandles)
{
    AssetGraphDraft draft{};
    const auto source = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(1), 0x1));
    const auto middle = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(2), 0x2));
    const auto sink = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Shader, schema(3), 0x3));
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, source, middle, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, middle, sink, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(rebuild_asset_graph_draft_snapshot(draft));
    ASSERT_TRUE(draft.has_graph_snapshot());

    const AssetGraph* graph = draft.graph_snapshot();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(wz::core::graph::node_count(*graph), 3u);
    EXPECT_EQ(wz::core::graph::edge_count(*graph), 2u);

    const AssetGraphDraftNode* source_node =
        find_asset_graph_draft_node(draft, source);
    const AssetGraphDraftNode* middle_node =
        find_asset_graph_draft_node(draft, middle);
    const AssetGraphDraftNode* sink_node =
        find_asset_graph_draft_node(draft, sink);
    ASSERT_NE(source_node, nullptr);
    ASSERT_NE(middle_node, nullptr);
    ASSERT_NE(sink_node, nullptr);

    EXPECT_NE(source_node->graph_node, INVALID_ASSET_NODE);
    EXPECT_NE(middle_node->graph_node, INVALID_ASSET_NODE);
    EXPECT_NE(sink_node->graph_node, INVALID_ASSET_NODE);

    auto prereqs =
        wz::core::graph::parents(*graph, sink_node->graph_node);
    ASSERT_EQ(prereqs.size(), 1u);
    EXPECT_EQ(prereqs[0], middle_node->graph_node);
}

TEST(AssetGraphDraft, SnapshotFailsAndClearsStorageOnCycle)
{
    AssetGraphDraft draft{};
    const auto a = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(1), 0x1));
    const auto b = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(2), 0x2));

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, a, b, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_TRUE(rebuild_asset_graph_draft_snapshot(draft));
    ASSERT_TRUE(draft.has_graph_snapshot());

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, b, a, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_FALSE(rebuild_asset_graph_draft_snapshot(draft));
    EXPECT_FALSE(draft.has_graph_snapshot());
}

TEST(AssetGraphDraft, ToRegistrationsPreservesPortIndexedHoles)
{
    CompilerRegistry registry{};
    registry.register_compiler(AssetCompiler{
        .input_schema = schema(100),
        .output_type = AssetType::Material,
        .input_ports = {
            InputPort{ "mesh", AssetType::Mesh },
            InputPort{
                "texture",
                AssetType::Texture,
                InputPortRequirement::Optional,
            },
            InputPort{ "shader", AssetType::Shader },
        },
    });

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(1), 0x10));
    const auto shader = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Shader, schema(2), 0x30));
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(100), 0x99));

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, shader, material, 2),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    const std::vector<AssetGraphDraftRegistration> registrations =
        asset_graph_draft_to_registrations(draft, &registry);

    const AssetGraphDraftRegistration* registration =
        find_registration(registrations, material);
    ASSERT_NE(registration, nullptr);

    ASSERT_EQ(registration->dep_keys.size(), 3u);
    EXPECT_EQ(registration->dep_keys[0], make_key(0x10));
    EXPECT_EQ(registration->dep_keys[1], AssetKey{});
    EXPECT_EQ(registration->dep_keys[2], make_key(0x30));
}

TEST(AssetGraphDraft, ToRegistrationsFlattensManyPortInEdgeOrder)
{
    CompilerRegistry registry{};
    registry.register_compiler(AssetCompiler{
        .input_schema = schema(200),
        .output_type = AssetType::Material,
        .input_ports = {
            InputPort{
                "layers",
                AssetType::Texture,
                InputPortRequirement::Optional,
                InputPortArity::Many,
            },
        },
    });

    AssetGraphDraft draft{};
    const auto first = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(1), 0x1));
    const auto second = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(1), 0x2));
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(200), 0x3));

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, first, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, second, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    const std::vector<AssetGraphDraftRegistration> registrations =
        asset_graph_draft_to_registrations(draft, &registry);

    const AssetGraphDraftRegistration* registration =
        find_registration(registrations, material);
    ASSERT_NE(registration, nullptr);

    ASSERT_EQ(registration->dep_keys.size(), 2u);
    EXPECT_EQ(registration->dep_keys[0], make_key(0x1));
    EXPECT_EQ(registration->dep_keys[1], make_key(0x2));
}

TEST(AssetGraphDraft, ToRegistrationsWithoutRegistrySizesFromConnectedPorts)
{
    AssetGraphDraft draft{};
    const auto provider = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(1), 0x1));
    const auto consumer = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(2), 0x2));

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, provider, consumer, 3),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    const std::vector<AssetGraphDraftRegistration> registrations =
        asset_graph_draft_to_registrations(draft);

    const AssetGraphDraftRegistration* registration =
        find_registration(registrations, consumer);
    ASSERT_NE(registration, nullptr);

    ASSERT_EQ(registration->dep_keys.size(), 4u);
    EXPECT_EQ(registration->dep_keys[0], AssetKey{});
    EXPECT_EQ(registration->dep_keys[1], AssetKey{});
    EXPECT_EQ(registration->dep_keys[2], AssetKey{});
    EXPECT_EQ(registration->dep_keys[3], make_key(0x1));
}

TEST(AssetGraphDraft, ValidateReportsAllAvailableProblems)
{
    CompilerRegistry registry = make_draft_test_registry();

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x1),
        AssetGraphDraftNodeState::Existing);
    const auto texture = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(11), 0x2),
        AssetGraphDraftNodeState::Existing);
    const auto consumer = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20), 0x3),
        AssetGraphDraftNodeState::Existing);
    const auto missing_required = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20), 0x4),
        AssetGraphDraftNodeState::Existing);
    const auto missing_key = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Shader, schema(12)),
        AssetGraphDraftNodeState::Existing);
    const auto unknown = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(999), 0x5),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, texture, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, consumer, 8),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, mesh, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, texture, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, texture, mesh, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));

    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::UnknownCompiler));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::MissingRequiredInput));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::InvalidInputPort));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::DuplicateInputPort));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::TypeMismatch));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::MissingAssetKey));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::SelfDependency));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::Cycle));

    (void)missing_required;
    (void)missing_key;
    (void)unknown;
}

TEST(AssetGraphDraft, ValidateAllowsUnwiredOptionalInputs)
{
    CompilerRegistry registry = make_draft_test_registry();

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10)));
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20)));

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_TRUE(validate_asset_graph_draft(draft, registry));
    EXPECT_TRUE(draft.validation_messages.empty());
}

TEST(AssetGraphDraft, MaterializeKeysAllowsCreatedNodesToCommit)
{
    CompilerRegistry registry = make_draft_test_registry();

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10)));
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20)));

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));

    const AssetGraphDraftNode* mesh_node =
        find_asset_graph_draft_node(draft, mesh);
    const AssetGraphDraftNode* material_node =
        find_asset_graph_draft_node(draft, material);
    ASSERT_NE(mesh_node, nullptr);
    ASSERT_NE(material_node, nullptr);
    EXPECT_NE(mesh_node->node.key, AssetKey{});
    EXPECT_NE(material_node->node.key, AssetKey{});
    EXPECT_NE(mesh_node->node.key, material_node->node.key);

    AssetSystem system(registry);
    for (const AssetGraphDraftRegistration& registration :
         asset_graph_draft_to_registrations(draft, &registry))
    {
        ASSERT_TRUE(system.register_asset(
            registration.node,
            registration.dep_keys));
    }
    EXPECT_TRUE(system.commit());
}

TEST(AssetGraphDraft, EngineHookFallsBackToGenericKey)
{
    CompilerRegistry registry = make_draft_test_registry();
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10)));

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetGraphDraftNode* node =
        find_asset_graph_draft_node(draft, mesh);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->node.key, make_asset_key(node->node, {}));
}

TEST(AssetGraphDraft, EngineHookMaterializesFileCarrierKey)
{
    CompilerRegistry registry = make_engine_draft_key_test_registry();
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    AssetNode source{};
    source.type = AssetType::ShaderSource;
    source.schema = wz::engine::assets::kHLSLFileSchema;
    source.stage = AssetStage::Source;
    source.residency = ResidencyIntent::CompileOnly;
    source.meta =
        wz::engine::assets::internal::FileSourceDesc{
            .full_path = "D:/project/assets/shaders/test.hlsl",
            .canonical_path = "assets/shaders/test.hlsl",
        };

    AssetGraphDraft draft{};
    const auto source_id = add_asset_graph_draft_node(draft, source);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetGraphDraftNode* node =
        find_asset_graph_draft_node(draft, source_id);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(
        node->node.key,
        wz::engine::assets::make_file_key(
            "assets/shaders/test.hlsl",
            wz::engine::assets::kHLSLFileSchema));
}

// A carrier's key must be reproducible regardless of the process working
// directory: a RELATIVE source is read against the resource_root, so which key
// SHAPE (content-folded vs path-only) the carrier gets — and thus its key, and
// every disk-cache filename derived from it — does not depend on where the
// runtime was launched. Before the fix exists()/read resolved against the CWD, so
// the same bundle produced different asset keys from different directories
// (identity-guard §5 finding on #295).
TEST(AssetGraphDraft, EngineHookCarrierKeyResolvesReadAgainstResourceRoot)
{
    namespace ea = wz::engine::assets;

    const fs::path root = fs::temp_directory_path()
        / ("wz_keyfactory_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "sub");
    const std::string bytes_text = "carrier-bytes-v1";
    {
        std::ofstream out(root / "sub" / "data.bin", std::ios::binary);
        out << bytes_text;
    }

    CompilerRegistry registry = make_engine_draft_key_test_registry();

    AssetNode node{};
    node.schema = ea::kRawFileSchema;
    node.stage = AssetStage::Source;
    ParamBlock params;
    params.values["source_path"] = std::string("sub/data.bin");
    node.meta = params;

    const std::span<const std::uint8_t> bytes{
        reinterpret_cast<const std::uint8_t*>(bytes_text.data()),
        bytes_text.size() };
    const AssetKey content_key =
        ea::make_file_content_key("sub/data.bin", bytes, ea::kRawFileSchema);
    const AssetKey path_only_key =
        ea::make_file_key("sub/data.bin", ea::kRawFileSchema);
    ASSERT_NE(content_key, path_only_key);

    // WITH the resource_root the bytes are found there (not in the CWD, where
    // "sub/data.bin" does not resolve), so the carrier is content-keyed and the
    // key is CWD-independent.
    const auto rooted =
        ea::make_engine_asset_key_factory(registry, root.string());
    const auto rooted_key = rooted(node, {});
    ASSERT_TRUE(rooted_key.has_value());
    EXPECT_EQ(*rooted_key, content_key);

    // WITHOUT a resource_root the read falls back to the CWD, where the relative
    // path is absent, so it degrades to the path-only key — the CWD-dependence
    // the rooted form removes (rootless is preserved for unit-test callers).
    const auto rootless = ea::make_engine_asset_key_factory(registry);
    const auto rootless_key = rootless(node, {});
    ASSERT_TRUE(rootless_key.has_value());
    EXPECT_EQ(*rootless_key, path_only_key);

    std::error_code ec;
    fs::remove_all(root, ec);
}

// Pins what an ABSENT key factory means now that the derived key is
// authoritative for every node (B1-C2): "re-derive the whole graph
// generically", which is a destructive request rather than a conservative one.
// That is why key_factory has no default argument -- before B1-C2 the two-arg
// call re-keyed nothing at all, so the default was safe then and is not now.
//
// The consequence is visible here rather than only in a comment: two file
// carriers naming DIFFERENT files collapse onto one key, because the generic
// derivation hashes a non-ParamBlock meta by TYPE and not by value (B1-H2).
// Measured over the shipped test_mesh_001 graph, the factory-less call re-keys
// 109 of 143 nodes and produces 8 such collisions.
//
// If this test ever fails because the keys stayed distinct, do NOT restore a
// "keep the stored key when there is no factory" fallback: that is the branch
// B1-C2 removed, and it is what let a param edit reach the compiler under the
// old key.
TEST(AssetGraphDraft, MaterializeWithoutAKeyFactoryRederivesGenerically)
{
    CompilerRegistry registry = make_engine_draft_key_test_registry();
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    auto make_source = [](const char* full, const char* canonical)
    {
        AssetNode source{};
        source.type = AssetType::ShaderSource;
        source.schema = wz::engine::assets::kHLSLFileSchema;
        source.stage = AssetStage::Source;
        source.residency = ResidencyIntent::CompileOnly;
        source.meta =
            wz::engine::assets::internal::FileSourceDesc{
                .full_path = full,
                .canonical_path = canonical,
            };
        return source;
    };

    AssetGraphDraft draft{};
    const auto first = add_asset_graph_draft_node(
        draft,
        make_source(
            "D:/project/assets/shaders/first.hlsl",
            "assets/shaders/first.hlsl"));
    const auto second = add_asset_graph_draft_node(
        draft,
        make_source(
            "D:/project/assets/shaders/second.hlsl",
            "assets/shaders/second.hlsl"));

    // CONTROL: with the factory that produced them, the two files are two
    // assets.
    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, key_factory));
    const AssetKey first_key =
        find_asset_graph_draft_node(draft, first)->node.key;
    const AssetKey second_key =
        find_asset_graph_draft_node(draft, second)->node.key;
    EXPECT_EQ(
        first_key,
        wz::engine::assets::make_file_key(
            "assets/shaders/first.hlsl",
            wz::engine::assets::kHLSLFileSchema));
    ASSERT_NE(first_key, second_key);

    // Both nodes are still Created, which forces rematerialization on its own
    // and would let this test pass without the derived-key comparison ever
    // running. Settle them the way a committed graph reloaded from JSON
    // arrives: Existing, carrying the key above. Now the ONLY thing that can
    // re-key them is derived-vs-stored.
    find_asset_graph_draft_node(draft, first)->state =
        AssetGraphDraftNodeState::Existing;
    find_asset_graph_draft_node(draft, second)->state =
        AssetGraphDraftNodeState::Existing;

    // Same settled draft, no factory: every stored key is now measured against
    // the generic derivation and loses.
    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));
    const AssetKey first_generic =
        find_asset_graph_draft_node(draft, first)->node.key;
    const AssetKey second_generic =
        find_asset_graph_draft_node(draft, second)->node.key;
    EXPECT_NE(first_generic, first_key)
        << "an absent key factory must re-derive, not preserve";
    EXPECT_EQ(first_generic, second_generic)
        << "the generic derivation hashes a typed meta by type, so two "
           "different files become one asset";
}

TEST(AssetGraphDraft, EngineHookMaterializesParamBlockFileCarrierKey)
{
    CompilerRegistry registry{};
    register_test_file_carrier(
        registry,
        wz::engine::assets::kJSONFileSchema,
        wz::engine::assets::kAssetTypeTextFile);
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    ParamBlock params{};
    params.values["source_path"] =
        std::string("\\assets\\data\\scene.json");

    AssetNode source{};
    source.type = wz::engine::assets::kAssetTypeTextFile;
    source.schema = wz::engine::assets::kJSONFileSchema;
    source.stage = AssetStage::Source;
    source.residency = ResidencyIntent::CompileOnly;
    source.meta = params;

    AssetGraphDraft draft{};
    const auto source_id = add_asset_graph_draft_node(draft, source);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetGraphDraftNode* node =
        find_asset_graph_draft_node(draft, source_id);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(
        node->node.key,
        wz::engine::assets::make_file_key(
            "assets/data/scene.json",
            wz::engine::assets::kJSONFileSchema));
}

TEST(AssetGraphDraft, EngineHookFileCarrierKeyTracksFileContent)
{
    CompilerRegistry registry = make_engine_draft_key_test_registry();
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    TempDraftFile temp("test.hlsl");
    write_text(temp.path, "float4 main() : SV_Target { return 1; }\n");

    AssetNode source{};
    source.type = AssetType::ShaderSource;
    source.schema = wz::engine::assets::kHLSLFileSchema;
    source.stage = AssetStage::Source;
    source.residency = ResidencyIntent::CompileOnly;
    source.meta =
        wz::engine::assets::internal::FileSourceDesc{
            .full_path = temp.path.string(),
            .canonical_path = "assets/shaders/test.hlsl",
        };

    AssetGraphDraft draft{};
    const auto source_id =
        add_asset_graph_draft_node(
            draft,
            source,
            AssetGraphDraftNodeState::Existing);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetKey first_key =
        find_asset_graph_draft_node(draft, source_id)->node.key;

    write_text(temp.path, "float4 main() : SV_Target { return 0; }\n");

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetGraphDraftNode* source_node =
        find_asset_graph_draft_node(draft, source_id);
    ASSERT_NE(source_node, nullptr);
    EXPECT_NE(source_node->node.key, first_key);

    const auto bytes = wz::fs::read_file(temp.path.string());
    ASSERT_TRUE(bytes);
    EXPECT_EQ(
        source_node->node.key,
        wz::engine::assets::make_file_content_key(
            "assets/shaders/test.hlsl",
            bytes.value,
            wz::engine::assets::kHLSLFileSchema));
}

TEST(AssetGraphDraft, EngineHookParamBlockFileCarrierKeyTracksFileContent)
{
    CompilerRegistry registry = make_engine_draft_key_test_registry();
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    TempDraftFile temp("test.hlsl");
    write_text(temp.path, "float4 main() : SV_Target { return 1; }\n");

    ParamBlock params{};
    params.values["source_path"] = temp.path.string();

    AssetNode source{};
    source.type = AssetType::ShaderSource;
    source.schema = wz::engine::assets::kHLSLFileSchema;
    source.stage = AssetStage::Source;
    source.residency = ResidencyIntent::CompileOnly;
    source.meta = params;

    AssetGraphDraft draft{};
    const auto source_id =
        add_asset_graph_draft_node(
            draft,
            source,
            AssetGraphDraftNodeState::Existing);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetKey first_key =
        find_asset_graph_draft_node(draft, source_id)->node.key;

    write_text(temp.path, "float4 main() : SV_Target { return 0; }\n");

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetGraphDraftNode* source_node =
        find_asset_graph_draft_node(draft, source_id);
    ASSERT_NE(source_node, nullptr);
    EXPECT_NE(source_node->node.key, first_key);

    const auto bytes = wz::fs::read_file(temp.path.string());
    ASSERT_TRUE(bytes);
    EXPECT_EQ(
        source_node->node.key,
        wz::engine::assets::make_file_content_key(
            wz::engine::assets::detail::canonical_asset_path(
                temp.path.string()),
            bytes.value,
            wz::engine::assets::kHLSLFileSchema));
}

TEST(AssetGraphDraft, EngineHookMaterializesHlslShaderKey)
{
    CompilerRegistry registry = make_engine_draft_key_test_registry();
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    AssetNode source{};
    source.type = AssetType::ShaderSource;
    source.schema = wz::engine::assets::kHLSLFileSchema;
    source.stage = AssetStage::Source;
    source.residency = ResidencyIntent::CompileOnly;
    source.meta =
        wz::engine::assets::internal::FileSourceDesc{
            .full_path = "D:/project/assets/shaders/test.hlsl",
            .canonical_path = "assets/shaders/test.hlsl",
        };

    AssetNode shader{};
    shader.type = AssetType::Shader;
    shader.schema = wz::engine::assets::kHLSLShaderSchema;
    shader.stage = AssetStage::Source;
    shader.meta = wz::gpu::HLSLCompileDesc{
        .stage = wz::gpu::ShaderStage::Pixel,
        .entry = "main_ps",
        .target = "ps_5_0",
    };

    AssetGraphDraft draft{};
    const auto source_id = add_asset_graph_draft_node(draft, source);
    const auto shader_id = add_asset_graph_draft_node(draft, shader);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, source_id, shader_id, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetGraphDraftNode* source_node =
        find_asset_graph_draft_node(draft, source_id);
    const AssetGraphDraftNode* shader_node =
        find_asset_graph_draft_node(draft, shader_id);
    ASSERT_NE(source_node, nullptr);
    ASSERT_NE(shader_node, nullptr);

    const AssetKey expected_source =
        wz::engine::assets::make_file_key(
            "assets/shaders/test.hlsl",
            wz::engine::assets::kHLSLFileSchema);
    const AssetKey expected_shader =
        wz::engine::assets::make_hlsl_shader_key(
            expected_source,
            static_cast<uint8_t>(wz::gpu::ShaderStage::Pixel),
            "main_ps",
            "ps_5_0");

    EXPECT_EQ(source_node->node.key, expected_source);
    EXPECT_EQ(shader_node->node.key, expected_shader);

    const auto registrations =
        asset_graph_draft_to_registrations(
            draft,
            &registry,
            key_factory);
    const auto* shader_registration =
        find_registration(registrations, shader_id);
    ASSERT_NE(shader_registration, nullptr);
    ASSERT_EQ(shader_registration->dep_keys.size(), 1u);
    EXPECT_EQ(shader_registration->dep_keys[0], expected_source);
    EXPECT_EQ(shader_registration->node.key, expected_shader);
}

TEST(AssetGraphDraft, EngineHookShaderKeyTracksSourceFileContentChanges)
{
    CompilerRegistry registry = make_engine_draft_key_test_registry();
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    TempDraftFile temp("test.hlsl");
    write_text(temp.path, "float4 main() : SV_Target { return 1; }\n");

    AssetNode source{};
    source.type = AssetType::ShaderSource;
    source.schema = wz::engine::assets::kHLSLFileSchema;
    source.stage = AssetStage::Source;
    source.residency = ResidencyIntent::CompileOnly;
    source.meta =
        wz::engine::assets::internal::FileSourceDesc{
            .full_path = temp.path.string(),
            .canonical_path = "assets/shaders/test.hlsl",
        };

    AssetNode shader{};
    shader.type = AssetType::Shader;
    shader.schema = wz::engine::assets::kHLSLShaderSchema;
    shader.stage = AssetStage::Source;
    shader.meta = wz::gpu::HLSLCompileDesc{
        .stage = wz::gpu::ShaderStage::Pixel,
        .entry = "main",
        .target = "ps_5_0",
    };

    AssetGraphDraft draft{};
    const auto source_id =
        add_asset_graph_draft_node(
            draft,
            source,
            AssetGraphDraftNodeState::Existing);
    const auto shader_id =
        add_asset_graph_draft_node(
            draft,
            shader,
            AssetGraphDraftNodeState::Existing);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, source_id, shader_id, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetKey first_source_key =
        find_asset_graph_draft_node(draft, source_id)->node.key;
    const AssetKey first_shader_key =
        find_asset_graph_draft_node(draft, shader_id)->node.key;

    write_text(temp.path, "float4 main() : SV_Target { return 0; }\n");

    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft,
        registry,
        key_factory));

    const AssetGraphDraftNode* source_node =
        find_asset_graph_draft_node(draft, source_id);
    const AssetGraphDraftNode* shader_node =
        find_asset_graph_draft_node(draft, shader_id);
    ASSERT_NE(source_node, nullptr);
    ASSERT_NE(shader_node, nullptr);
    EXPECT_NE(source_node->node.key, first_source_key);
    EXPECT_NE(shader_node->node.key, first_shader_key);
}

TEST(AssetGraphDraft, EngineHookPropagatesCanonicalDependencyKeys)
{
    CompilerRegistry registry = make_engine_draft_key_test_registry();
    const auto key_factory =
        wz::engine::assets::make_engine_asset_key_factory(registry);

    AssetNode source_a{};
    source_a.type = AssetType::ShaderSource;
    source_a.schema = wz::engine::assets::kHLSLFileSchema;
    source_a.stage = AssetStage::Source;
    source_a.residency = ResidencyIntent::CompileOnly;
    source_a.meta =
        wz::engine::assets::internal::FileSourceDesc{
            .full_path = "D:/project/assets/shaders/a.hlsl",
            .canonical_path = "assets/shaders/a.hlsl",
        };

    AssetNode source_b = source_a;
    source_b.meta =
        wz::engine::assets::internal::FileSourceDesc{
            .full_path = "D:/project/assets/shaders/b.hlsl",
            .canonical_path = "assets/shaders/b.hlsl",
        };

    AssetNode shader{};
    shader.type = AssetType::Shader;
    shader.schema = wz::engine::assets::kHLSLShaderSchema;
    shader.stage = AssetStage::Source;
    shader.meta = wz::gpu::HLSLCompileDesc{
        .stage = wz::gpu::ShaderStage::Vertex,
        .entry = "main",
        .target = "vs_5_0",
    };

    AssetGraphDraft draft_a{};
    const auto source_a_id = add_asset_graph_draft_node(draft_a, source_a);
    const auto shader_a_id = add_asset_graph_draft_node(draft_a, shader);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft_a, source_a_id, shader_a_id, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft_a,
        registry,
        key_factory));

    AssetGraphDraft draft_b{};
    const auto source_b_id = add_asset_graph_draft_node(draft_b, source_b);
    const auto shader_b_id = add_asset_graph_draft_node(draft_b, shader);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft_b, source_b_id, shader_b_id, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_TRUE(materialize_asset_graph_draft_keys(
        draft_b,
        registry,
        key_factory));

    const AssetKey shader_a_key =
        find_asset_graph_draft_node(draft_a, shader_a_id)->node.key;
    const AssetKey shader_b_key =
        find_asset_graph_draft_node(draft_b, shader_b_id)->node.key;
    EXPECT_NE(shader_a_key, shader_b_key);
}

TEST(AssetGraphDraft, MaterializeKeysCascadesProviderChangesToDependents)
{
    CompilerRegistry registry = make_draft_test_registry();

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20), 0x20),
        AssetGraphDraftNodeState::Existing);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    const AssetKey old_mesh_key =
        find_asset_graph_draft_node(draft, mesh)->node.key;
    const AssetKey old_material_key =
        find_asset_graph_draft_node(draft, material)->node.key;

    ParamBlock params{};
    params.values["source_path"] = std::string("assets/mesh_a.glb");
    ASSERT_TRUE(set_asset_graph_draft_node_params(
        draft,
        mesh,
        std::move(params)));

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));

    const AssetKey new_mesh_key =
        find_asset_graph_draft_node(draft, mesh)->node.key;
    const AssetKey new_material_key =
        find_asset_graph_draft_node(draft, material)->node.key;

    EXPECT_NE(new_mesh_key, AssetKey{});
    EXPECT_NE(new_material_key, AssetKey{});
    EXPECT_NE(new_mesh_key, old_mesh_key);
    EXPECT_NE(new_material_key, old_material_key);
}

// B1-C2. materialize_asset_graph_draft_keys used to re-verify a stored key
// against its derivation ONLY when a key factory claimed the node. Nodes keyed
// by the generic fallback -- 83 of the 143 in the shipped test_mesh_001 graph --
// were trusted unconditionally, so params that arrived WITHOUT a Created or
// Modified state kept the old key while the new value still reached the
// compiler. The same AssetKey then denoted different content, which no disk
// cache can detect: its path is derived from the key, and its stored_key check
// compares against that same unchanged key, so every integrity check passes and
// the stale artifact is served.
//
// This is the shape a hand-edited assets.graph.json produces: the loader marks
// every node Existing and carries its stored key verbatim.
TEST(AssetGraphDraft, MaterializeKeysRekeysAnExistingNodeWhoseParamsChanged)
{
    CompilerRegistry registry = make_draft_test_registry();

    AssetNode authored = make_node(AssetType::Mesh, schema(10));
    ParamBlock params;
    params.values["subdivisions"] = int64_t{ 4 };
    authored.meta = params;

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(draft, authored);
    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));

    const AssetKey settled = find_asset_graph_draft_node(draft, mesh)->node.key;
    ASSERT_NE(settled, AssetKey{});

    // Re-materializing an untouched graph must be a no-op.
    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));
    EXPECT_EQ(find_asset_graph_draft_node(draft, mesh)->node.key, settled)
        << "a settled key must be stable across repeated materialization";

    // Now edit the param the way the JSON loader would: in place, with the node
    // still Existing and still carrying its old key. No editor API is involved,
    // so nothing marks it Modified and nothing invalidates the key.
    AssetGraphDraftNode* node = find_asset_graph_draft_node(draft, mesh);
    node->state = AssetGraphDraftNodeState::Existing;
    std::any_cast<ParamBlock>(&node->node.meta)->values["subdivisions"] =
        int64_t{ 9 };

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));
    EXPECT_NE(find_asset_graph_draft_node(draft, mesh)->node.key, settled)
        << "an authored param change must reach the key even when the node's "
           "state does not say so";
}

// B1-T1. StoredKeysAgreeWithTheirDerivation -- the whole-graph invariant that
// v1's shipped-project drift check (probe 3) asserted and that never landed as a
// test: after materialization EVERY node's stored key EQUALS the key its own
// params and resolved dependencies derive to, and re-materializing changes
// nothing. This is the property that lets a committed assets.graph.json load
// without silently re-keying, and it is the regression net for the B1-C2 class,
// where a stale generic key on an Existing node denoted changed content while
// every disk-cache integrity check still passed against that same stale key.
//
// Built as an in-memory fixture rather than over the shipped test_mesh_001: that
// project is a scratchpad whose keys self-heal on save (so it loads dirty), and
// committed tests must not depend on it. The fixture stamps keys that DISAGREE
// with their derivation the way the JSON loader delivers a hand-edited graph
// (every node Existing, carrying its stored key verbatim), so the first
// materialize must correct them -- which is exactly what the B1-C2 comparison in
// materialize_asset_graph_draft_keys does, and the revert-check confirms this
// test reddens the moment that comparison is neutered.
TEST(AssetGraphDraft, StoredKeysAgreeWithTheirDerivation)
{
    CompilerRegistry registry = make_draft_test_registry();

    // A three-node graph the way the JSON loader delivers one: nodes are
    // Existing and carry stored keys that were stamped by hand and do NOT match
    // what their params + deps derive to. mesh -> material <- texture, with the
    // mesh carrying an authored param so its content hash is non-trivial.
    AssetNode mesh_node = make_node(AssetType::Mesh, schema(10), 0x10);
    {
        ParamBlock params;
        params.values["subdivisions"] = int64_t{ 4 };
        mesh_node.meta = params;
    }
    AssetNode texture_node = make_node(AssetType::Texture, schema(11), 0x11);
    AssetNode material_node = make_node(AssetType::Material, schema(20), 0x20);

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft, mesh_node, AssetGraphDraftNodeState::Existing);
    const auto texture = add_asset_graph_draft_node(
        draft, texture_node, AssetGraphDraftNodeState::Existing);
    const auto material = add_asset_graph_draft_node(
        draft, material_node, AssetGraphDraftNodeState::Existing);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, texture, material, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    // With no key factory every node keys generically, so the derivation each
    // stored key must equal is make_asset_key(node, its resolved dep keys).
    const auto expect_agrees = [&](const char* when)
    {
        const std::vector<AssetGraphDraftRegistration> registrations =
            asset_graph_draft_to_registrations(draft, &registry, {});
        for (const AssetGraphDraftNode& node : draft.nodes) {
            if (node.state == AssetGraphDraftNodeState::Deleted) {
                continue;
            }
            const AssetGraphDraftRegistration* reg =
                find_registration(registrations, node.id);
            ASSERT_NE(reg, nullptr)
                << when << ": node " << node.id << " has no registration";
            EXPECT_EQ(make_asset_key(node.node, reg->dep_keys), node.node.key)
                << when << ": node " << node.id
                << " stored key disagrees with its live derivation";
        }
    };

    // First materialize settles every key from its derivation, correcting the
    // hand-stamped keys. INVARIANT 1: whole-graph stored == derived.
    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));
    expect_agrees("after first materialize");

    // INVARIANT 2: a second full-graph materialize re-keys nothing.
    std::vector<std::pair<AssetGraphDraftNodeId, AssetKey>> settled;
    for (const AssetGraphDraftNode& node : draft.nodes) {
        settled.emplace_back(node.id, node.node.key);
    }
    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));
    for (const auto& [id, key] : settled) {
        EXPECT_EQ(find_asset_graph_draft_node(draft, id)->node.key, key)
            << "node " << id << " re-keyed on an idempotent materialize";
    }

    // INVARIANT 3 (the B1-C2 cascade): a stale upstream generic key -- the shape
    // a hand-edited assets.graph.json produces, Existing with an out-of-date key
    // -- must be corrected AND propagate through deps_hash to its dependent,
    // restoring the whole-graph invariant. Corrupt only the mesh's stored key.
    const AssetKey good_mesh_key =
        find_asset_graph_draft_node(draft, mesh)->node.key;
    const AssetKey good_material_key =
        find_asset_graph_draft_node(draft, material)->node.key;
    {
        AssetGraphDraftNode* node = find_asset_graph_draft_node(draft, mesh);
        node->state = AssetGraphDraftNodeState::Existing;
        node->node.key = make_key(0xdead);  // a key no derivation produces
    }

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));

    EXPECT_EQ(find_asset_graph_draft_node(draft, mesh)->node.key, good_mesh_key)
        << "a stale generic key on an Existing node was not corrected";
    EXPECT_EQ(
        find_asset_graph_draft_node(draft, material)->node.key, good_material_key)
        << "the correction did not settle the dependent consistently";
    expect_agrees("after correcting a stale upstream key");
}

// B1-H2. param_value_hash folded only the value, so bool(false), int64(0) and
// double(0.0) -- and bool(true) vs int64(1) -- produced identical param hashes,
// and a param retyped Bool<->Int at the same number did not re-key. It now
// folds the variant's type index. (Labelled B1-H3 in the visit-1 report; it is
// the "ParamValue type index" item of the C1/H1/H2 decision.)
TEST(AssetGraphDraft, GenericKeyDistinguishesParamValueTypesAtEqualNumbers)
{
    const auto key_with = [](ParamValue value)
    {
        AssetNode node = make_node(AssetType::Mesh, schema(10));
        ParamBlock params;
        params.values["x"] = std::move(value);
        node.meta = params;
        return make_asset_key(node, {});
    };

    // Same number, different declared type -> different key.
    EXPECT_NE(key_with(ParamValue{ false }), key_with(ParamValue{ int64_t{ 0 } }));
    EXPECT_NE(key_with(ParamValue{ int64_t{ 0 } }), key_with(ParamValue{ 0.0 }));
    EXPECT_NE(key_with(ParamValue{ true }), key_with(ParamValue{ int64_t{ 1 } }));

    // Same type and value -> same key (determinism preserved).
    EXPECT_EQ(
        key_with(ParamValue{ int64_t{ 5 } }),
        key_with(ParamValue{ int64_t{ 5 } }));
    EXPECT_NE(
        key_with(ParamValue{ int64_t{ 5 } }),
        key_with(ParamValue{ int64_t{ 6 } }));
}

// B1-H1. content_hash did not fold node.payload, so two Source nodes differing
// only in their inline bytes produced the same key, contradicting the
// "hash of raw input bytes" contract in types.h. It now folds non-empty payload
// bytes; an empty payload -- which every shipped node carries, since the JSON
// does not persist payload -- stays a no-op, so nothing transient enters the
// key of a real project node.
TEST(AssetGraphDraft, GenericKeyFoldsInlinePayloadBytes)
{
    const auto key_with_bytes = [](std::vector<uint8_t> bytes)
    {
        AssetNode node = make_node(AssetType::Mesh, schema(10));
        node.payload = std::move(bytes);
        return make_asset_key(node, {});
    };

    // Different inline bytes -> different key.
    EXPECT_NE(
        key_with_bytes({ 1, 2, 3, 4 }), key_with_bytes({ 5, 6, 7, 8 }));
    EXPECT_NE(
        key_with_bytes({ 1, 2, 3, 4 }), key_with_bytes({ 1, 2, 3, 4, 5 }));

    // Empty payload does not perturb the key -- the shipped-node no-op path.
    EXPECT_EQ(
        key_with_bytes({}),
        make_asset_key(make_node(AssetType::Mesh, schema(10)), {}));

    // Determinism.
    EXPECT_EQ(key_with_bytes({ 9, 9, 9 }), key_with_bytes({ 9, 9, 9 }));
}

TEST(AssetGraphDraft, MaterializeKeysTreatsPortOrderAsIdentity)
{
    CompilerRegistry registry = make_draft_test_registry();
    registry.register_compiler(AssetCompiler{
        .input_schema = schema(30),
        .output_type = AssetType::Material,
        .input_ports = {
            InputPort{ "base", AssetType::Mesh },
            InputPort{ "target", AssetType::Mesh },
        },
    });

    // The two inputs must differ in CONTENT, not merely carry different
    // hand-assigned keys. materialize_asset_graph_draft_keys now re-derives an
    // Existing node's key and rekeys it when the stored key disagrees, so two
    // nodes with identical schema/stage/params are one asset by content
    // addressing no matter what keys the fixture stamps on them -- and then
    // both ports would carry the same key and this test would pass or fail for
    // reasons having nothing to do with port order. Distinct params keep the
    // two meshes genuinely distinct assets, which is what makes the port-order
    // assertion below meaningful.
    AssetNode mesh_a_node = make_node(AssetType::Mesh, schema(10), 0x10);
    ParamBlock mesh_a_params;
    mesh_a_params.values["slot"] = int64_t{ 1 };
    mesh_a_node.meta = mesh_a_params;

    AssetNode mesh_b_node = make_node(AssetType::Mesh, schema(10), 0x20);
    ParamBlock mesh_b_params;
    mesh_b_params.values["slot"] = int64_t{ 2 };
    mesh_b_node.meta = mesh_b_params;

    AssetGraphDraft draft{};
    const auto mesh_a = add_asset_graph_draft_node(
        draft,
        mesh_a_node,
        AssetGraphDraftNodeState::Existing);
    const auto mesh_b = add_asset_graph_draft_node(
        draft,
        mesh_b_node,
        AssetGraphDraftNodeState::Existing);
    const auto material_a = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(30)));
    const auto material_b = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(30)));

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh_a, material_a, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh_b, material_a, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh_a, material_b, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh_b, material_b, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));

    const AssetKey key_a =
        find_asset_graph_draft_node(draft, material_a)->node.key;
    const AssetKey key_b =
        find_asset_graph_draft_node(draft, material_b)->node.key;
    EXPECT_NE(key_a, key_b);
}

TEST(AssetGraphDraft, LoadFromRegisteredAssetsReconstructsInputPorts)
{
    const AssetKey mesh_key = make_key(0x10);
    const AssetKey shader_key = make_key(0x30);
    const AssetKey material_key = make_key(0x99);

    const std::vector<AssetGraphDraftRegisteredAsset> registered{
        AssetGraphDraftRegisteredAsset{
            .node = make_node(AssetType::Mesh, schema(10), 0x10),
        },
        AssetGraphDraftRegisteredAsset{
            .node = make_node(AssetType::Material, schema(20), 0x99),
            .dep_keys = { mesh_key, AssetKey{}, shader_key },
        },
        AssetGraphDraftRegisteredAsset{
            .node = make_node(AssetType::Shader, schema(12), 0x30),
        },
    };

    AssetGraphDraft draft{};
    load_asset_graph_draft_from_registered_assets(
        draft,
        std::span<const AssetGraphDraftRegisteredAsset>(registered));

    ASSERT_EQ(draft.nodes.size(), 3u);
    ASSERT_EQ(draft.edges.size(), 2u);

    const AssetGraphDraftNodeId material_id =
        draft.node_by_key.at(material_key);
    const auto registrations = asset_graph_draft_to_registrations(draft);
    const AssetGraphDraftRegistration* material_registration =
        find_registration(registrations, material_id);
    ASSERT_NE(material_registration, nullptr);

    ASSERT_EQ(material_registration->dep_keys.size(), 3u);
    EXPECT_EQ(material_registration->dep_keys[0], mesh_key);
    EXPECT_EQ(material_registration->dep_keys[1], AssetKey{});
    EXPECT_EQ(material_registration->dep_keys[2], shader_key);
}

TEST(AssetGraphDraft, LoadFromAuthoredPreservesSparseIdsEdgesAndMeta)
{
    ParamBlock params{};
    params.values["source_path"] = std::string("assets/test.mesh");

    AssetNode source = make_node(AssetType::Mesh, schema(10), 0x10);
    source.meta = params;
    AssetNode material = make_node(AssetType::Material, schema(20));

    const std::vector<AuthoredGraphNode> nodes{
        AuthoredGraphNode{ .node_id = 5u, .node = source },
        AuthoredGraphNode{ .node_id = 9u, .node = material },
    };
    const std::vector<AuthoredGraphEdge> edges{
        AuthoredGraphEdge{
            .from = 5u,
            .to = 9u,
            .to_input_port = 0u,
        },
    };

    AssetGraphDraft draft{};
    load_asset_graph_draft_from_authored(draft, nodes, edges);

    EXPECT_FALSE(draft.dirty);
    EXPECT_EQ(draft.next_node_id, 10u);
    ASSERT_EQ(draft.nodes.size(), 2u);
    ASSERT_EQ(draft.edges.size(), 1u);
    EXPECT_NE(find_asset_graph_draft_node(draft, 5u), nullptr);
    EXPECT_NE(find_asset_graph_draft_node(draft, 9u), nullptr);
    EXPECT_EQ(draft.edges[0].from, 5u);
    EXPECT_EQ(draft.edges[0].to, 9u);
    EXPECT_EQ(draft.edges[0].to_input_port, 0u);

    const auto* stored_params = std::any_cast<ParamBlock>(
        &find_asset_graph_draft_node(draft, 5u)->node.meta);
    ASSERT_NE(stored_params, nullptr);
    EXPECT_EQ(
        stored_params->get<std::string>("source_path", {}),
        "assets/test.mesh");
}

TEST(AssetGraphDraft, LoadFromAuthoredMaterializesKeysWithoutChangingIds)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetNode mesh = make_node(AssetType::Mesh, schema(10));
    AssetNode material = make_node(AssetType::Material, schema(20));

    const std::vector<AuthoredGraphNode> nodes{
        AuthoredGraphNode{ .node_id = 5u, .node = mesh },
        AuthoredGraphNode{ .node_id = 9u, .node = material },
    };
    const std::vector<AuthoredGraphEdge> edges{
        AuthoredGraphEdge{
            .from = 5u,
            .to = 9u,
            .to_input_port = 0u,
        },
    };

    AssetGraphDraft draft{};
    load_asset_graph_draft_from_authored(draft, nodes, edges);

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry, {}));

    ASSERT_NE(find_asset_graph_draft_node(draft, 5u), nullptr);
    ASSERT_NE(find_asset_graph_draft_node(draft, 9u), nullptr);
    EXPECT_NE(find_asset_graph_draft_node(draft, 5u)->node.key, AssetKey{});
    EXPECT_NE(find_asset_graph_draft_node(draft, 9u)->node.key, AssetKey{});
    EXPECT_EQ(draft.next_node_id, 10u);

    const auto registrations =
        asset_graph_draft_to_registrations(draft, &registry);
    const auto* material_registration =
        find_registration(registrations, 9u);
    ASSERT_NE(material_registration, nullptr);
    ASSERT_EQ(material_registration->dep_keys.size(), 3u);
    EXPECT_EQ(
        material_registration->dep_keys[0],
        find_asset_graph_draft_node(draft, 5u)->node.key);
}

TEST(AssetGraphDraftAdversarial, DuplicateAssetKeysAreValidationErrors)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetGraphDraft draft{};

    const auto first = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);
    const auto second = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);

    EXPECT_NE(first, second);
    EXPECT_NE(find_asset_graph_draft_node(draft, first), nullptr);
    EXPECT_NE(find_asset_graph_draft_node(draft, second), nullptr);

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));
    EXPECT_EQ(
        validation_count(draft, AssetGraphDraftValidationCode::DuplicateAssetKey),
        1u);
}

TEST(AssetGraphDraftAdversarial, SetParamsMergesInsteadOfReplacing)
{
    AssetGraphDraft draft{};
    const auto node_id = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);

    ParamBlock first{};
    first.values["a"] = int64_t{ 1 };
    ASSERT_TRUE(set_asset_graph_draft_node_params(
        draft,
        node_id,
        std::move(first)));

    ParamBlock second{};
    second.values["b"] = int64_t{ 2 };
    ASSERT_TRUE(set_asset_graph_draft_node_params(
        draft,
        node_id,
        std::move(second)));

    const auto* params = std::any_cast<ParamBlock>(
        &find_asset_graph_draft_node(draft, node_id)->node.meta);
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->get<int64_t>("a", -1), 1);
    EXPECT_EQ(params->get<int64_t>("b", -1), 2);
}

TEST(AssetGraphDraftAdversarial, SetParamsRefusesTypedMeta)
{
    AssetGraphDraft draft{};
    AssetNode node = make_node(AssetType::Material, schema(20), 0x20);
    node.meta = TypedDesc{ 7 };
    const auto node_id = add_asset_graph_draft_node(
        draft,
        std::move(node),
        AssetGraphDraftNodeState::Existing);

    ParamBlock params{};
    params.values["source_path"] = std::string("assets/test.bin");

    EXPECT_FALSE(set_asset_graph_draft_node_params(
        draft,
        node_id,
        std::move(params)));

    const auto* desc = std::any_cast<TypedDesc>(
        &find_asset_graph_draft_node(draft, node_id)->node.meta);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->value, 7);
}

TEST(AssetGraphDraftAdversarial, ProjectionMaterializesCreatedProviderKeys)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetGraphDraft draft{};

    const auto provider = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10)));
    const auto consumer = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20)));
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, provider, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    const std::vector<AssetGraphDraftRegistration> registrations =
        asset_graph_draft_to_registrations(draft, &registry);
    const auto* provider_registration =
        find_registration(registrations, provider);
    const auto* consumer_registration =
        find_registration(registrations, consumer);
    ASSERT_NE(provider_registration, nullptr);
    ASSERT_NE(consumer_registration, nullptr);

    ASSERT_NE(provider_registration->node.key, AssetKey{});
    ASSERT_FALSE(consumer_registration->dep_keys.empty());
    EXPECT_EQ(
        consumer_registration->dep_keys[0],
        provider_registration->node.key);
}

TEST(AssetGraphDraftAdversarial, OutOfRangePortDoesNotExpandProjection)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetGraphDraft draft{};
    const auto provider = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);
    const auto consumer = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20), 0x20),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, provider, consumer, 999),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::InvalidInputPort));

    const auto registrations =
        asset_graph_draft_to_registrations(draft, &registry);
    const auto* consumer_registration =
        find_registration(registrations, consumer);
    ASSERT_NE(consumer_registration, nullptr);
    EXPECT_EQ(consumer_registration->dep_keys.size(), 3u);
}

TEST(AssetGraphDraftAdversarial, DuplicateSingleArityEdgesAreValidationErrors)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetGraphDraft draft{};
    const auto provider = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);
    const auto consumer = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20), 0x20),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, provider, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, provider, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::DuplicateInputPort));
    EXPECT_FALSE(materialize_asset_graph_draft_keys(draft, registry, {}));
}

TEST(AssetGraphDraftAdversarial, ManyPortFollowedByAnotherPortIsInvalid)
{
    CompilerRegistry registry = make_draft_test_registry();
    registry.register_compiler(AssetCompiler{
        .input_schema = schema(40),
        .output_type = AssetType::Material,
        .input_ports = {
            InputPort{
                "layers",
                AssetType::Texture,
                InputPortRequirement::Optional,
                InputPortArity::Many,
            },
            InputPort{ "final", AssetType::Mesh },
        },
    });

    AssetGraphDraft draft{};
    const auto layer = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(11), 0x1),
        AssetGraphDraftNodeState::Existing);
    const auto final = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x2),
        AssetGraphDraftNodeState::Existing);
    const auto consumer = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(40), 0x3),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, layer, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, final, consumer, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::AmbiguousManyPortBoundary));
}

TEST(AssetGraphDraftAdversarial, ManyPortProjectionOrderSurvivesReconnect)
{
    CompilerRegistry registry{};
    registry.register_compiler(AssetCompiler{
        .input_schema = schema(200),
        .output_type = AssetType::Material,
        .input_ports = {
            InputPort{
                "layers",
                AssetType::Texture,
                InputPortRequirement::Optional,
                InputPortArity::Many,
            },
        },
    });

    AssetGraphDraft draft{};
    const auto first = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(11), 0x1),
        AssetGraphDraftNodeState::Existing);
    const auto second = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(11), 0x2),
        AssetGraphDraftNodeState::Existing);
    const auto third = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Texture, schema(11), 0x3),
        AssetGraphDraftNodeState::Existing);
    const auto consumer = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(200), 0x4),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, first, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    const auto second_edge =
        connect_asset_graph_draft_nodes(draft, second, consumer, 0);
    ASSERT_NE(second_edge, INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, third, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    auto registrations = asset_graph_draft_to_registrations(draft, &registry);
    auto* consumer_registration = find_registration(registrations, consumer);
    ASSERT_NE(consumer_registration, nullptr);
    ASSERT_EQ(consumer_registration->dep_keys.size(), 3u);
    EXPECT_EQ(consumer_registration->dep_keys[0], make_key(0x1));
    EXPECT_EQ(consumer_registration->dep_keys[1], make_key(0x2));
    EXPECT_EQ(consumer_registration->dep_keys[2], make_key(0x3));

    ASSERT_TRUE(disconnect_asset_graph_draft_edge(draft, second_edge));
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, second, consumer, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    registrations = asset_graph_draft_to_registrations(draft, &registry);
    consumer_registration = find_registration(registrations, consumer);
    ASSERT_NE(consumer_registration, nullptr);
    ASSERT_EQ(consumer_registration->dep_keys.size(), 3u);
    EXPECT_EQ(consumer_registration->dep_keys[0], make_key(0x1));
    EXPECT_EQ(consumer_registration->dep_keys[1], make_key(0x2));
    EXPECT_EQ(consumer_registration->dep_keys[2], make_key(0x3));
}

TEST(AssetGraphDraftAdversarial, SelfLoopIsInvalidAndSnapshotFails)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetGraphDraft draft{};
    const auto node = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, node, node, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::SelfDependency));
    EXPECT_FALSE(rebuild_asset_graph_draft_snapshot(draft));
    EXPECT_FALSE(materialize_asset_graph_draft_keys(draft, registry, {}));
}

TEST(AssetGraphDraftAdversarial, TypeMismatchIsValidationError)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);
    const auto wrong_mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x11),
        AssetGraphDraftNodeState::Existing);
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20), 0x20),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, wrong_mesh, material, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::TypeMismatch));
}

TEST(AssetGraphDraftAdversarial, StaleEdgesAfterSchemaChangeAreValidationErrors)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20), 0x20),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_TRUE(set_asset_graph_draft_node_type_schema(
        draft,
        material,
        AssetType::Texture,
        schema(11)));

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::InvalidInputPort));
}

TEST(AssetGraphDraftAdversarial, DanglingPublicEdgeIsValidationError)
{
    CompilerRegistry registry = make_draft_test_registry();
    AssetGraphDraft draft{};
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(20), 0x20),
        AssetGraphDraftNodeState::Existing);
    draft.edges.push_back(AssetGraphDraftEdge{
        .id = draft.next_edge_id++,
        .from = 9001u,
        .to = material,
        .to_input_port = 0,
    });
    rebuild_asset_graph_draft_indexes(draft);

    EXPECT_FALSE(validate_asset_graph_draft(draft, registry));
    EXPECT_TRUE(has_validation_code(
        draft,
        AssetGraphDraftValidationCode::MissingNode));

    const auto registrations =
        asset_graph_draft_to_registrations(draft, &registry);
    const auto* registration = find_registration(registrations, material);
    ASSERT_NE(registration, nullptr);
    ASSERT_EQ(registration->dep_keys.size(), 3u);
    EXPECT_EQ(registration->dep_keys[0], AssetKey{});
}

TEST(AssetGraphDraftAdversarial, SameProviderCanFeedTwoPorts)
{
    CompilerRegistry registry = make_draft_test_registry();
    registry.register_compiler(AssetCompiler{
        .input_schema = schema(30),
        .output_type = AssetType::Material,
        .input_ports = {
            InputPort{ "base", AssetType::Mesh },
            InputPort{ "target", AssetType::Mesh },
        },
    });

    AssetGraphDraft draft{};
    const auto mesh = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);
    const auto material = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Material, schema(30), 0x20),
        AssetGraphDraftNodeState::Existing);

    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, mesh, material, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    EXPECT_TRUE(validate_asset_graph_draft(draft, registry));
    ASSERT_TRUE(rebuild_asset_graph_draft_snapshot(draft));
    ASSERT_NE(draft.graph_snapshot(), nullptr);
    EXPECT_EQ(wz::core::graph::edge_count(*draft.graph_snapshot()), 2u);

    const auto registrations =
        asset_graph_draft_to_registrations(draft, &registry);
    const auto* registration = find_registration(registrations, material);
    ASSERT_NE(registration, nullptr);
    ASSERT_EQ(registration->dep_keys.size(), 2u);
    EXPECT_EQ(registration->dep_keys[0], make_key(0x10));
    EXPECT_EQ(registration->dep_keys[1], make_key(0x10));
}

TEST(AssetGraphDraftAdversarial, CompactPurgesDeletedChurn)
{
    AssetGraphDraft draft{};
    constexpr uint32_t kIterations = 1024u;
    for (uint32_t i = 0; i < kIterations; ++i) {
        const auto id = add_asset_graph_draft_node(
            draft,
            make_node(AssetType::Mesh, schema(10), i + 1u));
        ASSERT_TRUE(remove_asset_graph_draft_node(draft, id));
    }

    ASSERT_EQ(draft.nodes.size(), kIterations);
    compact_asset_graph_draft(draft);
    EXPECT_TRUE(draft.nodes.empty());
    EXPECT_TRUE(draft.node_index.empty());
    EXPECT_TRUE(draft.node_by_key.empty());
}

TEST(AssetGraphDraftAdversarial, RebuildSnapshotClearsDirty)
{
    AssetGraphDraft draft{};
    add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10));
    ASSERT_TRUE(draft.dirty);

    ASSERT_TRUE(rebuild_asset_graph_draft_snapshot(draft));
    EXPECT_FALSE(draft.dirty);
}

TEST(AssetGraphDraftAdversarial, AddDeletedNodeIsRejected)
{
    AssetGraphDraft draft{};
    const auto id = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Deleted);

    EXPECT_EQ(id, INVALID_ASSET_GRAPH_DRAFT_NODE);
    EXPECT_TRUE(draft.nodes.empty());
    EXPECT_TRUE(draft.node_index.empty());
}
