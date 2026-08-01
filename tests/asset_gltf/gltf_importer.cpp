// file: tests/asset/gltf_importer.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <engine/assets/gltf/gltf_importer.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>


#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <asset/types.h>


namespace
{
    std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};

        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(bytes.data()), size);

        return bytes;
    }

#ifndef WZ_TEST_FIXTURE_DIR
#define WZ_TEST_FIXTURE_DIR "tests/fixtures"
#endif

    std::filesystem::path fixture_path(const char* relative)
    {
        return std::filesystem::path(WZ_TEST_FIXTURE_DIR) / relative;
    }
}

TEST(GLTFImporter, RejectsEmptyInput)
{
    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    EXPECT_FALSE(wz::engine::assets::import_glb_meshes(nullptr, 0, options, out));
    EXPECT_TRUE(out.meshes.empty());
}

TEST(GLTFImporter, RejectsInvalidGLBBytes)
{
    const std::uint8_t bad_bytes[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    EXPECT_FALSE(wz::engine::assets::import_glb_meshes(
        bad_bytes,
        sizeof(bad_bytes),
        options,
        out));

    EXPECT_TRUE(out.meshes.empty());
}

TEST(GLTFImporter, ImportsCubeGLBAsMeshData)
{
    const auto path = fixture_path("gltf/low_poly_rock.glb");
    SCOPED_TRACE(path.string());

    const auto bytes = read_binary_file(path);
    ASSERT_FALSE(bytes.empty());

    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    ASSERT_TRUE(wz::engine::assets::import_glb_meshes(
        bytes.data(),
        bytes.size(),
        options,
        out));

    ASSERT_FALSE(out.meshes.empty());

    const auto& mesh = out.meshes[0].mesh;

    EXPECT_TRUE(mesh.valid());
    EXPECT_EQ(mesh.topology, wz::engine::assets::MeshPrimitiveTopology::TriangleList);
    EXPECT_EQ(mesh.index_format, wz::engine::assets::MeshIndexFormat::UInt32);

    EXPECT_GT(mesh.vertex_count(), 0u);
    EXPECT_GT(mesh.index_count(), 0u);
    EXPECT_EQ(mesh.index_count() % 3u, 0u);
    EXPECT_TRUE(mesh.has_normals);

    for (const auto index : mesh.indices)
        EXPECT_LT(index, mesh.vertex_count());
}

TEST(GLTFImporter, ImportsLowPolyRockGLBAsMeshData)
{
    const auto path = fixture_path("gltf/low_poly_rock.glb");
    SCOPED_TRACE(path.string());

    const auto bytes = read_binary_file(path);
    ASSERT_FALSE(bytes.empty());

    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    ASSERT_TRUE(wz::engine::assets::import_glb_meshes(
        bytes.data(),
        bytes.size(),
        options,
        out));

    ASSERT_FALSE(out.meshes.empty());

    const auto& mesh = out.meshes[0].mesh;

    EXPECT_TRUE(mesh.valid());
    EXPECT_EQ(mesh.topology, wz::engine::assets::MeshPrimitiveTopology::TriangleList);
    EXPECT_EQ(mesh.index_format, wz::engine::assets::MeshIndexFormat::UInt32);

    EXPECT_GT(mesh.vertex_count(), 100u);
    EXPECT_GT(mesh.index_count(), 300u);
    EXPECT_EQ(mesh.index_count() % 3u, 0u);
    EXPECT_TRUE(mesh.has_normals);

    for (const auto index : mesh.indices)
        EXPECT_LT(index, mesh.vertex_count());
}

TEST(GLTFImporter, ImportsSceneHierarchyAndTransforms)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [
    { "name": "tank_scene", "nodes": [0] }
  ],
  "nodes": [
    {
      "name": "tank_body",
      "translation": [1, 2, 3],
      "children": [1]
    },
    {
      "name": "turret",
      "translation": [0, 1, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [2, 2, 2]
    }
  ]
})";

    wz::engine::assets::ImportedGLTFScene out;
    std::string error;

    ASSERT_TRUE(wz::engine::assets::import_gltf_scene(
        reinterpret_cast<const std::uint8_t*>(gltf),
        std::strlen(gltf),
        wz::engine::assets::GLTFSceneImportOptions{},
        out,
        &error)) << error;

    ASSERT_EQ(out.nodes.size(), 2u);
    EXPECT_EQ(out.name, "tank_scene");

    EXPECT_EQ(out.nodes[0].id, "tank_body");
    EXPECT_FALSE(out.nodes[0].parent_id.has_value());
    EXPECT_FLOAT_EQ(out.nodes[0].local.translation[0], 1.0f);
    EXPECT_FLOAT_EQ(out.nodes[0].local.translation[1], 2.0f);
    EXPECT_FLOAT_EQ(out.nodes[0].local.translation[2], 3.0f);

    EXPECT_EQ(out.nodes[1].id, "turret");
    ASSERT_TRUE(out.nodes[1].parent_id.has_value());
    EXPECT_EQ(*out.nodes[1].parent_id, "tank_body");
    EXPECT_FLOAT_EQ(out.nodes[1].local.translation[1], 1.0f);
    EXPECT_FLOAT_EQ(out.nodes[1].local.scale[0], 2.0f);
    EXPECT_FLOAT_EQ(out.nodes[1].local.scale[1], 2.0f);
    EXPECT_FLOAT_EQ(out.nodes[1].local.scale[2], 2.0f);
}

