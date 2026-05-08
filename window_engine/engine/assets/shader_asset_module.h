#pragma once

// engine/assets/shader_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <gpu/gpu_types.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>

#include <string>

namespace wz::engine::assets
{
    // ─── Shader pair types ────────────────────────────────────────────────────────

    struct ShaderPairDesc
    {
        std::string name;

        wz::fs::Path vertex_path;
        wz::fs::Path pixel_path;

        std::string vertex_entry = "main";
        std::string pixel_entry  = "main";

        std::string vertex_target = "vs_5_0";
        std::string pixel_target  = "ps_5_0";
    };

    struct ShaderPairAsset
    {
        wz::asset::AssetKey vertex_shader{};
        wz::asset::AssetKey pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader != wz::asset::AssetKey{} &&
                   pixel_shader  != wz::asset::AssetKey{};
        }
    };

    struct ShaderPairHandles
    {
        wz::gpu::GPUHandle vertex{};
        wz::gpu::GPUHandle pixel{};

        bool valid() const noexcept
        {
            return vertex.valid() && pixel.valid();
        }
    };


    // ─── ShaderAssetModule ────────────────────────────────────────────────────────

    class ShaderAssetModule
    {
    public:
        ShaderAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            FileCarrierAssetModule& files
        );

        ShaderPairAsset   create_shader_pair(const ShaderPairDesc& desc);
        ShaderPairHandles get_shader_pair(const ShaderPairAsset& asset) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        FileCarrierAssetModule& files_;
    };

} // namespace wz::engine::assets
