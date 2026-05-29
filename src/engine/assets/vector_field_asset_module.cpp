#include <engine/assets/vector_field_asset_module.h>

#include <engine/assets/key_factories/vector_field.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    VectorFieldAssetModule::VectorFieldAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        VectorFieldTable& table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , table_(table)
    {
    }

    VectorFieldAsset VectorFieldAssetModule::create_vector_field(
        const VectorFieldFileDesc& desc)
    {
        VectorFieldAsset out{};

        const wz::asset::AssetKey file_key = files_.register_file_node(
            desc.path, kRawFileSchema, kAssetTypeRawFile);

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register vector field source file: "
                + desc.path);
            return out;
        }

        const VectorFieldCompileDesc compile_desc{
            .width = desc.width,
            .height = desc.height,
            .depth = desc.depth,
            .components_per_channel = desc.components_per_channel,
            .channels = desc.channels,
            .format = desc.format,
            .domain_kind = desc.domain_kind,
        };

        const wz::asset::AssetKey field_key = make_vector_field_key(
            file_key,
            compile_desc.width,
            compile_desc.height,
            compile_desc.depth,
            compile_desc.components_per_channel,
            compile_desc.channels,
            static_cast<uint8_t>(compile_desc.format),
            static_cast<uint8_t>(compile_desc.domain_kind));

        wz::asset::AssetNode node;
        node.key = field_key;
        node.type = kAssetTypeVectorField;
        node.schema = kVectorFieldFromRawF32Schema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), { file_key }))
            return VectorFieldAsset{ .output = field_key };

        out.output = field_key;
        return out;
    }

    VectorFieldHandle VectorFieldAssetModule::get_vector_field(
        const VectorFieldAsset& asset) const
    {
        VectorFieldHandle out{};

        if (!asset.valid())
            return out;

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("vector field handle not found");

        return out;
    }

    const VectorFieldData* VectorFieldAssetModule::get_vector_field_data(
        VectorFieldHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets
