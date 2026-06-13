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

        std::string error_detail(
            const SceneRuntimeBuildError& error)
        {
            std::string detail = error.message;
            if (!error.context.empty()) {
                detail += ": ";
                detail += error.context;
            }
            return detail;
        }

        SceneRuntimeBundleBuildResult failure_from_error(
            SceneRuntimeBuildError error)
        {
            SceneRuntimeBundleBuildResult result{};
            result.failed_phase = error.phase;
            result.completed_phase = error.completed_phase;
            result.error = std::move(error);
            result.error_detail = error_detail(result.error);
            result.status = result.error_detail;
            return result;
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

    SceneRuntimeBundle::SceneRuntimeBundle(
        wz::gpu::DeferredReleaseQueue& release_queue)
        : renderable_cache(release_queue)
    {
    }

    SceneRuntimeBundleBuildResult make_scene_runtime_bundle_build_failure(
        SceneRuntimeBuildPhase phase,
        SceneRuntimeBuildPhase completed_phase,
        std::string message,
        std::string context)
    {
        return failure_from_error(SceneRuntimeBuildError{
            .phase = phase,
            .completed_phase = completed_phase,
            .message = std::move(message),
            .context = std::move(context),
        });
    }

    SceneRuntimeBundleBuildResult build_scene_runtime_bundle(
        wz::gpu::DeferredReleaseQueue& release_queue,
        const SceneAssetData& authored,
        const SceneInstantiateContext& context,
        const SceneRuntimeBuildOptions& options)
    {
        SceneRuntimeBundleBuildResult result{};
        auto bundle = std::make_unique<SceneRuntimeBundle>(release_queue);

        SceneAssetRuntimeBuild runtime =
            build_scene_runtime_from_asset_snapshot(
                authored,
                context,
                options);
        if (!runtime.ok()) {
            result.bundle = std::move(bundle);
            result.completed_phase = runtime.completed_phase;
            result.failed_phase = runtime.failed_phase;
            result.error = runtime.error;
            result.error_detail = runtime.error_detail;
            result.status = runtime.status;
            return result;
        }

        bundle->authored_scene = std::move(runtime.snapshot);
        bundle->scene_instance = std::move(runtime.instance);
        bundle->compiled_scene = std::move(runtime.compiled_scene);
        bundle->render_ir = std::move(runtime.render_ir);
        bundle->render_frame = std::move(runtime.render_frame);
        bundle->sky_commands = std::move(runtime.sky_commands);
        bundle->scene_hash = runtime.scene_hash;
        bundle->scene_hash_text = std::move(runtime.scene_hash_text);
        bundle->status = std::move(runtime.status);
        bundle->valid = true;

        result.completed_phase = runtime.completed_phase;
        result.failed_phase = SceneRuntimeBuildPhase::None;
        result.status = bundle->status;
        result.bundle = std::move(bundle);
        return result;
    }

    bool commit_scene_runtime_bundle(
        SceneRuntimeBundle& live,
        SceneRuntimeBundleBuildResult candidate)
    {
        if (!candidate.ok()) {
            return false;
        }

        SceneRuntimeBundle& bundle = *candidate.bundle;
        live.assets = std::move(bundle.assets);
        live.authored_scene = std::move(bundle.authored_scene);
        live.scene_instance = std::move(bundle.scene_instance);
        live.resolver = std::move(bundle.resolver);
        live.renderable_cache.swap_with(bundle.renderable_cache);
        live.pipeline_cache = std::move(bundle.pipeline_cache);
        live.render_program_cache = std::move(bundle.render_program_cache);
        live.compiled_scene = std::move(bundle.compiled_scene);
        live.render_ir = std::move(bundle.render_ir);
        live.render_frame = std::move(bundle.render_frame);
        live.sky_commands = std::move(bundle.sky_commands);
        live.scene_hash = bundle.scene_hash;
        live.scene_hash_text = std::move(bundle.scene_hash_text);
        live.status = std::move(bundle.status);
        live.valid = true;
        return true;
    }
}
