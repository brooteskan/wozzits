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
        MeshWireframeDepthDebug,
        MeshDepthPrepassDebug,
        GaussianSplatDebug,
        ScalarFieldDebug,
        GaussianSplatPullDebug,  // pull-based splat: no IA, SRV at t0
        GaussianSplatNeighborhoodColorBlend,  // SplatPull + LOD color blend modes
        GaussianSplatTerrainCoverageDebug,    // SplatPull + coverage modes (depth-writing)

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

        // Optional companion asset key (e.g. GaussianSplatColorLOD).
        // Empty when not used.  The GPU realize step consults this to find
        // and pass through derived data to the upload pipeline.
        wz::asset::AssetKey companion_asset{};

        BuiltinRenderProgram program{};
        RenderDomain domain{};
        uint32_t policy_flags = RenderPolicy_None;

        // Optional: handle into RenderProgramTable.  Invalid until set by
        // call-site code that has resolved a RenderProgramAsset.
        // When valid, the submit path prefers this over the BuiltinRenderProgram.
        wz::asset::ResourceHandle render_program{};

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
        BuiltinRenderProgram program = BuiltinRenderProgram::MeshWireframeDebug;
        RenderDomain domain = RenderDomain::Debug;
        uint32_t policy_flags = RenderPolicy_Wireframe;
    };

    struct GaussianSplatDebugRenderableCompileDesc
    {
        wz::asset::AssetKey splat_cloud_asset{};

        // Optional derived color-LOD asset key.  Empty if not used.
        wz::asset::AssetKey color_lod_asset{};
    };

    struct ScalarFieldDebugRenderableCompileDesc
    {
        wz::asset::AssetKey scalar_field_asset{};
    };

    struct TerrainDebugRenderableCompileDesc
    {
        wz::asset::AssetKey terrain_asset{};
        BuiltinRenderProgram mesh_program =
            BuiltinRenderProgram::MeshWireframeDepthDebug;
        RenderDomain domain = RenderDomain::Debug;
        uint32_t mesh_policy_flags =
            RenderPolicy_Wireframe
            | RenderPolicy_DepthTest
            | RenderPolicy_DepthWrite;
    };
}
