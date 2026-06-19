#include <engine/project/project_manifest.h>

#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace wz::engine::project
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

        wz::fs::Path resolve_resource_path(
            const wz::fs::Path& resource_root,
            const wz::fs::Path& path)
        {
            return wz::fs::is_absolute(path) || resource_root.empty()
                ? path
                : wz::fs::join(resource_root, path);
        }

        void read_optional_project_path(
            const wz::json::JSONValue& root,
            std::string_view key,
            const wz::fs::Path& project_root,
            wz::fs::Path& out)
        {
            if (const auto value = wz::json::read_string(root, key)) {
                out = resolve_project_authored_path(
                    project_root,
                    std::string(*value));
            }
        }
    }

    wz::fs::Path project_manifest_path(const wz::fs::Path& project_root)
    {
        return wz::fs::join(
            wz::fs::join(project_root, ".wozzits"),
            "project.json");
    }

    wz::fs::Path resolve_project_authored_path(
        const wz::fs::Path& project_root,
        const wz::fs::Path& authored_path)
    {
        return wz::fs::is_absolute(authored_path)
            ? authored_path
            : wz::fs::join(project_root, authored_path);
    }

    ProjectManifestLoadResult load_project_manifest(
        const ProjectManifestLoadDesc& desc)
    {
        ProjectManifestLoadResult result;
        result.manifest.root = desc.project_root;
        result.manifest.manifest_path =
            project_manifest_path(desc.project_root);

        if (desc.project_root.empty()) {
            result.error = "project root is empty";
            return result;
        }

        const wz::fs::Path disk_manifest_path = resolve_resource_path(
            desc.resource_root,
            result.manifest.manifest_path);
        const std::string text = read_text_file(disk_manifest_path);
        if (text.empty()) {
            result.error =
                "cannot read project manifest: " + result.manifest.manifest_path;
            return result;
        }

        const wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(text);
        if (!parsed.ok || !parsed.document.root) {
            result.error = "invalid project manifest json";
            return result;
        }

        const wz::json::JSONValue& root = *parsed.document.root;
        const auto schema = wz::json::read_string(root, "schema");
        if (!schema || *schema != kProjectManifestSchema) {
            result.error = "unsupported project manifest schema";
            return result;
        }
        result.manifest.schema = std::string(*schema);

        const auto format_version = wz::json::read_uint(root, "formatVersion");
        if (!format_version
            || *format_version != kProjectManifestFormatVersion)
        {
            result.error = "unsupported project manifest formatVersion";
            return result;
        }
        result.manifest.format_version = *format_version;

        if (const auto name = wz::json::read_string(root, "name")) {
            result.manifest.name = std::string(*name);
        }
        result.manifest.rhi_render_path =
            wz::json::read_bool(root, "rhi_render_path").value_or(false);

        read_optional_project_path(
            root, "scene", desc.project_root, result.manifest.scene_path);
        read_optional_project_path(
            root,
            "asset_graph",
            desc.project_root,
            result.manifest.asset_graph_path);
        read_optional_project_path(
            root,
            "behavior_project_folder",
            desc.project_root,
            result.manifest.behavior_project_folder);
        read_optional_project_path(
            root,
            "behavior_module_folder",
            desc.project_root,
            result.manifest.behavior_module_folder);

        result.ok = true;
        return result;
    }
}
