// file: engine/assets/gltf/gltf_importer.cpp

#include <engine/assets/gltf/gltf_importer.h>

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
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
    }

    bool import_glb_meshes(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        const GLTFImportOptions& options,
        ImportedGLTFMeshSet& out)
    {
        out.meshes.clear();

        if (bytes == nullptr || byte_count == 0)
            return false;

        auto data = fastgltf::GltfDataBuffer::FromBytes(
            reinterpret_cast<const std::byte*>(bytes),
            byte_count);
        if (data.error() != fastgltf::Error::None)
            return false;

        fastgltf::Parser parser;

        // Empty path is fine for self-contained GLB data.
        // Later, file-based glTF import can provide the source directory here.
        auto asset = parser.loadGltf(
            data.get(),
            std::filesystem::path{},
            make_fastgltf_options(options));

        if (asset.error() != fastgltf::Error::None)
            return false;

        for (const auto& source_mesh : asset->meshes)
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

                if (!import_primitive(asset.get(), primitive, options, imported.mesh))
                {
                    out.meshes.clear();
                    return false;
                }

                out.meshes.push_back(std::move(imported));
            }
        }

        return !out.meshes.empty();
    }
}