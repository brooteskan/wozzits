#pragma once

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_fingerprint.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/rendering/render_resource_resolver.h>
#include <engine/rendering/render_program_pipeline_cache.h>
#include <engine/rendering/renderable_gpu_cache.h>
#include <engine/rendering/renderable_pipeline_cache.h>
#include <render/frame/render_frame.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    enum class SceneRuntimeBuildPhase
    {
        None = 0,
        Snapshot,
        MaterializeAssets,
        ResolveAssets,
        RealizeGpuResources,
        Instantiate,
        Propagate,
        CompileScene,
        BuildRenderIr,
        BuildRenderFrame,
        RebuildSkyCommands,
    };

    const char* scene_runtime_build_phase_name(
        SceneRuntimeBuildPhase phase) noexcept;

    struct SceneRuntimeBuildError
    {
        SceneRuntimeBuildPhase phase = SceneRuntimeBuildPhase::None;
        SceneRuntimeBuildPhase completed_phase =
            SceneRuntimeBuildPhase::None;
        std::string message;
        std::string context;

        [[nodiscard]] bool any() const noexcept
        {
            return phase != SceneRuntimeBuildPhase::None
                || !message.empty()
                || !context.empty();
        }
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
        SceneRuntimeBuildError error{};
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

    struct SceneRuntimeBundle
    {
        explicit SceneRuntimeBundle(
            wz::gpu::DeferredReleaseQueue& release_queue);

        SceneRuntimeBundle(const SceneRuntimeBundle&) = delete;
        SceneRuntimeBundle& operator=(const SceneRuntimeBundle&) = delete;
        SceneRuntimeBundle(SceneRuntimeBundle&&) = delete;
        SceneRuntimeBundle& operator=(SceneRuntimeBundle&&) = delete;

        std::unique_ptr<EngineAssetLibrary> assets{};
        SceneAssetData authored_scene{};
        SceneInstance scene_instance{};
        wz::engine::rendering::RenderResourceResolver resolver{};
        wz::engine::rendering::RenderableGpuCache renderable_cache;
        wz::engine::rendering::RenderablePipelineCache pipeline_cache{};
        wz::engine::rendering::RenderProgramPipelineCache
            render_program_cache{};
        wz::scene::CompiledSceneStorage compiled_scene{};
        wz::render::RenderIRStorage render_ir{};
        wz::render::RenderFrameStorage render_frame{};
        std::vector<wz::render::SkyDrawCommand> sky_commands{};
        uint64_t scene_hash = 0;
        std::string scene_hash_text;
        std::string status;
        bool valid = false;
    };

    struct SceneRuntimeBundleBuildResult
    {
        std::unique_ptr<SceneRuntimeBundle> bundle{};
        SceneRuntimeBuildError error{};
        SceneRuntimeBuildPhase completed_phase =
            SceneRuntimeBuildPhase::None;
        SceneRuntimeBuildPhase failed_phase = SceneRuntimeBuildPhase::None;
        std::string status;
        std::string error_detail;

        [[nodiscard]] bool ok() const noexcept
        {
            return bundle && bundle->valid && !error.any();
        }
        explicit operator bool() const noexcept { return ok(); }
    };

    SceneRuntimeBundleBuildResult build_scene_runtime_bundle(
        wz::gpu::DeferredReleaseQueue& release_queue,
        const SceneAssetData& authored,
        const SceneInstantiateContext& context = {},
        const SceneRuntimeBuildOptions& options = {});

    SceneRuntimeBundleBuildResult make_scene_runtime_bundle_build_failure(
        SceneRuntimeBuildPhase phase,
        SceneRuntimeBuildPhase completed_phase,
        std::string message,
        std::string context = {});

    bool commit_scene_runtime_bundle(
        SceneRuntimeBundle& live,
        SceneRuntimeBundleBuildResult candidate);
}
