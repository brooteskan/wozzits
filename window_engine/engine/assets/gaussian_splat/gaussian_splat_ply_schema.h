// window_engine/engine/assets/gaussian_splat/gaussian_splat_ply_schema.h
#pragma once

#include <ply/ply_reader.h>

#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct GaussianSplatPLYSchema
    {
        int x = -1;
        int y = -1;
        int z = -1;

        int opacity = -1;

        int scale_0 = -1;
        int scale_1 = -1;
        int scale_2 = -1;

        int rot_0 = -1;
        int rot_1 = -1;
        int rot_2 = -1;
        int rot_3 = -1;

        int f_dc_0 = -1;
        int f_dc_1 = -1;
        int f_dc_2 = -1;

        // Property indices into the source ScalarTable, ordered by numeric suffix.
        // Example: f_rest_0, f_rest_1, f_rest_2, ...
        std::vector<int> f_rest;

        bool has_required_fields() const noexcept;
    };

    struct GaussianSplatPLYSchemaResult
    {
        bool ok = false;
        GaussianSplatPLYSchema schema;
        std::string error;
    };

    GaussianSplatPLYSchemaResult detect_gaussian_splat_ply_schema(
        const wz::external::ply::ScalarTable& table);
}