TEST(GLTFImporter, RejectsUnsupportedSceneMatrixTransform)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [
    { "nodes": [0] }
  ],
  "nodes": [
    {
      "name": "sheared_node",
      "matrix": [
        1, 0, 0, 0,
        0.25, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
      ]
    }
  ]
})";

    wz::engine::assets::ImportedGLTFScene out;
    std::string error;

    EXPECT_FALSE(wz::engine::assets::import_gltf_scene(
        reinterpret_cast<const std::uint8_t*>(gltf),
        std::strlen(gltf),
        wz::engine::assets::GLTFSceneImportOptions{},
        out,
        &error));
    EXPECT_NE(error.find("sheared_node"), std::string::npos);
}

TEST(GLBMeshAsset, ImportsCubeThroughAssetGraph)
{
    wz::gpu::Device device{};
    wz::Logger logger{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        WZ_TEST_FIXTURE_DIR);

    const wz::asset::AssetKey file_key =
        assets.files().register_file_node(
            "gltf/cube.glb",
            wz::engine::assets::kRawFileSchema,
            wz::engine::assets::kAssetTypeRawFile);

    ASSERT_FALSE(file_key == wz::asset::AssetKey{});

    auto mesh = assets.meshes().create_glb_mesh({
        .name = "cube",
        .source_file = file_key,
        .mesh_index = 0,
    });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_TRUE(data->valid());
    EXPECT_GT(data->vertex_count(), 0u);
    EXPECT_GT(data->index_count(), 0u);
    EXPECT_EQ(data->index_count() % 3u, 0u);

    for (const auto index : data->indices)
        EXPECT_LT(index, data->vertex_count());
}

TEST(GLBMeshAsset, ImportsCubeThroughAssetGraphFromAbsolutePath)
{
    wz::gpu::Device device{};
    wz::Logger logger{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        WZ_TEST_FIXTURE_DIR);

    const wz::asset::AssetKey file_key =
        assets.files().register_file_node(
            wz::fs::join(WZ_TEST_FIXTURE_DIR, "gltf/cube.glb"),
            wz::engine::assets::kRawFileSchema,
            wz::engine::assets::kAssetTypeRawFile);

    ASSERT_FALSE(file_key == wz::asset::AssetKey{});

    auto mesh = assets.meshes().create_glb_mesh({
        .name = "cube_absolute_path",
        .source_file = file_key,
        .mesh_index = 0,
    });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_TRUE(data->valid());
    EXPECT_GT(data->vertex_count(), 0u);
    EXPECT_GT(data->index_count(), 0u);
    EXPECT_EQ(data->index_count() % 3u, 0u);
}

