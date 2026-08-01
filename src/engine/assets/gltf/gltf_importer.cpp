// file: engine/assets/gltf/gltf_importer.cpp

#include <engine/assets/gltf/gltf_importer.h>

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <math/mat4.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        void write_vec3(float dst[3], const fastgltf::math::fvec3& v)
        {
            dst[0] = v.x();
            dst[1] = v.y();
            dst[2] = v.z();
        }

        void write_vec2(float dst[2], const fastgltf::math::fvec2& v)
        {
            dst[0] = v.x();
            dst[1] = v.y();
        }

        bool import_primitive(
            const fastgltf::Asset& asset,
            const fastgltf::Primitive& primitive,
            const GLTFImportOptions& options,
            MeshData& out_mesh)
        {
            if (options.reject_non_triangles && primitive.type != fastgltf::PrimitiveType::Triangles)
                return false;

            const auto* position_attribute = primitive.findAttribute("POSITION");
            if (position_attribute == primitive.attributes.end())
                return false;

            const auto& position_accessor = asset.accessors[position_attribute->accessorIndex];
            if (!position_accessor.bufferViewIndex.has_value())
                return false;

            out_mesh.topology = MeshPrimitiveTopology::TriangleList;
            out_mesh.index_format = MeshIndexFormat::UInt32;
            out_mesh.vertices.clear();
            out_mesh.indices.clear();

            out_mesh.vertices.resize(position_accessor.count);
            out_mesh.has_normals = false;
            out_mesh.has_uv0 = false;

            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset,
                position_accessor,
                [&](const fastgltf::math::fvec3& position, std::size_t index)
                {
                    write_vec3(out_mesh.vertices[index].position, position);
                });

            if (options.import_normals)
            {
                const auto* normal_attribute = primitive.findAttribute("NORMAL");
                if (normal_attribute != primitive.attributes.end())
                {
                    const auto& normal_accessor = asset.accessors[normal_attribute->accessorIndex];

                    if (normal_accessor.count != out_mesh.vertices.size())
                        return false;

                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                        asset,
                        normal_accessor,
                        [&](const fastgltf::math::fvec3& normal, std::size_t index)
                        {
                            write_vec3(out_mesh.vertices[index].normal, normal);
                        });
                    out_mesh.has_normals = true;
                }
            }

            if (options.import_uv0)
            {
                const auto* uv_attribute = primitive.findAttribute("TEXCOORD_0");
                if (uv_attribute != primitive.attributes.end())
                {
                    const auto& uv_accessor = asset.accessors[uv_attribute->accessorIndex];

                    if (uv_accessor.count != out_mesh.vertices.size())
                        return false;

                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                        asset,
                        uv_accessor,
                        [&](const fastgltf::math::fvec2& uv, std::size_t index)
                        {
                            write_vec2(out_mesh.vertices[index].uv, uv);
                        });
                    out_mesh.has_uv0 = true;
                }
            }

            if (!primitive.indicesAccessor.has_value())
                return false;

            const auto& index_accessor = asset.accessors[primitive.indicesAccessor.value()];
            if (!index_accessor.bufferViewIndex.has_value())
                return false;

            out_mesh.indices.resize(index_accessor.count);

            fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                asset,
                index_accessor,
                [&](std::uint32_t index_value, std::size_t index)
                {
                    out_mesh.indices[index] = index_value;
                });

            return out_mesh.valid();
        }

        fastgltf::Options make_fastgltf_options(const GLTFImportOptions& options)
        {
            fastgltf::Options result = fastgltf::Options::None;

            if (options.generate_missing_indices)
                result |= fastgltf::Options::GenerateMeshIndices;

            return result;
        }

        bool load_gltf_asset(
            const std::uint8_t* bytes,
            std::size_t byte_count,
            fastgltf::Options options,
            fastgltf::Asset& out)
        {
            if (bytes == nullptr || byte_count == 0)
                return false;

            auto data = fastgltf::GltfDataBuffer::FromBytes(
                reinterpret_cast<const std::byte*>(bytes),
                byte_count);
            if (data.error() != fastgltf::Error::None)
                return false;

            fastgltf::Parser parser;

            auto asset = parser.loadGltf(
                data.get(),
                std::filesystem::path{},
                options);

            if (asset.error() != fastgltf::Error::None)
                return false;

            out = std::move(asset.get());
            return true;
        }

        std::string node_id_for_index(
            const fastgltf::Asset& asset,
            std::size_t node_index)
        {
            if (node_index >= asset.nodes.size())
                return {};

            const auto& node = asset.nodes[node_index];
            if (!node.name.empty())
                return std::string(node.name);

            return "glb_node_" + std::to_string(node_index);
        }

        wz::math::Mat4 to_wz_mat4(const fastgltf::math::fmat4x4& m)
        {
            wz::math::Mat4 out = wz::math::Mat4::identity();
            const float* src = m.data();
            std::copy(src, src + 16, out.m);
            return out;
        }

        void write_authored_transform(
            AuthoredTransform& out,
            const fastgltf::TRS& trs)
        {
            out.translation[0] = trs.translation.x();
            out.translation[1] = trs.translation.y();
            out.translation[2] = trs.translation.z();
            out.rotation_quat[0] = trs.rotation.x();
            out.rotation_quat[1] = trs.rotation.y();
            out.rotation_quat[2] = trs.rotation.z();
            out.rotation_quat[3] = trs.rotation.w();
            out.scale[0] = trs.scale.x();
            out.scale[1] = trs.scale.y();
            out.scale[2] = trs.scale.z();
        }

        bool write_authored_transform(
            AuthoredTransform& out,
            const fastgltf::math::fmat4x4& matrix)
        {
            wz::math::Transform trs{};
            if (!wz::math::decompose_trs(to_wz_mat4(matrix), trs))
                return false;

            out.translation[0] = trs.position.x;
            out.translation[1] = trs.position.y;
            out.translation[2] = trs.position.z;
            out.rotation_quat[0] = trs.rotation.x;
            out.rotation_quat[1] = trs.rotation.y;
            out.rotation_quat[2] = trs.rotation.z;
            out.rotation_quat[3] = trs.rotation.w;
            out.scale[0] = trs.scale.x;
            out.scale[1] = trs.scale.y;
            out.scale[2] = trs.scale.z;
            return true;
        }

        bool write_authored_transform(
            AuthoredTransform& out,
            const fastgltf::Node& node)
        {
            if (const auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
                write_authored_transform(out, *trs);
                return true;
            }
            if (const auto* matrix =
                    std::get_if<fastgltf::math::fmat4x4>(&node.transform))
            {
                return write_authored_transform(out, *matrix);
            }
            return false;
        }

        void append_unique_mesh_index(std::vector<uint32_t>& out, uint32_t mesh)
        {
            if (std::find(out.begin(), out.end(), mesh) == out.end())
                out.push_back(mesh);
        }

        std::vector<uint32_t> make_flattened_mesh_indices(
            const fastgltf::Asset& asset)
        {
            std::vector<uint32_t> out;
            out.reserve(asset.meshes.size());

            uint32_t imported_mesh_index = 0;
            for (const auto& mesh : asset.meshes) {
                out.push_back(imported_mesh_index);
                imported_mesh_index += static_cast<uint32_t>(
                    mesh.primitives.size());
            }

            return out;
        }
    }

    bool import_glb_meshes(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFImportOptions& options,
        ImportedGLTFMeshSet& out)
    {
        out.meshes.clear();

        fastgltf::Asset asset;
        if (!load_gltf_asset(
                bytes,
                byte_count,
                make_fastgltf_options(options),
                asset))
        {
            return false;
        }

        for (const auto& source_mesh : asset.meshes)
        {
            for (std::size_t primitive_index = 0; primitive_index < source_mesh.primitives.size(); ++primitive_index)
            {
                const auto& primitive = source_mesh.primitives[primitive_index];

                ImportedGLTFMesh imported;
                imported.name = std::string(source_mesh.name);

                if (source_mesh.primitives.size() > 1)
                {
                    imported.name += ".primitive_";
                    imported.name += std::to_string(primitive_index);
                }

                if (!import_primitive(asset, primitive, options, imported.mesh))
                {
                    out.meshes.clear();
                    return false;
                }

                out.meshes.push_back(std::move(imported));
            }
        }

        return !out.meshes.empty();
    }

    bool import_gltf_scene(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFSceneImportOptions& options,
        ImportedGLTFScene& out,
        std::string* error)
    {
        out = {};

        auto fail = [&](std::string message)
        {
            if (error)
                *error = std::move(message);
            out = {};
            return false;
        };

        fastgltf::Asset asset;
        if (!load_gltf_asset(
                bytes,
                byte_count,
                fastgltf::Options::None,
                asset))
        {
            return fail("failed to parse glTF scene");
        }

        if (asset.scenes.empty())
            return fail("glTF asset has no scenes");

        std::size_t scene_index = 0;
        if (options.scene_index) {
            scene_index = *options.scene_index;
        }
        else if (asset.defaultScene.has_value()) {
            scene_index = *asset.defaultScene;
        }

        if (scene_index >= asset.scenes.size())
            return fail("glTF scene index is out of range");

        const auto& scene = asset.scenes[scene_index];
        out.scene_index = static_cast<uint32_t>(scene_index);
        out.name = scene.name.empty()
            ? "glb_scene_" + std::to_string(scene_index)
            : std::string(scene.name);

        std::unordered_map<std::size_t, std::string> ids;
        ids.reserve(asset.nodes.size());
        for (std::size_t i = 0; i < asset.nodes.size(); ++i)
            ids.emplace(i, node_id_for_index(asset, i));

        const auto flattened_mesh_indices =
            make_flattened_mesh_indices(asset);

        // glTF requires the node hierarchy to be a disjoint union of STRICT
        // TREES, and nothing upstream enforces it: fastgltf::validate does not
        // check node.children bounds or acyclicity, and the range test below is
        // satisfied on every pass of a cycle. Without this set, a file whose
        // node lists itself as its own child recursed forever -- measured as a
        // STATUS_STACK_OVERFLOW from a 79-byte .gltf (issue #310, A4-C18).
        //
        // That is not survivable: a stack overflow is not a catchable C++
        // exception, so the ABI's catch(...) cannot contain it, and this runs at
        // PROJECT LOAD via resolve_glb_scene_sources -- so one such GLB source
        // made the project permanently unopenable in the editor AND the
        // standalone app, with no route to opening it to repair it.
        //
        // Revisiting is an ERROR rather than a skip because a node reached twice
        // is malformed even when the graph is acyclic: today it would be emitted
        // twice under two different parents.
        std::vector<std::uint8_t> visited(asset.nodes.size(), 0u);

        auto visit_node =
            [&](std::size_t node_index,
                const std::optional<std::string>& parent_id,
                auto& self) -> bool
        {
            if (node_index >= asset.nodes.size())
                return fail("glTF scene references node out of range");

            if (visited[node_index]) {
                return fail(
                    "glTF node graph is not a tree: node "
                    + std::to_string(node_index)
                    + " is reachable more than once (cycle or shared child)");
            }
            visited[node_index] = 1u;

            const auto& node = asset.nodes[node_index];

            ImportedGLTFSceneNode imported{};
            imported.node_index = static_cast<uint32_t>(node_index);
            imported.id = ids[node_index];
            imported.name = std::string(node.name);
            imported.parent_id = parent_id;

            if (!write_authored_transform(imported.local, node)) {
                return fail(
                    "glTF node '" + imported.id
                    + "' has unsupported matrix transform");
            }

            if (node.meshIndex.has_value()) {
                const auto mesh_index = *node.meshIndex;
                if (mesh_index >= asset.meshes.size()) {
                    return fail(
                        "glTF node '" + imported.id
                        + "' references mesh out of range");
                }
                if (asset.meshes[mesh_index].primitives.size() != 1) {
                    return fail(
                        "glTF node '" + imported.id
                        + "' references a multi-primitive mesh, which is not "
                          "supported by GLB scene import yet");
                }
                imported.mesh_index = flattened_mesh_indices[mesh_index];
                append_unique_mesh_index(out.mesh_indices, *imported.mesh_index);
            }

            const std::string child_parent_id = imported.id;
            out.nodes.push_back(std::move(imported));

            for (const auto child : node.children) {
                if (!self(child, child_parent_id, self))
                    return false;
            }

            return true;
        };

        for (const auto root_node : scene.nodeIndices) {
            if (!visit_node(root_node, std::nullopt, visit_node))
                return false;
        }

        return true;
    }
}
