#pragma once

// engine/assets/render_program/render_program.h

#include <asset/types.h>
#include <engine/assets/renderable/renderable.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    enum class ShaderResourceKind : uint8_t
    {
        ConstantBuffer,
        StructuredBuffer,
        Texture2D,
        Sampler,
    };

    enum class ShaderVisibility : uint8_t
    {
        All,
        Vertex,
        Pixel,
    };

    struct ShaderResourceBinding
    {
        ShaderResourceKind kind{};
        ShaderVisibility visibility{};

        uint32_t shader_register = 0;
        uint32_t register_space = 0;

        // ConstantBuffer: number of 32-bit values.
        // StructuredBuffer / Texture2D / Sampler: descriptor range size (1 = single binding).
        uint32_t count = 0;
    };

    enum class RenderBindingModel : uint8_t
    {
        MeshIA,
        SplatVertexInstanced,
        SplatPull,
        ScalarFieldTexture,
        Fullscreen,
        ParticlePull,
    };

    enum class RenderPrimitiveTopology : uint8_t
    {
        TriangleList,
        TriangleStrip,
    };

    // dep[0] = vertex_shader key, dep[1] = pixel_shader key.
    struct BuiltinRenderProgramDesc
    {
        std::string name;
        BuiltinRenderProgram program{};
        wz::asset::AssetKey vertex_shader{};
        wz::asset::AssetKey pixel_shader{};
    };

    struct RenderProgramData
    {
        BuiltinRenderProgram builtin_program{};

        RenderBindingModel binding_model{};
        RenderPrimitiveTopology topology{};

        RenderDomain default_domain{};
        uint32_t default_policy_flags = RenderPolicy_None;

        // Root parameters in declaration order.
        // Vector index == DX12 root parameter index.
        std::vector<ShaderResourceBinding> bindings;

        wz::asset::ResourceHandle vertex_shader{};
        wz::asset::ResourceHandle pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader.valid() && pixel_shader.valid();
        }
    };

    class RenderProgramTable
    {
    public:
        RenderProgramTable();

        wz::asset::ResourceHandle add(RenderProgramData data);
        const RenderProgramData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<RenderProgramData> programs_;
        std::vector<uint32_t> epochs_;
    };
}