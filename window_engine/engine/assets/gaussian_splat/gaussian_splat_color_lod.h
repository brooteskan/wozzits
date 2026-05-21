#pragma once

// engine/assets/gaussian_splat/gaussian_splat_color_lod.h
//
// Derived Gaussian splat color LOD product — per-splat prefiltered
// neighborhood color + confidence.
//
// Compiled from an existing GaussianSplatCloudData via a deterministic
// uniform-grid neighbor pass. Storage is parallel-array to
// GaussianSplatCloudData::splats: index i of the source cloud corresponds
// to index i of the LOD arrays.
//
// This is a CPU-side derived asset; GPU packing happens later in the
// upload pipeline, not here.

#include <asset/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    // ─── Compile descriptor ───────────────────────────────────────────────
    //
    // All fields are part of the asset's identity (hashed into the asset
    // key).  Changing any field invalidates the cached output.

    struct GaussianSplatColorLODCompileDesc
    {
        // Cell size for the uniform-grid neighbor search.  Also the maximum
        // radius at which a neighbor splat can contribute weight.
        float neighbor_radius_world = 0.05f;

        // Gaussian falloff sigma (world units).  Controls how quickly weight
        // drops with distance inside the search radius.
        float gaussian_sigma_world = 0.025f;

        // If true, the splat itself contributes to its own neighborhood
        // average (with full Gaussian weight at distance 0).
        bool include_self = true;

        // If true, multiply each neighbor's weight by its decoded opacity.
        bool use_opacity_weight = true;

        // Variance → confidence gain.  Higher gain means low confidence
        // is reached at lower variance.  confidence = 1 - clamp(var * gain).
        float color_variance_gain = 4.0f;
    };


    // ─── Compiled data ────────────────────────────────────────────────────
    //
    // Parallel arrays indexed by source splat index.

    struct GaussianSplatColorLODData
    {
        // Per-splat prefiltered neighborhood color, linear RGB in [0,1].
        // Stored as three floats per splat (R, G, B).
        std::vector<float> neighborhood_color;  // size = 3 * splat_count

        // Per-splat confidence in [0,1].
        //   high (close to 1) — low local variance, safe to blend toward
        //   low  (close to 0) — high local variance, keep original detail
        std::vector<float> lod_confidence;      // size = splat_count

        bool valid() const noexcept
        {
            return !lod_confidence.empty()
                && neighborhood_color.size() == 3 * lod_confidence.size();
        }

        uint64_t splat_count() const noexcept
        {
            return static_cast<uint64_t>(lod_confidence.size());
        }
    };


    // ─── Compile stats (logged after compile) ─────────────────────────────

    struct GaussianSplatColorLODCompileStats
    {
        uint64_t splat_count           = 0;
        uint64_t occupied_cell_count   = 0;
        uint32_t average_neighbor_count = 0;
        uint32_t max_neighbor_count    = 0;
        double   compile_time_ms       = 0.0;
    };


    // ─── Table ────────────────────────────────────────────────────────────

    class GaussianSplatColorLODTable
    {
    public:
        GaussianSplatColorLODTable();

        wz::asset::ResourceHandle add(GaussianSplatColorLODData data);
        const GaussianSplatColorLODData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<GaussianSplatColorLODData> entries_;
        std::vector<uint32_t> epochs_;
    };
}
