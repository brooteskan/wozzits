#pragma once

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_fingerprint.h>
#include <engine/assets/scene/scene_instance.h>
#include <render/frame/render_frame.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    enum class SceneRuntimeBuildPhase
    {
        None = 0,
        Snapshot,
        Instantiate,
        Propagate,
        CompileScene,
        BuildRenderIr,
        BuildRenderFrame,
    };

    struct SceneRuntimeBuildOptions
    {
        wz::scene::ViewData initial_view{};
        std::span<const wz::render::SkyDrawCommand> sky_commands{};
        bool build_render_storage = true;
    };

    // Lower-level construction result shared by editor preview and future
    // benchmark adapters. This is not an app owner; callers decide where the
    // snapshot, runtime instance, frame storage, input policy, and metrics live.
    //
    // This is the authored-to-runtime boundary for the current scene language:
    // SceneAssetData is copied into a snapshot, instantiate_scene(...) compiles
    // that authored source into SceneInstance, and scene-render later compiles
    // the runtime scene data into render-oriented storage.
    struct SceneAssetRuntimeBuild
    {
        SceneAssetData snapshot{};
        SceneInstance instance{};
        wz::scene::CompiledSceneStorage compiled_scene{};
        wz::render::RenderIRStorage render_ir{};
        wz::render::RenderFrameStorage render_frame{};
        std::vector<wz::render::SkyDrawCommand> sky_commands;
        uint64_t scene_hash = 0;
        std::string scene_hash_text;
        std::string status;
        std::string error_detail;
        SceneRuntimeBuildPhase completed_phase =
            SceneRuntimeBuildPhase::None;
        SceneRuntimeBuildPhase failed_phase = SceneRuntimeBuildPhase::None;
        bool valid = false;

        SceneAssetRuntimeBuild() = default;
        SceneAssetRuntimeBuild(const SceneAssetRuntimeBuild&) = delete;
        SceneAssetRuntimeBuild& operator=(const SceneAssetRuntimeBuild&) =
            delete;
        SceneAssetRuntimeBuild(SceneAssetRuntimeBuild&&) noexcept = default;
        SceneAssetRuntimeBuild& operator=(
            SceneAssetRuntimeBuild&&) noexcept = default;

        [[nodiscard]] bool ok() const noexcept { return valid; }
        explicit operator bool() const noexcept { return ok(); }
    };

    SceneAssetRuntimeBuild build_scene_runtime_from_asset_snapshot(
        const SceneAssetData& authored,
        const SceneInstantiateContext& context = {},
        const SceneRuntimeBuildOptions& options = {});

    bool commit_scene_runtime_build(
        SceneAssetRuntimeBuild& live,
        SceneAssetRuntimeBuild candidate);
}
