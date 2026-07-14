// src/engine/assets/star_catalog/star_catalog_compilers.cpp
//
// Compiler for the StarCatalogFromJSON recipe asset (Seam C-2, issue #266).
//
// Input (one dep):
//   dep[0] -- a compiled JSONDocument carrying the baked .star_catalog.json
//             (Seam C-2 schema v1: version / source_name / params / stars[]).
//
// Output: kAssetTypeStarCatalog -- a handle into the StarCatalogTable. When a
// shared wozzits-rhi registry is present the built stars are ALSO published as a
// resident point-source StructuredBuffer (variant "star_catalog"), the same
// 32-byte ResidentSkyPoint / DistantLight layout the sun's "sky_gaussian_points"
// buffer uses -- so a star field drops straight into the point-source layer.

#include <engine/assets/star_catalog/star_catalog_compilers.h>
#include <starfield/star_catalog_serialize.h>
#include <starfield/star_catalog_ply_importer.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <engine/assets/json/json.h>

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <any>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace sf = wz::engine::starfield;

    namespace
    {
        // The decoded per-star record uploaded into the resident StructuredBuffer
        // the star render path reads. 32-byte stride, byte-for-byte the
        // ResidentSkyPoint / HLSL DistantLight layout (direction+solid_angle in
        // one 16-byte register, radiance+... in the next) -- so stars share the
        // point-source layout with the sun. The trailing float carries the
        // apparent magnitude (the sun's record leaves it 0 as pad); the render
        // seam uses it for sprite size / LOD.
        struct ResidentStar
        {
            float direction[3] = { 0.0f, 1.0f, 0.0f };
            float solid_angle  = 0.0f;
            float radiance[3]  = { 0.0f, 0.0f, 0.0f };
            float magnitude    = 0.0f;
        };
        static_assert(sizeof(ResidentStar) == 32,
            "ResidentStar must be 32 bytes to match the point-source "
            "StructuredBuffer stride");

        ResidentStar encode_resident_star(const sf::Star& star)
        {
            ResidentStar out{};
            out.direction[0] = star.direction.x;
            out.direction[1] = star.direction.y;
            out.direction[2] = star.direction.z;
            out.solid_angle  = star.solid_angle;
            out.radiance[0]  = star.radiance.x;
            out.radiance[1]  = star.radiance.y;
            out.radiance[2]  = star.radiance.z;
            out.magnitude    = star.magnitude;
            return out;
        }

        // Publish the built star field's GPU residency onto the shared wozzits-rhi
        // registry as a StructuredBuffer (the surrogate pattern). Mirror of
        // publish_resident_sky_gaussian: best-effort (a failure is logged and does
        // not fail the compile), releases the handle on a failed upload, and
        // records (key -> identity) through the tracker. The identity
        // discriminator "star_catalog" matches render_binding_source_for.
        void publish_resident_star_catalog(
            const wz::asset::AssetKey& key,
            const sf::StarCatalog& catalog,
            wz::rhi::GpuResourceRegistry& gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            if (catalog.stars.empty()) {
                return;
            }

            std::vector<ResidentStar> resident;
            resident.reserve(catalog.stars.size());
            for (const sf::Star& star : catalog.stars) {
                resident.push_back(encode_resident_star(star));
            }

            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::buffer(
                static_cast<uint64_t>(resident.size()) * sizeof(ResidentStar),
                static_cast<uint32_t>(sizeof(ResidentStar)),
                wz::rhi::ResourceUsage_Sampled,
                wz::rhi::ResourceCpuAccess::WriteOnce);
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "star_catalog"), {} };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            const bool uploaded = handle.valid()
                && gpu_resources.update(handle, resident.data(), desc.size_bytes);
            if (!uploaded) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
                logger.warn("star catalog RHI resident upload failed");
                return;
            }

            logger.info(
                "asset compile: star catalog RHI resident upload stars="
                + std::to_string(resident.size()));

            if (rhi_resource_tracker) {
                std::vector<wz::rhi::ResourceIdentity> tracked{ desc.identity };
                rhi_resource_tracker(key, std::move(tracked));
            }
        }

        // Build the import dials from an authored node ParamBlock (the graph
        // path). The typed create_from_ply path passes a StarImportParams in
        // meta directly; this covers a graph-authored StarCatalogFromPLY node.
        sf::StarImportParams star_import_params_from_block(
            const wz::asset::ParamBlock& pb)
        {
            sf::StarImportParams p;
            p.reference_magnitude =
                pb.get<double>("reference_magnitude", p.reference_magnitude);
            p.exposure = pb.get<double>("exposure", p.exposure);
            p.color_saturation =
                pb.get<double>("color_saturation", p.color_saturation);
            p.magnitude_min = pb.get<double>("magnitude_min", p.magnitude_min);
            p.magnitude_max = pb.get<double>("magnitude_max", p.magnitude_max);
            p.magnitude_pivot =
                pb.get<double>("magnitude_pivot", p.magnitude_pivot);
            p.magnitude_contrast =
                pb.get<double>("magnitude_contrast", p.magnitude_contrast);
            p.warp_amplitude =
                pb.get<double>("warp_amplitude", p.warp_amplitude);
            p.warp_frequency =
                pb.get<double>("warp_frequency", p.warp_frequency);
            p.solid_angle = pb.get<double>("solid_angle", p.solid_angle);
            return p;
        }

    } // anonymous namespace

    void register_star_catalog_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        sf::StarCatalogTable& table,
        JSONTable& json_table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kStarCatalogFromJSONSchema,
            .output_type  = kAssetTypeStarCatalog,
            .input_ports = {
                { "star_json", kAssetTypeJSONDocument },
            },
            .compile = [&logger, &table, &json_table,
                        gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                // dep[0] = compiled JSONDocument carrying the baked catalog.
                if (dep_nodes.size() != 1 || dep_handles.size() != 1) {
                    logger.error(
                        "star catalog: expected exactly one JSON document "
                        "dependency, got "
                        + std::to_string(dep_nodes.size()));
                    return compile_failed_node(input);
                }

                const JSONData* json_data = json_table.get(dep_handles[0]);
                if (!json_data || !json_data->document.root) {
                    logger.error(
                        "star catalog: JSON document dependency is invalid");
                    return compile_failed_node(input);
                }

                std::vector<sf::CatalogRecord> records;
                sf::StarImportParams params;
                std::string source_name;
                std::string error;
                if (!sf::star_catalog_from_json(
                        *json_data->document.root,
                        records, params, source_name, error)) {
                    logger.error(
                        "star catalog: failed to parse baked catalog: " + error);
                    return compile_failed_node(input, error);
                }

                sf::StarCatalog catalog;
                catalog.source_name = std::move(source_name);
                catalog.stars = sf::build_catalog(records, params);

                if (catalog.stars.empty()) {
                    logger.error(
                        "star catalog: no stars survived the magnitude window");
                    return compile_failed_node(
                        input, "baked star catalog produced no stars");
                }

                // Publish residency BEFORE the move into the CPU table, so the
                // star render path can bind it. Best-effort; only when a shared
                // registry is present.
                if (gpu_resources) {
                    publish_resident_star_catalog(
                        input.key,
                        catalog,
                        *gpu_resources,
                        rhi_resource_tracker,
                        logger);
                }

                wz::asset::ResourceHandle handle = table.add(std::move(catalog));
                if (!handle.valid()) {
                    logger.error("star catalog: failed to store catalog");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        // StarCatalogFromPLY (Seam T-2, #266): import a baked PLY point cloud
        // (stage-1 tycho2_prep output) with the creative dials into the same
        // StarCatalogTable + resident buffer as the JSON path. The dials arrive
        // as a typed StarImportParams in meta (create_from_ply) or an authored
        // node ParamBlock; the PLY bytes come from the raw-file dep's payload.
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kStarCatalogFromPLYSchema,
            .output_type  = kAssetTypeStarCatalog,
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            // Authorable import dials (StarImportParams). Declaring them here is
            // what surfaces them in the editor inspector AND makes the editor
            // write them as typed floats -- without this schema the values are
            // stored as strings and star_import_params_from_block's
            // pb.get<double> silently falls back to the defaults, so every dial
            // is a no-op. Defaults MUST match StarImportParams so an untouched
            // node keeps its content hash. The brightness knobs
            // (exposure / reference_magnitude / magnitude_contrast) lift the
            // faint Tycho-2 majority into view; the magnitude window
            // (magnitude_min / magnitude_max) is the coarse brightness cull.
            .parameters = {
                {
                    .name = "exposure",
                    .type = wz::asset::ParamType::Float,
                    .label = "Exposure",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "reference_magnitude",
                    .type = wz::asset::ParamType::Float,
                    .label = "Reference magnitude (unit flux)",
                    .default_num = 0.0,
                    .min = -5.0,
                    .max = 15.0,
                },
                {
                    .name = "magnitude_min",
                    .type = wz::asset::ParamType::Float,
                    .label = "Magnitude min (brighter cull)",
                    .default_num = -30.0,
                    .min = -30.0,
                    .max = 15.0,
                },
                {
                    .name = "magnitude_max",
                    .type = wz::asset::ParamType::Float,
                    .label = "Magnitude max (fainter cull)",
                    .default_num = 30.0,
                    .min = -5.0,
                    .max = 30.0,
                },
                {
                    .name = "magnitude_pivot",
                    .type = wz::asset::ParamType::Float,
                    .label = "Magnitude remap pivot",
                    .default_num = 0.0,
                    .min = -5.0,
                    .max = 15.0,
                },
                {
                    .name = "magnitude_contrast",
                    .type = wz::asset::ParamType::Float,
                    .label = "Magnitude remap contrast",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 3.0,
                },
                {
                    .name = "color_saturation",
                    .type = wz::asset::ParamType::Float,
                    .label = "Color saturation",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 3.0,
                },
                {
                    .name = "warp_amplitude",
                    .type = wz::asset::ParamType::Float,
                    .label = "Warp amplitude",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "warp_frequency",
                    .type = wz::asset::ParamType::Float,
                    .label = "Warp frequency",
                    .default_num = 3.0,
                    .min = 0.0,
                    .max = 20.0,
                },
            },
            .compile = [&logger, &table, gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                if (dep_nodes.size() != 1) {
                    logger.error(
                        "star catalog PLY: expected exactly one file dependency");
                    return compile_failed_node(input);
                }
                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);
                if (!bytes || bytes->empty()) {
                    logger.error(
                        "star catalog PLY: file dependency has no bytes");
                    return compile_failed_node(input);
                }

                sf::StarImportParams params;
                if (const auto* typed =
                        std::any_cast<sf::StarImportParams>(&input.meta)) {
                    params = *typed;
                } else if (const auto* pb =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta)) {
                    params = star_import_params_from_block(*pb);
                }

                const sf::StarCatalogPlyImportResult imported =
                    sf::import_star_catalog_ply_bytes(
                        { bytes->data(), bytes->size() }, params);
                if (!imported.ok) {
                    logger.error(
                        "star catalog PLY: import failed: " + imported.error);
                    return compile_failed_node(input, imported.error);
                }

                sf::StarCatalog catalog = imported.catalog;

                if (gpu_resources) {
                    publish_resident_star_catalog(
                        input.key, catalog, *gpu_resources,
                        rhi_resource_tracker, logger);
                }

                wz::asset::ResourceHandle handle = table.add(std::move(catalog));
                if (!handle.valid()) {
                    logger.error("star catalog PLY: failed to store catalog");
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
