// window_engine/engine/assets/gaussian_splat/gaussian_splat_ply_importer.cpp

#include <engine/assets/gaussian_splat/gaussian_splat_ply_importer.h>

#include <engine/assets/gaussian_splat/gaussian_splat_ply_schema.h>

#include <ply/ply_reader.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace wz::engine::assets
{
    namespace
    {
        const wz::external::ply::ScalarTable* find_scalar_table(
            const wz::external::ply::Document& document,
            const std::string& name)
        {
            for (const wz::external::ply::ScalarTable& table : document.scalar_tables)
            {
                if (table.element_name == name)
                {
                    return &table;
                }
            }

            return nullptr;
        }

        float table_value_as_float(
            const wz::external::ply::ScalarTable& table,
            size_t row,
            int property_index)
        {
            const size_t index =
                row * table.properties.size() + static_cast<size_t>(property_index);

            return static_cast<float>(table.values[index]);
        }

        void expand_bounds(GaussianSplatBounds& bounds, const float position[3])
        {
            if (!bounds.valid)
            {
                bounds.min[0] = position[0];
                bounds.min[1] = position[1];
                bounds.min[2] = position[2];

                bounds.max[0] = position[0];
                bounds.max[1] = position[1];
                bounds.max[2] = position[2];

                bounds.valid = true;
                return;
            }

            for (int axis = 0; axis < 3; ++axis)
            {
                bounds.min[axis] = std::min(bounds.min[axis], position[axis]);
                bounds.max[axis] = std::max(bounds.max[axis], position[axis]);
            }
        }

        bool is_finite_splat(const GaussianSplat& splat)
        {
            for (float v : splat.position)
            {
                if (!std::isfinite(v))
                {
                    return false;
                }
            }

            if (!std::isfinite(splat.opacity))
            {
                return false;
            }

            for (float v : splat.scale)
            {
                if (!std::isfinite(v))
                {
                    return false;
                }
            }

            for (float v : splat.rotation)
            {
                if (!std::isfinite(v))
                {
                    return false;
                }
            }

            for (float v : splat.color_dc)
            {
                if (!std::isfinite(v))
                {
                    return false;
                }
            }

            for (float v : splat.color_rest)
            {
                if (!std::isfinite(v))
                {
                    return false;
                }
            }

            return true;
        }

        GaussianSplatImportResult import_gaussian_splat_table(
            const wz::external::ply::ScalarTable& table,
            const GaussianSplatPLYSchema& schema)
        {
            GaussianSplatImportResult result;

            GaussianSplatCloudData cloud;
            cloud.f_rest_count = static_cast<uint32_t>(schema.f_rest.size());

            cloud.splats.reserve(static_cast<size_t>(table.row_count));

            // Default values used when optional fields are absent.
            // logit(0.9) gives a clearly visible splat; exp(-3) ≈ 0.05 world-unit radius.
            constexpr float kDefaultLogitOpacity = 2.1972245773f;
            constexpr float kDefaultLogScale     = -3.0f;
            constexpr float kSH_C0               = 0.28209479177387814f;

            bool initialized_ranges = false;

            for (size_t row = 0; row < static_cast<size_t>(table.row_count); ++row)
            {
                GaussianSplat splat;

                splat.position[0] = table_value_as_float(table, row, schema.x);
                splat.position[1] = table_value_as_float(table, row, schema.y);
                splat.position[2] = table_value_as_float(table, row, schema.z);

                splat.opacity =
                    schema.opacity >= 0
                    ? table_value_as_float(table, row, schema.opacity)
                    : kDefaultLogitOpacity;

                splat.scale[0] =
                    schema.scale_0 >= 0
                    ? table_value_as_float(table, row, schema.scale_0)
                    : kDefaultLogScale;
                splat.scale[1] =
                    schema.scale_1 >= 0
                    ? table_value_as_float(table, row, schema.scale_1)
                    : kDefaultLogScale;
                splat.scale[2] =
                    schema.scale_2 >= 0
                    ? table_value_as_float(table, row, schema.scale_2)
                    : kDefaultLogScale;

                // Identity quaternion: rot_0 = w = 1, others = 0.
                splat.rotation[0] =
                    schema.rot_0 >= 0
                    ? table_value_as_float(table, row, schema.rot_0)
                    : 1.0f;
                splat.rotation[1] =
                    schema.rot_1 >= 0
                    ? table_value_as_float(table, row, schema.rot_1)
                    : 0.0f;
                splat.rotation[2] =
                    schema.rot_2 >= 0
                    ? table_value_as_float(table, row, schema.rot_2)
                    : 0.0f;
                splat.rotation[3] =
                    schema.rot_3 >= 0
                    ? table_value_as_float(table, row, schema.rot_3)
                    : 0.0f;

                if (schema.f_dc_0 >= 0)
                {
                    // Full 3DGS format: SH DC coefficients stored directly.
                    splat.color_dc[0] = table_value_as_float(table, row, schema.f_dc_0);
                    splat.color_dc[1] = table_value_as_float(table, row, schema.f_dc_1);
                    splat.color_dc[2] = table_value_as_float(table, row, schema.f_dc_2);
                }
                else if (schema.red >= 0)
                {
                    // RGB fallback: convert display-space color to SH DC coefficients.
                    float r = table_value_as_float(table, row, schema.red);
                    float g = table_value_as_float(table, row, schema.green);
                    float b = table_value_as_float(table, row, schema.blue);

                    if (schema.color_is_byte_rgb)
                    {
                        r /= 255.0f;
                        g /= 255.0f;
                        b /= 255.0f;
                    }

                    splat.color_dc[0] = (r - 0.5f) / kSH_C0;
                    splat.color_dc[1] = (g - 0.5f) / kSH_C0;
                    splat.color_dc[2] = (b - 0.5f) / kSH_C0;
                }
                else
                {
                    // No color data — default to white.
                    splat.color_dc[0] = splat.color_dc[1] = splat.color_dc[2] =
                        0.5f / kSH_C0;
                }

                splat.color_rest.reserve(schema.f_rest.size());

                for (int property_index : schema.f_rest)
                {
                    splat.color_rest.push_back(
                        table_value_as_float(table, row, property_index));
                }

                if (!is_finite_splat(splat))
                {
                    result.ok = false;
                    result.error = "Gaussian splat PLY contains non-finite numeric values";
                    return result;
                }

                expand_bounds(cloud.bounds, splat.position);

                const float splat_scale_min =
                    std::min({ splat.scale[0], splat.scale[1], splat.scale[2] });

                const float splat_scale_max =
                    std::max({ splat.scale[0], splat.scale[1], splat.scale[2] });

                if (!initialized_ranges)
                {
                    cloud.opacity_min = splat.opacity;
                    cloud.opacity_max = splat.opacity;

                    cloud.scale_min = splat_scale_min;
                    cloud.scale_max = splat_scale_max;

                    initialized_ranges = true;
                }
                else
                {
                    cloud.opacity_min = std::min(cloud.opacity_min, splat.opacity);
                    cloud.opacity_max = std::max(cloud.opacity_max, splat.opacity);

                    cloud.scale_min = std::min(cloud.scale_min, splat_scale_min);
                    cloud.scale_max = std::max(cloud.scale_max, splat_scale_max);
                }

                cloud.splats.push_back(std::move(splat));
            }

            result.ok = true;
            result.cloud = std::move(cloud);
            return result;
        }

        GaussianSplatImportResult import_from_document(
            const wz::external::ply::Document& document)
        {
            const wz::external::ply::ScalarTable* vertex_table =
                find_scalar_table(document, "vertex");

            if (!vertex_table)
            {
                GaussianSplatImportResult result;
                result.ok = false;
                result.error = "Gaussian splat PLY does not contain a scalar vertex table";
                return result;
            }

            const GaussianSplatPLYSchemaResult schema_result =
                detect_gaussian_splat_ply_schema(*vertex_table);

            if (!schema_result.ok)
            {
                GaussianSplatImportResult result;
                result.ok = false;
                result.error = schema_result.error;
                return result;
            }

            return import_gaussian_splat_table(*vertex_table, schema_result.schema);
        }
    }

    GaussianSplatImportResult import_gaussian_splat_ply_file(
        const std::filesystem::path& path)
    {
        const wz::external::ply::ReadResult read =
            wz::external::ply::read_ply_file(path);

        if (!read.ok)
        {
            GaussianSplatImportResult result;
            result.ok = false;
            result.error = "Failed to read PLY file: " + read.error.message;
            return result;
        }

        return import_from_document(read.document);
    }

    GaussianSplatImportResult import_gaussian_splat_ply_bytes(
        std::span<const std::uint8_t> bytes)
    {
        const wz::external::ply::ReadResult read =
            wz::external::ply::read_ply_bytes(bytes);

        if (!read.ok)
        {
            GaussianSplatImportResult result;
            result.ok = false;
            result.error = "Failed to parse PLY bytes: " + read.error.message;
            return result;
        }

        return import_from_document(read.document);
    }
}