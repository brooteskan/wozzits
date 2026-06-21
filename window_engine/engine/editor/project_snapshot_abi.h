#pragma once

#include <engine/project/project_manifest.h>

#include <cstdint>
#include <vector>

namespace wz::engine::editor
{
    struct AssetGraphConnectionCheck;
    struct AssetGraphSnapshot;
    struct AssetGraphSnapshotLoadResult;
    struct ProjectSnapshotLoadResult;

    std::vector<uint8_t> project_snapshot_abi_blob(
        const ProjectSnapshotLoadResult& result);

    std::vector<uint8_t> asset_graph_snapshot_abi_blob(
        const AssetGraphSnapshotLoadResult& result);

    std::vector<uint8_t> asset_graph_snapshot_abi_blob(
        const AssetGraphSnapshot& snapshot);

    std::vector<uint8_t> asset_graph_connection_check_abi_blob(
        const AssetGraphConnectionCheck& check);

    std::vector<uint8_t> project_create_abi_blob(
        const wz::engine::project::ProjectManifestCreateResult& result);

    // Serialize the device-free authoring catalog (build_asset_catalog) into the
    // WzEditorAssetCatalog ABI blob.
    std::vector<uint8_t> asset_catalog_abi_blob();
}
