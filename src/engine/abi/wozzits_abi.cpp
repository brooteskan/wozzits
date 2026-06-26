#include <engine/abi/wozzits_abi.h>

#include <engine/app/editor_runtime.h>
#include <engine/editor/asset_graph_editor_session.h>
#include <engine/editor/asset_graph_layout.h>
#include <engine/editor/asset_graph_schema_registry.h>
#include <engine/editor/project_snapshot_abi.h>
#include <engine/editor/project_snapshot.h>
#include <engine/project/project_runtime_launch.h>

#include <math/quaternion.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct WzHostSession
{
    wz::asset::CompilerRegistry registry;
    std::unique_ptr<wz::engine::editor::AssetGraphEditorSession> editor;
};

struct WzHostRuntime
{
    wz::app::EditorRuntimeControl control;
    std::thread thread;
    WzHostLogCallback log_callback = nullptr;
    void* log_user = nullptr;

    // Host-capability flag (the "one ABI" role gate). Set true only by
    // wz_host_runtime_start — the editor/host launcher — and required by the
    // scene-mutation verbs (see require_host_scene_authoring). A future
    // behavior-as-consumer that obtains a runtime through some non-host path
    // would NOT have this set and is therefore denied mutation by construction.
    // Today every WzHostRuntime is host-started, so this is always true; the
    // point is that the gate exists and the verbs check it.
    bool host_capability = false;
};

namespace
{
    WzResult result(WzResultCode code, const char* message)
    {
        return WzResult{
            .code = code,
            .message = message,
        };
    }

    WzResult dynamic_error(WzResultCode code, std::string message)
    {
        thread_local std::string last_error;
        last_error = std::move(message);
        return result(code, last_error.c_str());
    }

    WzResult copy_bytes_to_buffer(const std::vector<uint8_t>& bytes, WzBuffer* out)
    {
        const uint64_t size = static_cast<uint64_t>(bytes.size());
        auto* data = static_cast<uint8_t*>(std::malloc(size == 0u ? 1u : size));
        if (!data) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }

