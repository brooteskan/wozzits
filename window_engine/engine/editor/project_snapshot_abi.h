#pragma once

#include <engine/project/project_manifest.h>

#include <cstdint>
#include <vector>

namespace wz::engine::editor
{
    struct ProjectSnapshotLoadResult;

    std::vector<uint8_t> project_snapshot_abi_blob(
        const ProjectSnapshotLoadResult& result);

    std::vector<uint8_t> project_create_abi_blob(
        const wz::engine::project::ProjectManifestCreateResult& result);
}
