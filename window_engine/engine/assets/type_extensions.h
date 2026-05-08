#pragma once
 
// engine/assets/type_extensions.h

#include <engine/assets/engine_asset_key_core.h>

namespace wz::engine::assets {

    // ─── Engine AssetType extensions ─────────────────────────────────────────────
    //
    // Cast through the underlying type so no change to the generic enum is needed.
    //
    // Numeric range allocation:
    //
    //    [0,   7]   Reserved — generic wz::asset::AssetType enum values (Unknown–Font,
    //               ShaderSource). Do not add engine values here.
    //    [8,  63]   Reserved for future growth of the generic library.
    //   [64, 127]   Engine asset types (this file) — CPU-side and intermediate forms.
    //               Examples: scalar fields, mesh CPU data, texture CPU data, splat clouds.
    //  [128, 191]   Renderer/backend asset types — GPU-resident resources.
    //               Examples: GPU mesh buffers, compiled GPU textures, pipeline state objects.
    //  [192, 255]   Game/project-level asset types — scene prefabs, level data, etc.
    //  [256+    ]   Not currently allocated; extend when ranges are exhausted.

    inline constexpr wz::asset::AssetType kAssetTypeRawFile =
        static_cast<wz::asset::AssetType>(64);

    inline constexpr wz::asset::AssetType kAssetTypeScalarField =
        static_cast<wz::asset::AssetType>(65);

    inline constexpr wz::asset::AssetType kAssetTypeGaussianSplatCloud =
        static_cast<wz::asset::AssetType>(66);

}