// src/engine/assets/sky_gaussian/sky_gaussian_serialize.cpp

#include <engine/assets/sky_gaussian/sky_gaussian_serialize.h>

#include <external/json/json_read_helpers.h>

#include <memory>
#include <utility>

namespace wz::engine::assets::sky
{
    using wz::json::JSONMember;
    using wz::json::JSONValue;
    using wz::json::JSONValueKind;
    using wz::json::JSONValuePtr;

    namespace
    {
        JSONValuePtr json_number(double value)
        {
            auto out = std::make_unique<JSONValue>();
            out->kind = JSONValueKind::Number;
            out->number_value = value;
            return out;
        }

        JSONValuePtr json_string(std::string value)
        {
            auto out = std::make_unique<JSONValue>();
            out->kind = JSONValueKind::String;
            out->string_value = std::move(value);
            return out;
        }

        JSONValuePtr json_object()
        {
            auto out = std::make_unique<JSONValue>();
            out->kind = JSONValueKind::Object;
            return out;
        }

        JSONValuePtr json_array()
        {
            auto out = std::make_unique<JSONValue>();
            out->kind = JSONValueKind::Array;
            return out;
        }

        void add_member(JSONValue& obj, std::string key, JSONValuePtr value)
        {
            obj.object_members.push_back(
                JSONMember{ std::move(key), std::move(value) });
        }

        JSONValuePtr json_vec3(const wz::math::Vec3& v)
        {
            auto arr = json_array();
            arr->array_values.push_back(json_number(v.x));
            arr->array_values.push_back(json_number(v.y));
            arr->array_values.push_back(json_number(v.z));
            return arr;
        }
    } // namespace

    JSONValue sky_gaussian_to_json(const SkyGaussianSet& set)
    {
        JSONValue root;
        root.kind = JSONValueKind::Object;

        add_member(root, "version", json_number(1.0));
        add_member(root, "source_name", json_string(set.source_name));
        add_member(root, "source_width",
            json_number(static_cast<double>(set.source_width)));
        add_member(root, "source_height",
            json_number(static_cast<double>(set.source_height)));

        auto lobes = json_array();
        for (const auto& g : set.lobes) {
            auto o = json_object();
            add_member(*o, "direction", json_vec3(g.direction));
            add_member(*o, "sharpness",
                json_number(static_cast<double>(g.sharpness)));
            add_member(*o, "amplitude", json_vec3(g.amplitude));
            lobes->array_values.push_back(std::move(o));
        }
        add_member(root, "lobes", std::move(lobes));

        auto points = json_array();
        for (const auto& ps : set.point_sources) {
            auto o = json_object();
            add_member(*o, "direction", json_vec3(ps.direction));
            add_member(*o, "radiance", json_vec3(ps.radiance));
            add_member(*o, "solid_angle",
                json_number(static_cast<double>(ps.solid_angle)));
            points->array_values.push_back(std::move(o));
        }
        add_member(root, "point_sources", std::move(points));

        return root;
    }

    bool sky_gaussian_from_json(
        const JSONValue& value,
        SkyGaussianSet& out,
        std::string& error)
    {
        if (value.kind != JSONValueKind::Object) {
            error = "root is not a JSON object";
            return false;
        }

        out = SkyGaussianSet{};

        if (auto s = wz::json::read_string(value, "source_name")) {
            out.source_name = std::string(*s);
        }
        if (auto w = wz::json::read_uint(value, "source_width")) {
            out.source_width = *w;
        }
        if (auto h = wz::json::read_uint(value, "source_height")) {
            out.source_height = *h;
        }

        if (const JSONValue* lobes =
                wz::json::find_member(value, "lobes")) {
            if (lobes->kind != JSONValueKind::Array) {
                error = "\"lobes\" is not an array";
                return false;
            }
            for (const auto& item : lobes->array_values) {
                if (!item || item->kind != JSONValueKind::Object) {
                    error = "lobe entry is not an object";
                    return false;
                }
                SkyGaussianLobe g;
                float dir[3];
                if (!wz::json::read_float3(*item, "direction", dir)) {
                    error = "lobe entry missing \"direction\"";
                    return false;
                }
                g.direction = { dir[0], dir[1], dir[2] };
                const auto sharp = wz::json::read_number(*item, "sharpness");
                if (!sharp) {
                    error = "lobe entry missing \"sharpness\"";
                    return false;
                }
                g.sharpness = static_cast<float>(*sharp);
                float amp[3];
                if (!wz::json::read_float3(*item, "amplitude", amp)) {
                    error = "lobe entry missing \"amplitude\"";
                    return false;
                }
                g.amplitude = { amp[0], amp[1], amp[2] };
                out.lobes.push_back(g);
            }
        }

        if (const JSONValue* points =
                wz::json::find_member(value, "point_sources")) {
            if (points->kind != JSONValueKind::Array) {
                error = "\"point_sources\" is not an array";
                return false;
            }
            for (const auto& item : points->array_values) {
                if (!item || item->kind != JSONValueKind::Object) {
                    error = "point-source entry is not an object";
                    return false;
                }
                SkyPointSource ps;
                float dir[3];
                if (!wz::json::read_float3(*item, "direction", dir)) {
                    error = "point-source entry missing \"direction\"";
                    return false;
                }
                ps.direction = { dir[0], dir[1], dir[2] };
                float rad[3];
                if (!wz::json::read_float3(*item, "radiance", rad)) {
                    error = "point-source entry missing \"radiance\"";
                    return false;
                }
                ps.radiance = { rad[0], rad[1], rad[2] };
                const auto sa = wz::json::read_number(*item, "solid_angle");
                if (!sa) {
                    error = "point-source entry missing \"solid_angle\"";
                    return false;
                }
                ps.solid_angle = static_cast<float>(*sa);
                out.point_sources.push_back(ps);
            }
        }

        return true;
    }

} // namespace wz::engine::assets::sky
