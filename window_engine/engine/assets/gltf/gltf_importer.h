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

    bool import_glb_meshes(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFImportOptions& options,
        ImportedGLTFMeshSet& out);

    bool import_gltf_scene(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFSceneImportOptions& options,
        ImportedGLTFScene& out,
        std::string* error = nullptr);
}
