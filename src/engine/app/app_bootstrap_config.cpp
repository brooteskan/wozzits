#include <engine/app/app_bootstrap_config.h>

#include <engine/project/project_manifest.h>  // resolve_project_authored_path

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <fstream>
#include <iterator>
#include <string>

namespace wz::app
{
    namespace
    {
        std::string read_text_file(const wz::fs::Path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return {};
            }

            return std::string(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
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

        result.status = AppBootstrapConfigStatus::Valid;
        return result;
    }
}
