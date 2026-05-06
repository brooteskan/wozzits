#pragma once

// engine/assets/schema_ids.h

#include <engine/assets/engine_asset_key_core.h>

namespace wz::engine::assets {

    // ─── Schema ID constants ──────────────────────────────────────────────────────
    //
    // Each schema describes the format/interpretation a compiler expects.
    // Values are stable compile-time constants; do not change them once shipped
    // as that would silently invalidate persisted caches.

    // File carriers — raw bytes, no interpretation by the asset system.
    inline constexpr wz::asset::SchemaID kRawFileSchema{ 0xF11E'CA55'E7'000001ull };

    // HLSL shader source file, to be compiled by the HLSL compiler.
    inline constexpr wz::asset::SchemaID kHLSLFileSchema{ 0xF11E'CA55'E7'000002ull };

    // Compiled HLSL shader node (source file → GPU shader resource).
    inline constexpr wz::asset::SchemaID kHLSLShaderSchema{ 0xF11E'CA55'E7'000003ull };

    // Raw float32 scalar field file.
    inline constexpr wz::asset::SchemaID kScalarFieldRawF32Schema{ 0xF11E'CA55'E7'000004ull };

    // Gaussian splat cloud in PLY format.
    inline constexpr wz::asset::SchemaID kGaussianSplatPLYSchema{ 0xF11E'CA55'E7'000005ull };

    // Gaussian splat cloud generated from a scalar field.
    inline constexpr wz::asset::SchemaID kGaussianSplatFromFieldSchema{ 0xF11E'CA55'E7'000006ull };

}