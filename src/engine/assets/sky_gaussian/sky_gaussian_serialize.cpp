// src/engine/assets/sky_gaussian/sky_gaussian_serialize.cpp

#include <engine/assets/sky_gaussian/sky_gaussian_serialize.h>

#include <external/json/json_read_helpers.h>

#include <math/vec3.h>

#include <cmath>
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
        // sky_gaussian_from_json is the ONE choke point every consumer passes
        // through -- the compiler copies these fields verbatim into the resident
        // buffers and the shaders use them raw. It range-checked `direction` and
        // `amplitude` (via read_float3) but not `sharpness` or `solid_angle`, so
        // an authored 1e39 loaded as +inf on adjacent lines of the same function
        // (issue #316, C3-C11).
        //
        // Hand-authored .sky_gaussian.json is a real production path here --
        // night_sky.sky_gaussian.json is wired into the live project graph and
        // carries sharpness values the fitter's own floor cannot produce -- so
        // these are not defensive checks against our own writer.
        [[nodiscard]] bool finite3(const wz::math::Vec3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // The unit-direction contract is documented in three places
        // (sky_gaussian.h, and the SkyLobe/SkyPoint structs in both shaders) and
        // was enforced in none. A non-unit axis breaks the closed-form SG bound
        // exp(|dm| - la - lb) <= 1, which is what keeps the product integral
        // finite: measured, an axis 5% long at the sharpness roughness=0
        // produces (2/m^2 = 20000) evaluates to +inf (issue #316, C3-H1).
        //
        // Merely non-unit is REPAIRED rather than rejected: the fitter always
        // emits unit axes, so a 0.99-length direction in a hand-written file
        // means the direction, not a scaled one. Degenerate or non-finite is a
        // hard error, because there is no direction to recover.
        [[nodiscard]] bool normalize_direction(
            wz::math::Vec3& d, const char* what, std::string& error)
        {
            if (!finite3(d)) {
                error = std::string(what) + " \"direction\" is not finite";
                return false;
            }
            const float len = wz::math::length(d);
            if (!(len > 1e-6f)) {
                error = std::string(what) + " \"direction\" is degenerate";
                return false;
            }
            d = d * (1.0f / len);
            return true;
        }

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
                if (!normalize_direction(g.direction, "lobe", error)) {
                    return false;
                }
                const auto sharp = wz::json::read_number(*item, "sharpness");
                if (!sharp) {
                    error = "lobe entry missing \"sharpness\"";
                    return false;
                }
                g.sharpness = static_cast<float>(*sharp);
                // Positive and finite. A NEGATIVE sharpness inverts the lobe
                // into an unbounded anti-lobe: evaluate_lobe has no sign guard
                // while lobe_energy eleven lines below it floors at 1e-8, so a
                // negative value grew without limit toward the ANTIPODE of its
                // own axis (issue #316, C3-C12).
                if (!std::isfinite(g.sharpness) || !(g.sharpness > 0.0f)) {
                    error = "lobe \"sharpness\" must be finite and positive";
                    return false;
                }
                float amp[3];
                if (!wz::json::read_float3(*item, "amplitude", amp)) {
                    error = "lobe entry missing \"amplitude\"";
                    return false;
                }
                g.amplitude = { amp[0], amp[1], amp[2] };
                if (!finite3(g.amplitude)) {
                    error = "lobe \"amplitude\" is not finite";
                    return false;
                }
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
                if (!normalize_direction(ps.direction, "point-source", error)) {
                    return false;
                }
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
                // A solid angle outside (0, 4*pi] is not a cone on the sphere.
                // Past it the shaders' smoothstep edges invert and the emitter
                // floods the whole sky dome at full radiance (issue #316).
                if (!std::isfinite(ps.solid_angle)
                    || !(ps.solid_angle > 0.0f)
                    || ps.solid_angle > 4.0f * 3.14159265358979323846f)
                {
                    error = "point-source \"solid_angle\" must be in (0, 4*pi]";
                    return false;
                }
                if (!finite3(ps.radiance)) {
                    error = "point-source \"radiance\" is not finite";
                    return false;
                }
                out.point_sources.push_back(ps);
            }
        }

        return true;
    }

} // namespace wz::engine::assets::sky
