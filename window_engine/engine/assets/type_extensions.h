#pragma once
 
// engine/assets/type_extensions.h

#include <engine/assets/engine_asset_key_core.h>

namespace wz::engine::assets {

    // ─── Engine AssetType extensions ─────────────────────────────────────────────
    //
    // Start at 64 to give the generic library room to grow into [8, 63].
    // Cast through the underlying type so no change to the generic enum is needed.

    inline constexpr wz::asset::AssetType kAssetTypeRawFile =
        static_cast<wz::asset::AssetType>(64);

    inline constexpr wz::asset::AssetType kAssetTypeScalarField =
        static_cast<wz::asset::AssetType>(65);

    inline constexpr wz::asset::AssetType kAssetTypeGaussianSplatCloud =
        static_cast<wz::asset::AssetType>(66);

}