        if (size != 0u) {
            std::memcpy(data, bytes.data(), static_cast<size_t>(size));
        }
        out->data = data;
        out->size = size;
        return result(WZ_RESULT_OK, "");
    }

    WzResult validate_scene_edit_target(
        const char* project_root_utf8,
        const char* node_id_utf8)
    {
        if (!project_root_utf8 || project_root_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "project_root_utf8 must not be empty");
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "node_id_utf8 must not be empty");
        }
        return result(WZ_RESULT_OK, "");
    }

    WzResult scene_mutations_not_wired_yet()
    {
        return result(
            WZ_RESULT_INTERNAL_ERROR,
            "scene authoring mutations are not implemented in the engine ABI yet");
    }

    WzResult validate_project_root(const char* project_root_utf8)
    {
        if (!project_root_utf8 || project_root_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "project_root_utf8 must not be empty");
        }
        return result(WZ_RESULT_OK, "");
    }

    WzResult validate_session(
        WzHostSession* session,
        const char* parameter_name = "session")
    {
        if (!session || !session->editor) {
            return dynamic_error(
                WZ_RESULT_INVALID_ARGUMENT,
                std::string(parameter_name) + " must not be null");
        }
        return result(WZ_RESULT_OK, "");
    }

    WzResult prepare_output_buffer(WzBuffer* out, const char* parameter_name)
    {
        if (!out) {
            return dynamic_error(
                WZ_RESULT_INVALID_ARGUMENT,
                std::string(parameter_name) + " must not be null");
        }

        out->data = nullptr;
        out->size = 0u;
        return result(WZ_RESULT_OK, "");
    }

    void emit_editor_runtime_log(
        wz::LogLevel level,
        std::string_view timestamp,
        std::string_view text,
        void* user)
    {
        auto* runtime = static_cast<WzHostRuntime*>(user);
        if (!runtime || !runtime->log_callback) {
            return;
        }

        runtime->log_callback(
            static_cast<uint32_t>(level),
            timestamp.data(),
            static_cast<uint64_t>(timestamp.size()),
            text.data(),
            static_cast<uint64_t>(text.size()),
            runtime->log_user);
    }

    // ─── Host-capability gate for scene-mutation verbs ──────────────────────
    // THE single documented gate point for the "one ABI" mutation role. Every
    // scene-authoring verb (behavior bindings AND optional components) calls this
    // first. It fails closed: a null runtime or one lacking the host capability
    // is rejected with WZ_RESULT_INVALID_ARGUMENT, so a future non-host consumer
    // of this same ABI (e.g. a behavior calling in) is denied by construction.
    // Returns OK only for a host-capable runtime.
    WzResult require_host_scene_authoring(const WzHostRuntime* runtime)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (!runtime->host_capability) {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "caller lacks the host capability for scene mutation");
        }
        return result(WZ_RESULT_OK, "");
    }

    // Parse the ABI config (kind, value) pair into a SceneBehaviorConfigValue.
    // kind is "bool" | "int" | "float" | "string"; int/float both store as a
    // Number. Returns false for an unknown kind. Malformed numeric text parses
    // as 0 / false rather than erroring (the host validates input upstream).
    bool parse_behavior_config_value(
        std::string key,
        const char* kind_utf8,
        const char* value_utf8,
        wz::engine::assets::SceneBehaviorConfigValue& out)
    {
        const std::string kind = kind_utf8 ? kind_utf8 : "";
        const std::string value = value_utf8 ? value_utf8 : "";
        out.key = std::move(key);

        using Kind = wz::engine::assets::SceneBehaviorConfigValueKind;
        if (kind == "bool") {
            out.kind = Kind::Bool;
            out.bool_value =
                value == "true" || value == "1" || value == "True";
            return true;
        }
        if (kind == "int" || kind == "float") {
            out.kind = Kind::Number;
            try {
                out.number_value = value.empty() ? 0.0 : std::stod(value);
            }
            catch (...) {
                out.number_value = 0.0;
            }
            return true;
        }
        if (kind == "string") {
            out.kind = Kind::String;
            out.string_value = value;
            return true;
        }
        return false;
    }

    // Parse a newline-delimited UTF-8 string into channel tokens, trimming
    // surrounding whitespace (incl. a trailing '\r' from CRLF) and dropping
    // blank lines. Used by the set-events verb so the engine, not the host,
    // owns the parse.
    std::vector<std::string> parse_newline_delimited(const char* text_utf8)
    {
        std::vector<std::string> out;
        if (!text_utf8) {
            return out;
        }
        const std::string text = text_utf8;
        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t nl = text.find('\n', start);
            const std::size_t end =
                (nl == std::string::npos) ? text.size() : nl;
            std::size_t a = start;
            std::size_t b = end;
            while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) {
                ++a;
            }
            while (b > a
                && std::isspace(static_cast<unsigned char>(text[b - 1]))) {
                --b;
            }
            if (b > a) {
                out.push_back(text.substr(a, b - a));
            }
            if (nl == std::string::npos) {
                break;
            }
            start = nl + 1;
        }
        return out;
    }
}

