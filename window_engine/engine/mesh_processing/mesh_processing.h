#pragma once

// engine/mesh_processing/mesh_processing.h

#include <engine/assets/mesh/mesh.h>

#include <cstdint>
#include <string>

namespace wz::engine::mesh_processing
{
    struct MeshDecimationDesc
    {
        uint32_t target_vertex_count = 0;
        uint32_t target_triangle_count = 0;
        float target_ratio = 0.0f;

        bool preserve_boundary = true;

        float aspect_ratio = 0.0f;
        float edge_length = 0.0f;
        uint32_t max_valence = 0;
        float normal_deviation = 0.0f;
        float hausdorff_error = 0.0f;
    };

    struct MeshProcessingResult
    {
        bool ok = false;
        wz::engine::assets::MeshData mesh;
        std::string error;
    };

    [[nodiscard]] MeshProcessingResult decimate_mesh(
        const wz::engine::assets::MeshData& source,
        const MeshDecimationDesc& desc);
}
