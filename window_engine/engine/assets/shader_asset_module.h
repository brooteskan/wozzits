#pragma once

// engine/assets/shader_asset_module.h

#include <asset/system.h>

#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>

namespace wz::engine::assets
{
    // Shell for the shader asset module.
    //
    // Shader creation and handle-retrieval APIs currently remain on
    // EngineAssetLibrary and will migrate here in a later phase.
    // This class exists now to establish the correct member layout and
    // initialization order inside EngineAssetLibrary.

    class ShaderAssetModule
    {
    public:
        ShaderAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            FileCarrierAssetModule& files
        );

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        FileCarrierAssetModule& files_;
    };

} // namespace wz::engine::assets