TEST(GLBMeshAsset, UsesProjectDiskCache)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_glb_mesh_disk_cache_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path cache_root =
        wz::fs::join(root, ".wozzits/cache");

    wz::gpu::Device device{};
    wz::Logger logger{};

    auto resolve_mesh = [&]() -> wz::engine::assets::MeshData
    {
        wz::engine::assets::EngineAssetLibrary assets{
            device,
            logger,
            WZ_TEST_FIXTURE_DIR,
            wz::engine::assets::EngineAssetCacheSettings{
                .root = cache_root,
                .enabled = true,
            },
        };

        const wz::asset::AssetKey file_key =
            assets.files().register_file_node(
                "gltf/cube.glb",
                wz::engine::assets::kRawFileSchema,
                wz::engine::assets::kAssetTypeRawFile);
        EXPECT_FALSE(file_key == wz::asset::AssetKey{});

        const auto mesh = assets.meshes().create_glb_mesh({
            .name = "cube_cached",
            .source_file = file_key,
            .mesh_index = 0,
        });
        EXPECT_TRUE(mesh.valid());

        EXPECT_TRUE(assets.commit());
        const auto report = assets.resolve_all();
        EXPECT_TRUE(report.ok());

        const auto handle = assets.meshes().get_mesh(mesh);
        EXPECT_TRUE(handle.valid());
        const auto* data = assets.meshes().get_mesh_data(handle);
        EXPECT_NE(data, nullptr);
        return data ? *data : wz::engine::assets::MeshData{};
    };

    const wz::engine::assets::MeshData first = resolve_mesh();
    ASSERT_TRUE(first.valid());

    const wz::fs::Path cache_directory =
        wz::fs::join(wz::fs::join(cache_root, "assets"), "glb_mesh");
    const auto entries = wz::fs::list_directory(cache_directory);
    ASSERT_EQ(entries.error, wz::fs::FileError::None);
    EXPECT_FALSE(entries.value.empty());

    const wz::engine::assets::MeshData second = resolve_mesh();
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.vertex_count(), first.vertex_count());
    EXPECT_EQ(second.index_count(), first.index_count());
    EXPECT_EQ(second.has_normals, first.has_normals);
    EXPECT_EQ(second.has_uv0, first.has_uv0);
}

// --------------------------------------------------------------------------
// Hostile scene graphs (issue #310, A4-C18).
//
// glTF requires the node hierarchy to be a disjoint union of STRICT TREES, and
// nothing upstream enforced it: fastgltf::validate checks neither node.children
// bounds nor acyclicity, and the importer's own range check is satisfied on
// every pass of a cycle. The walk recursed forever.
//
// Measured before the fix: both files below produced STATUS_STACK_OVERFLOW
// (0xC00000FD) from under 100 bytes. That is not a recoverable failure -- a
// stack overflow is not a catchable C++ exception, so the ABI's catch(...)
// cannot contain it -- and this path runs at PROJECT LOAD via
// resolve_glb_scene_sources, so one such GLB source made the project
// permanently unopenable in both the editor and the standalone app.
//
// These two are the regression pin: with the visited set removed they do not
// fail, they CRASH THE TEST RUNNER, which is a louder signal than a red test.
// --------------------------------------------------------------------------

TEST(GLTFImporter, RejectsSelfReferencingSceneNode)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "children": [0] } ]
})";

    wz::engine::assets::ImportedGLTFScene out;
    std::string error;

    EXPECT_FALSE(wz::engine::assets::import_gltf_scene(
        reinterpret_cast<const std::uint8_t*>(gltf),
        std::strlen(gltf),
        wz::engine::assets::GLTFSceneImportOptions{},
        out,
        &error));
    EXPECT_FALSE(error.empty());
}

TEST(GLTFImporter, RejectsCyclicSceneNodeGraph)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "children": [1] }, { "children": [0] } ]
})";

    wz::engine::assets::ImportedGLTFScene out;
    std::string error;

    EXPECT_FALSE(wz::engine::assets::import_gltf_scene(
        reinterpret_cast<const std::uint8_t*>(gltf),
        std::strlen(gltf),
        wz::engine::assets::GLTFSceneImportOptions{},
        out,
        &error));
    EXPECT_FALSE(error.empty());
}

