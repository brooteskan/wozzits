// src/engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule_compilers.cpp

#include <engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/key_factories/clipmap_lattice_schedule.h>
#include <engine/assets/mesh/clipmap_lattice_mesh.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>

namespace wz::engine::assets::internal
{
    namespace
    {
        wz::asset::AssetNode compiled_clipmap_lattice_schedule_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }

    void register_clipmap_lattice_schedule_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        ClipmapLatticeScheduleTable& clipmap_lattice_schedule_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kClipmapLatticeScheduleSchema,
            .output_type = kAssetTypeClipmapLatticeSchedule,
            // One required port: the height field. The schedule reads a single
            // number out of it — N, its texel count per side — never its
            // samples, so this is a resolution dependency, not a data one.
            .input_ports = {
                { "height_field", kAssetTypeScalarField },
            },
            // The same authored dials the clipmap lattice MESH recipe declares.
            // A compiler reading params through pb.get<T> MUST declare them here
            // or the editor stores them as strings and the dials go dead.
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "world_extent",
                    .type = wz::asset::ParamType::Float,
                    .label = "World extent",
                    .default_num = 256.0,
                    .min = 0.0001,
                    .max = 1000000.0,
                },
                {
                    .name = "horizon",
                    .type = wz::asset::ParamType::Float,
                    .label = "Horizon",
                    .default_num = 128.0,
                    .min = 0.0001,
                    .max = 1000000.0,
                },
                {
                    .name = "triangle_budget",
                    .type = wz::asset::ParamType::Int,
                    .label = "Triangle budget",
                    .default_num = 200000,
                    .min = 1,
                    .max = 100000000,
                },
            },
            .compile = [&logger, &scalar_field_table,
                        &clipmap_lattice_schedule_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                // The height field is a REQUIRED input port, so exactly one
                // compiled ScalarField dependency must be present.
                if (dep_handles.size() != 1) {
                    logger.error(
                        "clipmap lattice schedule requires exactly one height "
                        "field dependency");
                    return compile_failed_node(
                        input,
                        "clipmap lattice schedule requires exactly one height "
                        "field dependency");
                }

                const ScalarFieldData* field =
                    scalar_field_table.get(dep_handles[0]);
                if (!field || !field->valid()) {
                    logger.error(
                        "clipmap lattice schedule height field is invalid");
                    return compile_failed_node(
                        input,
                        "clipmap lattice schedule height field is invalid");
                }

                // Typed meta for programmatic callers, ParamBlock for the
                // graph/editor path — the same two-way read the placement
                // compiler uses. Neither present leaves the struct's declared
                // defaults, which match the .parameters defaults above.
                ClipmapLatticePhysicalParams physical{};
                if (const auto* desc =
                        std::any_cast<ClipmapLatticePhysicalParams>(&input.meta))
                {
                    physical = *desc;
                }
                else if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    physical =
                        clipmap_lattice_physical_params_from_params(*params);
                }

                // N = the field's texel count per side. The field is assumed
                // SQUARE; width is used as N, matching the lattice mesh recipe
                // and the runtime clipmap path.
                const uint32_t n = field->width;
                if (n == 0u) {
                    logger.error(
                        "clipmap lattice schedule height field has zero width");
                    return compile_failed_node(
                        input,
                        "clipmap lattice schedule height field has zero width");
                }

                // s = world_extent / N: the world size of one finest texel and,
                // by construction, the finest lattice cell. resolve_clipmap_
                // lattice guards a non-finite / non-positive s (falls back to
                // 1.0) and clamps the result.
                const float metres_per_texel =
                    physical.world_extent / static_cast<float>(n);

                const ResolvedClipmapLattice resolved =
                    resolve_clipmap_lattice(
                        physical.horizon,
                        static_cast<uint64_t>(physical.triangle_budget),
                        metres_per_texel);

                ClipmapLatticeScheduleData data{};
                data.base_resolution = resolved.params.base_resolution;
                data.level_count = resolved.params.level_count;
                data.cell_size = resolved.params.cell_size;
                data.achieved_horizon = resolved.achieved_horizon_metres;
                data.triangle_count = resolved.triangle_count;
                if (!data.valid()) {
                    logger.error("compiled clipmap lattice schedule is invalid");
                    return compile_failed_node(
                        input, "compiled clipmap lattice schedule is invalid");
                }

                wz::asset::ResourceHandle handle =
                    clipmap_lattice_schedule_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store clipmap lattice schedule");
                    return compile_failed_node(
                        input, "failed to store clipmap lattice schedule");
                }

                return compiled_clipmap_lattice_schedule_node(input, handle);
            }
        });
    }
}
