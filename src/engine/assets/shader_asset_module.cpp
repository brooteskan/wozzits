// src/engine/assets/shader_asset_module.cpp

#include <engine/assets/shader_asset_module.h>

namespace wz::engine::assets
{

    ShaderAssetModule::ShaderAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        FileCarrierAssetModule& files)
        : system_(system)
        , logger_(logger)
        , files_(files)
    {
    }

} // namespace wz::engine::assets
