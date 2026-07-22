#pragma once
// engine/assets/compiler_version_tokens.h

#include <engine/assets/engine_asset_key_core.h>

namespace wz::engine::assets {
    // ─── Compiler version tokens ──────────────────────────────────────────────────
    //
    // Bump the relevant constant whenever the corresponding compile function's
    // logic changes in a way that would produce different output from identical
    // inputs. This invalidates stale cache entries automatically.
    //
    // Version tokens are embedded in compiler_hash. They do NOT need to be
    // globally unique across recipe types — they only need to be monotone within
    // each (schema, output_type) pair.
    //
    // When to bump:
    //   • Output data layout changes (struct layout, field order, byte representation).
    //   • Validation rules change (previously accepted input is now rejected).
    //   • Generated values change (algorithm fix, floating-point rounding, constants).
    //   • Import defaults change (e.g. default format, domain_kind, or sample layout).
    //   • Dependency interpretation changes (e.g. which dep index is the primary source).
    //
    // Do NOT bump for: pure refactors with identical output, error message changes,
    // logging additions, or performance improvements with no output difference.
    //
    // When adding a new compiler family, add its token here:
    //   kTextureCompilerVersion
    //   kMeshCompilerVersion
    //   kGPUMeshUploadCompilerVersion
    //   kGPUTextureUploadCompilerVersion
    //   (and so on for each independent (schema, output_type) pair)

