#pragma once

#include <scene/compile/compiled_scene.h>
#include <scene/compile/terrain_lod_selector.h>

#include <vector>

namespace wz::scene
{
    struct TerrainTransitionStripInfo
    {
        TerrainChunkId chunk_id{};
        TerrainChunkId neighbor_chunk_id{ kInvalidTerrainChunkId };
        TerrainLodId lod_id{};
        TerrainLodId neighbor_lod_id{};
        TerrainVisualProxyBoundaryEdge edge =
            TerrainVisualProxyBoundaryEdge::NegativeX;
    };

    uint32_t terrain_visual_proxy_transition_strip_count(
        const wz::engine::assets::TerrainVisualProxyData* proxy) noexcept;

    void append_terrain_chunk_infos(
        std::vector<TerrainChunkInfo>& out,
        const TerrainVisualInstance& instance,
        uint32_t terrain_instance_index);

    void append_terrain_transition_strips(
        std::vector<TerrainTransitionStripInfo>& out,
        const TerrainVisualInstance& instance);
}
