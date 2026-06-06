// src/engine/assets/mesh/mesh_compilers.cpp

#include <engine/assets/mesh/mesh_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/mesh/procedural_mesh.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/mesh_processing/mesh_processing.h>

namespace wz::engine::assets::internal
{
    namespace
    {
        wz::asset::AssetNode compile_procedural_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshData(*make_mesh)())
        {
            if (!dep_nodes.empty()) {
                logger.error("procedural mesh node should not have dependencies");
                return compile_failed_node(input);
            }

            MeshData data = make_mesh();

            if (!data.valid()) {
                logger.error("procedural mesh builder produced invalid mesh data");
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(data));

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        wz::asset::AssetNode compile_glb_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            wz::Logger& logger,
            MeshTable& mesh_table)
        {
            if (dep_nodes.size() != 1) {
                logger.error("GLB mesh node should have exactly one file dependency");
                return compile_failed_node(input);
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

            if (!bytes || bytes->empty()) {
                logger.error("GLB mesh dependency did not provide file bytes");
                return compile_failed_node(input);
            }

            GLTFImportOptions options{};
            ImportedGLTFMeshSet imported{};

            if (!import_glb_meshes(
                bytes->data(),
                bytes->size(),
                options,
                imported)) {
                logger.error("failed to import GLB mesh");
                return compile_failed_node(input);
            }

            if (imported.meshes.empty()) {
                logger.error("GLB import produced no meshes");
                return compile_failed_node(input);
            }

            uint32_t mesh_index = 0;
            if (const auto* desc = std::any_cast<GLBMeshDesc>(&input.meta))
                mesh_index = desc->mesh_index;

            if (mesh_index >= imported.meshes.size()) {
                logger.error("GLB mesh_index is out of range");
                return compile_failed_node(input);
            }

            MeshData data = std::move(imported.meshes[mesh_index].mesh);

            if (!data.valid()) {
                logger.error("GLB importer produced invalid mesh data");
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(data));

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        wz::asset::AssetNode compile_decimated_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table)
        {
            const auto* desc =
                std::any_cast<MeshDecimationAssetDesc>(&input.meta);
            if (!desc) {
                logger.error("decimated mesh node missing compile desc");
                return compile_failed_node(input);
            }
            if (dep_handles.size() != 1u) {
                logger.error("decimated mesh node requires one mesh dependency");
                return compile_failed_node(input);
            }

            const MeshData* source = mesh_table.get(dep_handles[0]);
            if (!source || !source->valid()) {
                logger.error("decimated mesh source is invalid");
                return compile_failed_node(input);
            }

            const wz::engine::mesh_processing::MeshDecimationDesc
                processing_desc{
                    .target_vertex_count = desc->target_vertex_count,
                    .target_triangle_count = desc->target_triangle_count,
                    .target_ratio = desc->target_ratio,
                    .preserve_boundary = desc->preserve_boundary,
                    .aspect_ratio = desc->aspect_ratio,
                    .edge_length = desc->edge_length,
                    .max_valence = desc->max_valence,
                    .normal_deviation = desc->normal_deviation,
                    .hausdorff_error = desc->hausdorff_error,
                };

            wz::engine::mesh_processing::MeshProcessingResult result =
                wz::engine::mesh_processing::decimate_mesh(
                    *source,
                    processing_desc);
            if (!result.ok || !result.mesh.valid()) {
                logger.error(
                    result.error.empty()
                    ? "decimated mesh compiler produced invalid data"
                    : result.error.c_str());
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(result.mesh));
            if (!handle.valid()) {
                logger.error("failed to store decimated mesh");
                return compile_failed_node(input);
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

    } // anonymous namespace


    void register_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table)
    {
        // ── Procedural mesh compilers ─────────────────────────────────────────
        //
        // Dispatch on procedural mesh schemas.
        // These are CPU-side mesh assets: generated MeshData is stored in
        // MeshTable, and the compiled AssetNode stores the returned ResourceHandle.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralTriangleMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_procedural_mesh_node(
                    input, dep_nodes, logger, mesh_table, &make_triangle_mesh);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralQuadMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_procedural_mesh_node(
                    input, dep_nodes, logger, mesh_table, &make_quad_mesh);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralCubeMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_procedural_mesh_node(
                    input, dep_nodes, logger, mesh_table, &make_cube_mesh);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kPlaceholderMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_procedural_mesh_node(
                    input, dep_nodes, logger, mesh_table, &make_cube_mesh);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGLBMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_glb_mesh_node(
                    input, dep_nodes, logger, mesh_table);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshDecimationSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                return compile_decimated_mesh_node(
                    input, dep_handles, logger, mesh_table);
            }
            });
    }

} // namespace wz::engine::assets::internal
