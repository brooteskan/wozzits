// src/engine/assets/scene_asset_module.cpp

#include <engine/assets/scene_asset_module.h>

#include <engine/assets/gltf/gltf_importer.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/scene.h>
#include <asset/key_utils.h>

#include <algorithm>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        wz::asset::Hash scene_reference_bindings_hash(
            std::vector<SceneAssetReferenceBinding> refs,
            std::vector<SceneAssetReferenceBinding> collision_refs = {},
            std::vector<SceneAssetReferenceBinding> terrain_refs = {},
            std::vector<SceneAssetReferenceBinding> mesh_refs = {},
            std::vector<SceneAssetReferenceBinding> scalar_field_refs = {},
            std::vector<SceneAssetReferenceBinding> vector_field_refs = {})
        {
            refs.insert(
                refs.end(),
                collision_refs.begin(),
                collision_refs.end());
            refs.insert(
                refs.end(),
                terrain_refs.begin(),
                terrain_refs.end());
            refs.insert(
                refs.end(),
                mesh_refs.begin(),
                mesh_refs.end());
            refs.insert(
                refs.end(),
                scalar_field_refs.begin(),
                scalar_field_refs.end());
            refs.insert(
                refs.end(),
                vector_field_refs.begin(),
                vector_field_refs.end());

            if (refs.empty())
                return {};

            std::sort(refs.begin(), refs.end(),
                [](const auto& a, const auto& b) {
                    return a.uri < b.uri;
                });

            uint64_t lo = 0;
            uint64_t hi = 0;

            for (const auto& ref : refs) {
                const auto uri_hash = detail::hash_str(ref.uri);
                const auto key_hash = detail::key_to_dep_hash(ref.key);

                lo = detail::mix64(lo, uri_hash.lo);
                lo = detail::mix64(lo, key_hash.lo);
                hi = detail::mix64(hi, uri_hash.hi);
                hi = detail::mix64(hi, key_hash.hi);
            }

            return { lo, hi };
        }

        void append_unique_dependency(
            std::vector<wz::asset::AssetKey>& deps,
            wz::asset::AssetKey key)
        {
            wz::asset::append_unique_non_empty_key(deps, key);
        }

        void append_reference_dependencies(
            std::vector<wz::asset::AssetKey>& deps,
            const std::vector<SceneAssetReferenceBinding>& refs)
        {
            for (const auto& ref : refs)
                append_unique_dependency(deps, ref.key);
        }

        wz::asset::Hash glb_scene_bindings_hash(
            const std::vector<SceneGLBMeshRenderableBinding>& bindings)
        {
            uint64_t lo = 0;
            uint64_t hi = 0;

            for (const auto& binding : bindings) {
                const auto mesh_hash =
                    detail::key_to_dep_hash(binding.mesh_asset);
                const auto renderable_hash =
                    detail::key_to_dep_hash(binding.renderable_asset);

                lo = detail::mix64(lo, binding.mesh_index);
                lo = detail::mix64(lo, mesh_hash.lo);
                lo = detail::mix64(lo, renderable_hash.lo);
                hi = detail::mix64(hi, mesh_hash.hi);
                hi = detail::mix64(hi, renderable_hash.hi);
            }

            return { lo, hi };
        }
    }

    SceneAssetModule::SceneAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        JSONAssetModule& json,
        MeshAssetModule& meshes,
        MeshRenderStyleAssetModule& mesh_render_styles,
        RenderableAssetModule& renderables,
        SceneAssetTable& table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , json_(json)
        , meshes_(meshes)
        , mesh_render_styles_(mesh_render_styles)
        , renderables_(renderables)
        , table_(table)
    {
    }

    SceneAsset SceneAssetModule::create_scene_from_json(
        const SceneFromJSONDesc& desc)
    {
        const auto json_asset = json_.create_json({
            .name = desc.name + "_json",
            .path = desc.path,
        });

        if (!json_asset.valid()) {
            logger_.error("failed to create JSON document for scene: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey scene_key =
            make_scene_from_json_key(
                json_asset.output,
                scene_reference_bindings_hash(
                    desc.renderable_asset_references,
                    desc.collision_asset_references,
                    desc.terrain_asset_references,
                    desc.mesh_asset_references,
                    desc.scalar_field_asset_references,
                    desc.vector_field_asset_references));

        wz::asset::AssetNode node;
        node.key = scene_key;
        node.type = kAssetTypeScene;
        node.schema = kSceneFromJSONSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = SceneFromJSONCompileDesc{
            .renderable_asset_references =
                desc.renderable_asset_references,
            .collision_asset_references =
                desc.collision_asset_references,
            .terrain_asset_references = desc.terrain_asset_references,
            .mesh_asset_references = desc.mesh_asset_references,
            .scalar_field_asset_references =
                desc.scalar_field_asset_references,
            .vector_field_asset_references =
                desc.vector_field_asset_references,
        };

        std::vector<wz::asset::AssetKey> deps;
        append_unique_dependency(deps, json_asset.output);
        append_reference_dependencies(
            deps,
            desc.renderable_asset_references);
        append_reference_dependencies(
            deps,
            desc.collision_asset_references);
        append_reference_dependencies(
            deps,
            desc.terrain_asset_references);
        append_reference_dependencies(
            deps,
            desc.mesh_asset_references);
        append_reference_dependencies(
            deps,
            desc.scalar_field_asset_references);
        append_reference_dependencies(
            deps,
            desc.vector_field_asset_references);

        if (!system_.register_asset(std::move(node), std::move(deps))) {
            logger_.error("failed to register scene node: " + desc.name);
            return {};
        }

        return SceneAsset{ .output = scene_key };
    }

    SceneAsset SceneAssetModule::create_scene_from_glb(
        const SceneFromGLBDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("GLB scene has empty name");
            return {};
        }

        const wz::asset::AssetKey file_key =
            files_.register_file_node(
                desc.path,
                kRawFileSchema,
                kAssetTypeRawFile);

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register GLB file for scene: " + desc.name);
            return {};
        }

        const auto file_result =
            wz::fs::read_file(files_.resolve_path(desc.path));
        if (!file_result) {
            logger_.error("failed to read GLB scene source: " + desc.path);
            return {};
        }

        ImportedGLTFScene imported{};
        std::string import_error;
        if (!import_gltf_scene(
                file_result.value.data(),
                file_result.value.size(),
                GLTFSceneImportOptions{ .scene_index = desc.scene_index },
                imported,
                &import_error))
        {
            logger_.error(
                "failed to discover GLB scene '" + desc.name + "': "
                + import_error);
            return {};
        }

        MeshRenderStyleAsset default_style{};
        if (!imported.mesh_indices.empty()) {
            MeshRenderStyleData style{};
            default_style = mesh_render_styles_.create_mesh_render_style({
                .name = desc.name + "/default_mesh_style",
                .style = style,
            });
            if (!default_style.valid()) {
                logger_.error(
                    "failed to register GLB scene default style: "
                    + desc.name);
                return {};
            }
        }

        std::vector<SceneGLBMeshRenderableBinding> bindings;
        bindings.reserve(imported.mesh_indices.size());

        for (const uint32_t mesh_index : imported.mesh_indices) {
            MeshAsset mesh = meshes_.create_glb_mesh({
                .name = desc.name + "/mesh_" + std::to_string(mesh_index),
                .source_file = file_key,
                .mesh_index = mesh_index,
            });
            if (!mesh.valid()) {
                logger_.error("failed to register GLB mesh for scene: "
                    + desc.name);
                return {};
            }

            RenderableAsset renderable = renderables_.create_mesh_styled({
                .name = desc.name + "/renderable_" + std::to_string(mesh_index),
                .mesh = mesh,
                .style = default_style,
            });
            if (!renderable.valid()) {
                logger_.error("failed to register GLB renderable for scene: "
                    + desc.name);
                return {};
            }

            bindings.push_back(SceneGLBMeshRenderableBinding{
                .mesh_index = mesh_index,
                .mesh_asset = mesh.output,
                .renderable_asset = renderable.output,
            });
        }

        const wz::asset::AssetKey scene_key =
            make_scene_from_glb_key(
                file_key,
                imported.scene_index,
                glb_scene_bindings_hash(bindings));

        wz::asset::AssetNode node;
        node.key = scene_key;
        node.type = kAssetTypeScene;
        node.schema = kSceneFromGLBSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = SceneFromGLBCompileDesc{
            .scene_index = imported.scene_index,
            .mesh_renderables = bindings,
        };

        std::vector<wz::asset::AssetKey> deps;
        append_unique_dependency(deps, file_key);
        if (default_style.valid())
            append_unique_dependency(deps, default_style.output);
        for (const auto& binding : bindings) {
            append_unique_dependency(deps, binding.mesh_asset);
            append_unique_dependency(deps, binding.renderable_asset);
        }

        if (!system_.register_asset(std::move(node), std::move(deps))) {
            logger_.error("failed to register GLB scene node: " + desc.name);
            return {};
        }

        return SceneAsset{ .output = scene_key };
    }

    SceneHandle SceneAssetModule::get_scene(const SceneAsset& asset) const
    {
        if (!asset.valid())
            return {};

        SceneHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("scene handle not found");

        return out;
    }

    const SceneAssetData* SceneAssetModule::get_scene_data(
        SceneHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets
