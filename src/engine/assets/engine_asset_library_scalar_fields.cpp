// src/engine/assets/engine_assets_library_scalar_fields.cpp

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/scalar_field.h>
#include <engine/assets/key_factories/scalar_field_procedural.h>


namespace wz::engine::assets
{

    ScalarFieldAsset EngineAssetLibrary::create_scalar_field(
        const ScalarFieldFileDesc& desc)
    {
        ScalarFieldAsset out{};

        // Register the raw file carrier node. This is a generic file-bytes node;
        // the scalar field compiler will interpret those bytes.
        const wz::asset::AssetKey file_key = register_file_node(
            desc.path,
            kRawFileSchema,
            kAssetTypeRawFile
        );

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register scalar field source file: " + desc.path);
            return out;
        }

        // Register the scalar field recipe node, depending on the file node.
        const ScalarFieldCompileDesc compile_desc{
            .width = desc.width,
            .height = desc.height,
            .depth = desc.depth,
            .format = desc.format,
            .domain_kind = desc.domain_kind,
        };

        const wz::asset::AssetKey field_key =
            register_scalar_field_node(file_key, compile_desc);

        if (field_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register scalar field node: " + desc.name);
            return out;
        }

        out.output = field_key;
        return out;
    }

    ScalarFieldHandle EngineAssetLibrary::get_scalar_field(
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

    const ScalarFieldData* EngineAssetLibrary::get_scalar_field_data(
        ScalarFieldHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return scalar_fields_.get(handle.handle);
    }

    wz::asset::AssetKey EngineAssetLibrary::register_scalar_field_node(
        wz::asset::AssetKey          source_file,
        const ScalarFieldCompileDesc& desc)
    {
        const wz::asset::AssetKey key = make_scalar_field_key(
            source_file,
            desc.width,
            desc.height,
            desc.depth,
            static_cast<uint8_t>(desc.format),
            static_cast<uint8_t>(desc.domain_kind) // new argument
        );

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeScalarField;
        node.schema = kScalarFieldFromRawF32Schema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = desc;

        if (!system_.register_asset(
            std::move(node),
            std::vector<wz::asset::AssetKey>{ source_file }))
        {
            logger_.error("failed to register scalar field node");
            return {};
        }

        return key;
    }
}