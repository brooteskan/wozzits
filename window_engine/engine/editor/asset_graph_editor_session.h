#pragma once

#include <asset/compiler.h>
#include <asset/draft.h>
#include <asset/types.h>
#include <engine/editor/asset_graph_snapshot.h>
#include <engine/project/project_manifest.h>

#include <external/json/json_document.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

namespace wz::engine::editor
{
    enum class AssetGraphConnectionStatus : uint32_t
    {
        Compatible = 0,
        MissingNode,
        MissingCompiler,
        InvalidInputPort,
        TypeMismatch,
        SelfDependency,
        Cycle,
        DuplicateInputPort,
    };

    struct AssetGraphConnectionCheck
    {
        AssetGraphConnectionStatus status =
            AssetGraphConnectionStatus::MissingNode;
        bool compatible = false;
        bool replaces_existing = false;
        wz::asset::AssetGraphDraftNodeId from =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        wz::asset::AssetGraphDraftNodeId to =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        uint32_t to_input_port = 0;
        wz::asset::AssetType from_type = wz::asset::AssetType::Unknown;
        wz::asset::AssetType to_type = wz::asset::AssetType::Unknown;
        std::string from_type_name;
        std::string to_type_name;
        std::string message;
    };

    class AssetGraphEditorSession
    {
    public:
        AssetGraphEditorSession(
            wz::engine::project::ProjectManifestLoadDesc desc,
            wz::json::JSONDocument document,
            wz::asset::AssetGraphDraft draft,
            const wz::asset::CompilerRegistry& registry);

        AssetGraphEditorSession(const AssetGraphEditorSession&) = delete;
        AssetGraphEditorSession& operator=(const AssetGraphEditorSession&) =
            delete;

        AssetGraphEditorSession(AssetGraphEditorSession&&) noexcept = default;
        AssetGraphEditorSession& operator=(AssetGraphEditorSession&&) noexcept =
            default;

        [[nodiscard]] const wz::engine::project::ProjectManifestLoadDesc&
        desc() const noexcept
        {
            return desc_;
        }

        [[nodiscard]] bool dirty() const noexcept
        {
            return draft_.dirty;
        }

        [[nodiscard]] const wz::asset::AssetGraphDraft& draft() const noexcept
        {
            return draft_;
        }

        [[nodiscard]] wz::asset::AssetGraphDraft& draft() noexcept
        {
            return draft_;
        }

        [[nodiscard]] AssetGraphSnapshot snapshot() const;

        [[nodiscard]] AssetGraphConnectionCheck can_connect(
            wz::asset::AssetGraphDraftNodeId from,
            wz::asset::AssetGraphDraftNodeId to,
            uint32_t to_input_port) const;

        [[nodiscard]] AssetGraphConnectionCheck connect(
            wz::asset::AssetGraphDraftNodeId from,
            wz::asset::AssetGraphDraftNodeId to,
            uint32_t to_input_port);

        [[nodiscard]] bool disconnect_edge(
            wz::asset::AssetGraphDraftEdgeId edge_id);

        [[nodiscard]] bool disconnect_input(
            wz::asset::AssetGraphDraftNodeId node,
            uint32_t input_port);

        // Add a new authored source node for (schema, type) with compiler
        // default params. Returns the new draft node id, or
        // INVALID_ASSET_GRAPH_DRAFT_NODE on failure. Layout position is the
        // editor's concern (set via set_node_position after the snapshot).
        [[nodiscard]] wz::asset::AssetGraphDraftNodeId add_node(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type);

        // Find-or-create the shared "Inochi shared assets" subgraph (the puppet
        // render program + its two shaders) in the draft, staging the embedded
        // puppet shaders into <project>/shaders/puppet/. Added once per project;
        // a second call returns the existing program node. Returns the puppet-
        // program draft node id (to wire puppet renderables' program port to),
        // or INVALID_ASSET_GRAPH_DRAFT_NODE on failure.
        [[nodiscard]] wz::asset::AssetGraphDraftNodeId
        add_inochi_shared_assets();

        // Remove a node (and any edges touching it) from the draft. Returns
        // false if the node does not exist or was already deleted.
        [[nodiscard]] bool remove_node(
            wz::asset::AssetGraphDraftNodeId node_id);

        // Update node position / zoom in the session's in-memory layout (read by
        // snapshot(), persisted by save()). Works for not-yet-saved nodes since
        // it validates against the live draft, not the on-disk graph.
        [[nodiscard]] bool set_node_position(
            wz::asset::AssetGraphDraftNodeId node_id,
            double x,
            double y);

        [[nodiscard]] bool set_zoom(double zoom);

        // Set one node param from display text, converting to the param's
        // declared ParamType (bool/int/float/float3/color/string/filepath/enum;
        // enum accepts an option name or an index). Returns false if the node is
        // missing. Invalidates the node key and marks the draft dirty.
        [[nodiscard]] bool set_node_param(
            wz::asset::AssetGraphDraftNodeId node_id,
            std::string_view name,
            std::string_view value);

        // The in-memory graph document root (carries the "layout" object).
        [[nodiscard]] const wz::json::JSONValue* document_root() const noexcept
        {
            return document_.root.get();
        }

        // ─── The draft loan (issue #313, B4-C1) ─────────────────────────────
        // A bind MOVES draft_ out of this session onto the engine thread and
        // moves the bound draft back when it finishes (Option Y, #189). For
        // that whole window draft() is a moved-from HUSK, and the window is not
        // brief: it is seconds of GPU work, which the editor deliberately runs
        // on a background thread (Task.Run) so its UI stays live and reachable.
        //
        // So the husk is observable by another thread, and every use of it is
        // wrong in a way that destroys authored data: a READER (the save) writes
        // an empty assets.graph.json over the project's real graph, and a WRITER
        // (add node, connect, set param) has its mutation silently destroyed
        // when the bound draft moves back on top of it.
        //
        // draft_on_loan() is how a caller refuses instead of doing either. The
        // ABI gates every session export on it in ONE place (validate_session),
        // so a new session export cannot forget to ask.
        [[nodiscard]] bool draft_on_loan() const noexcept
        {
            return loan_depth_.depth.load(std::memory_order_acquire) > 0;
        }

        // RAII bracket for the window above. Counted rather than boolean so a
        // second overlapping bind cannot end the first one's loan early (that
        // overlap is its own defect — #313 B4-C2 — and this must not paper over
        // it by reporting the draft present while a bind still holds it).
        class DraftLoan
        {
        public:
            explicit DraftLoan(AssetGraphEditorSession& session) noexcept
                : session_(&session)
            {
                session_->loan_depth_.depth.fetch_add(
                    1, std::memory_order_acq_rel);
            }

            DraftLoan(const DraftLoan&) = delete;
            DraftLoan& operator=(const DraftLoan&) = delete;
            DraftLoan(DraftLoan&&) = delete;
            DraftLoan& operator=(DraftLoan&&) = delete;

            // Releases on ANY exit, including a throwing bind — the draft is
            // back in the session by then (bind_asset_graph's own handback
            // guarantees that), so leaving the loan held would wedge saving for
            // the rest of the session.
            ~DraftLoan()
            {
                session_->loan_depth_.depth.fetch_sub(
                    1, std::memory_order_acq_rel);
            }

        private:
            AssetGraphEditorSession* session_ = nullptr;
        };

    private:
        // std::atomic is not movable and this class keeps its defaulted move
        // operations, so the counter rides in a holder that moves by value.
        struct LoanDepth
        {
            std::atomic<int> depth{ 0 };

            LoanDepth() = default;

            LoanDepth(LoanDepth&& other) noexcept
                : depth(other.depth.load(std::memory_order_acquire))
            {
            }

            LoanDepth& operator=(LoanDepth&& other) noexcept
            {
                depth.store(
                    other.depth.load(std::memory_order_acquire),
                    std::memory_order_release);
                return *this;
            }
        };

        wz::engine::project::ProjectManifestLoadDesc desc_;
        wz::json::JSONDocument document_;
        wz::asset::AssetGraphDraft draft_;
        const wz::asset::CompilerRegistry* registry_ = nullptr;
        LoanDepth loan_depth_;
    };

    struct AssetGraphEditorSessionOpenResult
    {
        bool ok = false;
        std::string error;
        std::unique_ptr<AssetGraphEditorSession> session;
    };

    AssetGraphEditorSessionOpenResult open_asset_graph_editor_session(
        const wz::engine::project::ProjectManifestLoadDesc& desc,
        const wz::asset::CompilerRegistry& registry);
}
