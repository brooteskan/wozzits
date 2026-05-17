#include <engine/assets/gltf/gltf_importer.h>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

namespace wz::engine::assets
{
    bool import_glb_meshes(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFImportOptions& options,
        ImportedGLTFMeshSet& out)
    {
        (void)bytes;
        (void)byte_count;
        (void)options;
        out.meshes.clear();

        return false;
    }
}