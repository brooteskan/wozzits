// src/engine/assets/sky_gaussian/sky_gaussian_compilers.cpp
//
// Compiler for the SkyGaussianFromJSON recipe asset (Seam B1c, issue #260).
//
// Input (one dep):
//   dep[0] — a compiled JSONDocument carrying the baked .sky_gaussian.json
//            (Seam A schema v1: version / source_* / lobes[] / point_sources[]).
//
// Output: kAssetTypeSkyGaussian — a handle into the SkyGaussianTable. When a
// shared wozzits-rhi registry is present the decoded lobes are ALSO published
// as a resident StructuredBuffer (variant "sky_gaussian"), the byte-for-byte
// mirror of publish_resident_gaussian_splat_cloud (#208). point_sources are
// deferred; v1 residency is lobes only.

#include <engine/assets/sky_gaussian/sky_gaussian_compilers.h>
#include <engine/assets/sky_gaussian/sky_gaussian_serialize.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <engine/assets/json/json.h>

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        // The decoded per-lobe record uploaded into the resident
        // StructuredBuffer the (future) sky render path reads. 32-byte stride.
        // Unlike the CPU SkyGaussianLobe (Vec3 direction/amplitude + sharpness),
        // this is the tightly-packed GPU-facing layout: direction+sharpness in
        // one 16-byte register, amplitude+pad in the next. Lobes only for v1 —
        // point_sources are deferred (that decode is not part of this seam).
        struct ResidentSkyLobe
        {
            float direction[3] = { 0.0f, 1.0f, 0.0f };
            float sharpness    = 1.0f;
            float amplitude[3] = { 0.0f, 0.0f, 0.0f };
            float pad0         = 0.0f;
        };
        static_assert(sizeof(ResidentSkyLobe) == 32,
            "ResidentSkyLobe must be 32 bytes to match the sky-gaussian "
            "StructuredBuffer stride");

        ResidentSkyLobe encode_resident_lobe(const sky::SkyGaussianLobe& lobe)
        {
            ResidentSkyLobe out{};
            out.direction[0] = lobe.direction.x;
            out.direction[1] = lobe.direction.y;
            out.direction[2] = lobe.direction.z;
            out.sharpness    = lobe.sharpness;
            out.amplitude[0] = lobe.amplitude.x;
            out.amplitude[1] = lobe.amplitude.y;
            out.amplitude[2] = lobe.amplitude.z;
            out.pad0         = 0.0f;
            return out;
        }

        // Publish the SG lobe set's GPU residency onto the shared wozzits-rhi
        // registry as a StructuredBuffer (the surrogate pattern — the registry
        // owns the GPU buffer, the asset is its surrogate). Byte-for-byte mirror
        // of publish_resident_gaussian_splat_cloud: best-effort (a failure is
        // logged and does not fail the compile), releases the handle on a failed
        // upload, and records (key → identity) through the tracker. The identity
        // discriminator "sky_gaussian" matches render_binding_source_for.
        void publish_resident_sky_gaussian(
            const wz::asset::AssetKey& key,
            const sky::SkyGaussianSet& set,
            wz::rhi::GpuResourceRegistry& gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            if (set.lobes.empty()) {
                return;
            }

            const auto started = std::chrono::steady_clock::now();

            std::vector<ResidentSkyLobe> resident;
            resident.reserve(set.lobes.size());
            for (const sky::SkyGaussianLobe& lobe : set.lobes) {
                resident.push_back(encode_resident_lobe(lobe));
            }

            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::buffer(
                static_cast<uint64_t>(resident.size()) * sizeof(ResidentSkyLobe),
                static_cast<uint32_t>(sizeof(ResidentSkyLobe)),
                wz::rhi::ResourceUsage_Sampled,
                wz::rhi::ResourceCpuAccess::WriteOnce);
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "sky_gaussian"),
                {},
            };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            const bool uploaded =
                handle.valid()
                && gpu_resources.update(
                    handle, resident.data(), desc.size_bytes);
            if (!uploaded) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
                logger.warn("sky gaussian RHI resident upload failed");
                return;
            }

            if (rhi_resource_tracker) {
                rhi_resource_tracker(key, { desc.identity });
            }

            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger.info(
                "asset compile: sky gaussian RHI resident upload lobes="
                + std::to_string(resident.size())
                + " upload_ms=" + std::to_string(elapsed_ms));
        }

    } // anonymous namespace

    void register_sky_gaussian_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        SkyGaussianTable& table,
        JSONTable& json_table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kSkyGaussianFromJSONSchema,
            .output_type  = kAssetTypeSkyGaussian,
            .input_ports = {
                { "sky_json", kAssetTypeJSONDocument },
            },
            .compile = [&logger, &table, &json_table,
                        gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                // dep[0] = compiled JSONDocument carrying the baked set.
                if (dep_nodes.size() != 1 || dep_handles.size() != 1) {
                    logger.error(
                        "sky gaussian: expected exactly one JSON document "
                        "dependency, got "
                        + std::to_string(dep_nodes.size()));
                    return compile_failed_node(input);
                }

                const JSONData* json_data = json_table.get(dep_handles[0]);
                if (!json_data || !json_data->document.root) {
                    logger.error(
                        "sky gaussian: JSON document dependency is invalid");
                    return compile_failed_node(input);
                }

                sky::SkyGaussianSet set;
                std::string error;
                if (!sky::sky_gaussian_from_json(
                        *json_data->document.root, set, error)) {
                    logger.error(
                        "sky gaussian: failed to parse baked set: " + error);
                    return compile_failed_node(input, error);
                }

                if (set.lobes.empty()) {
                    logger.error("sky gaussian: baked set has no lobes");
                    return compile_failed_node(
                        input, "baked sky-gaussian set has no lobes");
                }

                // Publish residency BEFORE the move into the CPU table, so the
                // sky render path can bind it. Best-effort; only when a shared
                // registry is present.
                if (gpu_resources) {
                    publish_resident_sky_gaussian(
                        input.key,
                        set,
                        *gpu_resources,
                        rhi_resource_tracker,
                        logger);
                }

                wz::asset::ResourceHandle handle = table.add(std::move(set));
                if (!handle.valid()) {
                    logger.error("sky gaussian: failed to store set");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }
}
