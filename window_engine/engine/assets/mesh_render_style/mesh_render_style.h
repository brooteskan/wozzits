#pragma once

// engine/assets/mesh_render_style/mesh_render_style.h

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    constexpr float kMeshRenderStyleOpaqueAlpha = 0.999f;

    struct MeshRenderLayerStyle
    {
        bool enabled = false;
        float color[4]{ 0.0f, 1.0f, 0.15f, 1.0f };
        float emissive_strength = 1.0f;

        bool valid() const noexcept;
    };

    enum class MeshFieldVisualizationPalette : uint8_t
    {
        Heat = 0,
        Grayscale,
        Diverging,
    };

    struct MeshFieldVisualizationStyle
    {
        bool enabled = false;
        uint32_t channel_id = 0;
        float value_min = 0.0f;
        float value_max = 1.0f;
        float gamma = 1.0f;
        MeshFieldVisualizationPalette palette =
            MeshFieldVisualizationPalette::Heat;

        bool valid() const noexcept;
    };

    enum class MeshMaskDomain : uint8_t
    {
        Face = 0,
        Vertex,
        Edge,
    };

    enum class MeshMaskProjectionMode : uint8_t
    {
        Direct = 0,
        Any,
        All,
        Majority,
    };

    enum class MeshMaskOverlapMode : uint8_t
    {
        Priority = 0,
        AlphaBlend,
    };

    struct MeshMaskRule
    {
        bool enabled = true;
        uint32_t input_channel_id = 0;
        float lo = 0.0f;
        float hi = 1.0f;
        float color[4]{ 1.0f, 0.15f, 0.05f, 1.0f };
        int32_t priority = 0;

        bool valid() const noexcept;
    };

    inline constexpr uint32_t kMaxMeshMaskRules = 16u;

    struct MeshMaskRenderStyleData
    {
        bool enabled = false;
        MeshMaskDomain domain = MeshMaskDomain::Face;
        MeshMaskProjectionMode projection_mode = MeshMaskProjectionMode::Direct;
        MeshMaskOverlapMode overlap_mode = MeshMaskOverlapMode::Priority;
        float unmatched_color[4]{ 0.18f, 0.18f, 0.18f, 1.0f };
        bool show_unmatched = true;
        std::vector<MeshMaskRule> rules{};

        bool valid() const noexcept;
    };

    struct MeshRenderStyleData
    {
        MeshRenderLayerStyle wireframe{
            true,
            { 0.0f, 1.0f, 0.15f, 1.0f },
            1.0f,
        };
        MeshRenderLayerStyle surface{
            false,
            { 0.25f, 0.9f, 0.35f, 1.0f },
            0.0f,
        };
        float alpha = 1.0f;
        bool depth_test = true;
        bool depth_write = false;
        bool double_sided = true;
        bool hidden_line_prepass = true;
        MeshFieldVisualizationStyle field_visualization{};
        MeshMaskRenderStyleData mask{};

        bool valid() const noexcept;
    };

    [[nodiscard]] inline bool is_mesh_render_style_transparent(
        const MeshRenderStyleData& style) noexcept
    {
        return style.alpha < kMeshRenderStyleOpaqueAlpha;
    }

    [[nodiscard]] inline bool is_mesh_render_style_drawable(
        const MeshRenderStyleData& style) noexcept
    {
        return style.wireframe.enabled
            || style.surface.enabled
            || style.field_visualization.enabled
            || style.mask.enabled;
    }

    struct MeshRenderStyleCompileDesc
    {
        MeshRenderStyleData style{};
    };

    class MeshRenderStyleTable
    {
    public:
        MeshRenderStyleTable();

        wz::asset::ResourceHandle add(MeshRenderStyleData data);
        const MeshRenderStyleData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<MeshRenderStyleData> styles_;
        std::vector<uint32_t> epochs_;
    };
}
