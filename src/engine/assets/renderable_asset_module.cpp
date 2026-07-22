// src/engine/assets/renderable_asset_module.cpp

#include <engine/assets/renderable_asset_module.h>

#include <engine/assets/key_factories/renderable.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    RenderableAssetModule::RenderableAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        RenderableAssetTable& table,
        RhiRenderableTable& rhi_table)
        : system_(system)
        , logger_(logger)
        , table_(table)
        , rhi_table_(rhi_table)
    {
    }

    RenderableAsset RenderableAssetModule::create_scalar_field_debug(
        const ScalarFieldDebugRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("scalar field debug renderable has empty name");
            return {};
        }

        if (!desc.scalar_field.valid()) {
            logger_.error("scalar field debug renderable has invalid scalar field: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_scalar_field_debug_renderable_key(
                desc.name,
                desc.scalar_field.output);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kScalarFieldDebugRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = ScalarFieldDebugRenderableCompileDesc{
            .scalar_field_asset = desc.scalar_field.output,
        };

        if (!system_.register_asset(std::move(node), { desc.scalar_field.output }))
            return RenderableAsset{ .output = key };

        return RenderableAsset{
            .output = key,
        };
    }

    RenderableAsset RenderableAssetModule::create_terrain_debug(
        const TerrainDebugRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("terrain debug renderable has empty name");
            return {};
        }

        if (!desc.terrain.valid()) {
            logger_.error("terrain debug renderable has invalid terrain: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_terrain_debug_renderable_key(
                desc.name,
                desc.terrain.output,
                desc.mesh_program,
                desc.mesh_policy_flags,
                desc.domain);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kTerrainDebugRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = TerrainDebugRenderableCompileDesc{
            .terrain_asset = desc.terrain.output,
            .mesh_program = desc.mesh_program,
            .domain = desc.domain,
            .mesh_policy_flags = desc.mesh_policy_flags,
        };

        if (!system_.register_asset(std::move(node), { desc.terrain.output }))
            return RenderableAsset{ .output = key };

        return RenderableAsset{
            .output = key,
        };
    }

    RenderableAsset RenderableAssetModule::create_terrain_surface(
        const TerrainSurfaceRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("terrain surface renderable has empty name");
            return {};
        }

        if (!desc.terrain.valid()) {
            logger_.error("terrain surface renderable has invalid terrain: "
                + desc.name);
            return {};
        }

        if (!desc.visual_proxy.valid()) {
            logger_.error("terrain surface renderable has invalid visual proxy: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_terrain_surface_renderable_key(
                desc.name,
                desc.terrain.output,
                desc.visual_proxy.output,
                desc.mesh_program,
                desc.mesh_policy_flags,
                desc.domain,
                desc.lighting,
                desc.target_pixels_per_triangle);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kTerrainSurfaceRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = TerrainSurfaceRenderableCompileDesc{
            .terrain_asset = desc.terrain.output,
            .visual_proxy_asset = desc.visual_proxy.output,
            .mesh_program = desc.mesh_program,
            .domain = desc.domain,
            .mesh_policy_flags = desc.mesh_policy_flags,
            .lighting = desc.lighting,
            .target_pixels_per_triangle =
                desc.target_pixels_per_triangle,
        };

        if (!system_.register_asset(
                std::move(node),
                { desc.terrain.output, desc.visual_proxy.output }))
        {
            return RenderableAsset{ .output = key };
        }

        return RenderableAsset{
            .output = key,
        };
    }

    RenderableAsset RenderableAssetModule::create_rhi_pull_mesh(
        const RhiPullMeshRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("RHI pull mesh renderable has empty name");
            return {};
        }

        if (!desc.mesh.valid()) {
            logger_.error("RHI pull mesh renderable has invalid mesh: "
                + desc.name);
            return {};
        }

        if (!desc.program.valid()) {
            logger_.error("RHI pull mesh renderable has invalid render program: "
                + desc.name);
            return {};
        }

        // Optional style (issue #195 slice A): folds into the key + is appended
        // as a 3rd graph dep in input-port order (geometry, program, style).
        const wz::asset::AssetKey style_key =
            desc.style.valid() ? desc.style.output : wz::asset::AssetKey{};

        const wz::asset::AssetKey key =
            make_rhi_pull_mesh_renderable_key(
                desc.name,
                desc.mesh.output,
                desc.program.key,
                style_key);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kRhiPullMeshRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = RhiPullMeshRenderableCompileDesc{
            .mesh_asset = desc.mesh.output,
            .render_program_asset = desc.program.key,
            .style_asset = style_key,
        };

        // Keep dep order in sync with the compiler's input ports:
        // geometry, program, then the optional style.
        std::vector<wz::asset::AssetKey> deps{
            desc.mesh.output, desc.program.key };
        if (!(style_key == wz::asset::AssetKey{})) {
            deps.push_back(style_key);
        }

        (void)system_.register_asset(std::move(node), std::move(deps));

        return RenderableAsset{ .output = key };
    }

    RenderableAsset
    RenderableAssetModule::create_gpu_sparse_mesh_renderable(
        const GpuSparseMeshRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("GPU sparse mesh renderable has empty name");
            return {};
        }

        if (!desc.sparse_mesh.valid()) {
            logger_.error(
                "GPU sparse mesh renderable has invalid sparse mesh: "
                + desc.name);
            return {};
        }

        if (!desc.program.valid()) {
            logger_.error(
                "GPU sparse mesh renderable has invalid render program: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_gpu_sparse_mesh_renderable_key(
                desc.name,
                desc.sparse_mesh.output,
                desc.program.key);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kGpuSparseMeshRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = GpuSparseMeshRenderableCompileDesc{
            .sparse_mesh_asset = desc.sparse_mesh.output,
            .render_program_asset = desc.program.key,
        };

        (void)system_.register_asset(
            std::move(node),
            { desc.sparse_mesh.output, desc.program.key });

        return RenderableAsset{ .output = key };
    }

    // create_clipmap_landscape (the 0x708 module API) was retired with the
    // schema (issue #234); the clipmap is authored as a 0x70A custom renderable.

    RenderableAsset
    RenderableAssetModule::create_gaussian_splat_cloud_rhi(
        const GaussianSplatCloudRhiRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error(
                "gaussian splat cloud RHI renderable has empty name");
            return {};
        }

        if (!desc.splat_cloud.valid()) {
            logger_.error(
                "gaussian splat cloud RHI renderable has invalid splat cloud: "
                + desc.name);
            return {};
        }

        if (!desc.program.valid()) {
            logger_.error(
                "gaussian splat cloud RHI renderable has invalid render "
                "program: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_gaussian_splat_cloud_rhi_renderable_key(
                desc.name,
                desc.splat_cloud.output,
                desc.program.key,
                desc.settings);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kGaussianSplatCloudRhiRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = GaussianSplatCloudRhiRenderableCompileDesc{
            .splat_cloud_asset = desc.splat_cloud.output,
            .render_program_asset = desc.program.key,
            .settings = desc.settings,
        };

        // Dependency order must match the compiler's input ports:
        // splat cloud, render program.
        (void)system_.register_asset(
            std::move(node),
            {
                desc.splat_cloud.output,
                desc.program.key,
            });

        return RenderableAsset{ .output = key };
    }

    RenderableAsset
    RenderableAssetModule::create_puppet_rhi(
        const PuppetRhiRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("puppet RHI renderable has empty name");
            return {};
        }

        if (!desc.puppet.valid()) {
            logger_.error(
                "puppet RHI renderable has invalid puppet: " + desc.name);
            return {};
        }

        if (!desc.program.valid()) {
            logger_.error(
                "puppet RHI renderable has invalid render program: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_puppet_rhi_renderable_key(
                desc.name,
                desc.puppet.output,
                desc.program.key);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kPuppetRhiRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = PuppetRhiRenderableCompileDesc{
            .puppet_asset = desc.puppet.output,
            .render_program_asset = desc.program.key,
        };

        // Dependency order must match the compiler's input ports:
        // puppet, render program.
        (void)system_.register_asset(
            std::move(node),
            {
                desc.puppet.output,
                desc.program.key,
            });

        return RenderableAsset{ .output = key };
    }

    RenderableAsset
    RenderableAssetModule::create_star_field_rhi(
        const StarFieldRhiRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("star field RHI renderable has empty name");
            return {};
        }

        if (!desc.star_catalog.valid()) {
            logger_.error(
                "star field RHI renderable has invalid star catalog: "
                + desc.name);
            return {};
        }

        if (!desc.program.valid()) {
            logger_.error(
                "star field RHI renderable has invalid render program: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_star_field_rhi_renderable_key(
                desc.name,
                desc.star_catalog.output,
                desc.program.key,
                desc.settings);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kStarFieldRhiRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = StarFieldRhiRenderableCompileDesc{
            .star_catalog_asset = desc.star_catalog.output,
            .render_program_asset = desc.program.key,
            .settings = desc.settings,
        };

        // Dependency order must match the compiler's input ports:
        // star catalog, render program.
        (void)system_.register_asset(
            std::move(node),
            {
                desc.star_catalog.output,
                desc.program.key,
            });

        return RenderableAsset{ .output = key };
    }

    RenderableAsset RenderableAssetModule::create_custom_renderable(
        const CustomRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("custom renderable has empty name");
            return {};
        }

        if (!desc.mesh.valid()) {
            logger_.error("custom renderable has invalid mesh: " + desc.name);
            return {};
        }

        if (!desc.program.valid()) {
            logger_.error(
                "custom renderable has invalid render program: " + desc.name);
            return {};
        }

        CustomRenderableCompileDesc compile_desc{};
        compile_desc.mesh_asset = desc.mesh.output;
        compile_desc.render_program_asset = desc.program.key;
        compile_desc.bindings = desc.bindings;
        compile_desc.constants = desc.constants;

        const wz::asset::AssetKey key =
            make_custom_renderable_key(desc.name, compile_desc);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kCustomRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        // Dependency order must match the key factory's deps fold: mesh,
        // program, then each WIRED binding source in authored order. An
        // unwired row stays desc-only (no graph edge) so its compile failure
        // names the semantic instead of tripping the dep resolver.
        std::vector<wz::asset::AssetKey> deps{
            desc.mesh.output, desc.program.key };
        for (const CustomRenderableCompileDesc::Binding& binding :
             compile_desc.bindings)
        {
            if (!(binding.asset == wz::asset::AssetKey{})) {
                deps.push_back(binding.asset);
            }
        }

        node.meta = std::move(compile_desc);

        (void)system_.register_asset(std::move(node), std::move(deps));

        return RenderableAsset{ .output = key };
    }

    namespace
    {
        // Renderable schemas whose compiled product is an RhiRenderableRecipe in
        // the RhiRenderableTable (not a legacy RenderableAssetData). Keeps
        // get_renderable() and get_rhi_renderable_recipe() in agreement.
        bool is_rhi_renderable_schema(wz::asset::SchemaID schema)
        {
            return schema == kRhiPullMeshRenderableSchema
                || schema == kGpuSparseMeshRenderableSchema
                || schema == kGaussianSplatCloudRhiRenderableSchema
                || schema == kStarFieldRhiRenderableSchema
                || schema == kPuppetRhiRenderableSchema
                || schema == kCustomRenderableSchema;
        }
    }

    RenderableHandle RenderableAssetModule::get_renderable(
        const RenderableAsset& asset) const
    {
        if (!asset.valid())
            return {};

        RenderableHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output)) {
            if (compiled->node
                && is_rhi_renderable_schema(compiled->node->schema))
            {
                return {};
            }
            out.handle = compiled->handle;
        }

        // No log here: this is a per-frame query from the renderer. A missing
        // renderable (e.g. an unresolved/broken graph node) is surfaced once via
        // the node's compile diagnostics, not spammed every frame (which floods
        // the editor log sink and can freeze the UI).
        return out;
    }

    const RenderableAssetData* RenderableAssetModule::get_renderable_data(
        RenderableHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

    const RhiRenderableRecipe*
    RenderableAssetModule::get_rhi_renderable_recipe(
        const RenderableAsset& asset) const
    {
        if (!asset.valid()) {
            return nullptr;
        }

        const auto* compiled = system_.find_compiled(asset.output);
        if (!compiled
            || !compiled->node
            || !is_rhi_renderable_schema(compiled->node->schema))
        {
            return nullptr;
        }

        return rhi_table_.get(compiled->handle);
    }
}
