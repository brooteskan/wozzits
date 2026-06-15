#include <gtest/gtest.h>

#include <asset/draft.h>
#include <asset/system.h>
#include <graph/static_dag.h>

#include <any>
#include <algorithm>
#include <string>

namespace
{
    using namespace wz::asset;

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

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry));

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

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry));

    const AssetKey new_mesh_key =
        find_asset_graph_draft_node(draft, mesh)->node.key;
    const AssetKey new_material_key =
        find_asset_graph_draft_node(draft, material)->node.key;

    EXPECT_NE(new_mesh_key, AssetKey{});
    EXPECT_NE(new_material_key, AssetKey{});
    EXPECT_NE(new_mesh_key, old_mesh_key);
    EXPECT_NE(new_material_key, old_material_key);
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

    AssetGraphDraft draft{};
    const auto mesh_a = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x10),
        AssetGraphDraftNodeState::Existing);
    const auto mesh_b = add_asset_graph_draft_node(
        draft,
        make_node(AssetType::Mesh, schema(10), 0x20),
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

    ASSERT_TRUE(materialize_asset_graph_draft_keys(draft, registry));

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
    EXPECT_FALSE(materialize_asset_graph_draft_keys(draft, registry));
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
    EXPECT_FALSE(materialize_asset_graph_draft_keys(draft, registry));
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
