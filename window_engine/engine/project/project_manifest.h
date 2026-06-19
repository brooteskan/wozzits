#pragma once

#include <file/filesystem.h>

#include <cstdint>
#include <string>

namespace wz::engine::project
{
    inline constexpr const char* kProjectManifestRelativePath =
        ".wozzits/project.json";
    inline constexpr const char* kProjectManifestSchema = "wozzits.project.v1";
    inline constexpr uint32_t kProjectManifestFormatVersion = 1u;

    struct ProjectManifest
    {
        wz::fs::Path root;
        wz::fs::Path manifest_path;
        std::string schema;
        uint32_t format_version = 0;
        std::string name;
        bool rhi_render_path = false;

        // Authored paths resolved against root unless already absolute.
        wz::fs::Path scene_path;
        wz::fs::Path asset_graph_path;
        wz::fs::Path behavior_project_folder;
        wz::fs::Path behavior_module_folder;
    };

    struct ProjectManifestLoadDesc
    {
        // Project root may be resource-root-relative or absolute.
        wz::fs::Path project_root;

        // Resource root used to read a resource-root-relative manifest.
        wz::fs::Path resource_root;
    };

    struct ProjectManifestLoadResult
    {
        bool ok = false;
        ProjectManifest manifest;
        std::string error;
    };

    wz::fs::Path project_manifest_path(const wz::fs::Path& project_root);

    wz::fs::Path resolve_project_authored_path(
        const wz::fs::Path& project_root,
        const wz::fs::Path& authored_path);

    ProjectManifestLoadResult load_project_manifest(
        const ProjectManifestLoadDesc& desc);
}
