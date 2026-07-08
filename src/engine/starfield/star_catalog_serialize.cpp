// src/engine/starfield/star_catalog_serialize.cpp

#include <engine/starfield/star_catalog_serialize.h>

#include <external/json/json_read_helpers.h>

namespace wz::engine::starfield
{
    using wz::json::JSONValue;
    using wz::json::JSONValueKind;

    namespace
    {
        void read_params(const JSONValue& obj, StarImportParams& p)
        {
            if (auto v = wz::json::read_number(obj, "reference_magnitude")) {
                p.reference_magnitude = *v;
            }
            if (auto v = wz::json::read_number(obj, "exposure")) {
                p.exposure = *v;
            }
            if (auto v = wz::json::read_number(obj, "color_saturation")) {
                p.color_saturation = *v;
            }
            if (auto v = wz::json::read_number(obj, "magnitude_min")) {
                p.magnitude_min = *v;
            }
            if (auto v = wz::json::read_number(obj, "magnitude_max")) {
                p.magnitude_max = *v;
            }
            if (auto v = wz::json::read_number(obj, "solid_angle")) {
                p.solid_angle = *v;
            }
        }
    } // namespace

    bool star_catalog_from_json(
        const JSONValue& value,
        std::vector<CatalogRecord>& out_records,
        StarImportParams& out_params,
        std::string& out_source_name,
        std::string& error)
    {
        if (value.kind != JSONValueKind::Object) {
            error = "root is not a JSON object";
            return false;
        }

        out_records.clear();
        out_params = StarImportParams{};
        out_source_name.clear();

        if (auto s = wz::json::read_string(value, "source_name")) {
            out_source_name = std::string(*s);
        }

        if (const JSONValue* params = wz::json::find_member(value, "params")) {
            if (params->kind != JSONValueKind::Object) {
                error = "\"params\" is not an object";
                return false;
            }
            read_params(*params, out_params);
        }

        const JSONValue* stars = wz::json::find_member(value, "stars");
        if (!stars) {
            error = "missing \"stars\" array";
            return false;
        }
        if (stars->kind != JSONValueKind::Array) {
            error = "\"stars\" is not an array";
            return false;
        }

        out_records.reserve(stars->array_values.size());
        for (const auto& item : stars->array_values) {
            if (!item || item->kind != JSONValueKind::Object) {
                error = "star entry is not an object";
                return false;
            }

            CatalogRecord record;
            const auto ra = wz::json::read_number(*item, "ra_hours");
            const auto dec = wz::json::read_number(*item, "dec_deg");
            const auto vmag = wz::json::read_number(*item, "vmag");
            if (!ra || !dec || !vmag) {
                error = "star entry missing ra_hours / dec_deg / vmag";
                return false;
            }
            record.ra_hours = *ra;
            record.dec_deg = *dec;
            record.vmag = *vmag;

            if (const auto bv = wz::json::read_number(*item, "bv")) {
                record.bv = *bv;
                record.has_bv = true;
            } else {
                record.bv = 0.0;
                record.has_bv = false;
            }

            out_records.push_back(record);
        }

        return true;
    }

} // namespace wz::engine::starfield
