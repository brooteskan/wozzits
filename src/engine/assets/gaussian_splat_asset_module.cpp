// src/engine/assets/gaussian_splat_asset_module.cpp

#include <engine/assets/gaussian_splat_asset_module.h>

#include <engine/assets/key_factories/gaussian_splat.h>
#include <engine/assets/key_factories/gaussian_splat_file.h>
#include <engine/assets/key_factories/gaussian_splat_from_scalar_field.h>
#include <engine/assets/key_factories/gaussian_splat_terrain_surface.h>
#include <engine/assets/key_factories/terrain_splat_from_gaea_r32.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    GaussianSplatAssetModule::GaussianSplatAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        GaussianSplatCloudTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    GaussianSplatCloudAsset GaussianSplatAssetModule::create_procedural_cloud(
        const ProceduralGaussianSplatCloudDesc& desc)
    {
        GaussianSplatCloudAsset out{};

        if (desc.count == 0) {
            logger_.error("procedural gaussian splat cloud has zero count: " + desc.name);
            return out;
        }

        if (desc.radius <= 0.0f) {
            logger_.error("procedural gaussian splat cloud has non-positive radius: " + desc.name);
            return out;
        }

        if (desc.splat_scale <= 0.0f) {
            logger_.error("procedural gaussian splat cloud has non-positive splat_scale: " + desc.name);
            return out;
        }

        const ProceduralGaussianSplatCloudCompileDesc compile_desc{
            .count = desc.count,
            .radius = desc.radius,
            .splat_scale = desc.splat_scale,
        };

        const wz::asset::AssetKey key =
            make_procedural_gaussian_splat_cloud_key(
                desc.name,
                compile_desc.count,
                compile_desc.radius,
                compile_desc.splat_scale);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeGaussianSplatCloud;
        node.schema = kProceduralGaussianSplatCloudSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), {})) {
            logger_.error("failed to register procedural gaussian splat cloud: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    GaussianSplatCloudAsset GaussianSplatAssetModule::create_from_ply(
        const GaussianSplatFromPLYDesc& desc)
    {
        GaussianSplatCloudAsset out{};

        if (desc.source_file == wz::asset::AssetKey{}) {
            logger_.error("PLY gaussian splat cloud has empty source file key: " + desc.name);
            return out;
        }

        const wz::asset::AssetKey key =
            make_gaussian_splat_from_ply_key(desc.source_file);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeGaussianSplatCloud;
        node.schema = kGaussianSplatFromPLYSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        if (!system_.register_asset(std::move(node), { desc.source_file })) {
            logger_.error("failed to register PLY gaussian splat cloud: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    GaussianSplatCloudAsset GaussianSplatAssetModule::create_from_scalar_field(
        const GaussianSplatFromScalarFieldDesc& desc)
    {
        GaussianSplatCloudAsset out{};

        if (desc.scalar_field_key == wz::asset::AssetKey{}) {
            logger_.error("gaussian splat from scalar field has empty scalar field key: " + desc.name);
            return out;
        }

        if (desc.splat_scale <= 0.0f) {
            logger_.error("gaussian splat from scalar field has non-positive splat_scale: " + desc.name);
            return out;
        }

        const GaussianSplatFromScalarFieldCompileDesc compile_desc{
            .height_scale     = desc.height_scale,
            .step_x           = desc.step_x,
            .step_z           = desc.step_z,
            .splat_scale      = desc.splat_scale,
            .opacity          = desc.opacity,
            .normalize_values = desc.normalize_values,
            .use_threshold    = desc.use_threshold,
            .emit_threshold   = desc.emit_threshold,
        };

        const wz::asset::AssetKey key =
            make_gaussian_splat_from_scalar_field_key(desc.scalar_field_key, compile_desc);

        wz::asset::AssetNode node{};
        node.key    = key;
        node.type   = kAssetTypeGaussianSplatCloud;
        node.schema = kGaussianSplatFromFieldSchema;
        node.stage  = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta   = compile_desc;

        if (!system_.register_asset(std::move(node), { desc.scalar_field_key })) {
            logger_.error("failed to register gaussian splat from scalar field: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    GaussianSplatCloudAsset
        GaussianSplatAssetModule::create_terrain_surface_from_height_field(
            const GaussianSplatTerrainSurfaceFromHeightFieldDesc& desc)
    {
        GaussianSplatCloudAsset out{};

        if (desc.scalar_field_key == wz::asset::AssetKey{}) {
            logger_.error(
                "terrain-surface splat: empty scalar field key: " + desc.name);
            return out;
        }
        if (desc.step_x <= 0.0f || desc.step_z <= 0.0f) {
            logger_.error(
                "terrain-surface splat: non-positive step_x or step_z: "
                + desc.name);
            return out;
        }
        if (desc.overlap_factor <= 0.0f) {
            logger_.error(
                "terrain-surface splat: non-positive overlap_factor: "
                + desc.name);
            return out;
        }

        const GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc compile_desc{
            .height_scale    = desc.height_scale,
            .step_x          = desc.step_x,
            .step_z          = desc.step_z,
            .overlap_factor  = desc.overlap_factor,
            .thickness       = desc.thickness,
            .subsample_step  = desc.subsample_step,
            .opacity         = desc.opacity,
            .flat_luminance  = desc.flat_luminance,
            .steep_luminance = desc.steep_luminance,
            .normal_smoothing_enabled      = desc.normal_smoothing_enabled,
            .normal_smoothing_radius_cells = desc.normal_smoothing_radius_cells,
            .normal_smoothing_sigma_cells  = desc.normal_smoothing_sigma_cells,
        };

        const wz::asset::AssetKey key =
            make_gaussian_splat_terrain_surface_from_height_field_key(
                desc.scalar_field_key, compile_desc);

        wz::asset::AssetNode node{};
        node.key     = key;
        node.type    = kAssetTypeGaussianSplatCloud;
        node.schema  = kGaussianSplatTerrainSurfaceFromHeightFieldSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = compile_desc;

        if (!system_.register_asset(
                std::move(node), { desc.scalar_field_key })) {
            logger_.error(
                "terrain-surface splat: failed to register: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    GaussianSplatCloudAsset
        GaussianSplatAssetModule::create_terrain_splat_from_gaea_r32(
            const TerrainSplatFromGaeaR32Desc& desc)
    {
        GaussianSplatCloudAsset out{};

        if (desc.r32_file_key == wz::asset::AssetKey{}) {
            logger_.error(
                "terrain-splat recipe: empty .r32 file key: " + desc.name);
            return out;
        }
        if (desc.sidecar_file_key == wz::asset::AssetKey{}) {
            logger_.error(
                "terrain-splat recipe: empty sidecar JSON file key: " + desc.name);
            return out;
        }

        const wz::asset::AssetKey key =
            make_terrain_splat_from_gaea_r32_key(
                desc.r32_file_key, desc.sidecar_file_key);

        wz::asset::AssetNode node{};
        node.key     = key;
        node.type    = kAssetTypeGaussianSplatCloud;
        node.schema  = kTerrainSplatFromGaeaR32Schema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        // No compile-desc meta — all parameters live in the JSON file dep.

        // Dep order is load-bearing: compiler reads dep[0] as .r32 bytes,
        // dep[1] as JSON sidecar bytes.
        if (!system_.register_asset(
                std::move(node),
                { desc.r32_file_key, desc.sidecar_file_key }))
        {
            logger_.error(
                "terrain-splat recipe: failed to register: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    GaussianSplatCloudHandle GaussianSplatAssetModule::get_cloud(
        const GaussianSplatCloudAsset& asset) const
    {
        if (!asset.valid())
            return {};

        GaussianSplatCloudHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("gaussian splat cloud handle not found");

        return out;
    }

    const GaussianSplatCloudData* GaussianSplatAssetModule::get_cloud_data(
        GaussianSplatCloudHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}