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

    inline constexpr uint64_t kHLSLCompilerVersion = 1;
    inline constexpr uint64_t kScalarFieldCompilerVersion = 1;
    inline constexpr uint64_t kGaussianSplatCompilerVersion = 1;
}