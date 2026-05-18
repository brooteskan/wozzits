// window_engine/engine/assets/gaussian_splat/gaussian_splat_ply_schema.cpp

#include <engine/assets/gaussian_splat/gaussian_splat_ply_schema.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        int find_property_index(
            const wz::external::ply::ScalarTable& table,
            std::string_view name)
        {
            for (size_t i = 0; i < table.properties.size(); ++i)
            {
                if (table.properties[i].name == name)
                {
                    return static_cast<int>(i);
                }
            }

            return -1;
        }

        bool starts_with(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size()
                && text.substr(0, prefix.size()) == prefix;
        }

        bool parse_non_negative_int_suffix(
            std::string_view text,
            int& out_value)
        {
            if (text.empty())
            {
                return false;
            }

            int value = 0;

            for (char c : text)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                {
                    return false;
                }

                const int digit = c - '0';

                // Enough for property names. Avoid getting silly if a malformed
                // property has a gigantic suffix.
                if (value > 1'000'000)
                {
                    return false;
                }

                value = value * 10 + digit;
            }

            out_value = value;
            return true;
        }

        std::vector<int> collect_f_rest_indices(
            const wz::external::ply::ScalarTable& table)
        {
            struct RestProperty
            {
                int suffix = -1;
                int property_index = -1;
            };

            std::vector<RestProperty> rest;

            constexpr std::string_view prefix = "f_rest_";

            for (size_t i = 0; i < table.properties.size(); ++i)
            {
                const std::string& name = table.properties[i].name;

                if (!starts_with(name, prefix))
                {
                    continue;
                }

                int suffix = -1;
                const std::string_view suffix_text =
                    std::string_view(name).substr(prefix.size());

                if (!parse_non_negative_int_suffix(suffix_text, suffix))
                {
                    continue;
                }

                rest.push_back(RestProperty{
                    suffix,
                    static_cast<int>(i)
                    });
            }

            std::sort(
                rest.begin(),
                rest.end(),
                [](const RestProperty& a, const RestProperty& b)
                {
                    if (a.suffix != b.suffix)
                    {
                        return a.suffix < b.suffix;
                    }

                    return a.property_index < b.property_index;
                });

            std::vector<int> indices;
            indices.reserve(rest.size());

            for (const RestProperty& property : rest)
            {
                indices.push_back(property.property_index);
            }

            return indices;
        }

        void require_property(
            const wz::external::ply::ScalarTable& table,
            std::string_view name,
            int& out_index,
            std::vector<std::string>& missing)
        {
            out_index = find_property_index(table, name);

            if (out_index < 0)
            {
                missing.push_back(std::string(name));
            }
        }

        std::string make_missing_error(const std::vector<std::string>& missing)
        {
            if (missing.empty())
            {
                return {};
            }

            std::string error = "Missing required Gaussian splat PLY properties: ";

            for (size_t i = 0; i < missing.size(); ++i)
            {
                if (i > 0)
                {
                    error += ", ";
                }

                error += missing[i];
            }

            return error;
        }
    }

    bool GaussianSplatPLYSchema::has_required_fields() const noexcept
    {
        return x >= 0
            && y >= 0
            && z >= 0
            && opacity >= 0
            && scale_0 >= 0
            && scale_1 >= 0
            && scale_2 >= 0
            && rot_0 >= 0
            && rot_1 >= 0
            && rot_2 >= 0
            && rot_3 >= 0
            && f_dc_0 >= 0
            && f_dc_1 >= 0
            && f_dc_2 >= 0;
    }

    GaussianSplatPLYSchemaResult detect_gaussian_splat_ply_schema(
        const wz::external::ply::ScalarTable& table)
    {
        GaussianSplatPLYSchemaResult result;

        if (table.element_name != "vertex")
        {
            result.ok = false;
            result.error = "Gaussian splat PLY schema detection expected a vertex table";
            return result;
        }

        GaussianSplatPLYSchema schema;
        std::vector<std::string> missing;

        require_property(table, "x", schema.x, missing);
        require_property(table, "y", schema.y, missing);
        require_property(table, "z", schema.z, missing);

        require_property(table, "opacity", schema.opacity, missing);

        require_property(table, "scale_0", schema.scale_0, missing);
        require_property(table, "scale_1", schema.scale_1, missing);
        require_property(table, "scale_2", schema.scale_2, missing);

        require_property(table, "rot_0", schema.rot_0, missing);
        require_property(table, "rot_1", schema.rot_1, missing);
        require_property(table, "rot_2", schema.rot_2, missing);
        require_property(table, "rot_3", schema.rot_3, missing);

        require_property(table, "f_dc_0", schema.f_dc_0, missing);
        require_property(table, "f_dc_1", schema.f_dc_1, missing);
        require_property(table, "f_dc_2", schema.f_dc_2, missing);

        schema.f_rest = collect_f_rest_indices(table);

        if (!missing.empty())
        {
            result.ok = false;
            result.schema = std::move(schema);
            result.error = make_missing_error(missing);
            return result;
        }

        result.ok = true;
        result.schema = std::move(schema);
        return result;
    }
}