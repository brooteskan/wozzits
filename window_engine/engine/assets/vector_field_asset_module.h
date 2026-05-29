#pragma once

// engine/assets/vector_field_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/vector_field/vector_field.h>

namespace wz::engine::assets
{
    struct VectorFieldFileDesc
    {
        std::string name;
        wz::fs::Path path;

        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;

        uint32_t components_per_channel = 0;
        std::vector<VectorFieldChannelDesc> channels;

        VectorFieldFormat format = VectorFieldFormat::Float32;
        VectorFieldDomainKind domain_kind = VectorFieldDomainKind::Spatial2D;
    };

    struct VectorFieldAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct VectorFieldHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class VectorFieldAssetModule
    {
    public:
        VectorFieldAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            FileCarrierAssetModule& files,
            VectorFieldTable& table);

        VectorFieldAsset create_vector_field(const VectorFieldFileDesc& desc);

        VectorFieldHandle get_vector_field(const VectorFieldAsset& asset) const;

        const VectorFieldData* get_vector_field_data(
            VectorFieldHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        FileCarrierAssetModule& files_;
        VectorFieldTable& table_;
    };

} // namespace wz::engine::assets
