#pragma once

// engine/assets/scalar_field_asset_module.h

#include <asset/system.h>

#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/scalar_field/scalar_field.h>

namespace wz::engine::assets
{
    // Shell for the scalar field asset module.
    //
    // Scalar field creation and handle-retrieval APIs currently remain on
    // EngineAssetLibrary and will migrate here in a later phase.
    // This class exists now to establish the correct member layout and
    // initialization order inside EngineAssetLibrary, and to hold the
    // reference to ScalarFieldTable that will be needed when the API moves.

    class ScalarFieldAssetModule
    {
    public:
        ScalarFieldAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            FileCarrierAssetModule& files,
            ScalarFieldTable&       table
        );

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        FileCarrierAssetModule& files_;
        ScalarFieldTable&       table_;
    };

} // namespace wz::engine::assets
