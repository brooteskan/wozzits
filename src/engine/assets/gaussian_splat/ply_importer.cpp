#include <engine/assets/gaussian_splat/ply_importer.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        struct PLYPropertyMap
        {
            int x = -1;
            int y = -1;
            int z = -1;

            int opacity = -1;

            int red = -1;
            int green = -1;
            int blue = -1;

            int f_dc_0 = -1;
            int f_dc_1 = -1;
            int f_dc_2 = -1;

            int scale_0 = -1;
            int scale_1 = -1;
            int scale_2 = -1;

            int rot_0 = -1;
            int rot_1 = -1;
            int rot_2 = -1;
            int rot_3 = -1;
        };

        bool parse_float(std::string_view text, float& out)
        {
            const char* first = text.data();
            const char* last = text.data() + text.size();

            const auto result = std::from_chars(first, last, out);
            return result.ec == std::errc{} && result.ptr == last;
        }

        bool parse_uint(std::string_view text, uint32_t& out)
        {
            const char* first = text.data();
            const char* last = text.data() + text.size();

            const auto result = std::from_chars(first, last, out);
            return result.ec == std::errc{} && result.ptr == last;
        }

        std::vector<std::string_view> split_ws(std::string_view line)
        {
            std::vector<std::string_view> parts;

            std::size_t i = 0;
            while (i < line.size()) {
                while (i < line.size() &&
                    (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
                    ++i;

                const std::size_t start = i;

                while (i < line.size() &&
                    line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
                    ++i;

                if (start != i)
                    parts.push_back(line.substr(start, i - start));
            }

            return parts;
        }

        float value_at(
            const std::vector<std::string_view>& fields,
            int index,
            float fallback)
        {
            if (index < 0)
                return fallback;

            const std::size_t i = static_cast<std::size_t>(index);
            if (i >= fields.size())
                return fallback;

            float value = fallback;
            if (!parse_float(fields[i], value))
                return fallback;

            return value;
        }

        float clamp01(float v)
        {
            return std::max(0.0f, std::min(1.0f, v));
        }

        float logistic(float v)
        {
            // Common 3DGS PLY files store opacity in logit space.
            // This is harmless for ordinary values near 0..1, but not perfect.
            // Later we can make this import option explicit.
            return 1.0f / (1.0f + std::exp(-v));
        }

        float exp_scale(float v)
        {
            // Common 3DGS PLY files store scale in log space.
            return std::exp(v);
        }

        void update_bounds(GaussianSplatCloudData& cloud, const GaussianSplat& splat, bool first)
        {
            for (int axis = 0; axis < 3; ++axis) {
                if (first) {
                    cloud.bounds_min[axis] = splat.position[axis];
                    cloud.bounds_max[axis] = splat.position[axis];
                }
                else {
                    cloud.bounds_min[axis] = std::min(cloud.bounds_min[axis], splat.position[axis]);
                    cloud.bounds_max[axis] = std::max(cloud.bounds_max[axis], splat.position[axis]);
                }
            }
        }

        void set_property_index(PLYPropertyMap& map, std::string_view name, int index)
        {
            if (name == "x") map.x = index;
            else if (name == "y") map.y = index;
            else if (name == "z") map.z = index;
            else if (name == "opacity") map.opacity = index;
            else if (name == "red") map.red = index;
            else if (name == "green") map.green = index;
            else if (name == "blue") map.blue = index;
            else if (name == "f_dc_0") map.f_dc_0 = index;
            else if (name == "f_dc_1") map.f_dc_1 = index;
            else if (name == "f_dc_2") map.f_dc_2 = index;
            else if (name == "scale_0") map.scale_0 = index;
            else if (name == "scale_1") map.scale_1 = index;
            else if (name == "scale_2") map.scale_2 = index;
            else if (name == "rot_0") map.rot_0 = index;
            else if (name == "rot_1") map.rot_1 = index;
            else if (name == "rot_2") map.rot_2 = index;
            else if (name == "rot_3") map.rot_3 = index;
        }
    }

    bool import_ascii_ply_gaussian_splats(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        GaussianSplatCloudData& out)
    {
        out = {};

        if (!bytes || byte_count == 0)
            return false;

        const std::string text(
            reinterpret_cast<const char*>(bytes),
            reinterpret_cast<const char*>(bytes) + byte_count);

        std::istringstream stream(text);

        std::string line;
        if (!std::getline(stream, line))
            return false;

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line != "ply")
            return false;

        bool ascii = false;
        bool in_vertex_element = false;
        uint32_t vertex_count = 0;
        int property_count = 0;
        PLYPropertyMap props{};

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const auto parts = split_ws(line);
            if (parts.empty())
                continue;

            if (parts[0] == "format") {
                if (parts.size() < 2)
                    return false;

                ascii = parts[1] == "ascii";
                if (!ascii)
                    return false;
            }
            else if (parts[0] == "element") {
                in_vertex_element = false;

                if (parts.size() >= 3 && parts[1] == "vertex") {
                    if (!parse_uint(parts[2], vertex_count))
                        return false;

                    in_vertex_element = true;
                    property_count = 0;
                }
            }
            else if (parts[0] == "property" && in_vertex_element) {
                if (parts.size() < 3)
                    return false;

                const std::string_view property_name = parts.back();
                set_property_index(props, property_name, property_count);
                ++property_count;
            }
            else if (parts[0] == "end_header") {
                break;
            }
        }

        if (!ascii || vertex_count == 0)
            return false;

        if (props.x < 0 || props.y < 0 || props.z < 0)
            return false;

        out.color_mode = GaussianSplatColorMode::RGB;
        out.splats.reserve(vertex_count);

        for (uint32_t i = 0; i < vertex_count; ++i) {
            if (!std::getline(stream, line))
                return false;

            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const auto fields = split_ws(line);
            if (fields.size() < static_cast<std::size_t>(property_count))
                return false;

            GaussianSplat splat{};

            splat.position[0] = value_at(fields, props.x, 0.0f);
            splat.position[1] = value_at(fields, props.y, 0.0f);
            splat.position[2] = value_at(fields, props.z, 0.0f);

            if (props.scale_0 >= 0 || props.scale_1 >= 0 || props.scale_2 >= 0) {
                splat.scale[0] = exp_scale(value_at(fields, props.scale_0, -3.0f));
                splat.scale[1] = exp_scale(value_at(fields, props.scale_1, -3.0f));
                splat.scale[2] = exp_scale(value_at(fields, props.scale_2, -3.0f));
            }
            else {
                splat.scale[0] = 0.05f;
                splat.scale[1] = 0.05f;
                splat.scale[2] = 0.05f;
            }

            if (props.rot_0 >= 0 || props.rot_1 >= 0 || props.rot_2 >= 0 || props.rot_3 >= 0) {
                splat.rotation[0] = value_at(fields, props.rot_0, 0.0f);
                splat.rotation[1] = value_at(fields, props.rot_1, 0.0f);
                splat.rotation[2] = value_at(fields, props.rot_2, 0.0f);
                splat.rotation[3] = value_at(fields, props.rot_3, 1.0f);
            }

            if (props.opacity >= 0)
                splat.opacity = clamp01(logistic(value_at(fields, props.opacity, 1.0f)));
            else
                splat.opacity = 1.0f;

            if (props.red >= 0 && props.green >= 0 && props.blue >= 0) {
                splat.color[0] = clamp01(value_at(fields, props.red, 255.0f) / 255.0f);
                splat.color[1] = clamp01(value_at(fields, props.green, 255.0f) / 255.0f);
                splat.color[2] = clamp01(value_at(fields, props.blue, 255.0f) / 255.0f);
            }
            else if (props.f_dc_0 >= 0 && props.f_dc_1 >= 0 && props.f_dc_2 >= 0) {
                // 3DGS SH DC coefficients. This rough conversion is enough for
                // debug rendering; we can refine color handling later.
                constexpr float c0 = 0.28209479177387814f;
                splat.color[0] = clamp01(0.5f + c0 * value_at(fields, props.f_dc_0, 0.0f));
                splat.color[1] = clamp01(0.5f + c0 * value_at(fields, props.f_dc_1, 0.0f));
                splat.color[2] = clamp01(0.5f + c0 * value_at(fields, props.f_dc_2, 0.0f));
            }
            else {
                splat.color[0] = 1.0f;
                splat.color[1] = 1.0f;
                splat.color[2] = 1.0f;
            }

            update_bounds(out, splat, out.splats.empty());
            out.splats.push_back(splat);
        }

        return out.valid();
    }
}