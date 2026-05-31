#pragma once

// engine/assets/mesh_render_style/mesh_render_style.h

#include <asset/types.h>

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

        bool valid() const noexcept;
    };

    [[nodiscard]] inline bool is_mesh_render_style_transparent(
        const MeshRenderStyleData& style) noexcept
    {
        return style.alpha < kMeshRenderStyleOpaqueAlpha;
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
