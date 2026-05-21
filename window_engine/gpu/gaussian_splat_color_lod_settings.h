#pragma once

// gpu/gaussian_splat_color_lod_settings.h
//
// Scene-wide settings consumed by the NeighborhoodColorBlend splat shader.
//
// These cannot live on DrawCommand (DrawCommand is owned by scene-render and
// out of scope for v1 changes), so the toolhost pushes them per-frame onto
// the GPU device and the dx12 submit path picks them up when populating the
// shader's root constants.
//
// When no LOD settings have been pushed, defaults are used (lod_mode = 0 →
// Natural / no blend).  Pushing zeroed settings is equivalent to disabling
// the LOD feature.

#include <gpu/gpu.h>

#include <cstdint>

namespace wz::gpu
{
    enum class SplatColorLODMode : uint32_t
    {
        Natural    = 0,  // base color only (pre-LOD behaviour)
        LODOnly    = 1,  // neighborhood color only (debug)
        Confidence = 2,  // confidence as grayscale (debug)
        Blended    = 3,  // smoothstep distance/stride blend (final mode)
    };

    struct SplatColorLODSettings
    {
        SplatColorLODMode mode = SplatColorLODMode::Natural;

        // Overall blend strength applied on top of the mode's natural mix.
        // 0 = no LOD influence (always base color), 1 = full LOD influence.
        float strength = 1.0f;

        // Distance smoothstep band, in view-space Z (world units).
        // Below `near_distance`     → no distance-driven blend.
        // Above `far_distance`      → full distance-driven blend.
        float near_distance = 5.0f;
        float far_distance  = 50.0f;

        // Stride normalization.  The shader receives a per-draw stride ratio
        // (rendered_count / total_count, in [0,1]).  Mapping to a blend
        // amount uses:
        //   stride_factor = smoothstep(1, max_stride_for_blend, effective_stride)
        // where effective_stride = 1 / ratio (clamped).
        // max_stride_for_blend = 1 disables the stride influence entirely.
        float max_stride_for_blend = 128.0f;

        // Multiply final blend amount by per-splat confidence.  Low-variance
        // (high-confidence) splats blend more freely toward neighborhood
        // color; high-variance regions keep more original detail.
        bool use_confidence = true;
    };

    // Push scene-wide splat color LOD settings onto the device.  Call this
    // once per frame from the toolhost before any draws.  The settings
    // persist until the next call.
    void set_splat_color_lod_settings(
        Device& device,
        const SplatColorLODSettings& settings);
}
