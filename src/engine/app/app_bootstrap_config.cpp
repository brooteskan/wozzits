#include <engine/app/app_bootstrap_config.h>

#include <engine/project/project_manifest.h>  // resolve_project_authored_path

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <iterator>
#include <string>
#include <vector>

namespace wz::app
{
    namespace
    {
        // Minimal JSON string escaper, matching the project manifest writer.
        // Escapes the backslashes that Windows source paths carry.
        std::string json_string(const std::string& text)
        {
            std::string out;
            out.push_back('"');
            for (const char ch : text) {
                switch (ch) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(ch); break;
                }
            }
            out.push_back('"');
            return out;
        }

        std::string read_text_file(const wz::fs::Path& path)
        {
            // Through wz::fs so a non-ASCII path reads correctly (a narrow
            // std::ifstream(path) would misread it on Windows). {} on failure.
            const wz::fs::FileResult<std::string> result =
                wz::fs::read_file_text(path);
            return result ? result.value : std::string{};
        }

        // Resolve one authored config path against the bundle base dir, reusing
        // the manifest module's absolute-or-join rule so the two stay identical.
        wz::fs::Path resolve(
            const wz::fs::Path& base_dir,
            const wz::fs::Path& authored)
        {
            return wz::engine::project::resolve_project_authored_path(
                base_dir, authored);
        }
    }

    wz::fs::Path app_bootstrap_config_path(const wz::fs::Path& base_dir)
    {
        return wz::fs::join(base_dir, kAppBootstrapConfigFileName);
    }

    AppBootstrapConfigResult load_app_bootstrap_config(
        const wz::fs::Path& base_dir)
    {
        AppBootstrapConfigResult result;

        const wz::fs::Path config_path = app_bootstrap_config_path(base_dir);
        if (!wz::fs::exists(config_path)) {
            result.status = AppBootstrapConfigStatus::Missing;
            result.error = "app bootstrap config is missing: " + config_path;
            return result;
        }

        const std::string text = read_text_file(config_path);
        if (text.empty()) {
            result.status = AppBootstrapConfigStatus::Invalid;
            result.error = "cannot read app bootstrap config: " + config_path;
            return result;
        }

        const wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(text);
        if (!parsed.ok || !parsed.document.root) {
            result.status = AppBootstrapConfigStatus::Invalid;
            result.error = "invalid app bootstrap config json: " + config_path;
            return result;
        }

        const wz::json::JSONValue& root = *parsed.document.root;

        const auto schema = wz::json::read_string(root, "schema");
        if (!schema || *schema != kAppBootstrapConfigSchema) {
            result.status = AppBootstrapConfigStatus::Invalid;
            result.error = "unsupported app bootstrap config schema";
            return result;
        }
        result.config.schema = std::string(*schema);

        const auto format_version = wz::json::read_uint(root, "formatVersion");
        if (!format_version
            || *format_version != kAppBootstrapConfigFormatVersion)
        {
            result.status = AppBootstrapConfigStatus::Invalid;
            result.error = "unsupported app bootstrap config formatVersion";
            return result;
        }
        result.config.format_version = *format_version;

        // asset_graph + scene are required: a bundle cannot render without them,
        // and the failure should name the missing field rather than surface later
        // as an empty-path load error deep in the runtime.
        const auto asset_graph = wz::json::read_string(root, "asset_graph");
        if (!asset_graph || asset_graph->empty()) {
            result.status = AppBootstrapConfigStatus::Invalid;
            result.error =
                "app bootstrap config is missing required field: asset_graph";
            return result;
        }
        const auto scene = wz::json::read_string(root, "scene");
        if (!scene || scene->empty()) {
            result.status = AppBootstrapConfigStatus::Invalid;
            result.error =
                "app bootstrap config is missing required field: scene";
            return result;
        }
        result.config.asset_graph = resolve(base_dir, std::string(*asset_graph));
        result.config.scene = resolve(base_dir, std::string(*scene));

        // resource_root defaults to <base_dir>/resources (the bundle layout) when
        // the config omits it.
        if (const auto resource_root =
                wz::json::read_string(root, "resource_root");
            resource_root && !resource_root->empty())
        {
            result.config.resource_root =
                resolve(base_dir, std::string(*resource_root));
        }
        else {
            result.config.resource_root = resolve(base_dir, "resources");
        }

        // behavior_modules is optional; absent/empty => built-in behaviors only.
        if (const auto behavior_modules =
                wz::json::read_string(root, "behavior_modules");
            behavior_modules && !behavior_modules->empty())
        {
            result.config.behavior_modules =
                resolve(base_dir, std::string(*behavior_modules));
        }

        // Optional `cache` block (issue #295, Seam 2): the baked disk-cache the
        // bundle runs from. Absent block, or a `root` that is missing/empty,
        // leaves the cache off (root empty). `root` is resolved against base_dir
        // like every other path so the bundle stays relocatable; `sealed` is a
        // plain bool defaulting false. A malformed sub-field is treated as absent
        // (lenient, matching behavior_modules) rather than failing the whole
        // config.
        if (const wz::json::JSONValue* cache =
                wz::json::find_member(root, "cache"))
        {
            if (const auto cache_root = wz::json::read_string(*cache, "root");
                cache_root && !cache_root->empty())
            {
                result.config.cache.root =
                    resolve(base_dir, std::string(*cache_root));
            }
            if (const auto sealed = wz::json::read_bool(*cache, "sealed")) {
                result.config.cache.sealed = *sealed;
            }
        }

        // Optional behavior-ABI stamp (issue #295, Seam 3.5); absent => 0 (no
        // check). Read leniently like the other optional fields.
        if (const auto abi = wz::json::read_uint(root, "behavior_abi_version")) {
            result.config.behavior_abi_version = *abi;
        }

        result.status = AppBootstrapConfigStatus::Valid;
        return result;
    }

    std::string serialize_app_bootstrap_config(const AppBootstrapConfigDoc& doc)
    {
        // Collect the set members, then join with ",\n" — avoids trailing-comma
        // bookkeeping as optional fields drop out.
        std::vector<std::string> members;
        members.push_back(
            "  \"schema\": " + json_string(kAppBootstrapConfigSchema));
        members.push_back(
            "  \"formatVersion\": "
            + std::to_string(kAppBootstrapConfigFormatVersion));
        if (!doc.resource_root.empty()) {
            members.push_back(
                "  \"resource_root\": " + json_string(doc.resource_root));
        }
        members.push_back("  \"asset_graph\": " + json_string(doc.asset_graph));
        members.push_back("  \"scene\": " + json_string(doc.scene));
        if (!doc.behavior_modules.empty()) {
            members.push_back(
                "  \"behavior_modules\": " + json_string(doc.behavior_modules));
        }
        if (doc.behavior_abi_version != 0) {
            members.push_back(
                "  \"behavior_abi_version\": "
                + std::to_string(doc.behavior_abi_version));
        }
        if (!doc.cache_root.empty()) {
            members.push_back(
                "  \"cache\": {\n"
                "    \"root\": " + json_string(doc.cache_root) + ",\n"
                "    \"sealed\": "
                + std::string(doc.cache_sealed ? "true" : "false") + "\n"
                "  }");
        }

        std::string out = "{\n";
        for (size_t i = 0; i < members.size(); ++i) {
            out += members[i];
            out += (i + 1 < members.size()) ? ",\n" : "\n";
        }
        out += "}\n";
        return out;
    }

    bool write_app_bootstrap_config(
        const wz::fs::Path& base_dir,
        const AppBootstrapConfigDoc& doc,
        std::string& error)
    {
        const wz::fs::Path path = app_bootstrap_config_path(base_dir);
        // Through wz::fs: UTF-8-correct path + a checked, chunked write (the old
        // stream reported success before the filebuf flushed on destruction).
        const wz::fs::FileError err = wz::fs::write_file_text(
            path, serialize_app_bootstrap_config(doc));
        if (err != wz::fs::FileError::None) {
            error = "failed to write: " + path;
            return false;
        }
        return true;
    }
}
