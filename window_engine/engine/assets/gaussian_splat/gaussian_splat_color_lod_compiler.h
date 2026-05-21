#pragma once

// engine/assets/gaussian_splat/gaussian_splat_color_lod_compiler.h
//
// Deterministic compiler for GaussianSplatColorLOD assets.
//
// The core neighbor pass is exposed as a free function so it can be unit-
// tested without going through asset registration / compile dispatch.

#include <asset/compiler.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <logging/logger.h>

namespace wz::engine::assets
{
    // ─── Core neighborhood pass (testable in isolation) ─────────────────────
    //
    // Computes per-splat prefiltered neighborhood color + confidence using
    // a deterministic uniform-grid neighbor search.
    //
    // Determinism guarantees (for identical inputs + desc):
    //   • Grid cell hash is stable.
    //   • Neighbor cell iteration order is fixed: (dz, dy, dx) in -1..+1.
    //   • Splat indices within each cell are stored in ascending order and
    //     accumulated in that order.
    //   • All sums use sequential float addition; no parallelism in v1.
    //
    // Output arrays are sized to match cloud.splats.size().
    //
    // If `stats_out` is non-null, fills it with diagnostic info.

    [[nodiscard]] GaussianSplatColorLODData compute_gaussian_splat_color_lod(
        const GaussianSplatCloudData& cloud,
        const GaussianSplatColorLODCompileDesc& desc,
        GaussianSplatColorLODCompileStats* stats_out = nullptr);


    // ─── Compiler registration ───────────────────────────────────────────────

    namespace internal
    {
        void register_gaussian_splat_color_lod_compiler(
            wz::asset::CompilerRegistry& registry,
            wz::Logger& logger,
            GaussianSplatCloudTable& cloud_table,
            GaussianSplatColorLODTable& lod_table);
    }
}
