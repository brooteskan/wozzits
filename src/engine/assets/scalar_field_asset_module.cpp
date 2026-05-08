// src/engine/assets/scalar_field_asset_module.cpp

#include <engine/assets/scalar_field_asset_module.h>

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

} // namespace wz::engine::assets
