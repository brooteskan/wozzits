// src/engine/assets/mesh_asset_module.cpp

#include <engine/assets/mesh_asset_module.h>

#include <engine/assets/key_factories/mesh.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    namespace
    {
        struct ProceduralMeshRecipe
        {
            wz::asset::AssetKey key{};
            wz::asset::SchemaID schema{};
        };

        ProceduralMeshRecipe make_procedural_mesh_recipe(
            ProceduralMeshKind kind) noexcept
        {
            switch (kind)
            {
            case ProceduralMeshKind::Triangle:
                return ProceduralMeshRecipe{
                    .key = make_procedural_triangle_mesh_key(),
                    .schema = kProceduralTriangleMeshSchema,
                };

            case ProceduralMeshKind::Quad:
                return ProceduralMeshRecipe{
                    .key = make_procedural_quad_mesh_key(),
                    .schema = kProceduralQuadMeshSchema,
                };

            case ProceduralMeshKind::Cube:
                return ProceduralMeshRecipe{
                    .key = make_procedural_cube_mesh_key(),
                    .schema = kProceduralCubeMeshSchema,
                };
            }

            return {};
        }
    }

    MeshAssetModule::MeshAssetModule(
        wz::asset::AssetSystem& system,
        MeshTable& table)
        : system_(system)
        , table_(table)
    {
    }

    MeshAsset MeshAssetModule::create_procedural_mesh(
        const ProceduralMeshDesc& desc)
    {
        const ProceduralMeshRecipe recipe =
            make_procedural_mesh_recipe(desc.kind);

        if (recipe.key == wz::asset::AssetKey{})
            return {};

        wz::asset::AssetNode node{};
        node.key = recipe.key;
        node.type = kAssetTypeMesh;
        node.schema = recipe.schema;
        node.stage = wz::asset::AssetStage::Source;

        if (!system_.register_asset(std::move(node), {}))
            return {};

        return MeshAsset{
            .output = recipe.key,
        };
    }

    MeshAsset MeshAssetModule::create_glb_mesh(
        const GLBMeshDesc& desc)
    {
        if (desc.source_file == wz::asset::AssetKey{})
            return {};

        const wz::asset::AssetKey mesh_key =
            make_glb_mesh_key(desc.source_file, desc.mesh_index);

        wz::asset::AssetNode node{};
        node.key = mesh_key;
        node.type = kAssetTypeMesh;
        node.schema = kGLBMeshSchema;
        node.stage = wz::asset::AssetStage::Source;

        if (!system_.register_asset(std::move(node), { desc.source_file }))
            return {};

        return MeshAsset{
            .output = mesh_key,
        };
    }

    MeshHandle MeshAssetModule::get_mesh(
        const MeshAsset& asset) const
    {
        if (!asset.valid())
            return {};

        MeshHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        return out;
    }

    const MeshData* MeshAssetModule::get_mesh_data(
        MeshHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}