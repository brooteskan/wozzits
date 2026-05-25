#pragma once

// gpu/dx12/dx12_terrain_surface_mesh.h
//
// DX12 renderer for terrain surface reconstruction meshes.
//
// Draws a displaced triangle mesh produced by reconstruct_terrain_grid().
// Owns root signature, solid + wireframe PSOs, and UPLOAD heap
// vertex/index buffers.
//
// The CPU-side reconstruction (TerrainReconResult) is produced externally;
// this renderer only handles GPU upload and drawing.

#include <gpu/gpu.h>

#include <cstdint>
#include <memory>
#include <span>

namespace wz::engine::assets { struct TerrainReconVertex; }

namespace wz::gpu::dx12
{
    // ─── Draw settings ───────────────────────────────────────────────────

    struct TerrainSurfaceMeshSettings
    {
        bool  wireframe        = false;
        float ambient          = 0.15f;
        float diffuse_strength = 0.85f;
        float light_dir[3]     = { 0.3f, 0.8f, 0.4f };
        int   debug_mode       = 0;  // 0=lit, 1=height, 2=normal, 3=weight, 4=albedo
        float height_min       = 0.0f;
        float height_max       = 1.0f;
    };

    // ─── Renderer ────────────────────────────────────────────────────────

    class TerrainSurfaceMeshRenderer
    {
    public:
        TerrainSurfaceMeshRenderer();
        ~TerrainSurfaceMeshRenderer();

        TerrainSurfaceMeshRenderer(const TerrainSurfaceMeshRenderer&) = delete;
        TerrainSurfaceMeshRenderer& operator=(const TerrainSurfaceMeshRenderer&) = delete;

        bool init(Device& device);
        void release();

        bool pipeline_ready() const noexcept;
        bool mesh_ready()     const noexcept;

        // Upload vertex + index data from a completed CPU reconstruction.
        bool upload(
            Device& device,
            std::span<const wz::engine::assets::TerrainReconVertex> vertices,
            std::span<const uint32_t> indices);

        // Draw the uploaded mesh.
        void draw(
            Device& device,
            const float view_projection[16],
            const TerrainSurfaceMeshSettings& settings);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}  // namespace wz::gpu::dx12
