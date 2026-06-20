#pragma once

#include <engine/project/project_manifest.h>

#include <string>

namespace wz::engine::project
{
    struct ProjectRuntimeLaunchDesc
    {
        wz::fs::Path project_root;
        wz::fs::Path resource_root;
    };

    struct ProjectRuntimeLaunch
    {
        ProjectManifest manifest;
        wz::fs::Path resource_root;
        wz::fs::Path asset_graph_path;
        wz::fs::Path scene_path;
    };

    struct ProjectRuntimeLaunchResult
    {
        bool ok = false;
        ProjectRuntimeLaunch launch;
        std::string error;
    };

    ProjectRuntimeLaunchResult load_project_runtime_launch(
        const ProjectRuntimeLaunchDesc& desc);
}
