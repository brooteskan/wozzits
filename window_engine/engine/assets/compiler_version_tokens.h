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
    inline constexpr uint64_t kGaussianSplatCompilerVersion = 1;
    inline constexpr uint64_t kCSVCompilerVersion = 1;
    inline constexpr uint64_t kJSONDocumentCompilerVersion = 1;
    inline constexpr uint64_t kTOMLDocumentCompilerVersion = 1;
}