#pragma once

// engine/assets/gaussian_splat/gaussian_splat_decode.h
//
// CPU-side decode of a GaussianSplat (PLY/asset-layer encoding) into
// world-space values ready for rendering or reconstruction.
//
// Centralizes the four decode steps that previously lived in:
//   - DX12 GPU upload (upload_gaussian_splat_cloud_dx12)
//   - terrain surface compiler
//   - terrain benchmark toolhost (capture_splats_for_recon)
//
// Rotation convention
// -------------------
// GaussianSplat::rotation[] uses PLY convention: [0]=w, [1]=x, [2]=y, [3]=z.
// DecodedGaussianSplat::rotation uses wz::math::Quaternion: {x, y, z, w}.
// The axis fields (axis_x/y/z) are extracted from the rotation and represent
// the world-space columns of the splat's local frame.

#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <math/math_types.h>

namespace wz::engine::assets
{
    struct DecodedGaussianSplat
    {
        wz::math::Vec3       position;    // world-space position (passthrough)
        wz::math::Vec3       scale;       // exp(log_scale) — world-space extents
        wz::math::Quaternion rotation;    // {x, y, z, w} convention
        float                opacity;     // sigmoid(logit_opacity) ∈ (0, 1)
        wz::math::Vec3       color;       // display RGB, decoded from SH DC ∈ [0, 1]

        // Rotation matrix columns — the splat's local coordinate frame.
        wz::math::Vec3       axis_x;      // tangent U
        wz::math::Vec3       axis_y;      // normal  (up for terrain)
        wz::math::Vec3       axis_z;      // tangent V
    };

    // Decode a single GaussianSplat from its stored (log-scale, logit-opacity,
    // SH-DC color, PLY-convention quaternion) encoding into world-space values.
    //
    // The input splat's color_rest / f_rest fields are ignored — only the DC
    // (zeroth-order SH) term is decoded.
    DecodedGaussianSplat decode_splat(const GaussianSplat& s);

}  // namespace wz::engine::assets
