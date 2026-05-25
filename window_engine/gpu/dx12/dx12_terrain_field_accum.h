#pragma once

// gpu/dx12/dx12_terrain_field_accum.h
//
// DX12 terrain field accumulation renderer.
//
// Two-pass approach:
//   1. Accumulation pass — draws splat cloud additively into offscreen
//      RGBA16F render targets (weighted color + normal), using the existing
//      depth buffer for occlusion.
//   2. Resolve pass — fullscreen triangle reads the accumulation textures
//      and writes a lit result to the backbuffer.
//
// Owns all DX12 resources (render targets, heaps, PSOs, root signatures).
// No engine-layer or asset-system dependencies beyond gpu types.

#include <gpu/gpu.h>
#include <gpu/gaussian_splat_coverage_settings.h>

#include <cstdint>
#include <memory>
#include <span>

namespace wz::gpu::dx12
{
    // ─── Resolve shading settings ────────────────────────────────────────

    struct TerrainFieldAccumSettings
    {
        float weight_scale     = 1.0f;
        float ambient          = 0.15f;
        float diffuse_strength = 0.85f;
        float light_dir[3]     = { 0.3f, 0.8f, 0.4f };
        int   debug_mode       = 0;  // 0=lit, 1=weight, 2=normal, 3=albedo
    };

    // ─── Per-draw splat batch ────────────────────────────────────────────
    //
    // The caller resolves DrawCommands + SplatHandles into these.  The
    // renderer does not know about the asset system or resolver.

    struct FieldAccumSplatDraw
    {
        float     world[16];         // column-major world transform
        GPUHandle splat_cloud;       // handle to uploaded DX12 splat cloud
        uint32_t  instance_count;    // 0 = use cloud's splat_count
    };

    // ─── Renderer ────────────────────────────────────────────────────────

    class TerrainFieldAccumRenderer
    {
    public:
        TerrainFieldAccumRenderer();
        ~TerrainFieldAccumRenderer();

        TerrainFieldAccumRenderer(const TerrainFieldAccumRenderer&) = delete;
        TerrainFieldAccumRenderer& operator=(const TerrainFieldAccumRenderer&) = delete;

        bool init(Device& device, uint32_t width, uint32_t height);
        void release();

        bool ready() const noexcept;

        // Full 2-pass render: accumulate splats, then resolve to backbuffer.
        void render(
            Device& device,
            std::span<const FieldAccumSplatDraw> draws,
            const float view_projection[16],
            const SplatCoverageSettings& coverage,
            const TerrainFieldAccumSettings& settings);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}  // namespace wz::gpu::dx12
