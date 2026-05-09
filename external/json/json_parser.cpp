// external/json/json_parser.cpp

#include <external/json/json_parser.h>

#include "./yyjson/yyjson.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace wz::json
{
    namespace
    {
        wz::json::JSONValuePtr copy_value(
            yyjson_val* value,
            wz::json::JSONParseError& error)
        {
            if (!value) {
                error.message = "null yyjson value";
                return nullptr;
            }

            auto out = std::make_unique<wz::json::JSONValue>();

            if (yyjson_is_null(value)) {
                out->kind = wz::json::JSONValueKind::Null;
                return out;
            }

            if (yyjson_is_bool(value)) {
                out->kind = wz::json::JSONValueKind::Bool;
                out->bool_value = yyjson_get_bool(value);
                return out;
            }

            if (yyjson_is_num(value)) {
                out->kind = wz::json::JSONValueKind::Number;
                out->number_value = yyjson_get_num(value);
                return out;
            }

            if (yyjson_is_str(value)) {
                out->kind = wz::json::JSONValueKind::String;
                const char* s = yyjson_get_str(value);
                out->string_value = s ? s : "";
                return out;
            }

            if (yyjson_is_arr(value)) {
                out->kind = wz::json::JSONValueKind::Array;

                yyjson_val* item = nullptr;
                yyjson_arr_iter iter;
                yyjson_arr_iter_init(value, &iter);

                while ((item = yyjson_arr_iter_next(&iter)) != nullptr) {
                    auto child = copy_value(item, error);
                    if (!child)
                        return nullptr;

                    out->array_values.push_back(std::move(child));
                }

                return out;
            }

            if (yyjson_is_obj(value)) {
                out->kind = wz::json::JSONValueKind::Object;

                yyjson_val* key = nullptr;
                yyjson_val* val = nullptr;
                yyjson_obj_iter iter;
                yyjson_obj_iter_init(value, &iter);

                while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
                    val = yyjson_obj_iter_get_val(key);

                    const char* key_text = yyjson_get_str(key);
                    if (!key_text) {
                        error.message = "JSON object key is not a string";
                        return nullptr;
                    }

                    const std::string key_string = key_text;

                    for (const auto& existing : out->object_members) {
                        if (existing.key == key_string) {
                            error.message = "duplicate JSON object key: " + key_string;
                            return nullptr;
                        }
                    }

                    auto child = copy_value(val, error);
                    if (!child)
                        return nullptr;

                    out->object_members.push_back(wz::json::JSONMember{
                        .key = key_string,
                        .value = std::move(child),
                        });
                }

                return out;
            }

            error.message = "unsupported JSON value type";
            return nullptr;
        }
    }

    wz::json::JSONParseResult parse_json_string(
        const std::string& text)
    {
        return parse_json_bytes(
            reinterpret_cast<const uint8_t*>(text.data()),
            text.size()
        );
    }

    JSONParseResult parse_json_bytes(
        const uint8_t* data,
        size_t size)
    {
        JSONParseResult result{};

        yyjson_read_err err{};
        std::string buffer(
            reinterpret_cast<const char*>(data),
            reinterpret_cast<const char*>(data) + size
        );

        yyjson_doc* doc = yyjson_read_opts(
            buffer.data(),
            buffer.size(),
            0,
            nullptr,
            &err
        );

        if (!doc) {
            result.ok = false;
            result.error.message = err.msg ? err.msg : "failed to parse JSON";
            result.error.position = err.pos;
            return result;
        }

        yyjson_val* root = yyjson_doc_get_root(doc);
        if (!root) {
            yyjson_doc_free(doc);
            result.ok = false;
            result.error.message = "JSON document has no root value";
            return result;
        }

        result.document.root = copy_value(root, result.error);
        yyjson_doc_free(doc);

        result.ok = static_cast<bool>(result.document.root);
        return result;
    }

    wz::json::JSONParseResult parse_json_bytes(
        const std::vector<uint8_t>& bytes)
    {
        return parse_json_bytes(bytes.data(), bytes.size());
    }
}