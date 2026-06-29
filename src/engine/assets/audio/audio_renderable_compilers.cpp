// src/engine/assets/audio/audio_renderable_compilers.cpp

#include <engine/assets/audio/audio_renderable_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets::internal
{
    namespace
    {
        AudioRenderableCompileDesc renderable_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            AudioRenderableCompileDesc desc{};
            desc.gain = params.get<float>("gain", desc.gain);
            desc.pitch = params.get<float>("pitch", desc.pitch);
            desc.looping = params.get<bool>("looping", desc.looping);
            return desc;
        }
    }

    void register_audio_renderable_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioRenderableTable& audio_renderable_table,
        AudioClipTable& audio_clip_table)
    {
        // Dispatches on kAudioRenderableSchema. Expects one kAssetTypeAudioClip
        // dependency; folds the resolved clip handle + playback params into an
        // executable AudioRenderableData.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kAudioRenderableSchema,
            .output_type = kAssetTypeAudioRenderable,
            .input_ports = {
                { "source", kAssetTypeAudioClip },
            },
            .parameters = {
                {
                    .name = "gain",
                    .type = wz::asset::ParamType::Float,
                    .label = "Gain",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 8.0,
                },
                {
                    .name = "pitch",
                    .type = wz::asset::ParamType::Float,
                    .label = "Pitch",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 8.0,
                },
                {
                    .name = "looping",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Looping",
                    .default_num = 0.0,
                },
            },
            .compile = [&logger, &audio_renderable_table, &audio_clip_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                (void)dep_nodes;

                // ── 1. Resolve params (typed desc or ParamBlock) ──────────────
                AudioRenderableCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<AudioRenderableCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc = renderable_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error("audio renderable node missing "
                        "AudioRenderableCompileDesc");
                    return compile_failed_node(input);
                }

                // ── 2. Validate source clip dependency ────────────────────────
                if (dep_handles.size() != 1) {
                    logger.error(
                        "audio renderable requires one audio clip dependency");
                    return compile_failed_node(input);
                }

                const AudioClipData* clip = audio_clip_table.get(dep_handles[0]);
                if (clip == nullptr || !clip->valid()) {
                    logger.error("audio renderable source clip is invalid");
                    return compile_failed_node(input);
                }

                // ── 3. Store the executable terminal ──────────────────────────
                const wz::asset::ResourceHandle handle =
                    audio_renderable_table.add(AudioRenderableData{
                        .clip = dep_handles[0],
                        .gain = desc->gain,
                        .pitch = desc->pitch,
                        .looping = desc->looping,
                    });
                if (!handle.valid()) {
                    logger.error("failed to store audio renderable recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

} // namespace wz::engine::assets::internal
