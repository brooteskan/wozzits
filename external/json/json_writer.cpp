#include <external/json/json_writer.h>

#include <cstddef>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace wz::json
{
    namespace
    {
        void write_indent(
            std::string& out,
            const JSONWriteOptions& options,
            uint32_t depth)
        {
            if (!options.pretty)
                return;

            out.append(depth * options.indent_spaces, ' ');
        }

        void write_escaped_string(std::string& out, const std::string& text)
        {
            out.push_back('"');
            for (const char ch : text) {
                switch (ch) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20) {
                        std::ostringstream escaped;
                        escaped << "\\u"
                            << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(
                                static_cast<unsigned char>(ch));
                        out += escaped.str();
                    }
                    else {
                        out.push_back(ch);
                    }
                    break;
                }
            }
            out.push_back('"');
        }

        void write_number(std::string& out, double value)
        {
            if (!std::isfinite(value)) {
                out += "null";
                return;
            }

            std::ostringstream ss;
            ss << std::setprecision(std::numeric_limits<double>::max_digits10)
               << value;
            out += ss.str();
        }

        void write_value(
            std::string& out,
            const JSONValue& value,
            const JSONWriteOptions& options,
            uint32_t depth)
        {
            switch (value.kind) {
            case JSONValueKind::Null:
                out += "null";
                break;
            case JSONValueKind::Bool:
                out += value.bool_value ? "true" : "false";
                break;
            case JSONValueKind::Number:
                write_number(out, value.number_value);
                break;
            case JSONValueKind::String:
                write_escaped_string(out, value.string_value);
                break;
            case JSONValueKind::Array:
                out.push_back('[');
                if (!value.array_values.empty() && options.pretty) {
                    out.push_back('\n');
                }
                for (size_t i = 0; i < value.array_values.size(); ++i) {
                    if (options.pretty) {
                        write_indent(out, options, depth + 1);
                    }
                    if (value.array_values[i]) {
                        write_value(
                            out,
                            *value.array_values[i],
                            options,
                            depth + 1);
                    }
                    else {
                        out += "null";
                    }
                    if (i + 1 < value.array_values.size()) {
                        out.push_back(',');
                    }
                    if (options.pretty) {
                        out.push_back('\n');
                    }
                }
                if (!value.array_values.empty() && options.pretty) {
                    write_indent(out, options, depth);
                }
                out.push_back(']');
                break;
            case JSONValueKind::Object:
                out.push_back('{');
                if (!value.object_members.empty() && options.pretty) {
                    out.push_back('\n');
                }
                for (size_t i = 0; i < value.object_members.size(); ++i) {
                    const auto& member = value.object_members[i];
                    if (options.pretty) {
                        write_indent(out, options, depth + 1);
                    }
                    write_escaped_string(out, member.key);
                    out += options.pretty ? ": " : ":";
                    if (member.value) {
                        write_value(out, *member.value, options, depth + 1);
                    }
                    else {
                        out += "null";
                    }
                    if (i + 1 < value.object_members.size()) {
                        out.push_back(',');
                    }
                    if (options.pretty) {
                        out.push_back('\n');
                    }
                }
                if (!value.object_members.empty() && options.pretty) {
                    write_indent(out, options, depth);
                }
                out.push_back('}');
                break;
            }
        }
    }

    std::string serialize_json(
        const JSONValue& value,
        const JSONWriteOptions& options)
    {
        std::string out;
        write_value(out, value, options, 0);
        if (options.pretty) {
            out.push_back('\n');
        }
        return out;
    }

    std::string serialize_json(
        const JSONDocument& document,
        const JSONWriteOptions& options)
    {
        if (!document.root) {
            return options.pretty ? "null\n" : "null";
        }
        return serialize_json(*document.root, options);
    }

} // namespace wz::json