extern "C"
{
    uint32_t wz_abi_version(void)
    {
        return WZ_ABI_VERSION;
    }

    WzResult wz_host_asset_catalog(WzBuffer* out_catalog)
    {
        if (const WzResult target =
                prepare_output_buffer(out_catalog, "out_catalog");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return copy_bytes_to_buffer(
                wz::engine::editor::asset_catalog_abi_blob(),
                out_catalog);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset catalog build failed");
        }
    }

    WzResult wz_host_load_project_snapshot(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        WzBuffer* out_snapshot)
    {
        if (!out_snapshot) {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "out_snapshot must not be null");
        }

        out_snapshot->data = nullptr;
        out_snapshot->size = 0u;

        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto snapshot =
                wz::engine::editor::load_project_snapshot(
                    wz::engine::project::ProjectManifestLoadDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                    });
            return copy_bytes_to_buffer(
                wz::engine::editor::project_snapshot_abi_blob(snapshot),
                out_snapshot);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "project snapshot load failed");
        }
    }

    WzResult wz_host_create_project(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        const char* name_utf8,
        WzBuffer* out_project)
    {
        if (!out_project) {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "out_project must not be null");
        }

        out_project->data = nullptr;
        out_project->size = 0u;

        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto created =
                wz::engine::project::create_project_manifest(
                    wz::engine::project::ProjectManifestCreateDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                        .name = name_utf8 ? name_utf8 : "",
                    });
            return copy_bytes_to_buffer(
                wz::engine::editor::project_create_abi_blob(created),
                out_project);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "project creation failed");
        }
    }

    WzResult wz_host_scene_set_node_properties(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        const char* node_id_utf8,
        const char* name_utf8,
        uint32_t visible)
    {
        (void)resource_root_utf8;
        (void)name_utf8;
        (void)visible;

        if (const WzResult target =
                validate_scene_edit_target(project_root_utf8, node_id_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        return scene_mutations_not_wired_yet();
    }

    WzResult wz_host_scene_set_node_transform(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        const char* node_id_utf8,
        const char* translation_x_utf8,
        const char* translation_y_utf8,
        const char* translation_z_utf8,
        const char* rotation_x_utf8,
        const char* rotation_y_utf8,
        const char* rotation_z_utf8,
        const char* scale_x_utf8,
        const char* scale_y_utf8,
        const char* scale_z_utf8)
    {
        (void)resource_root_utf8;
        (void)translation_x_utf8;
        (void)translation_y_utf8;
        (void)translation_z_utf8;
        (void)rotation_x_utf8;
        (void)rotation_y_utf8;
        (void)rotation_z_utf8;
        (void)scale_x_utf8;
        (void)scale_y_utf8;
        (void)scale_z_utf8;

        if (const WzResult target =
                validate_scene_edit_target(project_root_utf8, node_id_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        return scene_mutations_not_wired_yet();
    }

    WzResult wz_host_scene_set_camera(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        const char* node_id_utf8,
        const char* fov_y_utf8,
        const char* near_plane_utf8,
        const char* far_plane_utf8,
        const char* aspect_utf8)
    {
        (void)resource_root_utf8;
        (void)fov_y_utf8;
        (void)near_plane_utf8;
        (void)far_plane_utf8;
        (void)aspect_utf8;

        if (const WzResult target =
                validate_scene_edit_target(project_root_utf8, node_id_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        return scene_mutations_not_wired_yet();
    }

    WzResult wz_host_asset_graph_set_node_position(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        uint64_t node_id,
        double x,
        double y)
    {
        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto updated =
                wz::engine::editor::update_project_asset_graph_node_layout(
                    wz::engine::project::ProjectManifestLoadDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                    },
                    static_cast<wz::asset::AssetGraphDraftNodeId>(node_id),
                    x,
                    y);
            return updated.ok
                ? result(WZ_RESULT_OK, "")
                : dynamic_error(
                    WZ_RESULT_INVALID_ARGUMENT,
                    updated.error);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph layout update failed");
        }
    }

    WzResult wz_host_asset_graph_set_zoom(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        double zoom)
    {
        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto updated =
                wz::engine::editor::update_project_asset_graph_zoom(
                    wz::engine::project::ProjectManifestLoadDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                    },
                    zoom);
            return updated.ok
                ? result(WZ_RESULT_OK, "")
                : dynamic_error(
                    WZ_RESULT_INVALID_ARGUMENT,
                    updated.error);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph zoom update failed");
        }
    }

    WzResult wz_host_open_project_session(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        WzHostSession** out_session)
    {
        if (!out_session) {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "out_session must not be null");
        }
        *out_session = nullptr;

        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            auto session = std::make_unique<WzHostSession>();

            // Device-free compiler schemas: lets can_connect/snapshot validate
            // against the real declared input ports (not draft-inferred ones)
            // without an EngineAssetLibrary or a GPU device.
            session->registry =
                wz::engine::editor::build_asset_graph_schema_registry();

            auto opened =
                wz::engine::editor::open_asset_graph_editor_session(
                    wz::engine::project::ProjectManifestLoadDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                    },
                    session->registry);
            if (!opened.ok || !opened.session) {
                return dynamic_error(
                    WZ_RESULT_INVALID_ARGUMENT,
                    opened.error);
            }

            session->editor = std::move(opened.session);
            *out_session = session.release();
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "editor session open failed");
        }
    }

    void wz_host_close_session(WzHostSession* session)
    {
        delete session;
    }

    WzResult wz_host_session_asset_graph_snapshot(
        WzHostSession* session,
        WzBuffer* out_snapshot)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }
        if (const WzResult target =
                prepare_output_buffer(out_snapshot, "out_snapshot");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return copy_bytes_to_buffer(
                wz::engine::editor::asset_graph_snapshot_abi_blob(
                    session->editor->snapshot()),
                out_snapshot);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph snapshot failed");
        }
    }

    WzResult wz_host_asset_graph_can_connect(
        WzHostSession* session,
        uint64_t from_node_id,
        uint64_t to_node_id,
        uint32_t to_input_port,
        WzBuffer* out_check)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }
        if (const WzResult target =
                prepare_output_buffer(out_check, "out_check");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return copy_bytes_to_buffer(
                wz::engine::editor::asset_graph_connection_check_abi_blob(
                    session->editor->can_connect(
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            from_node_id),
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            to_node_id),
                        to_input_port)),
                out_check);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph connection check failed");
        }
    }

    WzResult wz_host_asset_graph_connect(
        WzHostSession* session,
        uint64_t from_node_id,
        uint64_t to_node_id,
        uint32_t to_input_port,
        WzBuffer* out_check)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }
        if (const WzResult target =
                prepare_output_buffer(out_check, "out_check");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return copy_bytes_to_buffer(
                wz::engine::editor::asset_graph_connection_check_abi_blob(
                    session->editor->connect(
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            from_node_id),
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            to_node_id),
                        to_input_port)),
                out_check);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph connect failed");
        }
    }

    WzResult wz_host_asset_graph_add_node(
        WzHostSession* session,
        uint64_t schema,
        uint32_t type,
        uint64_t* out_node_id)
    {
        if (out_node_id) {
            *out_node_id = 0u;
        }
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const wz::asset::AssetGraphDraftNodeId id =
                session->editor->add_node(
                    wz::asset::SchemaID{ schema },
                    static_cast<wz::asset::AssetType>(
                        static_cast<uint16_t>(type)));
            if (id == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE) {
                return result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "could not add asset graph node");
            }
            if (out_node_id) {
                *out_node_id = static_cast<uint64_t>(id);
            }
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph add node failed");
        }
    }

    WzResult wz_host_session_set_node_position(
        WzHostSession* session,
        uint64_t node_id,
        double x,
        double y)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return session->editor->set_node_position(
                       static_cast<wz::asset::AssetGraphDraftNodeId>(node_id),
                       x,
                       y)
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph node position update failed");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph node position update failed");
        }
    }

    WzResult wz_host_session_set_zoom(
        WzHostSession* session,
        double zoom)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return session->editor->set_zoom(zoom)
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph zoom update failed");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph zoom update failed");
        }
    }

    WzResult wz_host_asset_graph_remove_node(
        WzHostSession* session,
        uint64_t node_id)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return session->editor->remove_node(
                       static_cast<wz::asset::AssetGraphDraftNodeId>(node_id))
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph node not found");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph remove node failed");
        }
    }

    WzResult wz_host_asset_graph_disconnect_edge(
        WzHostSession* session,
        uint64_t edge_id)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return session->editor->disconnect_edge(
                       static_cast<wz::asset::AssetGraphDraftEdgeId>(edge_id))
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph edge not found");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph disconnect failed");
        }
    }

    WzResult wz_host_asset_graph_set_node_param_string(
        WzHostSession* session,
        uint64_t node_id,
        const char* name_utf8,
        const char* value_utf8)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }
        if (!name_utf8 || name_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "param name must not be empty");
        }

        try {
            // The editor passes the value as display text; the session converts
            // it to the param's declared ParamType (int/float/bool/enum/...).
            return session->editor->set_node_param(
                       static_cast<wz::asset::AssetGraphDraftNodeId>(node_id),
                       name_utf8,
                       value_utf8 ? value_utf8 : "")
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph node not found");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph set node param failed");
        }
    }

    WzResult wz_host_session_save(WzHostSession* session)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto saved = wz::engine::editor::save_project_asset_graph(
                session->editor->desc(),
                session->editor->draft(),
                session->editor->document_root());
            return saved.ok
                ? result(WZ_RESULT_OK, "")
                : dynamic_error(WZ_RESULT_INTERNAL_ERROR, saved.error);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(WZ_RESULT_INTERNAL_ERROR, "asset graph save failed");
        }
    }

    WzHostRuntime* wz_host_runtime_start(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        WzHostLogCallback log_callback,
        void* log_user)
    {
        if (!project_root_utf8 || project_root_utf8[0] == '\0') {
            return nullptr;
        }

        try {
            auto runtime = std::make_unique<WzHostRuntime>();
            const std::string project_root = project_root_utf8;
            const std::string resource_root =
                resource_root_utf8 ? resource_root_utf8 : "";
            runtime->log_callback = log_callback;
            runtime->log_user = log_user;
            // This runtime is started by the editor/host; grant it the host
            // capability so the scene-mutation verbs (behavior authoring, etc.)
            // accept it. A non-host caller never reaches this constructor.
            runtime->host_capability = true;

            WzHostRuntime* raw = runtime.get();
            raw->thread = std::thread([raw, project_root, resource_root]() {
                try {
                    const auto loaded =
                        wz::engine::project::load_project_runtime_launch(
                            wz::engine::project::ProjectRuntimeLaunchDesc{
                                .project_root = project_root,
                                .resource_root = resource_root,
                            });
                    if (!loaded.ok) {
                        const std::string message =
                            "resident engine launch failed: " + loaded.error;
                        emit_editor_runtime_log(
                            wz::LogLevel::Error,
                            {},
                            message,
                            raw);
                    }
                    else {
                        const int exit_code = wz::app::run_project_runtime(
                            "Wozzits Viewport",
                            loaded.launch.asset_graph_path,
                            loaded.launch.scene_path,
                            loaded.launch.resource_root,
                            &raw->control,
                            wz::app::EditorRuntimeLogSink{
                                .write = emit_editor_runtime_log,
                                .user = raw,
                            },
                            loaded.launch.manifest.behavior_module_folder);
                        emit_editor_runtime_log(
                            exit_code == 0
                                ? wz::LogLevel::Info
                                : wz::LogLevel::Error,
                            {},
                            exit_code == 0
                                ? "resident engine runtime exited."
                                : "resident engine runtime exited with an error.",
                            raw);
                    }
                }
                catch (...) {
                    emit_editor_runtime_log(
                        wz::LogLevel::Error,
                        {},
                        "resident engine runtime failed with an exception.",
                        raw);
                    // The runtime thread must not throw across the ABI; a
                    // failure simply ends the viewport.
                }
                // Always unblock any pending bind, including the init-fail path.
                raw->control.mark_finished();
            });

            return runtime.release();
        }
        catch (...) {
            return nullptr;
        }
    }

    void wz_host_runtime_stop(WzHostRuntime* runtime)
    {
        if (!runtime) {
            return;
        }
        runtime->control.request_stop();
        if (runtime->thread.joinable()) {
            runtime->thread.join();
        }
        delete runtime;
    }

    int wz_host_runtime_is_running(WzHostRuntime* runtime)
    {
        if (!runtime) {
            return 0;
        }
        // finished() flips true after the render loop exits (window closed or
        // stop serviced) and the thread called mark_finished(). Until then the
        // viewport is live. The runtime object itself outlives the thread; only
        // wz_host_runtime_stop frees it.
        return runtime->control.finished() ? 0 : 1;
    }

    WzResult wz_host_runtime_bind_draft(
        WzHostRuntime* runtime,
        WzHostSession* session)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            // The draft is moved to the engine thread and the bound draft moved
            // back into the session in place (AssetGraphDraft is move-only).
            wz::asset::AssetGraphDraft& draft = session->editor->draft();
            const wz::app::AssetGraphCompileResult report =
                runtime->control.bind(draft);
            if (report.ok) {
                return result(WZ_RESULT_OK, "");
            }
            std::string detail = "asset graph compile failed";
            bool first = true;
            for (const wz::asset::AssetGraphDraftValidationMessage& diag :
                 report.diagnostics)
            {
                if (diag.severity
                    != wz::asset::AssetGraphDraftValidationSeverity::Error)
                {
                    continue;
                }
                detail += first ? ": " : "; ";
                first = false;
                if (diag.node
                    != wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
                {
                    detail += "node "
                        + std::to_string(static_cast<uint64_t>(diag.node))
                        + ": ";
                }
                detail += diag.message.empty()
                    ? "unspecified error"
                    : diag.message;
            }
            if (first) {
                detail += ": "
                    + std::to_string(report.diagnostics.size())
                    + " diagnostic(s)";
            }
            return dynamic_error(WZ_RESULT_INTERNAL_ERROR, detail);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(WZ_RESULT_INTERNAL_ERROR, "asset graph bind failed");
        }
    }

    WzResult wz_host_runtime_set_node_transform(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        double translation_x,
        double translation_y,
        double translation_z,
        double rotation_x_degrees,
        double rotation_y_degrees,
        double rotation_z_degrees,
        double scale_x,
        double scale_y,
        double scale_z)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "node_id_utf8 must not be empty");
        }

        try {
            const wz::math::Quaternion q =
                wz::math::quaternion_from_euler_degrees(
                    static_cast<float>(rotation_x_degrees),
                    static_cast<float>(rotation_y_degrees),
                    static_cast<float>(rotation_z_degrees));

            wz::engine::assets::AuthoredTransform transform;
            transform.translation[0] = static_cast<float>(translation_x);
            transform.translation[1] = static_cast<float>(translation_y);
            transform.translation[2] = static_cast<float>(translation_z);
            transform.rotation_quat[0] = q.x;
            transform.rotation_quat[1] = q.y;
            transform.rotation_quat[2] = q.z;
            transform.rotation_quat[3] = q.w;
            transform.scale[0] = static_cast<float>(scale_x);
            transform.scale[1] = static_cast<float>(scale_y);
            transform.scale[2] = static_cast<float>(scale_z);

            // Fire-and-forget: queued for the engine thread, applied next frame.
            runtime->control.post_scene_node_transform(
                wz::app::SceneNodeTransformEdit{
                    .id = node_id_utf8,
                    .transform = transform,
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "scene node transform post failed");
        }
    }

    WzResult wz_host_runtime_set_node_properties(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* name_utf8,
        uint32_t visible)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "node_id_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_properties(
                wz::app::SceneNodePropertiesEdit{
                    .id = node_id_utf8,
                    .name = name_utf8 ? name_utf8 : "",
                    .visible = visible != 0u,
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "scene node properties post failed");
        }
    }

    WzResult wz_host_runtime_reparent_node(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* new_parent_id_utf8)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "node_id_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_reparent(
                wz::app::SceneNodeReparentEdit{
                    .id = node_id_utf8,
                    .new_parent_id =
                        new_parent_id_utf8 ? new_parent_id_utf8 : "",
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "scene node reparent post failed");
        }
    }

    WzResult wz_host_runtime_remove_node(
        WzHostRuntime* runtime,
        const char* node_id_utf8)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "node_id_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_remove(node_id_utf8);
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "scene node remove post failed");
        }
    }

    WzResult wz_host_runtime_save_scene(WzHostRuntime* runtime)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        runtime->control.request_save();
        return result(WZ_RESULT_OK, "");
    }

    WzResult wz_host_runtime_reload_behavior_modules(WzHostRuntime* runtime)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        runtime->control.request_reload_behavior_modules();
        return result(WZ_RESULT_OK, "");
    }

    WzResult wz_host_runtime_behavior_module_catalog(
        WzHostRuntime* runtime,
        WzBuffer* out_modules)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (const WzResult target =
                prepare_output_buffer(out_modules, "out_modules");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            // Newline-delimited registered module names (matches how behavior
            // event channels cross the ABI); the editor splits on '\n'. The
            // names come from the engine-thread-owned registry via the
            // control's published snapshot, so this read is lock-guarded and
            // never touches the registry directly.
            std::string joined;
            const std::vector<std::string> modules =
                runtime->control.behavior_modules();
            for (const std::string& module : modules) {
                if (!joined.empty()) {
                    joined.push_back('\n');
                }
                joined += module;
            }
            const std::vector<uint8_t> bytes(joined.begin(), joined.end());
            return copy_bytes_to_buffer(bytes, out_modules);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR, "behavior module catalog failed");
        }
    }

    WzResult wz_host_runtime_add_child_node(
        WzHostRuntime* runtime,
        const char* parent_id_utf8,
        WzBuffer* out_new_id)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (const WzResult target =
                prepare_output_buffer(out_new_id, "out_new_id");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const wz::engine::assets::SceneAddChildResult added =
                runtime->control.add_child(
                    parent_id_utf8 ? parent_id_utf8 : "");
            if (!added.ok) {
                return dynamic_error(WZ_RESULT_INVALID_ARGUMENT, added.error);
            }
            const std::vector<uint8_t> bytes(
                added.new_id.begin(), added.new_id.end());
            return copy_bytes_to_buffer(bytes, out_new_id);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(WZ_RESULT_INTERNAL_ERROR, "add child node failed");
        }
    }

    WzResult wz_host_runtime_add_node_behavior(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* module_utf8,
        WzBuffer* out_binding_id)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!module_utf8 || module_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "module_utf8 must not be empty");
        }
        if (const WzResult target =
                prepare_output_buffer(out_binding_id, "out_binding_id");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            // Blocking handshake (mirrors add_child): the host needs the minted
            // id back. Safe because the caller is the host UI thread, not a
            // behavior on the engine thread.
            const wz::engine::assets::SceneAddBehaviorResult added =
                runtime->control.add_node_behavior(node_id_utf8, module_utf8);
            if (!added.ok) {
                return dynamic_error(WZ_RESULT_INVALID_ARGUMENT, added.error);
            }
            const std::vector<uint8_t> bytes(
                added.binding_id.begin(), added.binding_id.end());
            return copy_bytes_to_buffer(bytes, out_binding_id);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(WZ_RESULT_INTERNAL_ERROR, "add node behavior failed");
        }
    }

    WzResult wz_host_runtime_remove_node_behavior(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* binding_id_utf8)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!binding_id_utf8 || binding_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "binding_id_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_behavior(
                wz::app::SceneNodeBehaviorEdit{
                    .op = wz::app::SceneNodeBehaviorEdit::Op::Remove,
                    .node_id = node_id_utf8,
                    .binding_id = binding_id_utf8,
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR, "remove node behavior post failed");
        }
    }

    WzResult wz_host_runtime_add_node_component(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* kind_utf8)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!kind_utf8 || kind_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "kind_utf8 must not be empty");
        }
        // Fail closed on an unrecognized kind up front (the engine-thread apply
        // would no-op anyway, but rejecting here gives the host a precise error).
        if (!wz::engine::assets::is_optional_component_kind(kind_utf8)) {
            return dynamic_error(
                WZ_RESULT_INVALID_ARGUMENT,
                std::string("unknown component kind '") + kind_utf8
                    + "' (expected camera|proximity|collision|motion)");
        }

        try {
            runtime->control.post_scene_node_component(
                wz::app::SceneNodeComponentEdit{
                    .op = wz::app::SceneNodeComponentEdit::Op::Add,
                    .node_id = node_id_utf8,
                    .kind = kind_utf8,
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR, "add node component post failed");
        }
    }

    WzResult wz_host_runtime_remove_node_component(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* kind_utf8)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!kind_utf8 || kind_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "kind_utf8 must not be empty");
        }
        if (!wz::engine::assets::is_optional_component_kind(kind_utf8)) {
            return dynamic_error(
                WZ_RESULT_INVALID_ARGUMENT,
                std::string("unknown component kind '") + kind_utf8
                    + "' (expected camera|proximity|collision|motion)");
        }

        try {
            runtime->control.post_scene_node_component(
                wz::app::SceneNodeComponentEdit{
                    .op = wz::app::SceneNodeComponentEdit::Op::Remove,
                    .node_id = node_id_utf8,
                    .kind = kind_utf8,
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR, "remove node component post failed");
        }
    }

    WzResult wz_host_runtime_set_node_renderable_asset(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        uint64_t asset_graph_node_id)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_renderable(
                wz::app::SceneNodeRenderableEdit{
                    .node_id = node_id_utf8,
                    .asset_graph_node_id = asset_graph_node_id,
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "set node renderable asset post failed");
        }
    }

    WzResult wz_host_runtime_set_node_behavior_enabled(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* binding_id_utf8,
        uint32_t enabled)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!binding_id_utf8 || binding_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "binding_id_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_behavior(
                wz::app::SceneNodeBehaviorEdit{
                    .op = wz::app::SceneNodeBehaviorEdit::Op::SetEnabled,
                    .node_id = node_id_utf8,
                    .binding_id = binding_id_utf8,
                    .enabled = enabled != 0u,
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "set node behavior enabled post failed");
        }
    }

    WzResult wz_host_runtime_set_node_behavior_fields(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* binding_id_utf8,
        const char* label_utf8,
        const char* module_utf8)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!binding_id_utf8 || binding_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "binding_id_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_behavior(
                wz::app::SceneNodeBehaviorEdit{
                    .op = wz::app::SceneNodeBehaviorEdit::Op::SetFields,
                    .node_id = node_id_utf8,
                    .binding_id = binding_id_utf8,
                    .label = label_utf8 ? label_utf8 : "",
                    .module = module_utf8 ? module_utf8 : "",
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "set node behavior fields post failed");
        }
    }

    WzResult wz_host_runtime_set_node_behavior_events(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* binding_id_utf8,
        const char* events_utf8)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!binding_id_utf8 || binding_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "binding_id_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_behavior(
                wz::app::SceneNodeBehaviorEdit{
                    .op = wz::app::SceneNodeBehaviorEdit::Op::SetEvents,
                    .node_id = node_id_utf8,
                    .binding_id = binding_id_utf8,
                    .events = parse_newline_delimited(events_utf8),
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "set node behavior events post failed");
        }
    }

    WzResult wz_host_runtime_set_node_behavior_config(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* binding_id_utf8,
        const char* key_utf8,
        const char* kind_utf8,
        const char* value_utf8)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!binding_id_utf8 || binding_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "binding_id_utf8 must not be empty");
        }
        if (!key_utf8 || key_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "key_utf8 must not be empty");
        }

        try {
            wz::engine::assets::SceneBehaviorConfigValue value;
            if (!parse_behavior_config_value(
                    key_utf8, kind_utf8, value_utf8, value))
            {
                return result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "kind_utf8 must be bool|int|float|string");
            }

            runtime->control.post_scene_node_behavior(
                wz::app::SceneNodeBehaviorEdit{
                    .op = wz::app::SceneNodeBehaviorEdit::Op::SetConfig,
                    .node_id = node_id_utf8,
                    .binding_id = binding_id_utf8,
                    .config_value = std::move(value),
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "set node behavior config post failed");
        }
    }

    WzResult wz_host_runtime_clear_node_behavior_config(
        WzHostRuntime* runtime,
        const char* node_id_utf8,
        const char* binding_id_utf8,
        const char* key_utf8)
    {
        if (const WzResult gate = require_host_scene_authoring(runtime);
            gate.code != WZ_RESULT_OK)
        {
            return gate;
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "node_id_utf8 must not be empty");
        }
        if (!binding_id_utf8 || binding_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "binding_id_utf8 must not be empty");
        }
        if (!key_utf8 || key_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "key_utf8 must not be empty");
        }

        try {
            runtime->control.post_scene_node_behavior(
                wz::app::SceneNodeBehaviorEdit{
                    .op = wz::app::SceneNodeBehaviorEdit::Op::ClearConfig,
                    .node_id = node_id_utf8,
                    .binding_id = binding_id_utf8,
                    .config_key = key_utf8,
                });
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "clear node behavior config post failed");
        }
    }

    void wz_free_buffer(WzBuffer* buffer)
    {
        if (!buffer) {
            return;
        }
        std::free(buffer->data);
        buffer->data = nullptr;
        buffer->size = 0u;
    }
}
