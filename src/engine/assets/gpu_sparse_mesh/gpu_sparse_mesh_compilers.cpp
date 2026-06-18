// src/engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.cpp

#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/gpu_sparse_mesh_asset_module.h>
#include <engine/assets/key_factories/mesh_sparse_operator.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        struct GpuPosition3
        {
            float position[3] = {};
        };

        void copy_mesh_bounds(
            float dst_min[3],
            float dst_max[3],
            const MeshData& mesh)
        {
            for (int axis = 0; axis < 3; ++axis) {
                dst_min[axis] = mesh.vertices[0].position[axis];
                dst_max[axis] = mesh.vertices[0].position[axis];
            }

            for (const MeshVertex& vertex : mesh.vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    dst_min[axis] =
                        (std::min)(dst_min[axis], vertex.position[axis]);
                    dst_max[axis] =
                        (std::max)(dst_max[axis], vertex.position[axis]);
                }
            }
        }

        constexpr std::string_view kOperatorKindOptions[] = {
            "Uniform adjacency",
        };

        constexpr std::string_view kDomainOptions[] = {
            "Vertex",
            "Edge",
            "Face",
            "Corner",
        };

        GpuSparseMeshDesc gpu_sparse_mesh_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> deps)
        {
            GpuSparseMeshDesc desc{};
            if (!deps.empty()) {
                desc.source_mesh = MeshAsset{ .output = deps[0].key };
            }
            desc.name = params.get<std::string>("name", {});
            desc.operator_kind =
                static_cast<MeshSparseOperatorKind>(
                    params.get<int64_t>(
                        "operator_kind",
                        static_cast<int64_t>(
                            MeshSparseOperatorKind::UniformAdjacency)));
            desc.operator_domain =
                static_cast<MeshOperatorDomain>(
                    params.get<int64_t>(
                        "operator_domain",
                        static_cast<int64_t>(MeshOperatorDomain::Vertex)));
            return desc;
        }

        bool publish_resident_gpu_sparse_mesh(
            const wz::asset::AssetKey& sparse_mesh_key,
            const GpuSparseMeshData& data,
            const MeshData& mesh,
            MeshFieldComputeBackend& compute,
            GpuResidentSparseMeshTable& resident_table,
            wz::Logger& logger)
        {
            if (!compute.available() || !data.valid()) {
                return false;
            }

            std::vector<GpuPosition3> positions;
            positions.reserve(mesh.vertices.size());
            for (const MeshVertex& vertex : mesh.vertices) {
                GpuPosition3 out{};
                out.position[0] = vertex.position[0];
                out.position[1] = vertex.position[1];
                out.position[2] = vertex.position[2];
                positions.push_back(out);
            }

            std::vector<uint32_t> source_vertices(mesh.vertex_count());
            for (uint32_t i = 0; i < mesh.vertex_count(); ++i) {
                source_vertices[i] = i;
            }

            const auto started = std::chrono::steady_clock::now();
            const wz::asset::ResourceHandle positions_buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes = sizeof(GpuPosition3),
                    .initial_data = positions.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            positions.size() * sizeof(GpuPosition3)),
                });
            const wz::asset::ResourceHandle indices_buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.index_count(),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = mesh.indices.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            mesh.indices.size() * sizeof(uint32_t)),
                });
            const wz::asset::ResourceHandle source_vertices_buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes = sizeof(uint32_t),
                    .initial_data = source_vertices.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            source_vertices.size() * sizeof(uint32_t)),
                });

            if (!positions_buffer.valid()
                || !indices_buffer.valid()
                || !source_vertices_buffer.valid())
            {
                for (wz::asset::ResourceHandle handle : {
                    positions_buffer,
                    indices_buffer,
                    source_vertices_buffer })
                {
                    if (handle.valid()) {
                        compute.release_buffer(handle);
                    }
                }
                logger.warn("GPU sparse mesh resident upload failed");
                return false;
            }

            GpuResidentSparseMeshEntry* entry =
                resident_table.find_or_add(sparse_mesh_key);
            if (!entry) {
                compute.release_buffer(positions_buffer);
                compute.release_buffer(indices_buffer);
                compute.release_buffer(source_vertices_buffer);
                return false;
            }

            for (wz::asset::ResourceHandle* handle : {
                &entry->positions,
                &entry->indices,
                &entry->source_vertices })
            {
                if (handle->valid()) {
                    compute.release_buffer(*handle);
                    *handle = {};
                }
            }

            entry->source_mesh_key = data.source_mesh_key;
            entry->sparse_operator_key = data.sparse_operator_key;
            entry->positions = positions_buffer;
            entry->indices = indices_buffer;
            entry->source_vertices = source_vertices_buffer;
            entry->vertex_count = data.vertex_count;
            entry->index_count = data.index_count;
            entry->source_triangle_count = data.source_triangle_count;

            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger.info(
                "asset compile: GPU sparse mesh resident upload vertices="
                + std::to_string(entry->vertex_count)
                + " triangles="
                + std::to_string(entry->source_triangle_count)
                + " upload_ms="
                + std::to_string(elapsed_ms));
            return entry->valid();
        }

        wz::asset::AssetNode compile_gpu_sparse_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshFieldComputeBackend& compute,
            MeshTable& mesh_table,
            GpuSparseMeshTable& gpu_sparse_mesh_table,
            GpuResidentSparseMeshTable& resident_sparse_mesh_table)
        {
            GpuSparseMeshDesc param_desc{};
            const auto* desc = std::any_cast<GpuSparseMeshDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        gpu_sparse_mesh_desc_from_params(*params, dep_nodes);
                    desc = &param_desc;
                }
            }
            if (!desc || dep_handles.empty()) {
                logger.error("GPU sparse mesh node missing desc");
                return compile_failed_node(input);
            }
            if (desc->operator_domain != MeshOperatorDomain::Vertex) {
                logger.error("GPU sparse mesh supports vertex operators in v0");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = nullptr;
            for (size_t i = 0; i < dep_handles.size(); ++i) {
                if (i < dep_nodes.size()
                    && dep_nodes[i].type != kAssetTypeMesh)
                {
                    continue;
                }
                source_mesh = mesh_table.get(dep_handles[i]);
                if (source_mesh) {
                    break;
                }
            }
            if (!source_mesh || !source_mesh->valid()) {
                logger.error("GPU sparse mesh source mesh handle invalid");
                return compile_failed_node(input);
            }

            MeshSparseOperatorDesc sparse_desc{
                .source_mesh = MeshAsset{ .output = desc->source_mesh.output },
                .kind = desc->operator_kind,
                .domain = desc->operator_domain,
            };
            GpuSparseMeshData data{
                .source_mesh_key = desc->source_mesh.output,
                .source_topology_hash =
                    compute_mesh_topology_hash(*source_mesh),
                .sparse_operator_key =
                    make_mesh_sparse_operator_key(
                        desc->source_mesh.output,
                        sparse_desc),
                .vertex_count = source_mesh->vertex_count(),
                .index_count = source_mesh->index_count(),
                .source_triangle_count = source_mesh->index_count() / 3u,
            };
            copy_mesh_bounds(data.bounds_min, data.bounds_max, *source_mesh);
            if (!data.valid()) {
                logger.error("GPU sparse mesh produced invalid data");
                return compile_failed_node(input);
            }

            (void)publish_resident_gpu_sparse_mesh(
                input.key,
                data,
                *source_mesh,
                compute,
                resident_sparse_mesh_table,
                logger);

            const wz::asset::ResourceHandle handle =
                gpu_sparse_mesh_table.add(std::move(data));
            if (!handle.valid()) {
                return compile_failed_node(input);
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }

    void register_gpu_sparse_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshFieldComputeBackend& compute,
        MeshTable& mesh_table,
        GpuSparseMeshTable& gpu_sparse_mesh_table,
        GpuResidentSparseMeshTable& resident_sparse_mesh_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGpuSparseMeshFromMeshSchema,
            .output_type = kAssetTypeGpuSparseMesh,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "operator_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Operator kind",
                    .default_num =
                        static_cast<double>(
                            MeshSparseOperatorKind::UniformAdjacency),
                    .options = kOperatorKindOptions,
                },
                {
                    .name = "operator_domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Operator domain",
                    .default_num =
                        static_cast<double>(MeshOperatorDomain::Vertex),
                    .options = kDomainOptions,
                },
            },
            .compile = [
                &logger,
                &compute,
                &mesh_table,
                &gpu_sparse_mesh_table,
                &resident_sparse_mesh_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_gpu_sparse_mesh_node(
                    input,
                    dep_nodes,
                    dep_handles,
                    logger,
                    compute,
                    mesh_table,
                    gpu_sparse_mesh_table,
                    resident_sparse_mesh_table);
            }
        });
    }
}
