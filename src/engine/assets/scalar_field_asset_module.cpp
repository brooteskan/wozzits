// src/engine/assets/scalar_field_asset_module.cpp

#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/scalar_field.h>
#include <engine/assets/key_factories/scalar_field_procedural.h>

#include <vector>

namespace wz::engine::assets
{

    ScalarFieldAssetModule::ScalarFieldAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        FileCarrierAssetModule& files,
        ScalarFieldTable&       table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , table_(table)
    {
    }

    ScalarFieldAsset ScalarFieldAssetModule::create_scalar_field(
        const ScalarFieldFileDesc& desc)
    {
        ScalarFieldAsset out{};

        const wz::asset::AssetKey file_key = files_.register_file_node(
            desc.path, kRawFileSchema, kAssetTypeRawFile);

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register scalar field source file: " + desc.path);
            return out;
        }

        // Inlined register_scalar_field_node:
        const ScalarFieldCompileDesc compile_desc{
            .width       = desc.width,
            .height      = desc.height,
            .depth       = desc.depth,
            .format      = desc.format,
            .domain_kind = desc.domain_kind,
        };

        const wz::asset::AssetKey field_key = make_scalar_field_key(
            file_key,
            compile_desc.width,
            compile_desc.height,
            compile_desc.depth,
            static_cast<uint8_t>(compile_desc.format),
            static_cast<uint8_t>(compile_desc.domain_kind)
        );

        wz::asset::AssetNode node;
        node.key     = field_key;
        node.type    = kAssetTypeScalarField;
        node.schema  = kScalarFieldFromRawF32Schema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = compile_desc;

        if (!system_.register_asset(std::move(node), { file_key })) {
            logger_.error("failed to register scalar field node: " + desc.name);
            return out;
        }

        out.output = field_key;
        return out;
    }

    ScalarFieldAsset ScalarFieldAssetModule::create_procedural_scalar_field(
        const ProceduralScalarFieldDesc& desc)
    {
        ScalarFieldAsset out{};

        const ProceduralScalarFieldCompileDesc compile_desc{
            .width       = desc.width,
            .height      = desc.height,
            .depth       = desc.depth,
            .generator   = desc.generator,
            .frequency   = desc.frequency,
            .amplitude   = desc.amplitude,
            .format      = desc.format,
            .domain_kind = desc.domain_kind,
        };

        // Inlined register_procedural_scalar_field_node:
        const wz::asset::AssetKey key = make_procedural_scalar_field_key(
            desc.name,
            compile_desc.width,
            compile_desc.height,
            compile_desc.depth,
            static_cast<uint8_t>(compile_desc.generator),
            compile_desc.frequency,
            compile_desc.amplitude,
            static_cast<uint8_t>(compile_desc.format),
            static_cast<uint8_t>(compile_desc.domain_kind)
        );

        wz::asset::AssetNode node;
        node.key     = key;
        node.type    = kAssetTypeScalarField;
        node.schema  = kScalarFieldProceduralSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = compile_desc;

        if (!system_.register_asset(std::move(node))) {
            logger_.error(
                "duplicate procedural scalar field key — "
                "name and parameters must be unique: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    ScalarFieldHandle ScalarFieldAssetModule::get_scalar_field(
        const ScalarFieldAsset& asset) const
    {
        ScalarFieldHandle out{};

        if (!asset.valid())
            return out;

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("scalar field handle not found");

        return out;
    }

    const ScalarFieldData* ScalarFieldAssetModule::get_scalar_field_data(
        ScalarFieldHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets
