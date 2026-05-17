#pragma once

// engine/assets/renderable/renderable.h

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    enum class RenderableKind : uint8_t
    {
        Mesh,
        GaussianSplatCloud,
        ScalarField,
    };

    enum class RenderDomain : uint8_t
    {
        Debug,
        Opaque,
        Transparent,
        Splat,
    };

    enum class BuiltinRenderProgram : uint8_t
    {
        MeshWireframeDebug,
        GaussianSplatDebug,
        ScalarFieldDebug,

        Count  // sentinel — keep last
    };

    static constexpr size_t kBuiltinRenderProgramCount =
        static_cast<size_t>(BuiltinRenderProgram::Count);

    enum RenderPolicyFlags : uint32_t
    {
        RenderPolicy_None = 0,
        RenderPolicy_Wireframe = 1u << 0,
        RenderPolicy_AlphaBlend = 1u << 1,
        RenderPolicy_DepthTest = 1u << 2,
        RenderPolicy_DepthWrite = 1u << 3,
    };

    struct RenderableAssetData
    {
        RenderableKind kind{};
        wz::asset::AssetKey source_asset{};

        BuiltinRenderProgram program{};
        RenderDomain domain{};
        uint32_t policy_flags = RenderPolicy_None;

        float bounds_min[3]{};
        float bounds_max[3]{};

        bool valid() const noexcept;
    };

    class RenderableAssetTable
    {
    public:
        RenderableAssetTable();

        wz::asset::ResourceHandle add(RenderableAssetData data);
        const RenderableAssetData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<RenderableAssetData> renderables_;
        std::vector<uint32_t> epochs_;
    };

    struct MeshWireframeRenderableCompileDesc
    {
        wz::asset::AssetKey mesh_asset{};
    };

    struct GaussianSplatDebugRenderableCompileDesc
    {
        wz::asset::AssetKey splat_cloud_asset{};
    };

    struct ScalarFieldDebugRenderableCompileDesc
    {
        wz::asset::AssetKey scalar_field_asset{};
    };
}