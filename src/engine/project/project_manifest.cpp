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

        bool write_text_file(const wz::fs::Path& path, const std::string& text)
        {
            std::ofstream file(path, std::ios::binary);
            if (!file) {
                return false;
            }
            file << text;
            return file.good();
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

        std::string default_project_name(const wz::fs::Path& project_root)
        {
            const wz::fs::Path normalized = wz::fs::normalize(project_root);
            const std::string name = wz::fs::filename(normalized);
            return name.empty() ? "Wozzits Project" : name;
        }

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
                default:
                    out.push_back(ch);
                    break;
                }
            }
            out.push_back('"');
            return out;
        }

        std::string default_manifest_json(const ProjectManifestCreateDesc& desc)
        {
            const std::string name = desc.name.empty()
                ? default_project_name(desc.project_root)
                : desc.name;
            return std::string{
                "{\n"
                "  \"schema\": \""
            } + kProjectManifestSchema + "\",\n"
                "  \"formatVersion\": "
                + std::to_string(kProjectManifestFormatVersion) + ",\n"
                "  \"name\": " + json_string(name) + "\n"
                "}\n";
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

    ProjectManifestProbeResult probe_project_manifest(
        const ProjectManifestLoadDesc& desc)
    {
        ProjectManifestProbeResult result;
        result.manifest.root = desc.project_root;
        result.manifest.manifest_path = project_manifest_path(desc.project_root);

        if (desc.project_root.empty()) {
            result.status = ProjectManifestProbeStatus::Invalid;
            result.error = "project root is empty";
            return result;
        }

        const wz::fs::Path disk_manifest_path = resolve_resource_path(
            desc.resource_root,
            result.manifest.manifest_path);
        if (!wz::fs::exists(disk_manifest_path)) {
            result.status = ProjectManifestProbeStatus::Missing;
            result.error =
                "project manifest is missing: " + result.manifest.manifest_path;
            return result;
        }

        const ProjectManifestLoadResult loaded = load_project_manifest(desc);
        result.manifest = loaded.manifest;
        if (loaded.ok) {
            result.status = ProjectManifestProbeStatus::Valid;
        }
        else {
            result.status = ProjectManifestProbeStatus::Invalid;
            result.error = loaded.error;
        }
        return result;
    }

    ProjectManifestCreateResult create_project_manifest(
        const ProjectManifestCreateDesc& desc)
    {
        ProjectManifestCreateResult result;
        result.manifest.root = desc.project_root;
        result.manifest.manifest_path = project_manifest_path(desc.project_root);

        if (desc.project_root.empty()) {
            result.error = "project root is empty";
            return result;
        }

        const ProjectManifestLoadDesc load_desc{
            .project_root = desc.project_root,
            .resource_root = desc.resource_root,
        };
        const ProjectManifestProbeResult probed =
            probe_project_manifest(load_desc);
        if (probed.valid()) {
            result.ok = true;
            result.created = false;
            result.manifest = probed.manifest;
            return result;
        }
        if (!probed.missing()) {
            result.error = probed.error;
            return result;
        }

        const wz::fs::Path disk_project_root =
            resolve_resource_path(desc.resource_root, desc.project_root);
        const wz::fs::Path disk_manifest_path =
            resolve_resource_path(desc.resource_root, result.manifest.manifest_path);
        const wz::fs::Path disk_metadata_dir =
            wz::fs::parent_path(disk_manifest_path);

        if (wz::fs::create_directories(disk_project_root) != wz::fs::FileError::None
            || wz::fs::create_directories(disk_metadata_dir)
                != wz::fs::FileError::None)
        {
            result.error = "cannot create project metadata directory";
            return result;
        }

        if (!write_text_file(disk_manifest_path, default_manifest_json(desc))) {
            result.error =
                "cannot write project manifest: " + result.manifest.manifest_path;
            return result;
        }

        const ProjectManifestLoadResult loaded = load_project_manifest(load_desc);
        if (!loaded.ok) {
            result.error = loaded.error;
            return result;
        }

        result.ok = true;
        result.created = true;
        result.manifest = loaded.manifest;
        return result;
    }
}
