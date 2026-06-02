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
#include <engine/assets/scene/scene.h>
#include <engine/assets/scene/scene_asset_data.h>

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

    struct SceneFromGLBDesc
    {
        std::string name;
        wz::fs::Path path;
        std::optional<uint32_t> scene_index;
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
            FileCarrierAssetModule& files,
            JSONAssetModule& json,
            MeshAssetModule& meshes,
            MeshRenderStyleAssetModule& mesh_render_styles,
            RenderableAssetModule& renderables,
            SceneAssetTable& table);

        SceneAsset create_scene_from_json(const SceneFromJSONDesc& desc);

        SceneAsset create_scene_from_glb(const SceneFromGLBDesc& desc);

        SceneHandle get_scene(const SceneAsset& asset) const;

        const SceneAssetData* get_scene_data(SceneHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        FileCarrierAssetModule& files_;
        JSONAssetModule& json_;
        MeshAssetModule& meshes_;
        MeshRenderStyleAssetModule& mesh_render_styles_;
        RenderableAssetModule& renderables_;
        SceneAssetTable& table_;
    };

} // namespace wz::engine::assets
