#include <engine/editor/project_snapshot.h>

namespace wz::engine::editor
{
    ProjectSnapshotLoadResult load_project_snapshot(
        const wz::engine::project::ProjectManifestLoadDesc& desc)
    {
        ProjectSnapshotLoadResult result;

        const auto probe =
            wz::engine::project::probe_project_manifest(desc);
        result.status = probe.status;
        if (!probe.valid()) {
            result.error = probe.error;
            return result;
        }

        result.ok = true;
        result.project_name = probe.manifest.name;
        result.asset_graph = load_project_asset_graph_snapshot(desc);
        result.scene = load_project_scene_snapshot(desc);
        return result;
    }
}
