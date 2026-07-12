#pragma once

#include <engine/assets/gltf/gltf_importer.h>
#include <engine/project/project_manifest.h>

#include <cstdint>
#include <string_view>
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

    // Serialize the device-free behavior-actuator catalog into the
    // WzEditorBehaviorActuatorCatalog ABI blob: the actuators a statechart `call`
    // effect can invoke (name/label + declared arg schema), from a throwaway
    // BehaviorRegistry populated by register_builtin_behaviors -- no device/session.
    std::vector<uint8_t> behavior_actuator_catalog_abi_blob();

    // Serialize a GLB scene-source hierarchy import (issue #213, Phase 3b-1) into
    // the WzEditorGlbSceneHierarchy ABI blob. `ok`/`error` carry the import outcome;
    // on failure pass ok=false with a default `scene` and the blob's component table
    // is empty. Each ImportedGLTFSceneNode maps to one WzEditorGlbComponent (the
    // HAS_PARENT/HAS_MESH flags come from the optionals; node_index is carried; the
    // `local` transform is intentionally not packed).
    std::vector<uint8_t> glb_scene_hierarchy_abi_blob(
        bool ok,
        std::string_view error,
        const wz::engine::assets::ImportedGLTFScene& scene);
}
