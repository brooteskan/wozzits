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

    // Authored render binding layout (issue #227): the SRG shape a custom
    // render program binds — root-constant block (head packer + declared tail
    // fields), SRV binding rows, static-sampler rows; registers derived from
    // row order. Zero dependencies; compiled directly from indexed params.
    // Lives in the shader/pipeline range as a root-signature-shaped recipe.
    inline constexpr wz::asset::SchemaID kRenderBindingLayoutSchema{
    0xF11E'CA55'E7'000104ull
    };

    // HLSL binding prelude (issue #231): prepends the generated shader-side
    // declarations of a render binding layout (cbuffer with packoffset, SRV
    // rows, static samplers) to an authored HLSL body, producing the combined
    // ShaderSource a shader node compiles. The include travels THROUGH the
    // asset DAG instead of a #include/include-handler so a layout edit
    // re-keys the prelude, the shader, and the program — generated
    // declarations can never go stale against the root signature. Deps: one
    // ShaderSource body + one RenderBindingLayout.
    inline constexpr wz::asset::SchemaID kHLSLBindingPreludeSchema{
    0xF11E'CA55'E7'000105ull
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

    // Scalar field recipe for a Gaea .r32 heightmap export. Same raw float32
    // bytes as kScalarFieldFromRawF32Schema, but the dimensions are NOT authored:
    // they follow Gaea's documented convention of a square grid whose side is
    // sqrt(sample_count). A non-square file is rejected (use the generic raw-F32
    // schema with explicit width/height for that). Produces kAssetTypeScalarField.
    inline constexpr wz::asset::SchemaID kScalarFieldFromGaeaR32Schema{
    0xF11E'CA55'E7'000203ull
    };

    // Procedural TERRAIN scalar field recipe: fractal noise shaped into a
    // plausible landscape, with a radial basin that flattens the middle.
    //
    // A separate schema from kScalarFieldProceduralSchema rather than a sixth
    // entry in its generator enum: that recipe's whole dial set is
    // frequency + amplitude, shared by every generator, and terrain needs eight
    // dials that mean nothing to Checkerboard. Splitting keeps each recipe's
    // dials honest and leaves the procedural key factory's identity untouched.
    //
    // Emits values normalised to exactly [0, 1], the same contract the Gaea .r32
    // path arrives with, so a procedural field and a hand-authored one are
    // interchangeable under one Placement with no re-tuning.
    // Produces kAssetTypeScalarField output.
    inline constexpr wz::asset::SchemaID kScalarFieldTerrainSchema{
    0xF11E'CA55'E7'000204ull
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

    // Level/range masks over a mesh-derived Float1 field. Produces UInt1
    // channels in the selected mesh domain, with one channel per authored
    // low/high region.
    inline constexpr wz::asset::SchemaID kMeshFieldLevelMaskSchema{
        0xF11E'CA55'E7'00040Eull
    };

    // Mesh simplification hierarchy recipe. Compiled from one
    // kAssetTypeMesh dependency and produces kAssetTypeMeshClusterHierarchy.
    // V0 supports an identity hierarchy used to validate asset/editor plumbing
    // before graph-coarsening algorithms land.
    inline constexpr wz::asset::SchemaID kMeshClusterHierarchySchema{
        0xF11E'CA55'E7'00040Full
    };

    // Mesh preview extracted from a MeshClusterHierarchy level. Compiled from
    // one kAssetTypeMeshClusterHierarchy dependency and produces
    // kAssetTypeMesh so existing renderable/material paths can inspect
    // hierarchy levels before renderer-native cluster selection exists.
    inline constexpr wz::asset::SchemaID
    kMeshClusterHierarchyPreviewMeshSchema{
        0xF11E'CA55'E7'000410ull
    };

    // Debug-only mesh filter that keeps every Nth source triangle and compacts
    // referenced vertices. Produces kAssetTypeMesh for render-path inspection.
    inline constexpr wz::asset::SchemaID kDebugTriangleStrideMeshSchema{
        0xF11E'CA55'E7'000411ull
    };

    // CSR sparse mesh operator built from one kAssetTypeMesh dependency.
    // One schema for the whole format: the operator kind and its
    // parameters are compiler arguments mixed into the content hash, not
    // separate schemas. Produces kAssetTypeMeshSparseOperator output.
    inline constexpr wz::asset::SchemaID kMeshSparseOperatorSchema{
        0xF11E'CA55'E7'00040Aull
    };

    // GPU-resident sparse mesh view built from one kAssetTypeMesh dependency.
    // Produces kAssetTypeGpuSparseMesh output; the CPU payload describes the
    // mesh view and sparse operator identity, while GPU buffers live in the
    // runtime resident sparse mesh table.
    inline constexpr wz::asset::SchemaID kGpuSparseMeshFromMeshSchema{
        0xF11E'CA55'E7'000412ull
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

    // Placement recipe: a generic world-space frame (origin, extent, base
    // height) compiled directly from authored parameters with no dependency.
    // "World-data" range (0xA00-0xAFF) because a Placement describes where and
    // how large world data sits, while staying free of any terrain/heightfield/
    // texel vocabulary. Stores world extent (metres), never a derived
    // metres-per-texel — consumers (e.g. the clipmap) derive that downstream.
    // Produces kAssetTypePlacement output.
    inline constexpr wz::asset::SchemaID kPlacementSchema{
        0xF11E'CA55'E7'000A03ull
    };

    // PlacedField combiner (issue #223): binds a frame-less field to a Placement
    // frame → kAssetTypePlacedField. Same "world-data" range (0xA00-0xAFF) as
    // Placement, next after it, since a PlacedField describes placed world data.
    inline constexpr wz::asset::SchemaID kPlacedFieldSchema{
        0xF11E'CA55'E7'000A04ull
    };

    // ClipmapLatticeSchedule: the RESOLVED geometry-clipmap LOD schedule
    // (base_resolution / level_count / cell_size, plus achieved horizon and
    // triangle count) → kAssetTypeClipmapLatticeSchedule. Derived from authored
    // world_extent / horizon / triangle_budget and ONE scalar-field dep whose
    // width supplies N. Same "world-data" range (0xA00-0xAFF) as Placement and
    // PlacedField, next after them, since a schedule describes how placed world
    // data is tiled.
    inline constexpr wz::asset::SchemaID kClipmapLatticeScheduleSchema{
        0xF11E'CA55'E7'000A05ull
    };

    // Atmosphere: the GLOBAL fog a frame is viewed through (colour, density,
    // start distance, height falloff, enabled) → kAssetTypeAtmosphere. A pure
    // params recipe with NO dependency, like Placement: fog is authored, never
    // derived. Same "world-data" range (0xA00-0xAFF) as Placement, PlacedField
    // and ClipmapLatticeSchedule, next after them, since an atmosphere describes

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

    // Collision asset derived DIRECTLY from a scalar-field heightfield plus a
    // world mapping, with no TerrainAsset intermediary. Produces a
    // TerrainHeightField shape from kAssetTypeScalarField input.
    // Produces kAssetTypeCollisionAsset output.
    inline constexpr wz::asset::SchemaID kCollisionFromHeightFieldSchema{
        0xF11E'CA55'E7'000B02ull
    };

    // Audio schemas occupy the reserved 0x000B80-0x000BFF sub-range (the upper
    // half of the 0x000B00 block; collision owns 0x000B00-0x000B7F). These IDs
    // may appear in disk-cache keys later, so keep them stable. Allocated so far:
    // WAV=B80, ProceduralTone=B81, Renderable=B82, ClipBankFromClips=B83,
    // ClipBankRenderable=B84, ClipBankFromDirectory=B85, GrainCloudRenderable=B86.
    //
    // Audio clip decoded from a WAV (RIFF/WAVE) file dependency into interleaved
    // float32 PCM. Expects one kRawFileSchema dependency carrying the WAV bytes.
    // Produces kAssetTypeAudioClip output.
    inline constexpr wz::asset::SchemaID kAudioClipFromWavSchema{
        0xF11E'CA55'E7'000B80ull
    };

    // Procedural tone audio clip: a band-unlimited V1 oscillator (sine/square/
    // saw/triangle) generated from parameters alone, no file dependency. A clean
    // file-free path for tests and an early sound source ahead of the wavetable
    // asset. Produces kAssetTypeAudioClip output.
    inline constexpr wz::asset::SchemaID kAudioClipProceduralToneSchema{
        0xF11E'CA55'E7'000B81ull
    };

    // Audio renderable: the terminal of an audio chain. Compiled from one
    // kAssetTypeAudioClip dependency plus playback params (gain/pitch/looping);
    // produces kAssetTypeAudioRenderable. V1 is the minimal source->out form —
    // filter nodes extend the chain later. The audio analog of a visual
    // renderable recipe.
    inline constexpr wz::asset::SchemaID kAudioRenderableSchema{
        0xF11E'CA55'E7'000B82ull
    };

    // Audio clip bank built from an ordered list of audio-clip dependencies.
    // Compiled from N kAssetTypeAudioClip dependencies plus a parallel list of
    // per-entry name hashes (carried in the node meta); produces
    // kAssetTypeAudioBank. A directory import enumerates *.wav under a folder,
    // imports each as a clip, and feeds this recipe. Runtime data is owned by
    // AudioClipBankTable.
    inline constexpr wz::asset::SchemaID kAudioClipBankFromClipsSchema{
        0xF11E'CA55'E7'000B83ull
    };

    // Bank-backed audio renderable: the terminal of an audio chain that holds a
    // whole clip table instead of a single clip. Compiled from one
    // kAssetTypeAudioBank dependency plus playback params (gain/pitch/looping)
    // and a default clip index; produces kAssetTypeAudioRenderable. The behavior
    // PLAY command selects a clip by index at trigger time, so one AudioSource
    // can trigger many sounds.
    inline constexpr wz::asset::SchemaID kAudioClipBankRenderableSchema{
        0xF11E'CA55'E7'000B84ull
    };

    // Audio clip bank built directly from a directory of WAV files. A no-dependency
    // source recipe: the compiler enumerates *.wav under an authored directory
    // (sorted for determinism), reads + decodes each WAV inline (the I/O boundary,
    // like a file carrier), and stores the clips in AudioClipTable + the bank.
    // Carries "directory" (string) and "recursive" (bool) params; produces
    // kAssetTypeAudioBank. This is the graph-authorable directory importer, vs.
    // kAudioClipBankFromClipsSchema which folds already-imported clip deps.
    inline constexpr wz::asset::SchemaID kAudioClipBankFromDirectorySchema{
        0xF11E'CA55'E7'000B85ull
    };

    // Grain-cloud audio renderable: a terminal that runs its source clips through
    // the granular generator (an evolving environmental bed) instead of playing one
    // as a voice. Compiled from one kAssetTypeAudioBank dependency plus granular
    // params; produces kAssetTypeAudioRenderable with kind == GrainCloud. The audio
    // analog of a particle-system renderable.
    inline constexpr wz::asset::SchemaID kAudioGrainCloudRenderableSchema{
        0xF11E'CA55'E7'000B86ull
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

    // Procedural icosphere mesh recipe. Generates a unit-ish sphere by
    // subdividing a regular icosahedron `subdivisions` times and projecting
    // each vertex onto the sphere of `radius`. Compiled directly from authored
    // parameters; has no dependency. Produces kAssetTypeMesh output.
    inline constexpr wz::asset::SchemaID kProceduralSphereMeshSchema{
        0xF11E'CA55'E7'000415ull
    };

    // Procedural geometry-clipmap lattice mesh recipe.
    // Generates a static, reusable nested-LOD-ring lattice in the XZ plane
    // (y = 0), centered at the origin in grid-unit space: a full m x m quad
    // center (level 0) surrounded by L-1 concentric square rings, each at
    // double the cell size of the level it encloses. Coarse/fine ring
    // boundaries are stitched crack-free (no T-junctions). Compiled directly
    // from authored parameters (levels, base resolution, spacing); has no
    // dependency. Produces kAssetTypeMesh output — the geometry the clipmap
    // terrain renderer (issue #198 step 3) displaces in a vertex shader.
    inline constexpr wz::asset::SchemaID kProceduralClipmapLatticeMeshSchema{
        0xF11E'CA55'E7'000413ull
    };

    // GLB static mesh import recipe.
    // Compiles a raw/binary GLB file dependency into CPU MeshData in MeshTable.
    // Produces kAssetTypeMesh output.
    inline constexpr wz::asset::SchemaID kGLBMeshSchema{
    0xF11E'CA55'E7'000403ull
    };

    // "Mesh from GLB scene" extractor recipe (issue #213). Compiled from one
    // kAssetTypeScene dependency (a "Scene from GLB" output, which embeds each
    // node's raw object-space geometry in SceneAssetData::glb_meshes). The
    // "node_id" parameter selects the GLB scene node; the compiler outputs that
    // node's MeshData verbatim — NO node transform is baked in, because the GLB
    // nodes are grafted into the runtime scene tree WITH their transforms.
    // Produces kAssetTypeMesh output.
    inline constexpr wz::asset::SchemaID kMeshFromGLBSceneSchema{
    0xF11E'CA55'E7'000414ull
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

    // 0x700 (mesh wireframe renderable), 0x701 (gaussian splat debug
    // renderable), and 0x705 (mesh styled renderable) were DELETED by the issue
    // #195 scrap-and-rebuild: a mesh renderable is ONE recipe — the 0x706 RHI
    // pull mesh below — where the wireframe/solid look is a render-program
    // property (RasterMode) and the style flows as data (an optional
    // MeshRenderStyle dependency baked into the recipe); the splat debug
    // renderable's replacement is 0x709 below (#208). Do not reuse these ids.

    // RHI-native mesh renderable recipe. Compiled from one mesh dependency,
    // one render-program dependency, and an OPTIONAL MeshRenderStyle dependency
    // (issue #195); produces kAssetTypeRenderable output containing an
    // RhiRenderableRecipe in the RHI renderable table.
    inline constexpr wz::asset::SchemaID kRhiPullMeshRenderableSchema{
        0xF11E'CA55'E7'000706ull
    };

    // Renderable recipe for a GPU sparse mesh and custom render program.
    // The render program must use the MeshVertexPull binding model.
    inline constexpr wz::asset::SchemaID kGpuSparseMeshRenderableSchema{
        0xF11E'CA55'E7'000707ull
    };

    // 0x708 (geometry-clipmap landscape renderable) was RETIRED by issue #234:
    // the clipmap is now authored as a 0x70A custom renderable with the
    // CameraSnappedTerrain constants head (#233) + a scalar_field_texture
    // binding (the height field) + an optional Placement port (the world
    // footprint). The camera-follow view math (clipmap_view.h) is unchanged and
    // shared. Do not reuse this id.

    // RHI renderable recipe (issue #208): a GaussianSplatCloud (resident as a
    // decoded splat StructuredBuffer) + a SplatPull render program, rendered as
    // a camera-facing splat cloud through RhiSceneRenderer. Replaced the legacy
    // 0x701 gaussian splat debug renderable (deleted by #195), which produced a
    // RenderableAssetData on the legacy DX12 splat path; this produces an
    // RhiRenderableRecipe carrying gaussian_splat_cloud_key.
    inline constexpr wz::asset::SchemaID kGaussianSplatCloudRhiRenderableSchema{
        0xF11E'CA55'E7'000709ull
    };

    // Custom renderable recipe (issue #228): a pull mesh + a custom render
    // program whose SRG comes from an authored binding layout (#227), plus
    // semantic resource bindings (binding0..7 ports, Any-typed — the source
    // kind is validated at compile time against the layout row) and authored
    // values for the layout's declared constant tail fields. Compiles to an
    // RhiRenderableRecipe with custom=true that RhiSceneRenderer binds
    // GENERICALLY (walks recipe.bindings, packs head + tail constants) — zero
    // per-look C++. Produces kAssetTypeRenderable output.
    inline constexpr wz::asset::SchemaID kCustomRenderableSchema{
        0xF11E'CA55'E7'00070Aull
    };

    // Star-field RHI renderable recipe (issue #266): a StarCatalog (resident as a
    // decoded point StructuredBuffer under "star_catalog") + a SplatPull render
    // program, rendered as instanced billboards through RhiSceneRenderer. Mirrors
    // the gaussian-splat-cloud recipe (0x709) with star_catalog_key in place of
    // gaussian_splat_cloud_key. Produces kAssetTypeRenderable output.
    inline constexpr wz::asset::SchemaID kStarFieldRhiRenderableSchema{
        0xF11E'CA55'E7'00070Bull
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

    // Sky-gaussian set compiled from a baked .sky_gaussian.json document
    // (Seam B1c, issue #260 / umbrella #259). The spherical-Gaussian lobes fit
    // to an HDR sky panorama, deserialized from the JSON sidecar and stored in
    // the SkyGaussianTable; when a shared rhi registry is present the lobes are
    // ALSO published as a resident StructuredBuffer (variant "sky_gaussian").
    // Source dep: one compiled kAssetTypeJSONDocument. Produces
    // kAssetTypeSkyGaussian output.
    inline constexpr wz::asset::SchemaID kSkyGaussianFromJSONSchema{
        0xF11E'CA55'E7'001003ull
    };

    // Star catalog compiled from a baked .star_catalog.json document (Seam C-2,
    // issue #266 / umbrella #259). Raw catalog rows (RA / Dec / magnitude / B-V)
    // plus grade dials are deserialized, run through the starfield astronomy
    // kernel at compile time, and stored in the StarCatalogTable; when a shared
    // rhi registry is present the built stars are ALSO published as a resident
    // point-source StructuredBuffer (variant "star_catalog"). Source dep: one
    // compiled kAssetTypeJSONDocument. Produces kAssetTypeStarCatalog output.
    inline constexpr wz::asset::SchemaID kStarCatalogFromJSONSchema{
        0xF11E'CA55'E7'001004ull
    };

    // Star catalog compiled from a baked PLY point cloud (Seam C-2/T-2, issue
    // #266 -- the two-stage Tycho-2 path). The stage-1 tycho2_prep tool bakes a
    // compact binary PLY (per-vertex direction + vmag + bv); this imports it with
    // the creative dials (authored as node params) and publishes the resident
    // star buffer. Source dep: one kAssetTypeRawFile. Produces kAssetTypeStarCatalog.
    inline constexpr wz::asset::SchemaID kStarCatalogFromPLYSchema{
        0xF11E'CA55'E7'001005ull
    };

    // Global atmosphere (the fog every program looks through): a params-only
    // recipe with no input ports. Produces kAssetTypeAtmosphere. Lighting /
    // environment range, beside the direct-light and ambient-lighting recipes
    // -- it is scene-level environment state, not a terrain recipe.
    inline constexpr wz::asset::SchemaID kAtmosphereSchema{
        0xF11E'CA55'E7'001006ull
    };
}
