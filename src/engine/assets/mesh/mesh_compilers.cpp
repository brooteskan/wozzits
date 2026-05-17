// src/engine/assets/mesh/mesh_compilers.cpp

#include <engine/assets/mesh/mesh_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/mesh/procedural_mesh.h>
#include <engine/assets/gltf/gltf_importer.h>

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

            // V1 imports the first mesh only. The asset key already includes mesh_index,
            // but the compiler does not yet read per-node mesh_index metadata.
            MeshData data = std::move(imported.meshes[0].mesh);

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
    }

} // namespace wz::engine::assets::internal