// A node reached twice is malformed even when the graph is ACYCLIC -- before
// the fix this emitted the shared node twice under two different parents, which
// is a quieter form of the same defect and is why revisiting is an error rather
// than a skip.
TEST(GLTFImporter, RejectsSharedChildNodeInSceneGraph)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "a", "children": [2] },
    { "name": "b", "children": [2] },
    { "name": "shared" }
  ]
})";

    wz::engine::assets::ImportedGLTFScene out;
    std::string error;

    EXPECT_FALSE(wz::engine::assets::import_gltf_scene(
        reinterpret_cast<const std::uint8_t*>(gltf),
        std::strlen(gltf),
        wz::engine::assets::GLTFSceneImportOptions{},
        out,
        &error));
    EXPECT_FALSE(error.empty());
}

// --------------------------------------------------------------------------
// Hostile GLB mesh payloads (issue #310, A4-C16 / A4-C17 / A4-C19 / A4-C21).
//
// fastgltf defers ALL of this to fastgltf::validate(), which this importer does
// not call -- and even validate() does not check that an accessor's byte range
// fits its bufferView, or the bufferView its buffer. Everything below is a
// number taken straight from the JSON that nothing was checking.
//
// Each case was revert-checked by neutering checked_accessor() in place:
//   hugecount   -> ACCESS_VIOLATION (0xC0000005)
//   wrongtype   -> Debug assert in fastgltf's tools.hpp; clang-release sets
//                  NDEBUG, so in a shipping build this is the OOB read instead
//   externaluri -> Debug assert on an unloaded buffer; in release, a read at
//                  nullptr + the JSON's byteOffset
// so these are pins on real behaviour, not on a guard's opinion of itself.
// --------------------------------------------------------------------------

namespace
{
    void put_u32_le(std::vector<std::uint8_t>& v, std::uint32_t x)
    {
        v.push_back(static_cast<std::uint8_t>(x));
        v.push_back(static_cast<std::uint8_t>(x >> 8));
        v.push_back(static_cast<std::uint8_t>(x >> 16));
        v.push_back(static_cast<std::uint8_t>(x >> 24));
    }

    std::vector<std::uint8_t> make_glb(std::string json,
                                       std::vector<std::uint8_t> bin)
    {
        while (json.size() % 4 != 0) json.push_back(' ');
        while (bin.size() % 4 != 0)  bin.push_back(0);

        std::vector<std::uint8_t> out;
        put_u32_le(out, 0x46546C67u);   // 'glTF'
        put_u32_le(out, 2u);
        put_u32_le(out, 0u);            // total length, patched below

        put_u32_le(out, static_cast<std::uint32_t>(json.size()));
        put_u32_le(out, 0x4E4F534Au);   // 'JSON'
        out.insert(out.end(), json.begin(), json.end());

        put_u32_le(out, static_cast<std::uint32_t>(bin.size()));
        put_u32_le(out, 0x004E4942u);   // 'BIN\0'
        out.insert(out.end(), bin.begin(), bin.end());

        const std::uint32_t total = static_cast<std::uint32_t>(out.size());
        std::memcpy(out.data() + 8, &total, 4);
        return out;
    }

    // One triangle: 3 vertices x 3 floats, then 3 uint32 indices.
    std::vector<std::uint8_t> triangle_bin()
    {
        std::vector<std::uint8_t> b;
        for (const float f : { 0.f,0.f,0.f, 1.f,0.f,0.f, 0.f,1.f,0.f }) {
            std::uint8_t t[4];
            std::memcpy(t, &f, 4);
            b.insert(b.end(), t, t + 4);
        }
        for (const std::uint32_t i : { 0u, 1u, 2u }) {
            std::uint8_t t[4];
            std::memcpy(t, &i, 4);
            b.insert(b.end(), t, t + 4);
        }
        return b;
    }

