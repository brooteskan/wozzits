#include <engine/assets/scene/scene_runtime_build.h>

#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>
#include <scene/scene_graph.h>

#include <utility>

namespace wz::engine::assets
{
    namespace
    {
        void mark_completed(
            SceneAssetRuntimeBuild& build,
            SceneRuntimeBuildPhase phase)
        {
            build.completed_phase = phase;
        }

        void mark_failed(
            SceneAssetRuntimeBuild& build,
            SceneRuntimeBuildPhase phase,
            std::string message,
            std::string context = {})
        {
            build.failed_phase = phase;
            build.error = SceneRuntimeBuildError{
                .phase = phase,
                .completed_phase = build.completed_phase,
                .message = std::move(message),
                .context = std::move(context),
            };
            build.error_detail = build.error.message;
            if (!build.error.context.empty()) {
                build.error_detail += ": ";
                build.error_detail += build.error.context;
            }
            build.status = build.error_detail;
            build.valid = false;
        }
    }

    const char* scene_runtime_build_phase_name(
        SceneRuntimeBuildPhase phase) noexcept
    {
        switch (phase) {
        case SceneRuntimeBuildPhase::None:
            return "none";
        case SceneRuntimeBuildPhase::Snapshot:
            return "snapshot";
        case SceneRuntimeBuildPhase::MaterializeAssets:
            return "materialize assets";
        case SceneRuntimeBuildPhase::ResolveAssets:
            return "resolve assets";
        case SceneRuntimeBuildPhase::RealizeGpuResources:
            return "realize gpu resources";
        case SceneRuntimeBuildPhase::Instantiate:
            return "instantiate";
        case SceneRuntimeBuildPhase::Propagate:
            return "propagate";
        case SceneRuntimeBuildPhase::CompileScene:
            return "compile scene";
        case SceneRuntimeBuildPhase::BuildRenderIr:
            return "build render ir";
        case SceneRuntimeBuildPhase::BuildRenderFrame:
            return "build render frame";
        case SceneRuntimeBuildPhase::RebuildSkyCommands:
            return "rebuild sky commands";
        }
        return "unknown";
    }

    SceneAssetRuntimeBuild build_scene_runtime_from_asset_snapshot(
        const SceneAssetData& authored,
        const SceneInstantiateContext& context,
        const SceneRuntimeBuildOptions& options)
    {
        SceneAssetRuntimeBuild build{};
        build.snapshot = authored;
        build.scene_hash = scene_asset_fingerprint(build.snapshot);
        build.scene_hash_text = scene_asset_fingerprint_string(build.snapshot);
        build.sky_commands.assign(
            options.sky_commands.begin(),
            options.sky_commands.end());
        mark_completed(build, SceneRuntimeBuildPhase::Snapshot);

        auto result = instantiate_scene(build.snapshot, context);
        if (!result.ok()) {
            mark_failed(
                build,
                SceneRuntimeBuildPhase::Instantiate,
                "instantiate failed",
                result.error_detail);
            return build;
        }

        build.instance = std::move(result.instance);
        mark_completed(build, SceneRuntimeBuildPhase::Instantiate);

        wz::scene::propagate_all(build.instance.storage.polytree);
        mark_completed(build, SceneRuntimeBuildPhase::Propagate);

        if (options.build_render_storage) {
            wz::scene::compile(
                build.compiled_scene,
                build.instance.storage.polytree,
                build.instance.renderables,
                build.instance.lights,
                options.initial_view);
            mark_completed(build, SceneRuntimeBuildPhase::CompileScene);

            wz::render::build_render_ir(
                build.render_ir,
                build.compiled_scene.scene);
            mark_completed(build, SceneRuntimeBuildPhase::BuildRenderIr);

            wz::render::build_frame(
                build.render_frame,
                build.render_ir.ir,
                build.compiled_scene.scene,
                build.sky_commands);
            mark_completed(build, SceneRuntimeBuildPhase::BuildRenderFrame);
        }

        build.status = "runtime scene ready: " + build.snapshot.name
            + " scene_hash=" + build.scene_hash_text;
        build.valid = true;
        return build;
    }

    bool commit_scene_runtime_build(
        SceneAssetRuntimeBuild& live,
        SceneAssetRuntimeBuild candidate)
    {
        if (!candidate.ok()) {
            return false;
        }

        live = std::move(candidate);
        return true;
    }
}