    inline constexpr uint64_t kHLSLCompilerVersion = 1;
    inline constexpr uint64_t kScalarFieldCompilerVersion = 1;
    // Its own token, not kScalarFieldCompilerVersion: the terrain generator will
    // be tuned (warp, better ridged fBm) long after the file-backed recipes have
    // settled, and sharing a token would force every Gaea field in every project
    // to recompile for a change that cannot affect them.
    inline constexpr uint64_t kScalarFieldTerrainCompilerVersion = 1;
    inline constexpr uint64_t kVectorFieldCompilerVersion = 1;
    inline constexpr uint64_t kGaussianSplatCompilerVersion = 1;
    inline constexpr uint64_t kInochiPuppetCompilerVersion = 1;
    inline constexpr uint64_t kCSVCompilerVersion = 1;
    inline constexpr uint64_t kJSONDocumentCompilerVersion = 1;
    inline constexpr uint64_t kTOMLDocumentCompilerVersion = 1;
    inline constexpr uint64_t kMeshCompilerVersion = 2;
    inline constexpr uint64_t kMeshFromGLBSceneCompilerVersion = 1;
    inline constexpr uint64_t kMeshDerivedFieldCompilerVersion = 3;
    inline constexpr uint64_t kMeshWaveletAnalysisCompilerVersion = 2;
    inline constexpr uint64_t kBehaviorFieldPlaceholderCompilerVersion = 1;
    inline constexpr uint64_t kMeshComputeDerivedFieldCompilerVersion = 1;
    inline constexpr uint64_t kMeshSparseApplyFieldCompilerVersion = 2;
    inline constexpr uint64_t kMeshSparseDiffusionBandsCompilerVersion = 2;
    inline constexpr uint64_t kMeshFieldLevelMaskCompilerVersion = 1;
    inline constexpr uint64_t kMeshSparseOperatorCompilerVersion = 2;
    inline constexpr uint64_t kGpuSparseMeshCompilerVersion = 2;
    inline constexpr uint64_t kMeshClusterHierarchyCompilerVersion = 5;
    inline constexpr uint64_t kMeshClusterHierarchyPreviewMeshCompilerVersion = 1;
    inline constexpr uint64_t kProceduralGaussianSplatCloudCompilerVersion = 1;
    inline constexpr uint64_t kInlineDataTableCompilerVersion = 1;
    inline constexpr uint64_t kDiagnosticTableResampleTimeSeriesCompilerVersion = 1;
    inline constexpr uint64_t kCSVExportCompilerVersion = 1;
    inline constexpr uint64_t kMeshRenderStyleCompilerVersion = 3;
    inline constexpr uint64_t kRhiPullMeshRenderableCompilerVersion = 1;
    inline constexpr uint64_t kGpuSparseMeshRenderableCompilerVersion = 1;
    // kClipmapLandscapeRenderableCompilerVersion retired with the 0x708 schema
    // (issue #234); the clipmap is now a 0x70A CameraSnappedTerrain renderable.
    inline constexpr uint64_t kGaussianSplatCloudRhiRenderableCompilerVersion = 1;
    inline constexpr uint64_t kStarFieldRhiRenderableCompilerVersion = 1;
    inline constexpr uint64_t kScalarFieldDebugRenderableCompilerVersion = 1;
    inline constexpr uint64_t kTerrainDebugRenderableCompilerVersion = 1;
    inline constexpr uint64_t kTerrainSurfaceRenderableCompilerVersion = 6;
    inline constexpr uint64_t kDiagnosticResampledTimeSeriesToDataTableCompilerVersion = 1;
    inline constexpr uint64_t kDiagnosticTimeframeSummaryCompilerVersion = 1;
    inline constexpr uint64_t kDiagnosticTimeframeSummaryToDataTableCompilerVersion = 1;
    inline constexpr uint64_t kBuiltinRenderProgramCompilerVersion = 4;
    // Bumped 1 -> 2: custom render programs can now declare static samplers
    // (folded into the key + serialized into the root signature); the clipmap
    // landscape program declares a LinearClamp VS sampler for bilinear height
    // sampling (#211). Bump invalidates stale caches lacking the sampler.
    // Bumped 2 -> 3 (issue #227): the compiler now copies static_samplers into
    // RenderProgramData (previously dropped) and accepts an optional render
    // binding layout dependency that defines the whole SRG. Bump invalidates
    // caches compiled without samplers/layout consumption.
    // Bumped 3 -> 4 (issue #228): RenderProgramData now carries the wired
    // layout's constants contract (has_authored_binding_layout, constants_head,
    // constant_fields) that the custom renderable compiler validates against.
    inline constexpr uint64_t kCustomRenderProgramCompilerVersion = 4;
    // Bumped 1 -> 2 (issue #231): tail_dwords()/total_constants_dwords now
    // follow HLSL cbuffer packing (no-straddle field alignment), so a layout
    // whose declared fields cross a 16-byte register derives a larger
    // root-constant block than the old tight sum.
    inline constexpr uint64_t kRenderBindingLayoutCompilerVersion = 2;
    // HLSL binding prelude 0x105 (issue #231): generated declarations from an
    // authored render binding layout, prepended to a shader source dep.
    inline constexpr uint64_t kHLSLBindingPreludeCompilerVersion = 1;
    // Custom renderable recipe 0x70A (issue #228): semantic resource bindings
    // + declared-constant values validated against the wired program's
    // authored binding layout.
    // Bumped 1 -> 2 (issue #229): recipe.constants now carries EVERY declared
    // tail field (authored value or zero default), not just the authored
    // ones, so per-instance scene-node overrides can address any field by
    // name at pack time.
    // Bumped 2 -> 3 (issue #231): baked field offsets follow HLSL cbuffer
    // packing (no-straddle alignment) instead of a tight running sum — the
    // block is read through a cbuffer, so the old offsets misaddressed any
    // field the packing rule moves.
    inline constexpr uint64_t kCustomRenderableCompilerVersion = 3;
    inline constexpr uint64_t kComputePipelineCompilerVersion = 1;
    // v2: added subsample_step + RHI residency publishing (#208).
    // v3: XZ + height normalization fixes (unit [0,1], raw height, texel-index
    //     placement so the cloud aligns with the clipmap) -- forces a re-bake.
    // v4: XZ over DIMS (texel-cell convention) so the cloud sits flush on the
    //     clipmap vertices (pairs with the clipmap VS floor(uv*dims) fetch).
    inline constexpr uint64_t kGaussianSplatFromScalarFieldCompilerVersion = 4;
    inline constexpr uint64_t kGaussianSplatColorLODCompilerVersion = 1;
    inline constexpr uint64_t kGaussianSplatTerrainSurfaceFromHeightFieldCompilerVersion = 2;
    inline constexpr uint64_t kTerrainSplatFromGaeaR32CompilerVersion = 2;
    inline constexpr uint64_t kTerrainCompilerVersion = 4;
    inline constexpr uint64_t kTerrainVisualProxyCompilerVersion = 5;
    inline constexpr uint64_t kPlacementCompilerVersion = 1;
    inline constexpr uint64_t kPlacedFieldCompilerVersion = 1;
    inline constexpr uint64_t kClipmapLatticeScheduleCompilerVersion = 1;
    // Bumped 1 -> 2: AtmosphereData grew fog_saturation, packed into the
    // formerly-unused fog_params.w so the environment-lit haze can desaturate or
    // boost the sky colour it scatters. Recompile so cached atmospheres carry it.
    inline constexpr uint64_t kAtmosphereCompilerVersion = 2;
    inline constexpr uint64_t kFrameEnvironmentCompilerVersion = 1;
    inline constexpr uint64_t kTextureCompilerVersion = 1;
    // Bumped 2 -> 3: the collision-from-height-field recipe now accepts an
    // optional Placement input port that overrides its origin/size/vertical/
    // base when connected (issue #218 Phase 1). Bump invalidates stale caches.
    // Bumped 3 -> 4: placement-driven collision now records a placement_driven
    // flag in its compiled data so the runtime skips the carrying node's
    // transform (issue #224); compiled-output semantics changed.
    inline constexpr uint64_t kCollisionCompilerVersion = 4;
    inline constexpr uint64_t kSceneFromJSONCompilerVersion = 5;
    inline constexpr uint64_t kSceneFromGLBCompilerVersion = 1;
    inline constexpr uint64_t kDirectLightCompilerVersion = 1;
    inline constexpr uint64_t kAmbientLightingCompilerVersion = 1;
    inline constexpr uint64_t kHDRIEnvironmentCompilerVersion = 4;
    inline constexpr uint64_t kSkyGaussianFromJSONCompilerVersion = 1;
    inline constexpr uint64_t kStarCatalogFromJSONCompilerVersion = 1;
    inline constexpr uint64_t kStarCatalogFromPLYCompilerVersion = 1;
    inline constexpr uint64_t kAudioClipCompilerVersion = 1;
    inline constexpr uint64_t kAudioRenderableCompilerVersion = 1;
    inline constexpr uint64_t kAudioClipBankCompilerVersion = 1;
    inline constexpr uint64_t kAudioClipBankRenderableCompilerVersion = 1;
    inline constexpr uint64_t kAudioClipBankFromDirectoryCompilerVersion = 1;
    inline constexpr uint64_t kAudioGrainCloudRenderableCompilerVersion = 1;
}
