#include <external/toml/toml_parser.h>

#include <toml.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace wz::toml
{
    namespace
    {
        TOMLValuePtr make_value(TOMLValueKind kind)
        {
            auto value = std::make_unique<TOMLValue>();
            value->kind = kind;
            return value;
        }

        TOMLValuePtr copy_node(const ::toml::node& node, TOMLParseError& error)
        {
            if (node.is_boolean()) {
                auto out = make_value(TOMLValueKind::Bool);
                out->bool_value = node.value<bool>().value_or(false);
                return out;
            }

            if (node.is_integer()) {
                auto out = make_value(TOMLValueKind::Integer);
                out->integer_value = node.value<int64_t>().value_or(0);
                return out;
            }

            if (node.is_floating_point()) {
                auto out = make_value(TOMLValueKind::Float);
                out->float_value = node.value<double>().value_or(0.0);
                return out;
            }

            if (node.is_string()) {
                auto out = make_value(TOMLValueKind::String);
                out->string_value = node.value<std::string>().value_or("");
                return out;
            }

            if (node.is_array()) {
                const auto* array = node.as_array();
                if (!array) {
                    error.message = "TOML node reported array but as_array() failed";
                    return nullptr;
                }

                auto out = make_value(TOMLValueKind::Array);

                for (const auto& element : *array) {
                    auto child = copy_node(element, error);
                    if (!child)
                        return nullptr;

                    out->array_values.push_back(std::move(child));
                }

                return out;
            }

            if (node.is_table()) {
                const auto* table = node.as_table();
                if (!table) {
                    error.message = "TOML node reported table but as_table() failed";
                    return nullptr;
                }

                auto out = make_value(TOMLValueKind::Table);

                for (const auto& [key, value] : *table) {
                    auto child = copy_node(value, error);
                    if (!child)
                        return nullptr;

                    out->table_members.push_back(TOMLMember{
                        .key = std::string{ key.str() },
                        .value = std::move(child),
                        });
                }

                return out;
            }

            error.message =
                "unsupported TOML value type; dates/times are not supported in V1";
            return nullptr;
        }
    }

    TOMLParseResult parse_toml_bytes(
        const uint8_t* data,
        size_t         size)
    {
        TOMLParseResult result{};

        if (!data && size != 0) {
            result.error.message = "null TOML input pointer";
            return result;
        }

        const std::string text(
            reinterpret_cast<const char*>(data),
            reinterpret_cast<const char*>(data) + size
        );

        try {
            auto parsed = ::toml::parse(text);

            result.document.root = copy_node(parsed, result.error);

            if (!result.document.root)
                return result;

            result.ok = true;
            return result;
        }
        catch (const ::toml::parse_error& err) {
            result.error.message = std::string{ err.description() };

            const auto source = err.source();
            result.error.line =
                static_cast<uint32_t>(source.begin.line);
            result.error.column =
                static_cast<uint32_t>(source.begin.column);

            return result;
        }

        if (!result.document.root)
            return result;

        result.ok = true;
        return result;
    }

    TOMLParseResult parse_toml_string(const std::string& text)
    {
        return parse_toml_bytes(
            reinterpret_cast<const uint8_t*>(text.data()),
            text.size()
        );
    }

    TOMLParseResult parse_toml_bytes(const std::vector<uint8_t>& bytes)
    {
        return parse_toml_bytes(bytes.data(), bytes.size());
    }

} // namespace wz::toml