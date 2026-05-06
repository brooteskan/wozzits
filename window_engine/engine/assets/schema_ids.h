#pragma once

// engine/assets/schema_ids.h

#include <engine/assets/engine_asset_key_core.h>

namespace wz::engine::assets {

    // ─── Schema ID constants ──────────────────────────────────────────────────────
    //
    // Each schema identifies the compiler recipe a node participates in — it
    // describes the input contract the compiler expects, not the file format.
    //
    // Pattern:
    //   file/carrier node  → schema selects which file carrier compiler reads it
    //   compile/recipe node → schema selects which domain compiler processes it
    //
    // Values are stable compile-time constants; do not change them once shipped
    // as that would silently invalidate persisted caches.

    // Generic raw file carrier — bytes only, no domain interpretation.
    // Used as the source file node schema for any raw binary asset (e.g. .rawf32).
    inline constexpr wz::asset::SchemaID kRawFileSchema{ 0xF11E'CA55'E7'000001ull };

    // HLSL source file carrier — read by the HLSL file carrier compiler.
    inline constexpr wz::asset::SchemaID kHLSLFileSchema{ 0xF11E'CA55'E7'000002ull };

    // HLSL shader recipe — compiled by the HLSL shader compiler.
    // Expects a kHLSLFileSchema dependency carrying source bytes.
    inline constexpr wz::asset::SchemaID kHLSLShaderSchema{ 0xF11E'CA55'E7'000003ull };

    // Scalar field recipe: interpret a raw float32 file dependency as ScalarFieldData.
    // Compiled by the scalar field compiler; expects a kRawFileSchema dependency.
    // Multiple scalar field schemas may coexist for different recipe types
    // (e.g. kScalarFieldProceduralSchema, kScalarFieldFromWaveletBandSchema).
    // All produce kAssetTypeScalarField output.
    inline constexpr wz::asset::SchemaID kScalarFieldFromRawF32Schema{ 0xF11E'CA55'E7'000004ull };

    // Gaussian splat cloud loaded from a PLY file.
    inline constexpr wz::asset::SchemaID kGaussianSplatPLYSchema{ 0xF11E'CA55'E7'000005ull };

    // Gaussian splat cloud generated from a scalar field.
    inline constexpr wz::asset::SchemaID kGaussianSplatFromFieldSchema{ 0xF11E'CA55'E7'000006ull };

}
