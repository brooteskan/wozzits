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
    inline constexpr uint64_t kVectorFieldCompilerVersion = 1;
    inline constexpr uint64_t kGaussianSplatCompilerVersion = 1;
    inline constexpr uint64_t kCSVCompilerVersion = 1;
    inline constexpr uint64_t kJSONDocumentCompilerVersion = 1;
    inline constexpr uint64_t kTOMLDocumentCompilerVersion = 1;
    inline constexpr uint64_t kMeshCompilerVersion = 2;
    inline constexpr uint64_t kMeshDerivedFieldCompilerVersion = 1;
    inline constexpr uint64_t kMeshWaveletAnalysisCompilerVersion = 2;
    inline constexpr uint64_t kBehaviorFieldPlaceholderCompilerVersion = 1;
    inline constexpr uint64_t kProceduralGaussianSplatCloudCompilerVersion = 1;
    inline constexpr uint64_t kInlineDataTableCompilerVersion = 1;
    inline constexpr uint64_t kDiagnosticTableResampleTimeSeriesCompilerVersion = 1;
    inline constexpr uint64_t kCSVExportCompilerVersion = 1;
    inline constexpr uint64_t kMeshRenderStyleCompilerVersion = 1;
    inline constexpr uint64_t kMeshWireframeRenderableCompilerVersion = 1;
    inline constexpr uint64_t kMeshStyledRenderableCompilerVersion = 2;
    inline constexpr uint64_t kGaussianSplatDebugRenderableCompilerVersion = 2;
    inline constexpr uint64_t kScalarFieldDebugRenderableCompilerVersion = 1;
    inline constexpr uint64_t kTerrainDebugRenderableCompilerVersion = 1;
    inline constexpr uint64_t kTerrainSurfaceRenderableCompilerVersion = 6;
    inline constexpr uint64_t kDiagnosticResampledTimeSeriesToDataTableCompilerVersion = 1;
    inline constexpr uint64_t kDiagnosticTimeframeSummaryCompilerVersion = 1;
    inline constexpr uint64_t kDiagnosticTimeframeSummaryToDataTableCompilerVersion = 1;
    inline constexpr uint64_t kBuiltinRenderProgramCompilerVersion = 4;
    inline constexpr uint64_t kCustomRenderProgramCompilerVersion = 1;
    inline constexpr uint64_t kComputePipelineCompilerVersion = 1;
    inline constexpr uint64_t kGaussianSplatFromScalarFieldCompilerVersion = 1;
    inline constexpr uint64_t kGaussianSplatColorLODCompilerVersion = 1;
    inline constexpr uint64_t kGaussianSplatTerrainSurfaceFromHeightFieldCompilerVersion = 2;
    inline constexpr uint64_t kTerrainSplatFromGaeaR32CompilerVersion = 2;
    inline constexpr uint64_t kTerrainCompilerVersion = 4;
    inline constexpr uint64_t kTerrainVisualProxyCompilerVersion = 5;
    inline constexpr uint64_t kCollisionCompilerVersion = 2;
    inline constexpr uint64_t kSceneFromJSONCompilerVersion = 4;
    inline constexpr uint64_t kSceneFromGLBCompilerVersion = 1;
    inline constexpr uint64_t kDirectLightCompilerVersion = 1;
    inline constexpr uint64_t kAmbientLightingCompilerVersion = 1;
    inline constexpr uint64_t kHDRIEnvironmentCompilerVersion = 4;
}