    bool import_glb(const std::vector<std::uint8_t>& glb,
                    wz::engine::assets::ImportedGLTFMeshSet& out)
    {
        return wz::engine::assets::import_glb_meshes(
            glb.data(), glb.size(),
            wz::engine::assets::GLTFImportOptions{},
            out);
    }
}

// Control: the hand-built container itself is well-formed and imports, so a
// rejection below is about the payload rather than the harness.
TEST(GLTFImporter, ImportsHandBuiltTriangleGLB)
{
    const char* json = R"({"asset":{"version":"2.0"},
"buffers":[{"byteLength":48}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
               {"buffer":0,"byteOffset":36,"byteLength":12}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
             {"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";

    wz::engine::assets::ImportedGLTFMeshSet out;
    ASSERT_TRUE(import_glb(make_glb(json, triangle_bin()), out));
    ASSERT_EQ(out.meshes.size(), 1u);
    EXPECT_EQ(out.meshes[0].mesh.vertex_count(), 3u);
    EXPECT_EQ(out.meshes[0].mesh.index_count(), 3u);
}

TEST(GLTFImporter, RejectsAccessorCountLargerThanItsBufferView)
{
    // 12 real bytes; the accessor claims 10,000,000 VEC3 elements. Before the
    // fix this resized to 10M vertices and read ~120 MB past a 12-byte buffer.
    const char* json = R"({"asset":{"version":"2.0"},
"buffers":[{"byteLength":12}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":12}],
"accessors":[{"bufferView":0,"componentType":5126,"count":10000000,"type":"VEC3"},
             {"bufferView":0,"componentType":5125,"count":3,"type":"SCALAR"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";

    wz::engine::assets::ImportedGLTFMeshSet out;
    EXPECT_FALSE(import_glb(make_glb(json, std::vector<std::uint8_t>(12u, 0u)), out));
}

TEST(GLTFImporter, RejectsAccessorTypeThatDisagreesWithTheElementRead)
{
    // POSITION declared SCALAR (stride 4) but iterated as fvec3 (12 bytes per
    // element), so every read runs 8 bytes past its step.
    const char* json = R"({"asset":{"version":"2.0"},
"buffers":[{"byteLength":48}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
               {"buffer":0,"byteOffset":36,"byteLength":12}],
"accessors":[{"bufferView":0,"componentType":5126,"count":9,"type":"SCALAR"},
             {"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";

    wz::engine::assets::ImportedGLTFMeshSet out;
    EXPECT_FALSE(import_glb(make_glb(json, triangle_bin()), out));
}

TEST(GLTFImporter, RejectsTriangleIndexPastTheVertexCount)
{
    // The index is in bounds of its own bufferView and nonsense as a vertex
    // reference. MeshData::valid() checks only non-empty and count % 3, so this
    // was accepted here and dereferenced downstream.
    std::vector<std::uint8_t> bin = triangle_bin();
    const std::uint32_t huge = 0xFFFFFFFFu;
    std::memcpy(bin.data() + 36 + 8, &huge, 4);

    const char* json = R"({"asset":{"version":"2.0"},
"buffers":[{"byteLength":48}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
               {"buffer":0,"byteOffset":36,"byteLength":12}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
             {"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";

    wz::engine::assets::ImportedGLTFMeshSet out;
    EXPECT_FALSE(import_glb(make_glb(json, bin), out));
}

TEST(GLTFImporter, RejectsBufferWhoseDataWasNeverLoaded)
{
    // Declining to open an external URI is correct. Continuing as though we had
    // is not: the adapter's empty-span fallback was then subspan'd at the
    // JSON's byteOffset, giving a read at nullptr + an attacker-chosen offset.
    const char* json = R"({"asset":{"version":"2.0"},
"buffers":[{"uri":"data.bin","byteLength":4096}],
"bufferViews":[{"buffer":0,"byteOffset":140737488355328,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":0}]}]})";

    wz::engine::assets::ImportedGLTFMeshSet out;
    EXPECT_FALSE(import_glb(make_glb(json, {}), out));
}
