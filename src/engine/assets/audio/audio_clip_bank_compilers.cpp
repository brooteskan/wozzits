// src/engine/assets/audio/audio_clip_bank_compilers.cpp

#include <engine/assets/audio/audio_clip_bank_compilers.h>
#include <engine/assets/audio/wav_decoder.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        // FNV-1a/32 over a name, matching the per-entry hash the from-clips path
        // and the runtime name lookup use.
        uint32_t bank_name_hash(std::string_view s) noexcept
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : s) {
                h ^= static_cast<uint32_t>(c);
                h *= 16777619u;
            }
            return h;
        }

        bool is_wav_path(const std::filesystem::path& p)
        {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext == ".wav";
        }
    }

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


        // ── Audio clip bank from directory ────────────────────────────────────
        //
        // Dispatches on kAudioClipBankFromDirectorySchema. A no-dependency source
        // recipe: enumerates *.wav under the authored "directory" param (sorted
        // lexicographically for a deterministic index space), reads + decodes each
        // WAV inline, adds the decoded clips to audio_clip_table, and builds the
        // bank with names = filename stems. The directory must be an absolute path
        // (authored that way, like a file carrier's resolved source path); the
        // compiler does not root relative paths.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kAudioClipBankFromDirectorySchema,
            .output_type = kAssetTypeAudioBank,
            .parameters = {
                {
                    .name = "directory",
                    .type = wz::asset::ParamType::String,
                    .label = "Directory",
                },
                {
                    .name = "recursive",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Recursive",
                    .default_num = 0.0,
                },
            },
            .compile = [&logger, &audio_clip_bank_table, &audio_clip_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                (void)dep_nodes;
                (void)dep_handles;

                // ── 1. Resolve params ─────────────────────────────────────────
                const auto* params =
                    std::any_cast<wz::asset::ParamBlock>(&input.meta);
                if (!params) {
                    logger.error(
                        "audio clip bank from directory node missing params");
                    return compile_failed_node(input);
                }
                std::string directory =
                    params->get<std::string>("directory", {});
                // Strip a single surrounding pair of double quotes (Windows
                // "Copy as path" wraps the path), matching the file carrier.
                if (directory.size() >= 2
                    && directory.front() == '"' && directory.back() == '"')
                {
                    directory = directory.substr(1, directory.size() - 2);
                }
                const bool recursive = params->get<bool>("recursive", false);
                if (directory.empty()) {
                    logger.error(
                        "audio clip bank from directory missing 'directory'");
                    return compile_failed_node(input);
                }

                std::error_code ec;
                const std::filesystem::path dir{ directory };
                if (!std::filesystem::is_directory(dir, ec)) {
                    logger.error(
                        "audio clip bank directory not found: " + directory);
                    return compile_failed_node(input);
                }

                // ── 2. Enumerate + sort WAVs (deterministic index space) ──────
                std::vector<std::filesystem::path> wavs;
                if (recursive) {
                    for (auto it = std::filesystem::recursive_directory_iterator(
                             dir, ec);
                         it != std::filesystem::recursive_directory_iterator();
                         ++it)
                    {
                        if (it->is_regular_file(ec) && is_wav_path(it->path()))
                            wavs.push_back(it->path());
                    }
                }
                else {
                    for (auto it = std::filesystem::directory_iterator(dir, ec);
                         it != std::filesystem::directory_iterator(); ++it)
                    {
                        if (it->is_regular_file(ec) && is_wav_path(it->path()))
                            wavs.push_back(it->path());
                    }
                }
                std::sort(wavs.begin(), wavs.end(),
                    [](const std::filesystem::path& a,
                       const std::filesystem::path& b) {
                        return a.generic_string() < b.generic_string();
                    });
                if (wavs.empty()) {
                    logger.error(
                        "audio clip bank directory has no WAV files: "
                        + directory);
                    return compile_failed_node(input);
                }

                // ── 3. Read + decode each WAV → AudioClipTable → bank entry ────
                AudioClipBankData bank{};
                bank.entries.reserve(wavs.size());
                bank.names.reserve(wavs.size());

                for (const std::filesystem::path& wav : wavs) {
                    const wz::fs::FileResult<wz::fs::Buffer> file =
                        wz::fs::read_file(wav.string());
                    if (!file) {
                        logger.error(
                            "audio clip bank failed to read WAV: "
                            + wav.string());
                        return compile_failed_node(input);
                    }

                    AudioClipData clip;
                    std::string error;
                    if (!decode_wav(file.value, clip, error)) {
                        logger.error(
                            "audio clip bank WAV decode failed ("
                            + wav.string() + "): " + error);
                        return compile_failed_node(input);
                    }
                    if (!clip.valid()) {
                        logger.error(
                            "audio clip bank WAV produced invalid data: "
                            + wav.string());
                        return compile_failed_node(input);
                    }

                    const wz::asset::ResourceHandle clip_handle =
                        audio_clip_table.add(std::move(clip));
                    if (!clip_handle.valid()) {
                        logger.error(
                            "audio clip bank failed to store clip: "
                            + wav.string());
                        return compile_failed_node(input);
                    }

                    const std::string name = wav.stem().string();
                    bank.entries.push_back(AudioClipBankEntry{
                        .name_hash = bank_name_hash(name),
                        .clip = clip_handle,
                    });
                    bank.names.push_back(name);
                }

                if (!bank.valid()) {
                    logger.error(
                        "audio clip bank from directory produced invalid data");
                    return compile_failed_node(input);
                }

                // ── 4. Store the bank ─────────────────────────────────────────
                const wz::asset::ResourceHandle handle =
                    audio_clip_bank_table.add(std::move(bank));
                if (!handle.valid()) {
                    logger.error(
                        "failed to store audio clip bank from directory");
                    return compile_failed_node(input);
                }

                logger.info(
                    "asset compile: audio clip bank from directory '"
                    + directory + "' with " + std::to_string(wavs.size())
                    + " clips");

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

} // namespace wz::engine::assets::internal
