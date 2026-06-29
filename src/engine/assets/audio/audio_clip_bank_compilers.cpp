// src/engine/assets/audio/audio_clip_bank_compilers.cpp

#include <engine/assets/audio/audio_clip_bank_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <string>

namespace wz::engine::assets::internal
{

    void register_audio_clip_bank_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AudioClipBankTable& audio_clip_bank_table,
        AudioClipTable& audio_clip_table)
    {
        // Dispatches on kAudioClipBankFromClipsSchema. Expects N ordered
        // kAssetTypeAudioClip dependencies on the "clips" port; pairs each
        // resolved clip handle with the parallel name hash carried in the node
        // meta and stores them as an AudioClipBankData.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kAudioClipBankFromClipsSchema,
            .output_type = kAssetTypeAudioBank,
            .input_ports = {
                {
                    "clips",
                    kAssetTypeAudioClip,
                    wz::asset::InputPortRequirement::Required,
                    wz::asset::InputPortArity::Many,
                },
            },
            .compile = [&logger, &audio_clip_bank_table, &audio_clip_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                (void)dep_nodes;

                // ── 1. Resolve the name-hash meta (typed desc only) ───────────
                AudioClipBankCompileDesc empty_desc{};
                const auto* desc =
                    std::any_cast<AudioClipBankCompileDesc>(&input.meta);
                if (!desc) {
                    desc = &empty_desc;
                }

                // ── 2. Validate dependencies ──────────────────────────────────
                if (dep_handles.empty()) {
                    logger.error(
                        "audio clip bank requires at least one audio clip");
                    return compile_failed_node(input);
                }

                // ── 3. Resolve each clip + pair with its name hash ────────────
                AudioClipBankData bank{};
                bank.entries.reserve(dep_handles.size());
                bank.names.reserve(dep_handles.size());

                for (size_t i = 0; i < dep_handles.size(); ++i) {
                    const AudioClipData* clip =
                        audio_clip_table.get(dep_handles[i]);
                    if (clip == nullptr || !clip->valid()) {
                        logger.error(
                            "audio clip bank source clip "
                            + std::to_string(i) + " is invalid");
                        return compile_failed_node(input);
                    }

                    const uint32_t name_hash =
                        (i < desc->name_hashes.size())
                            ? desc->name_hashes[i]
                            : 0u;

                    bank.entries.push_back(AudioClipBankEntry{
                        .name_hash = name_hash,
                        .clip = dep_handles[i],
                    });
                    // Names are debug/editor only; the recipe carries hashes, not
                    // strings, so the runtime parallel name vector is empty here.
                    bank.names.emplace_back();
                }

                if (!bank.valid()) {
                    logger.error("audio clip bank produced invalid data");
                    return compile_failed_node(input);
                }

                // ── 4. Store the bank ─────────────────────────────────────────
                const wz::asset::ResourceHandle handle =
                    audio_clip_bank_table.add(std::move(bank));
                if (!handle.valid()) {
                    logger.error("failed to store audio clip bank");
                    return compile_failed_node(input);
                }

                logger.info(
                    "asset compile: audio clip bank with "
                    + std::to_string(dep_handles.size()) + " clips");

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

} // namespace wz::engine::assets::internal
