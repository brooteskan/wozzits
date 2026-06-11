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
    //   0x000B00 – 0x000BFF   Collision (0x000B00-0x000B7F) and audio
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

    // CPU-side declaration of how a BuiltinRenderProgram is drawn:
    // binding model, topology, default render domain/policy, and shader pair.
    // This is not a backend-owned PSO/root-signature object.
    inline constexpr wz::asset::SchemaID kBuiltinRenderProgramSchema{
    0xF11E'CA55'E7'000101ull
    };

    // CPU-side declaration for a compute kernel pipeline: compiled compute
    // shader dependency, binding layout, root constants, and dispatch group size.
    inline constexpr wz::asset::SchemaID kComputePipelineSchema{
    0xF11E'CA55'E7'000102ull
    };

    // CPU-side declaration of a fully-authored custom render program:
    // binding model, topology, declarative PSO state, root constants,
    // descriptor bindings, and shader pair.  No BuiltinRenderProgram lookup.
    inline constexpr wz::asset::SchemaID kCustomRenderProgramSchema{
    0xF11E'CA55'E7'000103ull
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

    // Vector field recipe: interpret a raw float32 file dependency as
    // VectorFieldData. Values are interleaved by sample, then channel, then
    // component. Produces kAssetTypeVectorField output.
    inline constexpr wz::asset::SchemaID kVectorFieldFromRawF32Schema{
    0xF11E'CA55'E7'000202ull
    };

    // Gaussian splat cloud loaded from a PLY file.
    inline constexpr wz::asset::SchemaID kGaussianSplatFromPLYSchema{
    0xF11E'CA55'E7'000500ull
    };

    // Gaussian splat cloud generated from a scalar field.
    inline constexpr wz::asset::SchemaID kGaussianSplatFromFieldSchema{
    0xF11E'CA55'E7'000501ull
    };

    // Derived Gaussian splat color LOD product.
    // Compiled from a kAssetTypeGaussianSplatCloud dependency; produces
    // kAssetTypeGaussianSplatColorLOD output containing per-splat prefiltered
    // neighborhood color + confidence. Compile parameters are part of identity.
    inline constexpr wz::asset::SchemaID kGaussianSplatColorLODSchema{
    0xF11E'CA55'E7'000502ull
    };

    // Gaussian splat cloud generated as a terrain surface from a 2D scalar
    // height field.  Distinct from kGaussianSplatFromFieldSchema (the simple
    // grayscale debug heightmap splatter) — this compiler produces anisotropic,
    // surface-tangent-aligned splats and treats heightmap values as raw world
    // elevations.  Source: one kAssetTypeScalarField dep.
    inline constexpr wz::asset::SchemaID kGaussianSplatTerrainSurfaceFromHeightFieldSchema{
    0xF11E'CA55'E7'000503ull
    };

    // Terrain splat recipe bundling a Gaea .r32 heightmap file with a JSON
    // sidecar that carries world-space interpretation parameters
    // (height_scale, step_x, step_z, overlap_factor, thickness,
    // subsample_step) and optional dimension overrides.  Source deps are one
    // raw .r32 file carrier and one compiled JSONDocument sidecar.  Produces a
    // kAssetTypeGaussianSplatCloud directly — the intermediate ScalarField
    // is built transiently inside the compiler and not exposed as a
    // separate asset.
    inline constexpr wz::asset::SchemaID kTerrainSplatFromGaeaR32Schema{
    0xF11E'CA55'E7'000504ull
    };

    // Mesh decimation recipe. Compiled from a kAssetTypeMesh dependency and
    // implemented through the engine mesh processing abstraction.
    inline constexpr wz::asset::SchemaID kMeshDecimationSchema{
        0xF11E'CA55'E7'000405ull
    };

    // Explicit/test mesh-derived field payload. Compiled from one
    // kAssetTypeMesh dependency and produces kAssetTypeMeshDerivedField.
    inline constexpr wz::asset::SchemaID kMeshDerivedFieldExplicitSchema{
        0xF11E'CA55'E7'000406ull
    };

    // CPU reference graph-wavelet analysis over a source mesh. Produces a
    // vertex-domain kAssetTypeMeshDerivedField for validation/debug use.
    inline constexpr wz::asset::SchemaID kMeshWaveletAnalysisSchema{
        0xF11E'CA55'E7'000407ull
    };

    // Behavior field placeholder. Creates a zeroed single-channel
    // vertex-domain MeshDerivedField whose element_count is discovered
    // from the source mesh at compile time. Used when a behavior compute
    // kernel publishes mesh field data without wavelet analysis.
    inline constexpr wz::asset::SchemaID kBehaviorFieldPlaceholderSchema{
        0xF11E'CA55'E7'000408ull
    };

    // Project-authored GPU compute kernel deriving per-mesh data offline.
    // Dispatches the authored compute pipeline over declared mesh inputs at
    // compile time, reads results back, and produces a disk-cached
    // kAssetTypeMeshDerivedField. A recipe within the MeshDerivedField
    // format, not a new asset type (see issue #150 design rule).
    inline constexpr wz::asset::SchemaID kMeshComputeDerivedFieldSchema{
        0xF11E'CA55'E7'000409ull
    };

    // Built-in scene-authored mesh-derived scalar field recipe. Produces a
    // MeshDerivedField from one mesh dependency without embedding payload
    // bytes in the authored scene.
    inline constexpr wz::asset::SchemaID kBuiltinMeshDerivedFieldSchema{
        0xF11E'CA55'E7'00040Bull
    };

    // Sparse-operator application over a MeshDerivedField input. Produces a
    // MeshDerivedField recipe output; v0 supports vertex-domain Float1
    // residual apply only.
    inline constexpr wz::asset::SchemaID kMeshSparseApplyFieldSchema{
        0xF11E'CA55'E7'00040Cull
    };

    // Repeated sparse-operator smoothing/diffusion over a MeshDerivedField
    // input. Produces one Float1 channel per authored scale/detail band.
    inline constexpr wz::asset::SchemaID kMeshSparseDiffusionBandsSchema{
        0xF11E'CA55'E7'00040Dull
    };

    // CSR sparse mesh operator built from one kAssetTypeMesh dependency.
    // One schema for the whole format: the operator kind and its
    // parameters are compiler arguments mixed into the content hash, not
    // separate schemas. Produces kAssetTypeMeshSparseOperator output.
    inline constexpr wz::asset::SchemaID kMeshSparseOperatorSchema{
        0xF11E'CA55'E7'00040Aull
    };

    // Semantic terrain asset generated from a 2D scalar height field.
    // Produces kAssetTypeTerrain output.
    inline constexpr wz::asset::SchemaID kTerrainFromHeightFieldSchema{
        0xF11E'CA55'E7'000A00ull
    };

    // Semantic terrain asset generated from a mesh surface.
    // Produces kAssetTypeTerrain output.
    inline constexpr wz::asset::SchemaID kTerrainFromMeshSchema{
        0xF11E'CA55'E7'000A01ull
    };

    // Terrain visual proxy metadata generated from semantic terrain data.
    // Produces kAssetTypeTerrainVisualProxy output.
    inline constexpr wz::asset::SchemaID kTerrainVisualProxySchema{
        0xF11E'CA55'E7'000A02ull
    };

    // Collision schemas intentionally occupy 0x000B00-0x000B7F. These IDs
    // are persisted in disk-cache keys, so keep them stable; reserve
    // 0x000B80-0x000BFF for future audio schemas.
    // Collision asset derived from CPU MeshData. The recipe chooses how the
    // source mesh occupies space: bounds, triangle surface, or future proxy
    // methods. Produces kAssetTypeCollisionAsset output.
    inline constexpr wz::asset::SchemaID kCollisionFromMeshSchema{
        0xF11E'CA55'E7'000B00ull
    };

    // Collision asset derived from semantic terrain data. The recipe chooses
    // terrain's query-facing occupancy independently of its visual render path.
    // Produces kAssetTypeCollisionAsset output.
    inline constexpr wz::asset::SchemaID kCollisionFromTerrainSchema{
        0xF11E'CA55'E7'000B01ull
    };

    // CSV table recipe: compiled by the CSV parser; expects a kCSVFileSchema
    // dependency. header_mode ordinal is encoded in the key so the same file
    // compiled with different modes produces distinct asset keys.
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

    // GLB static mesh import recipe.
    // Compiles a raw/binary GLB file dependency into CPU MeshData in MeshTable.
    // Produces kAssetTypeMesh output.
    inline constexpr wz::asset::SchemaID kGLBMeshSchema{
    0xF11E'CA55'E7'000403ull
    };

    // Placeholder mesh recipe used when a mesh node has no authored data source.
    // Produces visible CPU MeshData so scene authoring and rendering paths can
    // remain valid while a real source is selected later.
    inline constexpr wz::asset::SchemaID kPlaceholderMeshSchema{
        0xF11E'CA55'E7'000404ull
    };

    // Procedural / debug Gaussian splat cloud.
    // NOTE: was 0x000502 until that value was reassigned to
    // kGaussianSplatColorLODSchema.  Moved to 0x000505 to resolve the
    // collision.
    inline constexpr wz::asset::SchemaID kProceduralGaussianSplatCloudSchema{
    0xF11E'CA55'E7'000505ull
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

    // Timeframe summary recipe — filters a DataTable by a frame-index column,
    // splits into fixed-size frame buckets, and computes per-metric statistics
    // (min/max/mean/delta/first/last) with a deterministic summary_text per bucket.
    inline constexpr wz::asset::SchemaID kDiagnosticTimeframeSummarySchema{
    0xF11E'CA55'E7'001204ull
    };

    // Bridge recipe — promotes a compiled DiagnosticTimeframeSummary into a
    // DataTable so it can feed into CSVExport or any other DataTable consumer.
    inline constexpr wz::asset::SchemaID kDiagnosticTimeframeSummaryToDataTableSchema{
    0xF11E'CA55'E7'001205ull
    };

    inline constexpr wz::asset::SchemaID kMeshRenderStyleSchema{
        0xF11E'CA55'E7'000600ull
    };

    inline constexpr wz::asset::SchemaID kMeshWireframeRenderableSchema{
    0xF11E'CA55'E7'000700ull
    };

    inline constexpr wz::asset::SchemaID kMeshStyledRenderableSchema{
        0xF11E'CA55'E7'000705ull
    };

    inline constexpr wz::asset::SchemaID kGaussianSplatDebugRenderableSchema{
        0xF11E'CA55'E7'000701ull
    };

    inline constexpr wz::asset::SchemaID kScalarFieldDebugRenderableSchema{
    0xF11E'CA55'E7'000702ull
    };

    inline constexpr wz::asset::SchemaID kTerrainDebugRenderableSchema{
    0xF11E'CA55'E7'000703ull
    };

    inline constexpr wz::asset::SchemaID kTerrainSurfaceRenderableSchema{
    0xF11E'CA55'E7'000704ull
    };

    // Scene asset compiled from a JSON document dependency.
    // Produces kAssetTypeScene output containing SceneAssetData in SceneAssetTable.
    inline constexpr wz::asset::SchemaID kSceneFromJSONSchema{
    0xF11E'CA55'E7'000710ull
    };

    // Scene asset imported from a GLB/glTF scene hierarchy.
    // Produces kAssetTypeScene output containing SceneAssetData in SceneAssetTable.
    inline constexpr wz::asset::SchemaID kSceneFromGLBSchema{
    0xF11E'CA55'E7'000711ull
    };

    inline constexpr wz::asset::SchemaID kDirectLightSchema{
        0xF11E'CA55'E7'001000ull
    };

    inline constexpr wz::asset::SchemaID kAmbientLightingSchema{
        0xF11E'CA55'E7'001001ull
    };

    inline constexpr wz::asset::SchemaID kHDRIEnvironmentSchema{
        0xF11E'CA55'E7'001002ull
    };
}
