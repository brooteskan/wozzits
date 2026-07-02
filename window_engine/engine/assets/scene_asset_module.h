#pragma once

// engine/assets/scene_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/json_asset_module.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_render_style_asset_module.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/scene/scene.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <gpu/gpu.h>

#include <optional>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct SceneFromJSONDesc
    {
        std::string  name;
        wz::fs::Path path;
        std::vector<SceneAssetReferenceBinding> renderable_asset_references;
        std::vector<SceneAssetReferenceBinding> collision_asset_references;
        std::vector<SceneAssetReferenceBinding> terrain_asset_references;
        std::vector<SceneAssetReferenceBinding> mesh_asset_references;
        std::vector<SceneAssetReferenceBinding> scalar_field_asset_references;
        std::vector<SceneAssetReferenceBinding> vector_field_asset_references;
    };

    // Per-component render-style override for a GLB scene import (issue #213).
    // Keyed by GLB mesh index (the common case: distinct meshes per part). An
    // override naming a mesh index not present in the imported scene is ignored.
    struct SceneFromGLBStyleOverride
    {
        uint32_t mesh_index = 0;
        MeshRenderStyleData style{};
    };

    struct SceneFromGLBDesc
    {
        std::string name;
        wz::fs::Path path;
        std::optional<uint32_t> scene_index;

        // Per-component style mapping (issue #213): a base style applied to every
        // mesh, plus sparse per-mesh-index overrides. When base_style is unset the
        // built-in default MeshRenderStyleData is used (reproducing prior
        // behavior). The resolved style for a mesh is the override for its index
        // if present, else the base. The editor resolves GLB subtree -> mesh-index
        // before building this mapping in a later phase.
        std::optional<MeshRenderStyleData> base_style;
        std::vector<SceneFromGLBStyleOverride> style_overrides;
    };

    struct SceneAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct SceneHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class SceneAssetModule
    {
    public:
        SceneAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            wz::gpu::Device& device,
            FileCarrierAssetModule& files,
            JSONAssetModule& json,
            MeshAssetModule& meshes,
            MeshRenderStyleAssetModule& mesh_render_styles,
            RenderableAssetModule& renderables,
            ShaderAssetModule& shaders,
            RenderProgramAssetModule& render_programs,
            SceneAssetTable& table);

        SceneAsset create_scene_from_json(const SceneFromJSONDesc& desc);

        SceneAsset create_scene_from_glb(const SceneFromGLBDesc& desc);

        SceneHandle get_scene(const SceneAsset& asset) const;

        const SceneAssetData* get_scene_data(SceneHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        // GPU device (validity gate): shader assets cannot compile without a
        // device, so the GLB import only provisions the mesh_style render
        // program + 0x706 renderable when the device is valid. Deviceless, a
        // GLB part gets no renderable — matching the app model where a node
        // with geometry but no program does not draw.
        wz::gpu::Device& device_;
        FileCarrierAssetModule& files_;
        JSONAssetModule& json_;
        MeshAssetModule& meshes_;
        MeshRenderStyleAssetModule& mesh_render_styles_;
        RenderableAssetModule& renderables_;
        ShaderAssetModule& shaders_;
        RenderProgramAssetModule& render_programs_;
        SceneAssetTable& table_;
    };

} // namespace wz::engine::assets
