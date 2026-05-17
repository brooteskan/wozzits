#include <gtest/gtest.h>

#include <engine/assets/gltf/gltf_importer.h>

#include <string>

TEST(GLTFImporter, RejectsEmptyInput)
{
    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    EXPECT_FALSE(wz::engine::assets::import_glb_meshes(nullptr, 0, options, out));
    EXPECT_TRUE(out.meshes.empty());
}