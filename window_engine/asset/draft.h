#pragma once

// wz/asset/draft.h
//
// Editable asset DAG draft state.
//
// AssetGraph is the immutable traversal view built by AssetSystem::commit().
// AssetGraphDraft is an authoring model: stable draft IDs, editable node/edge
// records, ordered input-slot metadata, validation messages, and an optional
// built AssetGraph snapshot for graph algorithms and preview.

#include "compiler.h"
#include "dag.h"

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wz::asset {

    using AssetGraphDraftNodeId = uint64_t;
    using AssetGraphDraftEdgeId = uint64_t;

    inline constexpr AssetGraphDraftNodeId INVALID_ASSET_GRAPH_DRAFT_NODE =
        (std::numeric_limits<AssetGraphDraftNodeId>::max)();

    inline constexpr AssetGraphDraftEdgeId INVALID_ASSET_GRAPH_DRAFT_EDGE =
        (std::numeric_limits<AssetGraphDraftEdgeId>::max)();

    enum class AssetGraphDraftNodeState : uint8_t {
        Existing = 0,
        Created,
        Modified,
        Deleted,
    };

    enum class AssetGraphDraftValidationSeverity : uint8_t {
        Info = 0,
        Warning,
        Error,
    };

    enum class AssetGraphDraftValidationCode : uint8_t {
        None = 0,
        UnknownCompiler,
        MissingRequiredInput,
        InvalidInputPort,
        DuplicateInputPort,
        TypeMismatch,
        MissingNode,
        MissingAssetKey,
        SelfDependency,
        Cycle,
        DuplicateAssetKey,
        TypedMetaConflict,
        AmbiguousManyPortBoundary,
    };

    [[nodiscard]] constexpr std::string_view asset_graph_draft_validation_severity_name(
        AssetGraphDraftValidationSeverity severity) noexcept
    {
        switch (severity) {
        case AssetGraphDraftValidationSeverity::Info:
            return "info";
        case AssetGraphDraftValidationSeverity::Warning:
            return "warning";
        case AssetGraphDraftValidationSeverity::Error:
            return "error";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr std::string_view asset_graph_draft_validation_code_name(
        AssetGraphDraftValidationCode code) noexcept
    {
        switch (code) {
        case AssetGraphDraftValidationCode::None:
            return "None";
        case AssetGraphDraftValidationCode::UnknownCompiler:
            return "UnknownCompiler";
        case AssetGraphDraftValidationCode::MissingRequiredInput:
            return "MissingRequiredInput";
        case AssetGraphDraftValidationCode::InvalidInputPort:
            return "InvalidInputPort";
        case AssetGraphDraftValidationCode::DuplicateInputPort:
            return "DuplicateInputPort";
        case AssetGraphDraftValidationCode::TypeMismatch:
            return "TypeMismatch";
        case AssetGraphDraftValidationCode::MissingNode:
            return "MissingNode";
        case AssetGraphDraftValidationCode::MissingAssetKey:
            return "MissingAssetKey";
        case AssetGraphDraftValidationCode::SelfDependency:
            return "SelfDependency";
        case AssetGraphDraftValidationCode::Cycle:
            return "Cycle";
        case AssetGraphDraftValidationCode::DuplicateAssetKey:
            return "DuplicateAssetKey";
        case AssetGraphDraftValidationCode::TypedMetaConflict:
            return "TypedMetaConflict";
        case AssetGraphDraftValidationCode::AmbiguousManyPortBoundary:
            return "AmbiguousManyPortBoundary";
        }
        return "Unknown";
    }

    struct AssetGraphDraftNode {
        AssetGraphDraftNodeId id = INVALID_ASSET_GRAPH_DRAFT_NODE;

        // node.key is the draft's current asset key. For Created or Modified
        // nodes this may be provisional; stable draft identity is id, and final
        // content-addressed keys are established by the projection step.
        AssetNode node{};

        // Node index in graph_snapshot(), when a snapshot has been built.
        // Draft edits can invalidate this; rebuild the snapshot before using it.
        NodeHandle graph_node = INVALID_ASSET_NODE;

        AssetGraphDraftNodeState state = AssetGraphDraftNodeState::Existing;
    };

    struct AssetGraphDraftEdge {
        AssetGraphDraftEdgeId id = INVALID_ASSET_GRAPH_DRAFT_EDGE;

        // Edge direction follows AssetGraph: from is prerequisite/provider,
        // to is dependent/consumer.
        AssetGraphDraftNodeId from = INVALID_ASSET_GRAPH_DRAFT_NODE;
        AssetGraphDraftNodeId to = INVALID_ASSET_GRAPH_DRAFT_NODE;

        // Ordered input slot on the dependent node. This maps to
        // AssetCompiler::input_ports and materializes as dep_keys[slot].
        uint32_t to_input_port = 0;

        AssetEdge edge{};
    };

    struct AssetGraphDraftValidationMessage {
        AssetGraphDraftValidationSeverity severity =
            AssetGraphDraftValidationSeverity::Info;
        AssetGraphDraftValidationCode code =
            AssetGraphDraftValidationCode::None;
        AssetGraphDraftNodeId node = INVALID_ASSET_GRAPH_DRAFT_NODE;
        AssetGraphDraftEdgeId edge = INVALID_ASSET_GRAPH_DRAFT_EDGE;
        uint32_t input_port = 0;
        std::string message;
    };

    struct AssetGraphDraftRegistration {
        AssetGraphDraftNodeId draft_node = INVALID_ASSET_GRAPH_DRAFT_NODE;
        AssetNode node{};
        std::vector<AssetKey> dep_keys;
    };

    struct AssetGraphDraftRegisteredAsset {
        AssetNode node{};
        std::vector<AssetKey> dep_keys;
    };

    struct AuthoredGraphNode {
        AssetGraphDraftNodeId node_id = INVALID_ASSET_GRAPH_DRAFT_NODE;
        AssetNode node{};
    };

    struct AuthoredGraphEdge {
        AssetGraphDraftNodeId from = INVALID_ASSET_GRAPH_DRAFT_NODE;
        AssetGraphDraftNodeId to = INVALID_ASSET_GRAPH_DRAFT_NODE;
        uint32_t to_input_port = 0;
        AssetEdge edge{};
    };

    struct AssetGraphDraft {
        // Editable source of truth.
        std::vector<AssetGraphDraftNode> nodes;
        std::vector<AssetGraphDraftEdge> edges;

        // Optional immutable snapshot rebuilt from nodes/edges. AssetGraph is a
        // view, so the draft owns it through AssetStorage.
        std::optional<AssetStorage> storage;

        // Lookup tables for UI/controllers and materialization passes.
        std::unordered_map<AssetGraphDraftNodeId, size_t> node_index;
        std::unordered_map<AssetGraphDraftEdgeId, size_t> edge_index;

        // Lookup by the draft node's current AssetKey. Keys for created or
        // modified nodes may be provisional; do not use this as stable node
        // identity across edits or projection.
        std::unordered_map<AssetKey, AssetGraphDraftNodeId, AssetKeyHash>
            node_by_key;

        std::vector<AssetGraphDraftValidationMessage> validation_messages;

        AssetGraphDraftNodeId next_node_id = 1;
        AssetGraphDraftEdgeId next_edge_id = 1;
        bool dirty = false;

        [[nodiscard]] const AssetGraph* graph_snapshot() const noexcept
        {
            return storage ? &storage->dag() : nullptr;
        }

        [[nodiscard]] bool has_graph_snapshot() const noexcept
        {
            return storage.has_value();
        }
    };

    [[nodiscard]] inline bool asset_graph_draft_key_valid(
        const AssetKey& key) noexcept
    {
        return !(key == AssetKey{});
    }

    using AssetKeyFactoryFn = std::function<std::optional<AssetKey>(
        const AssetNode& node,
        std::span<const AssetKey> dep_keys)>;

    namespace asset_graph_draft_detail
    {
        [[nodiscard]] constexpr uint64_t mix64(
            uint64_t a,
            uint64_t b) noexcept
        {
            return a
                ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
        }

        [[nodiscard]] constexpr Hash hash_u64(uint64_t value) noexcept
        {
            return { value, 0 };
        }

        [[nodiscard]] constexpr uint64_t fnv1a_64(
            std::string_view text) noexcept
        {
            uint64_t h = 14695981039346656037ull;
            for (const unsigned char c : text) {
                h ^= static_cast<uint64_t>(c);
                h *= 1099511628211ull;
            }
            return h;
        }

        [[nodiscard]] constexpr Hash key_to_dep_hash(
            const AssetKey& key) noexcept
        {
            const uint64_t lo =
                mix64(mix64(key.content_hash.lo, key.schema_hash.lo),
                      mix64(key.compiler_hash.lo, key.deps_hash.lo));
            const uint64_t hi =
                mix64(mix64(key.content_hash.hi, key.schema_hash.hi),
                      mix64(key.compiler_hash.hi, key.deps_hash.hi));
            return { lo, hi };
        }

        [[nodiscard]] constexpr Hash combine_hashes(
            Hash a,
            Hash b) noexcept
        {
            return { mix64(a.lo, b.lo), mix64(a.hi, b.hi) };
        }

        [[nodiscard]] inline uint64_t param_value_hash(
            const ParamValue& value)
        {
            return std::visit(
                [](const auto& v) -> uint64_t
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, bool>) {
                        return v ? 1ull : 0ull;
                    }
                    else if constexpr (std::is_same_v<T, int64_t>) {
                        return static_cast<uint64_t>(v);
                    }
                    else if constexpr (std::is_same_v<T, double>) {
                        uint64_t bits = 0;
                        static_assert(sizeof(bits) == sizeof(v));
                        std::memcpy(&bits, &v, sizeof(bits));
                        return bits;
                    }
                    else if constexpr (std::is_same_v<
                                           T,
                                           std::array<float, 3>>)
                    {
                        uint64_t h = 3ull;
                        for (float f : v) {
                            uint32_t bits = 0;
                            static_assert(sizeof(bits) == sizeof(f));
                            std::memcpy(&bits, &f, sizeof(bits));
                            h = mix64(h, bits);
                        }
                        return h;
                    }
                    else {
                        return fnv1a_64(v);
                    }
                },
                value);
        }

        [[nodiscard]] inline Hash meta_hash(const std::any& meta)
        {
            if (!meta.has_value()) {
                return {};
            }

            if (const auto* params = std::any_cast<ParamBlock>(&meta)) {
                std::vector<std::string_view> names;
                names.reserve(params->values.size());
                for (const auto& [name, value] : params->values) {
                    (void)value;
                    names.push_back(name);
                }
                std::sort(names.begin(), names.end());

                uint64_t lo = fnv1a_64("ParamBlock");
                uint64_t hi = params->values.size();
                for (std::string_view name : names) {
                    const auto& value = params->values.at(std::string(name));
                    lo = mix64(lo, fnv1a_64(name));
                    hi = mix64(hi, param_value_hash(value));
                }
                return { lo, hi };
            }

            return {
                fnv1a_64(meta.type().name()),
                static_cast<uint64_t>(meta.type().hash_code()),
            };
        }

        inline void add_validation_message(
            AssetGraphDraft& draft,
            AssetGraphDraftValidationCode code,
            AssetGraphDraftNodeId node = INVALID_ASSET_GRAPH_DRAFT_NODE,
            AssetGraphDraftEdgeId edge = INVALID_ASSET_GRAPH_DRAFT_EDGE,
            uint32_t input_port = 0,
            std::string message = {})
        {
            draft.validation_messages.push_back(
                AssetGraphDraftValidationMessage{
                    .severity = AssetGraphDraftValidationSeverity::Error,
                    .code = code,
                    .node = node,
                    .edge = edge,
                    .input_port = input_port,
                    .message = std::move(message),
                });
        }
    }

    [[nodiscard]] inline AssetKey make_asset_key(
        const AssetNode& node,
        std::span<const AssetKey> dep_keys)
    {
        using namespace asset_graph_draft_detail;

        uint64_t identity =
            mix64(static_cast<uint64_t>(node.type), node.schema.value);
        identity = mix64(identity, static_cast<uint64_t>(node.stage));
        identity = mix64(identity, static_cast<uint64_t>(node.residency));
        identity = mix64(identity, static_cast<uint64_t>(node.kind));
        identity = mix64(identity, static_cast<uint64_t>(node.demand_root));

        const Hash meta = meta_hash(node.meta);
        Hash deps{};
        for (const AssetKey& dep_key : dep_keys) {
            deps = combine_hashes(deps, key_to_dep_hash(dep_key));
        }

        return AssetKey{
            .content_hash = combine_hashes(hash_u64(identity), meta),
            .schema_hash = hash_u64(node.schema.value),
            .compiler_hash =
                hash_u64(mix64(static_cast<uint64_t>(node.type), 1ull)),
            .deps_hash = deps,
        };
    }

    [[nodiscard]] inline AssetKey resolve_asset_graph_draft_key(
        const AssetNode& node,
        std::span<const AssetKey> dep_keys,
        const AssetKeyFactoryFn& key_factory)
    {
        if (key_factory) {
            if (std::optional<AssetKey> key = key_factory(node, dep_keys)) {
                return *key;
            }
        }
        return make_asset_key(node, dep_keys);
    }

    inline void rebuild_asset_graph_draft_indexes(AssetGraphDraft& draft)
    {
        draft.node_index.clear();
        draft.edge_index.clear();
        draft.node_by_key.clear();

        for (size_t i = 0; i < draft.nodes.size(); ++i) {
            const AssetGraphDraftNode& node = draft.nodes[i];
            draft.node_index[node.id] = i;
            if (node.state != AssetGraphDraftNodeState::Deleted
                && asset_graph_draft_key_valid(node.node.key))
            {
                draft.node_by_key[node.node.key] = node.id;
            }
        }

        for (size_t i = 0; i < draft.edges.size(); ++i) {
            draft.edge_index[draft.edges[i].id] = i;
        }
    }

    [[nodiscard]] inline AssetGraphDraftNode* find_asset_graph_draft_node(
        AssetGraphDraft& draft,
        AssetGraphDraftNodeId id)
    {
        const auto it = draft.node_index.find(id);
        if (it == draft.node_index.end() || it->second >= draft.nodes.size()) {
            return nullptr;
        }
        return &draft.nodes[it->second];
    }

    [[nodiscard]] inline const AssetGraphDraftNode* find_asset_graph_draft_node(
        const AssetGraphDraft& draft,
        AssetGraphDraftNodeId id)
    {
        const auto it = draft.node_index.find(id);
        if (it == draft.node_index.end() || it->second >= draft.nodes.size()) {
            return nullptr;
        }
        return &draft.nodes[it->second];
    }

    [[nodiscard]] inline AssetGraphDraftEdge* find_asset_graph_draft_edge(
        AssetGraphDraft& draft,
        AssetGraphDraftEdgeId id)
    {
        const auto it = draft.edge_index.find(id);
        if (it == draft.edge_index.end() || it->second >= draft.edges.size()) {
            return nullptr;
        }
        return &draft.edges[it->second];
    }

    [[nodiscard]] inline const AssetGraphDraftEdge* find_asset_graph_draft_edge(
        const AssetGraphDraft& draft,
        AssetGraphDraftEdgeId id)
    {
        const auto it = draft.edge_index.find(id);
        if (it == draft.edge_index.end() || it->second >= draft.edges.size()) {
            return nullptr;
        }
        return &draft.edges[it->second];
    }

    inline void mark_asset_graph_draft_node_modified(
        AssetGraphDraftNode& node)
    {
        if (node.state == AssetGraphDraftNodeState::Existing) {
            node.state = AssetGraphDraftNodeState::Modified;
        }
    }

    inline void invalidate_asset_graph_draft_node_key(
        AssetGraphDraftNode& node)
    {
        node.node.key = {};
        node.graph_node = INVALID_ASSET_NODE;
    }

    inline AssetGraphDraftNodeId add_asset_graph_draft_node(
        AssetGraphDraft& draft,
        AssetNode node,
        AssetGraphDraftNodeState state = AssetGraphDraftNodeState::Created)
    {
        if (state == AssetGraphDraftNodeState::Deleted) {
            return INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        const AssetGraphDraftNodeId id = draft.next_node_id++;
        draft.nodes.push_back(AssetGraphDraftNode{
            .id = id,
            .node = std::move(node),
            .state = state,
        });
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return id;
    }

    inline AssetGraphDraftNodeId add_asset_graph_draft_node_with_id(
        AssetGraphDraft& draft,
        AssetNode node,
        AssetGraphDraftNodeId id,
        AssetGraphDraftNodeState state = AssetGraphDraftNodeState::Existing)
    {
        if (id == INVALID_ASSET_GRAPH_DRAFT_NODE
            || state == AssetGraphDraftNodeState::Deleted)
        {
            return INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        rebuild_asset_graph_draft_indexes(draft);
        if (draft.node_index.find(id) != draft.node_index.end()) {
            return INVALID_ASSET_GRAPH_DRAFT_NODE;
        }

        draft.nodes.push_back(AssetGraphDraftNode{
            .id = id,
            .node = std::move(node),
            .state = state,
        });
        if (draft.next_node_id <= id) {
            draft.next_node_id = id + 1u;
        }
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return id;
    }

    inline bool remove_asset_graph_draft_node(
        AssetGraphDraft& draft,
        AssetGraphDraftNodeId id)
    {
        AssetGraphDraftNode* node = find_asset_graph_draft_node(draft, id);
        if (!node || node->state == AssetGraphDraftNodeState::Deleted) {
            return false;
        }

        node->state = AssetGraphDraftNodeState::Deleted;
        draft.edges.erase(
            std::remove_if(
                draft.edges.begin(),
                draft.edges.end(),
                [id](const AssetGraphDraftEdge& edge)
                {
                    return edge.from == id || edge.to == id;
                }),
            draft.edges.end());
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return true;
    }

    inline AssetGraphDraftEdgeId connect_asset_graph_draft_nodes(
        AssetGraphDraft& draft,
        AssetGraphDraftNodeId from,
        AssetGraphDraftNodeId to,
        uint32_t to_input_port,
        AssetEdge edge = {})
    {
        const AssetGraphDraftNode* from_node =
            find_asset_graph_draft_node(draft, from);
        const AssetGraphDraftNode* to_node =
            find_asset_graph_draft_node(draft, to);
        if (!from_node || !to_node
            || from_node->state == AssetGraphDraftNodeState::Deleted
            || to_node->state == AssetGraphDraftNodeState::Deleted)
        {
            return INVALID_ASSET_GRAPH_DRAFT_EDGE;
        }

        const AssetGraphDraftEdgeId id = draft.next_edge_id++;
        draft.edges.push_back(AssetGraphDraftEdge{
            .id = id,
            .from = from,
            .to = to,
            .to_input_port = to_input_port,
            .edge = edge,
        });
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return id;
    }

    inline bool disconnect_asset_graph_draft_edge(
        AssetGraphDraft& draft,
        AssetGraphDraftEdgeId id)
    {
        const auto before = draft.edges.size();
        draft.edges.erase(
            std::remove_if(
                draft.edges.begin(),
                draft.edges.end(),
                [id](const AssetGraphDraftEdge& edge)
                {
                    return edge.id == id;
                }),
            draft.edges.end());
        if (draft.edges.size() == before) {
            return false;
        }
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return true;
    }

    inline bool disconnect_asset_graph_draft_input(
        AssetGraphDraft& draft,
        AssetGraphDraftNodeId node,
        uint32_t input_port)
    {
        const auto before = draft.edges.size();
        draft.edges.erase(
            std::remove_if(
                draft.edges.begin(),
                draft.edges.end(),
                [node, input_port](const AssetGraphDraftEdge& edge)
                {
                    return edge.to == node
                        && edge.to_input_port == input_port;
                }),
            draft.edges.end());
        if (draft.edges.size() == before) {
            return false;
        }
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return true;
    }

    inline bool set_asset_graph_draft_node_params(
        AssetGraphDraft& draft,
        AssetGraphDraftNodeId id,
        ParamBlock params)
    {
        AssetGraphDraftNode* node = find_asset_graph_draft_node(draft, id);
        if (!node || node->state == AssetGraphDraftNodeState::Deleted) {
            return false;
        }

        if (node->node.meta.has_value()) {
            auto* existing = std::any_cast<ParamBlock>(&node->node.meta);
            if (!existing) {
                return false;
            }
            for (auto& [name, value] : params.values) {
                existing->values.insert_or_assign(
                    std::move(name),
                    std::move(value));
            }
        }
        else {
            node->node.meta = std::move(params);
        }
        invalidate_asset_graph_draft_node_key(*node);
        mark_asset_graph_draft_node_modified(*node);
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return true;
    }

    inline bool set_asset_graph_draft_node_schema(
        AssetGraphDraft& draft,
        AssetGraphDraftNodeId id,
        SchemaID schema)
    {
        AssetGraphDraftNode* node = find_asset_graph_draft_node(draft, id);
        if (!node || node->state == AssetGraphDraftNodeState::Deleted) {
            return false;
        }

        node->node.schema = schema;
        invalidate_asset_graph_draft_node_key(*node);
        mark_asset_graph_draft_node_modified(*node);
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return true;
    }

    inline bool set_asset_graph_draft_node_type_schema(
        AssetGraphDraft& draft,
        AssetGraphDraftNodeId id,
        AssetType type,
        SchemaID schema)
    {
        AssetGraphDraftNode* node = find_asset_graph_draft_node(draft, id);
        if (!node || node->state == AssetGraphDraftNodeState::Deleted) {
            return false;
        }

        node->node.type = type;
        node->node.schema = schema;
        invalidate_asset_graph_draft_node_key(*node);
        mark_asset_graph_draft_node_modified(*node);
        draft.dirty = true;
        rebuild_asset_graph_draft_indexes(draft);
        return true;
    }

    inline bool rebuild_asset_graph_draft_snapshot(AssetGraphDraft& draft)
    {
        rebuild_asset_graph_draft_indexes(draft);

        AssetBuilder builder;
        std::unordered_map<AssetGraphDraftNodeId, NodeHandle> handles;
        handles.reserve(draft.nodes.size());

        for (AssetGraphDraftNode& node : draft.nodes) {
            node.graph_node = INVALID_ASSET_NODE;
            if (node.state == AssetGraphDraftNodeState::Deleted) {
                continue;
            }

            const NodeHandle handle =
                wz::core::graph::add_node(builder, node.node);
            node.graph_node = handle;
            handles[node.id] = handle;
        }

        for (const AssetGraphDraftEdge& edge : draft.edges) {
            const auto from = handles.find(edge.from);
            const auto to = handles.find(edge.to);
            if (from == handles.end() || to == handles.end()) {
                continue;
            }
            if (!wz::core::graph::add_edge(
                    builder,
                    from->second,
                    to->second,
                    edge.edge))
            {
                draft.storage.reset();
                return false;
            }
        }

        std::optional<AssetStorage> storage =
            asset_build(std::move(builder));
        if (!storage) {
            draft.storage.reset();
            return false;
        }

        draft.storage = std::move(*storage);
        draft.dirty = false;
        return true;
    }

    inline void compact_asset_graph_draft(AssetGraphDraft& draft)
    {
        draft.nodes.erase(
            std::remove_if(
                draft.nodes.begin(),
                draft.nodes.end(),
                [](const AssetGraphDraftNode& node)
                {
                    return node.state == AssetGraphDraftNodeState::Deleted;
                }),
            draft.nodes.end());

        draft.edges.erase(
            std::remove_if(
                draft.edges.begin(),
                draft.edges.end(),
                [&draft](const AssetGraphDraftEdge& edge)
                {
                    return std::none_of(
                            draft.nodes.begin(),
                            draft.nodes.end(),
                            [&edge](const AssetGraphDraftNode& node)
                            {
                                return node.id == edge.from;
                            })
                        || std::none_of(
                            draft.nodes.begin(),
                            draft.nodes.end(),
                            [&edge](const AssetGraphDraftNode& node)
                            {
                                return node.id == edge.to;
                            });
                }),
            draft.edges.end());

        rebuild_asset_graph_draft_indexes(draft);
    }

    inline std::vector<AssetGraphDraftRegistration>
    asset_graph_draft_to_registrations(
        const AssetGraphDraft& draft,
        const CompilerRegistry* registry = nullptr,
        const AssetKeyFactoryFn& key_factory = {});

    template<class Registration>
    inline void load_asset_graph_draft_from_registered_assets(
        AssetGraphDraft& draft,
        std::span<const Registration> registrations)
    {
        draft = AssetGraphDraft{};

        std::unordered_map<AssetKey, AssetGraphDraftNodeId, AssetKeyHash>
            ids_by_key;
        ids_by_key.reserve(registrations.size());

        for (const Registration& registration : registrations) {
            const AssetGraphDraftNodeId id =
                add_asset_graph_draft_node(
                    draft,
                    registration.node,
                    AssetGraphDraftNodeState::Existing);
            if (asset_graph_draft_key_valid(registration.node.key)) {
                ids_by_key[registration.node.key] = id;
            }
        }

        for (size_t to_index = 0; to_index < registrations.size(); ++to_index) {
            const AssetGraphDraftNodeId to_id =
                draft.nodes[to_index].id;
            const Registration& registration = registrations[to_index];

            for (uint32_t port = 0;
                 port < static_cast<uint32_t>(registration.dep_keys.size());
                 ++port)
            {
                const AssetKey& dep_key = registration.dep_keys[port];
                if (dep_key == AssetKey{}) {
                    continue;
                }

                const auto from = ids_by_key.find(dep_key);
                const AssetGraphDraftEdgeId edge_id = draft.next_edge_id++;
                draft.edges.push_back(AssetGraphDraftEdge{
                    .id = edge_id,
                    .from = from != ids_by_key.end()
                        ? from->second
                        : INVALID_ASSET_GRAPH_DRAFT_NODE,
                    .to = to_id,
                    .to_input_port = port,
                });
            }
        }

        draft.dirty = false;
        rebuild_asset_graph_draft_indexes(draft);
    }

    inline void load_asset_graph_draft_from_authored(
        AssetGraphDraft& draft,
        std::span<const AuthoredGraphNode> nodes,
        std::span<const AuthoredGraphEdge> edges)
    {
        draft = AssetGraphDraft{};

        for (const AuthoredGraphNode& authored_node : nodes) {
            add_asset_graph_draft_node_with_id(
                draft,
                authored_node.node,
                authored_node.node_id,
                AssetGraphDraftNodeState::Existing);
        }

        for (const AuthoredGraphEdge& authored_edge : edges) {
            connect_asset_graph_draft_nodes(
                draft,
                authored_edge.from,
                authored_edge.to,
                authored_edge.to_input_port,
                authored_edge.edge);
        }

        draft.dirty = false;
        rebuild_asset_graph_draft_indexes(draft);
    }

    inline bool validate_asset_graph_draft(
        AssetGraphDraft& draft,
        const CompilerRegistry& registry,
        bool allow_missing_existing_keys = false)
    {
        using namespace asset_graph_draft_detail;

        rebuild_asset_graph_draft_indexes(draft);
        draft.validation_messages.clear();

        std::unordered_map<
            AssetGraphDraftNodeId,
            std::vector<const AssetGraphDraftEdge*>>
            incoming_edges;
        std::unordered_map<AssetKey, AssetGraphDraftNodeId, AssetKeyHash>
            seen_keys;

        for (const AssetGraphDraftNode& node : draft.nodes) {
            if (node.state == AssetGraphDraftNodeState::Deleted) {
                continue;
            }

            if (!allow_missing_existing_keys
                && node.state == AssetGraphDraftNodeState::Existing
                && !asset_graph_draft_key_valid(node.node.key))
            {
                add_validation_message(
                    draft,
                    AssetGraphDraftValidationCode::MissingAssetKey,
                    node.id,
                    INVALID_ASSET_GRAPH_DRAFT_EDGE,
                    0,
                    "node has no asset key");
            }

            if (asset_graph_draft_key_valid(node.node.key)) {
                const auto [it, inserted] =
                    seen_keys.emplace(node.node.key, node.id);
                if (!inserted) {
                    // Asset keys are content hashes, so two nodes with the same
                    // key are the SAME asset (an identical recipe: same schema,
                    // params, and inputs). Name both so the author can find and
                    // remove or differentiate one of them.
                    add_validation_message(
                        draft,
                        AssetGraphDraftValidationCode::DuplicateAssetKey,
                        node.id,
                        INVALID_ASSET_GRAPH_DRAFT_EDGE,
                        0,
                        "node " + std::to_string(node.id)
                            + " has the same asset key as node "
                            + std::to_string(it->second)
                            + " (identical recipe — delete one, or differentiate "
                              "them, e.g. give them distinct params)");
                }
            }

            if (node.node.kind != AssetNodeKind::DemandRoot
                && !registry.find(node.node.schema, node.node.type))
            {
                add_validation_message(
                    draft,
                    AssetGraphDraftValidationCode::UnknownCompiler,
                    node.id,
                    INVALID_ASSET_GRAPH_DRAFT_EDGE,
                    0,
                    "no compiler registered for node schema/type");
            }
        }

        for (const AssetGraphDraftEdge& edge : draft.edges) {
            const AssetGraphDraftNode* from =
                find_asset_graph_draft_node(draft, edge.from);
            const AssetGraphDraftNode* to =
                find_asset_graph_draft_node(draft, edge.to);

            if (!from || !to
                || from->state == AssetGraphDraftNodeState::Deleted
                || to->state == AssetGraphDraftNodeState::Deleted)
            {
                add_validation_message(
                    draft,
                    AssetGraphDraftValidationCode::MissingNode,
                    edge.to,
                    edge.id,
                    edge.to_input_port,
                    "edge endpoint is missing");
                continue;
            }

            if (edge.from == edge.to) {
                add_validation_message(
                    draft,
                    AssetGraphDraftValidationCode::SelfDependency,
                    edge.to,
                    edge.id,
                    edge.to_input_port,
                    "edge connects a node to itself");
            }

            incoming_edges[edge.to].push_back(&edge);

            const AssetCompiler* compiler =
                registry.find(to->node.schema, to->node.type);
            if (!compiler) {
                continue;
            }

            if (edge.to_input_port >= compiler->input_ports.size()) {
                add_validation_message(
                    draft,
                    AssetGraphDraftValidationCode::InvalidInputPort,
                    edge.to,
                    edge.id,
                    edge.to_input_port,
                    "edge targets an input port outside the compiler schema");
                continue;
            }

            const InputPort& port =
                compiler->input_ports[edge.to_input_port];
            if (from->node.type != port.type) {
                add_validation_message(
                    draft,
                    AssetGraphDraftValidationCode::TypeMismatch,
                    edge.to,
                    edge.id,
                    edge.to_input_port,
                    "edge provider type does not match the input port type");
            }
        }

        for (const AssetGraphDraftNode& node : draft.nodes) {
            if (node.state == AssetGraphDraftNodeState::Deleted) {
                continue;
            }

            const AssetCompiler* compiler =
                registry.find(node.node.schema, node.node.type);
            if (!compiler) {
                continue;
            }

            for (uint32_t port = 0;
                 port < static_cast<uint32_t>(compiler->input_ports.size());
                 ++port)
            {
                const InputPort& input_port = compiler->input_ports[port];
                if (input_port_many(input_port)
                    && port + 1u
                        < static_cast<uint32_t>(
                            compiler->input_ports.size()))
                {
                    add_validation_message(
                        draft,
                        AssetGraphDraftValidationCode::
                            AmbiguousManyPortBoundary,
                        node.id,
                        INVALID_ASSET_GRAPH_DRAFT_EDGE,
                        port,
                        "many input ports must be the final input port");
                }
            }

            const auto& edges = incoming_edges[node.id];
            std::vector<uint32_t> counts(compiler->input_ports.size(), 0u);
            for (const AssetGraphDraftEdge* edge : edges) {
                if (edge->to_input_port < counts.size()) {
                    ++counts[edge->to_input_port];
                }
            }

            for (uint32_t port = 0;
                 port < static_cast<uint32_t>(compiler->input_ports.size());
                 ++port)
            {
                const InputPort& input_port = compiler->input_ports[port];
                if (input_port_required(input_port) && counts[port] == 0u) {
                    add_validation_message(
                        draft,
                        AssetGraphDraftValidationCode::MissingRequiredInput,
                        node.id,
                        INVALID_ASSET_GRAPH_DRAFT_EDGE,
                        port,
                        "required input port is unwired");
                }
                if (!input_port_many(input_port) && counts[port] > 1u) {
                    add_validation_message(
                        draft,
                        AssetGraphDraftValidationCode::DuplicateInputPort,
                        node.id,
                        INVALID_ASSET_GRAPH_DRAFT_EDGE,
                        port,
                        "single input port has more than one edge");
                }
            }
        }

        AssetBuilder builder;
        std::unordered_map<AssetGraphDraftNodeId, NodeHandle> handles;
        handles.reserve(draft.nodes.size());
        for (const AssetGraphDraftNode& node : draft.nodes) {
            if (node.state == AssetGraphDraftNodeState::Deleted) {
                continue;
            }
            handles[node.id] =
                wz::core::graph::add_node(builder, node.node);
        }

        for (const AssetGraphDraftEdge& edge : draft.edges) {
            const auto from = handles.find(edge.from);
            const auto to = handles.find(edge.to);
            if (from == handles.end() || to == handles.end()) {
                continue;
            }
            wz::core::graph::add_edge(
                builder,
                from->second,
                to->second,
                edge.edge);
        }

        if (!asset_build(std::move(builder))) {
            add_validation_message(
                draft,
                AssetGraphDraftValidationCode::Cycle,
                INVALID_ASSET_GRAPH_DRAFT_NODE,
                INVALID_ASSET_GRAPH_DRAFT_EDGE,
                0,
                "draft graph contains a cycle");
        }

        return std::none_of(
            draft.validation_messages.begin(),
            draft.validation_messages.end(),
            [](const AssetGraphDraftValidationMessage& message)
            {
                return message.severity
                    == AssetGraphDraftValidationSeverity::Error;
            });
    }

    inline bool materialize_asset_graph_draft_keys(
        AssetGraphDraft& draft,
        const CompilerRegistry& registry,
        const AssetKeyFactoryFn& key_factory = {})
    {
        if (!validate_asset_graph_draft(
                draft,
                registry,
                true))
        {
            return false;
        }

        if (!rebuild_asset_graph_draft_snapshot(draft)) {
            return false;
        }

        const AssetGraph* graph = draft.graph_snapshot();
        if (!graph) {
            return false;
        }

        std::unordered_map<NodeHandle, AssetGraphDraftNodeId>
            draft_node_by_graph_node;
        draft_node_by_graph_node.reserve(draft.nodes.size());
        for (const AssetGraphDraftNode& node : draft.nodes) {
            if (node.state != AssetGraphDraftNodeState::Deleted
                && node.graph_node != INVALID_ASSET_NODE)
            {
                draft_node_by_graph_node[node.graph_node] = node.id;
            }
        }

        std::unordered_set<AssetGraphDraftNodeId> rematerialized;

        for (NodeHandle graph_node : wz::core::graph::topo_order(*graph)) {
            const auto draft_it = draft_node_by_graph_node.find(graph_node);
            if (draft_it == draft_node_by_graph_node.end()) {
                continue;
            }

            AssetGraphDraftNode* node =
                find_asset_graph_draft_node(draft, draft_it->second);
            if (!node || node->state == AssetGraphDraftNodeState::Deleted) {
                continue;
            }

            bool must_materialize =
                node->state == AssetGraphDraftNodeState::Created
                || node->state == AssetGraphDraftNodeState::Modified
                || !asset_graph_draft_key_valid(node->node.key);

            std::vector<AssetKey> dep_keys;
            for (const AssetGraphDraftRegistration& registration :
                 asset_graph_draft_to_registrations(
                     draft,
                     &registry,
                     key_factory))
            {
                if (registration.draft_node == node->id) {
                    dep_keys = registration.dep_keys;
                    break;
                }
            }

            const std::optional<AssetKey> factory_key =
                key_factory
                    ? key_factory(node->node, dep_keys)
                    : std::nullopt;

            if (factory_key && *factory_key != node->node.key) {
                must_materialize = true;
            }

            for (const AssetGraphDraftEdge& edge : draft.edges) {
                if (edge.to == node->id
                    && rematerialized.count(edge.from) != 0u)
                {
                    must_materialize = true;
                    break;
                }
            }

            if (!must_materialize) {
                continue;
            }

            node->node.key =
                factory_key
                    ? *factory_key
                    : resolve_asset_graph_draft_key(
                        node->node,
                        dep_keys,
                        key_factory);
            rematerialized.insert(node->id);
        }

        rebuild_asset_graph_draft_indexes(draft);
        return true;
    }

    inline std::vector<AssetGraphDraftRegistration>
    asset_graph_draft_to_registrations(
        const AssetGraphDraft& draft,
        const CompilerRegistry* registry,
        const AssetKeyFactoryFn& key_factory)
    {
        std::vector<AssetGraphDraftRegistration> registrations;
        std::unordered_map<AssetGraphDraftNodeId, const AssetGraphDraftNode*>
            nodes_by_id;
        nodes_by_id.reserve(draft.nodes.size());
        for (const AssetGraphDraftNode& node : draft.nodes) {
            if (node.state != AssetGraphDraftNodeState::Deleted) {
                nodes_by_id[node.id] = &node;
            }
        }

        auto compiler_for_node =
            [registry](const AssetGraphDraftNode& node)
                -> const AssetCompiler*
            {
                return registry
                    ? registry->find(node.node.schema, node.node.type)
                    : nullptr;
            };

        auto port_count_for_node =
            [&](const AssetGraphDraftNode& node) -> size_t
            {
                const AssetCompiler* compiler = compiler_for_node(node);
                size_t port_count =
                    compiler ? compiler->input_ports.size() : size_t{ 0 };
                if (!compiler) {
                    for (const AssetGraphDraftEdge& edge : draft.edges) {
                        if (edge.to == node.id) {
                            port_count =
                                (std::max)(
                                    port_count,
                                    static_cast<size_t>(edge.to_input_port)
                                        + 1u);
                        }
                    }
                }
                return port_count;
            };

        std::unordered_map<AssetGraphDraftNodeId, AssetKey> computed_keys;
        std::unordered_set<AssetGraphDraftNodeId> visiting;

        std::function<std::vector<AssetKey>(const AssetGraphDraftNode&)>
            dep_keys_for_node;
        std::function<AssetKey(AssetGraphDraftNodeId)> key_for_node;

        dep_keys_for_node =
            [&](const AssetGraphDraftNode& node) -> std::vector<AssetKey>
            {
                const AssetCompiler* compiler = compiler_for_node(node);
                const size_t port_count = port_count_for_node(node);
                std::vector<
                    std::vector<std::pair<AssetGraphDraftNodeId, AssetKey>>>
                    keys_by_port(port_count);

                for (const AssetGraphDraftEdge& edge : draft.edges) {
                    if (edge.to != node.id
                        || static_cast<size_t>(edge.to_input_port)
                            >= keys_by_port.size())
                    {
                        continue;
                    }

                    const auto from_it = nodes_by_id.find(edge.from);
                    if (from_it == nodes_by_id.end()) {
                        continue;
                    }

                    keys_by_port[edge.to_input_port].push_back({
                        edge.from,
                        key_for_node(edge.from),
                    });
                }

                std::vector<AssetKey> dep_keys;
                dep_keys.reserve(port_count);
                for (size_t port = 0; port < port_count; ++port) {
                    const InputPort* input_port =
                        compiler && port < compiler->input_ports.size()
                        ? &compiler->input_ports[port]
                        : nullptr;
                    const bool many = input_port && input_port_many(*input_port);

                    if (keys_by_port[port].empty()) {
                        dep_keys.push_back({});
                        continue;
                    }

                    if (many) {
                        std::sort(
                            keys_by_port[port].begin(),
                            keys_by_port[port].end(),
                            [](const auto& a, const auto& b)
                            {
                                return a.first < b.first;
                            });
                        for (const auto& [from, key] : keys_by_port[port]) {
                            (void)from;
                            dep_keys.push_back(key);
                        }
                    }
                    else {
                        dep_keys.push_back(keys_by_port[port].front().second);
                    }
                }
                return dep_keys;
            };

        key_for_node =
            [&](AssetGraphDraftNodeId id) -> AssetKey
            {
                const auto computed = computed_keys.find(id);
                if (computed != computed_keys.end()) {
                    return computed->second;
                }

                const auto node_it = nodes_by_id.find(id);
                if (node_it == nodes_by_id.end()) {
                    return {};
                }

                const AssetGraphDraftNode& node = *node_it->second;
                if (!visiting.insert(id).second) {
                    return node.node.key;
                }

                const bool must_materialize =
                    !asset_graph_draft_key_valid(node.node.key);
                AssetKey key = node.node.key;
                if (must_materialize) {
                    const std::vector<AssetKey> dep_keys =
                        dep_keys_for_node(node);
                    key = resolve_asset_graph_draft_key(
                        node.node,
                        dep_keys,
                        key_factory);
                }

                visiting.erase(id);
                computed_keys[id] = key;
                return key;
            };

        for (const AssetGraphDraftNode& node : draft.nodes) {
            if (node.state == AssetGraphDraftNodeState::Deleted) {
                continue;
            }

            AssetGraphDraftRegistration registration{};
            registration.draft_node = node.id;
            registration.node = node.node;
            registration.dep_keys = dep_keys_for_node(node);
            if (!asset_graph_draft_key_valid(node.node.key)) {
                registration.node.key = key_for_node(node.id);
            }

            registrations.push_back(std::move(registration));
        }

        return registrations;
    }

} // namespace wz::asset
