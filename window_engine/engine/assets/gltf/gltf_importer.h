#pragma once

// file: engine/assets/gltf/gltf_importer.h

#include <engine/assets/mesh/mesh.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct ImportedGLTFMesh
    {
        std::string name;
        MeshData mesh;
    };

    struct ImportedGLTFMeshSet
    {
        std::vector<ImportedGLTFMesh> meshes;
    };

    struct ImportedGLTFSceneNode
    {
        uint32_t node_index = 0;
        std::string id;
        std::string name;
        std::optional<std::string> parent_id;
        std::optional<uint32_t> mesh_index;
        AuthoredTransform local{};
    };

    struct ImportedGLTFScene
    {
        std::string name;
        uint32_t scene_index = 0;
        std::vector<ImportedGLTFSceneNode> nodes;
        std::vector<uint32_t> mesh_indices;
    };

    struct GLTFSceneImportOptions
    {
        std::optional<uint32_t> scene_index;
    };

    struct GLTFImportOptions
    {
        bool import_normals = true;
        bool import_uv0 = true;

        // For now, import only triangle-list mesh primitives.
        bool reject_non_triangles = true;

        // If true, ask fastgltf to synthesize indices for unindexed primitives.
        bool generate_missing_indices = true;
    };

    // ADVISORY VALIDATION (issue #310, A4-Q5, ruled 2026-08-03).
    //
    // Both entry points optionally report what fastgltf::validate() thinks of
    // the asset. `validation_note` is a DIAGNOSTIC channel, never a verdict:
    // a non-empty note NEVER changes the return value and never rejects a
    // file. The ruling is explicit -- "do not use validate to reject GLB files
    // that are not perfect examples; there is a large variation among existing
    // GLB files" -- so validate() applies the spec more strictly than the
    // real-world corpus honours, and gating on it would reject art that loads
    // and renders correctly today.
    //
    // If you find yourself writing `if (!note.empty()) return false;`, that is
    // the exact change this was ruled against. The safety checks that DO reject
    // live in resolve_accessor (A4-C15..C21) and are independent of this.
    //
    // Passing nullptr skips the validate() call entirely, so a caller with
    // nowhere to log pays nothing for it. The importer owns no logger by
    // design (same shape as the gpu layer's take_debug_messages): it hands the
    // string up to whoever has one.

    bool import_glb_meshes(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFImportOptions& options,
        ImportedGLTFMeshSet& out,
        std::string* validation_note = nullptr);

    bool import_gltf_scene(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFSceneImportOptions& options,
        ImportedGLTFScene& out,
        std::string* error = nullptr,
        std::string* validation_note = nullptr);
}
