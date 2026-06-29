// src/engine/assets/audio/audio_renderable_compilers.cpp

#include <engine/assets/audio/audio_renderable_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>

namespace wz::engine::assets::internal
{
    namespace
    {
        // A pitch of 0 would freeze the read head (step 0 -> never advances, never
        // finishes). Floor it to a small positive value on every path.
        constexpr float kMinPitch = 0.01f;

        AudioRenderableCompileDesc renderable_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            AudioRenderableCompileDesc desc{};
            desc.gain = params.get<float>("gain", desc.gain);
            desc.pitch = params.get<float>("pitch", desc.pitch);
            desc.looping = params.get<bool>("looping", desc.looping);
            desc.default_index = static_cast<uint32_t>(std::max<int64_t>(
                0, params.get<int64_t>(
                    "default_index",
                    static_cast<int64_t>(desc.default_index))));
            return desc;
        }
    }

    void register_audio_renderable_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioRenderableTable& audio_renderable_table,
        AudioClipTable& audio_clip_table,
        AudioClipBankTable& audio_clip_bank_table)
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
                    .min = static_cast<double>(kMinPitch),
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
                        .clips = { dep_handles[0] },
                        .default_index = 0,
                        .gain = desc->gain,
                        .pitch = std::max(desc->pitch, kMinPitch),
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


        // ── Bank-backed audio renderable ──────────────────────────────────────
        //
        // Dispatches on kAudioClipBankRenderableSchema. Expects one
        // kAssetTypeAudioBank dependency; copies the bank's resolved clip handles
        // into the terminal so the behavior PLAY command can select one by index.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kAudioClipBankRenderableSchema,
            .output_type = kAssetTypeAudioRenderable,
            .input_ports = {
                { "bank", kAssetTypeAudioBank },
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
                    .min = static_cast<double>(kMinPitch),
                    .max = 8.0,
                },
                {
                    .name = "looping",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Looping",
                    .default_num = 0.0,
                },
                {
                    .name = "default_index",
                    .type = wz::asset::ParamType::Int,
                    .label = "Default clip index",
                    .default_num = 0,
                    .min = 0,
                    .max = 65535,
                },
            },
            .compile = [&logger, &audio_renderable_table, &audio_clip_bank_table](
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
                    logger.error("bank audio renderable node missing "
                        "AudioRenderableCompileDesc");
                    return compile_failed_node(input);
                }

                // ── 2. Validate the bank dependency ───────────────────────────
                if (dep_handles.size() != 1) {
                    logger.error(
                        "bank audio renderable requires one audio bank "
                        "dependency");
                    return compile_failed_node(input);
                }

                const AudioClipBankData* bank =
                    audio_clip_bank_table.get(dep_handles[0]);
                if (bank == nullptr || !bank->valid()) {
                    logger.error("bank audio renderable source bank is invalid");
                    return compile_failed_node(input);
                }

                // ── 3. Copy the bank's clip handles + name hashes into the
                //       terminal (the latter for behavior select-by-name) ───────
                std::vector<wz::asset::ResourceHandle> clips;
                std::vector<uint32_t> clip_name_hashes;
                clips.reserve(bank->entries.size());
                clip_name_hashes.reserve(bank->entries.size());
                for (const AudioClipBankEntry& e : bank->entries) {
                    clips.push_back(e.clip);
                    clip_name_hashes.push_back(e.name_hash);
                }

                // Clamp default_index into [0, clips.size()) (clips is non-empty
                // because bank->valid()).
                uint32_t default_index = desc->default_index;
                if (default_index >= clips.size()) {
                    default_index =
                        static_cast<uint32_t>(clips.size() - 1);
                }

                const wz::asset::ResourceHandle handle =
                    audio_renderable_table.add(AudioRenderableData{
                        .clips = std::move(clips),
                        .clip_name_hashes = std::move(clip_name_hashes),
                        .default_index = default_index,
                        .gain = desc->gain,
                        .pitch = std::max(desc->pitch, kMinPitch),
                        .looping = desc->looping,
                    });
                if (!handle.valid()) {
                    logger.error("failed to store bank audio renderable recipe");
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
