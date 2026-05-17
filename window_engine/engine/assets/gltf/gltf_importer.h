#pragma once

#include <engine/assets/mesh/mesh.h>

#include <cstddef>
#include <cstdint>
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

    struct GLTFImportOptions
    {
        bool import_normals = true;
        bool import_uv0 = true;
        bool generate_missing_indices = true;
        bool reject_non_triangles = true;
    };

    bool import_glb_meshes(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFImportOptions& options,
        ImportedGLTFMeshSet& out);
}