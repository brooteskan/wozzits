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
    //   file/carrier node   → schema selects which file carrier compiler reads it
    //   compile/recipe node → schema selects which domain compiler processes it
    //
    // Values are stable compile-time constants; do not change them once shipped
    // as that would silently invalidate persisted caches.
    //
    // Numeric range allocation (low 24 bits of the uint64 value):
    //
    //   0x000001 – 0x0000FF   File carriers (raw bytes, no domain interpretation)
    //   0x000100 – 0x0001FF   Shaders and GPU pipelines
    //   0x000200 – 0x0002FF   Scalar fields (file-backed and procedural)
    //   0x000300 – 0x0003FF   Textures
    //   0x000400 – 0x0004FF   Meshes
    //   0x000500 – 0x0005FF   Gaussian splat clouds
    //   0x000600+             Reserved for future asset families
    //
    // The high bits (0xF11ECA55E7______) are a fixed magic prefix to reduce
    // collision probability with any external schema registries.

    // Generic raw file carrier — bytes only, no domain interpretation.
    // Used as the source file node schema for any raw binary asset (e.g. .rawf32).
    inline constexpr wz::asset::SchemaID kRawFileSchema{ 0xF11E'CA55'E7'000001ull };

    // HLSL source file carrier — read by the HLSL file carrier compiler.
    inline constexpr wz::asset::SchemaID kHLSLFileSchema{ 0xF11E'CA55'E7'000002ull };

    // HLSL shader recipe — compiled by the HLSL shader compiler.
    // Expects a kHLSLFileSchema dependency carrying source bytes.
    inline constexpr wz::asset::SchemaID kHLSLShaderSchema{ 0xF11E'CA55'E7'000100ull };

    // Scalar field recipe: interpret a raw float32 file dependency as ScalarFieldData.
    // Compiled by the scalar field compiler; expects a kRawFileSchema dependency.
    // Multiple scalar field schemas may coexist for different recipe types
    // (e.g. kScalarFieldProceduralSchema, kScalarFieldFromWaveletBandSchema).
    // All produce kAssetTypeScalarField output.
    inline constexpr wz::asset::SchemaID kScalarFieldFromRawF32Schema{ 0xF11E'CA55'E7'000200ull };

    // Procedural scalar field recipe.
    // Compiled directly from metadata; has no file dependency.
    // Produces kAssetTypeScalarField output.
    // Multiple procedural recipes may coexist alongside file-backed recipes;
    // all share the same output type and ScalarFieldTable.
    inline constexpr wz::asset::SchemaID kScalarFieldProceduralSchema{ 0xF11E'CA55'E7'000201ull };

    // Gaussian splat cloud loaded from a PLY file.
    inline constexpr wz::asset::SchemaID kGaussianSplatFromPLYSchema{ 0xF11E'CA55'E7'000500ull };

    // Gaussian splat cloud generated from a scalar field.
    inline constexpr wz::asset::SchemaID kGaussianSplatFromFieldSchema{ 0xF11E'CA55'E7'000501ull };

}
