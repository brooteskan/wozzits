#pragma once

// engine/assets/schema_ids.h

#include <engine/assets/engine_asset_key_core.h>

namespace wz::engine::assets {

    // Numeric range allocation (low 24 bits of the uint64 value):
    //
    //   0x000001 – 0x0000FF   File carriers
    //                          Raw bytes, text files, external refs, archives,
    //                          and other source/carrier schemas.
    //
    //   0x000100 – 0x0001FF   Shaders and GPU pipelines
    //                          HLSL/GLSL/WGSL recipes, shader modules,
    //                          root signatures, pipeline recipes.
    //
    //   0x000200 – 0x0002FF   Scalar fields
    //                          File-backed, procedural, derived fields,
    //                          field transforms, field-to-field recipes.
    //
    //   0x000300 – 0x0003FF   Textures and images
    //                          Raw RGBA, procedural textures, image imports,
    //                          texture transforms, mip generation.
    //
    //   0x000400 – 0x0004FF   Meshes and geometry
    //                          Procedural meshes, file-backed meshes,
    //                          mesh transforms, LODs, collision/nav derivations.
    //
    //   0x000500 – 0x0005FF   Gaussian splat clouds and point-based geometry
    //                          PLY/custom imports, scalar-field-derived splats,
    //                          splat LOD/chunk recipes.
    //
    //   0x000600 – 0x0006FF   Materials and binding descriptions
    //
    //   0x000700 – 0x0007FF   Scenes, prefabs, renderables, worlds, and authored content
    //                          Scene/prefab recipes, renderable bindings,
    //                          authored world content, data-to-scene interpretation recipes.
    //
    //   0x000800 – 0x0008FF   Animation
    //
    //   0x000900 – 0x0009FF   Physics
    //
    //   0x000A00 – 0x000AFF   Terrain and world-data recipes
    //
    //   0x000B00 – 0x000BFF   Audio
    //
    //   0x000C00 – 0x000CFF   UI and text
    //
    //   0x000D00 – 0x000DFF   Gameplay/data
    //
    //   0x000E00 – 0x000EFF   AI
    //
    //   0x000F00 – 0x000FFF   VFX and particles
    //
    //   0x001000 – 0x0010FF   Lighting and rendering environment
    //
    //   0x001100 – 0x0011FF   Cinematics
    //
    //   0x001200 – 0x0012FF   Editor/tooling/build/import/cooked assets
    //
    //   0x001300 – 0xFFFFFF   Unallocated extension range
    //
    // The high bits (0xF11ECA55E7______) are a fixed magic prefix to reduce
    // collision probability with any external schema registries.


    // ───First schemas implemented for testing, but they are real ───────────────────────────────
    inline constexpr wz::asset::SchemaID kRawFileSchema{
    0xF11E'CA55'E7'000001ull
    };

    // HLSL source file carrier — read by the HLSL file carrier compiler.
    inline constexpr wz::asset::SchemaID kHLSLFileSchema{
    0xF11E'CA55'E7'000002ull
    };

    // HLSL shader recipe — compiled by the HLSL shader compiler.
    // Expects a kHLSLFileSchema dependency carrying source bytes.
    inline constexpr wz::asset::SchemaID kHLSLShaderSchema{
    0xF11E'CA55'E7'000100ull
    };

    // Scalar field recipe: interpret a raw float32 file dependency as ScalarFieldData.
    // Compiled by the scalar field compiler; expects a kRawFileSchema dependency.
    // Multiple scalar field schemas may coexist for different recipe types
    // (e.g. kScalarFieldProceduralSchema, kScalarFieldFromWaveletBandSchema).
    // All produce kAssetTypeScalarField output.
    inline constexpr wz::asset::SchemaID kScalarFieldFromRawF32Schema{
    0xF11E'CA55'E7'000200ull
    };

    // Procedural scalar field recipe.
    // Compiled directly from metadata; has no file dependency.
    // Produces kAssetTypeScalarField output.
    // Multiple procedural recipes may coexist alongside file-backed recipes;
    // all share the same output type and ScalarFieldTable.
    inline constexpr wz::asset::SchemaID kScalarFieldProceduralSchema{
    0xF11E'CA55'E7'000201ull
    };

    // Gaussian splat cloud loaded from a PLY file.
    inline constexpr wz::asset::SchemaID kGaussianSplatFromPLYSchema{
    0xF11E'CA55'E7'000500ull
    };

    // Gaussian splat cloud generated from a scalar field.
    inline constexpr wz::asset::SchemaID kGaussianSplatFromFieldSchema{
    0xF11E'CA55'E7'000501ull
    };

