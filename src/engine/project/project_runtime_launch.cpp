#include <engine/project/project_runtime_launch.h>

namespace wz::engine::project
{
    namespace
    {
        wz::fs::Path absolute_path(
            const wz::fs::Path& base,
            const wz::fs::Path& path)
        {
            return wz::fs::normalize(
                wz::fs::is_absolute(path) ? path : wz::fs::join(base, path));
        }
    }

    ProjectRuntimeLaunchResult load_project_runtime_launch(
        const ProjectRuntimeLaunchDesc& desc)
    {
        ProjectRuntimeLaunchResult result;

        const wz::fs::Path current_dir = wz::fs::current_directory();
        const wz::fs::Path resource_root = desc.resource_root.empty()
            ? current_dir
            : absolute_path(current_dir, desc.resource_root);
        const wz::fs::Path project_root =
            absolute_path(resource_root, desc.project_root);

        const ProjectManifestLoadResult loaded = load_project_manifest(
            ProjectManifestLoadDesc{
                .project_root = project_root,
                .resource_root = {},
            });
        if (!loaded.ok) {
            result.error = loaded.error;
            return result;
        }

        if (!loaded.manifest.rhi_render_path) {
            result.error = "project does not enable the RHI render path";
            return result;
        }
        if (loaded.manifest.asset_graph_path.empty()) {
            result.error = "project manifest does not define an asset graph";
            return result;
        }
        if (loaded.manifest.scene_path.empty()) {
            result.error = "project manifest does not define a scene";
            return result;
        }

        result.ok = true;
        result.launch.manifest = loaded.manifest;
        result.launch.resource_root = loaded.manifest.root;
        result.launch.asset_graph_path = loaded.manifest.asset_graph_path;
        result.launch.scene_path = loaded.manifest.scene_path;
        return result;
    }
}