    // CSV table recipe — compiled by the CSV parser; expects a kCSVFileSchema dependency.
    // header_mode ordinal is encoded in the key so the same file compiled with
    // different modes produces distinct asset keys.
    inline constexpr wz::asset::SchemaID kCSVTableSchema{
        0xF11E'CA55'E7'000D00ull
    };

    // ─── Foundation / carrier asset types  ───────────────────────────────
    inline constexpr wz::asset::SchemaID kTextFileSchema{
        0xF11E'CA55'E7'000003ull
    };

    inline constexpr wz::asset::SchemaID kBinaryBlobSchema{
        0xF11E'CA55'E7'000004ull
    };

    inline constexpr wz::asset::SchemaID kManifestSchema{
        0xF11E'CA55'E7'000005ull
    };

    inline constexpr wz::asset::SchemaID kAssetBundleSchema{
        0xF11E'CA55'E7'000006ull
    };

    inline constexpr wz::asset::SchemaID kPackageSchema{
        0xF11E'CA55'E7'000007ull
    };

    inline constexpr wz::asset::SchemaID kImportedSourceFileSchema{
        0xF11E'CA55'E7'000008ull
    };

    inline constexpr wz::asset::SchemaID kExternalReferenceSchema{
        0xF11E'CA55'E7'000009ull
    };

    inline constexpr wz::asset::SchemaID kJSONFileSchema{
        0xF11E'CA55'E7'00000Aull
    };

    inline constexpr wz::asset::SchemaID kYAMLFileSchema{
        0xF11E'CA55'E7'00000Bull
    };

    inline constexpr wz::asset::SchemaID kTOMLFileSchema{
        0xF11E'CA55'E7'00000Cull
    };

    inline constexpr wz::asset::SchemaID kXMLFileSchema{
        0xF11E'CA55'E7'00000Dull
    };

    inline constexpr wz::asset::SchemaID kCSVFileSchema{
        0xF11E'CA55'E7'00000Eull
    };

    inline constexpr wz::asset::SchemaID kCustomBinaryFileSchema{
        0xF11E'CA55'E7'00000Full
    };

    inline constexpr wz::asset::SchemaID kDirectoryAssetSchema{
        0xF11E'CA55'E7'000010ull
    };

    inline constexpr wz::asset::SchemaID kArchiveAssetSchema{
        0xF11E'CA55'E7'000011ull
    };

    // Parsed JSON document recipe.
    // Consumes a file carrier dependency and produces JSONData in JSONTable.
    inline constexpr wz::asset::SchemaID kJSONDocumentSchema{
        0xF11E'CA55'E7'000012ull
    };

    // Parsed TOML document recipe.
    // Consumes a file carrier dependency and produces TOMLData in TOMLTable.
    inline constexpr wz::asset::SchemaID kTOMLDocumentSchema{
        0xF11E'CA55'E7'000013ull
    };

    inline constexpr wz::asset::SchemaID kProceduralTriangleMeshSchema{
        0xF11E'CA55'E7'000400ull
    };
    
    inline constexpr wz::asset::SchemaID kProceduralQuadMeshSchema{
        0xF11E'CA55'E7'000401ull
    };

    inline constexpr wz::asset::SchemaID kProceduralCubeMeshSchema{
        0xF11E'CA55'E7'000402ull
    };

    inline constexpr wz::asset::SchemaID kProceduralGaussianSplatCloudSchema{
    0xF11E'CA55'E7'000502ull
    };

    inline constexpr wz::asset::SchemaID kInlineDataTableSchema{
    0xF11E'CA55'E7'001200ull
    };

    inline constexpr wz::asset::SchemaID kDiagnosticTableResampleTimeSeriesSchema{
    0xF11E'CA55'E7'001201ull
    };

    // CSV export recipe — compiles a DataTable dependency to deterministic CSV text.
    // File writing is an explicit module operation, not a compiler side effect.
    inline constexpr wz::asset::SchemaID kCSVExportSchema{
    0xF11E'CA55'E7'001202ull
    };

    // Bridge recipe — promotes a compiled DiagnosticResampledTimeSeries into a
    // DataTable so it can feed into CSVExport or any other DataTable consumer.
    inline constexpr wz::asset::SchemaID kDiagnosticResampledTimeSeriesToDataTableSchema{
    0xF11E'CA55'E7'001203ull
    };

    inline constexpr wz::asset::SchemaID kMeshWireframeRenderableSchema{
    0xF11E'CA55'E7'000700ull
    };

    inline constexpr wz::asset::SchemaID kGaussianSplatDebugRenderableSchema{
        0xF11E'CA55'E7'000701ull
    };

    inline constexpr wz::asset::SchemaID kScalarFieldDebugRenderableSchema{
    0xF11E'CA55'E7'000702ull
    };
}
