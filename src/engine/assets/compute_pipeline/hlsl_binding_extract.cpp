#include <engine/assets/compute_pipeline/hlsl_binding_extract.h>

#include <file/filesystem.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace wz::engine::assets
{
    namespace
    {
        std::string strip_comments(std::string_view source)
        {
            std::string out;
            out.reserve(source.size());

            bool line_comment = false;
            bool block_comment = false;
            for (size_t i = 0; i < source.size(); ++i) {
                const char ch = source[i];
                const char next = i + 1u < source.size() ? source[i + 1u] : '\0';

                if (line_comment) {
                    if (ch == '\n') {
                        line_comment = false;
                        out.push_back(ch);
                    }
                    continue;
                }
                if (block_comment) {
                    if (ch == '*' && next == '/') {
                        block_comment = false;
                        ++i;
                    }
                    else if (ch == '\n') {
                        out.push_back(ch);
                    }
                    continue;
                }
                if (ch == '/' && next == '/') {
                    line_comment = true;
                    ++i;
                    continue;
                }
                if (ch == '/' && next == '*') {
                    block_comment = true;
                    ++i;
                    continue;
                }
                out.push_back(ch);
            }

            return out;
        }

        uint32_t scalar_dwords(std::string_view type) noexcept
        {
            if (type == "uint" || type == "float") {
                return 1u;
            }
            if (type == "uint2" || type == "float2") {
                return 2u;
            }
            if (type == "uint3" || type == "float3") {
                return 3u;
            }
            if (type == "uint4" || type == "float4") {
                return 4u;
            }
            return 0u;
        }

        WzGpuPortKind scalar_port_kind(std::string_view type) noexcept
        {
            if (type.starts_with("uint")) {
                return WZ_GPU_PORT_U32;
            }
            if (type.starts_with("float")) {
                return WZ_GPU_PORT_F32;
            }
            return WZ_GPU_PORT_NONE;
        }

        uint32_t structured_stride_bytes(
            std::string_view type,
            const std::unordered_map<std::string, uint32_t>& struct_sizes)
        {
            if (const uint32_t dwords = scalar_dwords(type); dwords > 0u) {
                return dwords * 4u;
            }
            if (const auto it = struct_sizes.find(std::string(type));
                it != struct_sizes.end())
            {
                return it->second;
            }
            return 0u;
        }

        uint32_t parse_register_space(
            const std::ssub_match& space_match)
        {
            if (!space_match.matched) {
                return 0u;
            }
            const std::string text = space_match.str();
            constexpr std::string_view prefix = "space";
            if (text.size() <= prefix.size()) {
                return 0u;
            }
            return static_cast<uint32_t>(
                std::stoul(text.substr(prefix.size())));
        }

        std::unordered_map<std::string, uint32_t> extract_struct_sizes(
            const std::string& source,
            std::vector<std::string>& diagnostics)
        {
            std::unordered_map<std::string, uint32_t> out;
            const std::regex struct_re(
                R"(struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([^}]*)\}\s*;)");
            const std::regex field_re(
                R"(\b(uint|float|uint[234]|float[234])\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*\[[0-9]+\])?\s*;)");
            auto begin = std::sregex_iterator(
                source.begin(),
                source.end(),
                struct_re);
            const auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                const std::string name = (*it)[1].str();
                const std::string body = (*it)[2].str();
                uint32_t dwords = 0u;
                size_t consumed = 0u;
                auto fb = std::sregex_iterator(
                    body.begin(),
                    body.end(),
                    field_re);
                for (auto fit = fb; fit != end; ++fit) {
                    dwords += scalar_dwords((*fit)[1].str());
                    consumed += static_cast<size_t>((*fit).length());
                }
                if (dwords == 0u || consumed == 0u) {
                    diagnostics.push_back(
                        "unsupported or empty struct element type '" + name + "'");
                    continue;
                }
                out[name] = dwords * 4u;
            }
            return out;
        }

        void update_root_constant_total(
            HlslBindingExtraction& extraction,
            const HlslBindingPort& port)
        {
            const uint64_t end =
                static_cast<uint64_t>(port.root_constant_offset)
                + static_cast<uint64_t>(port.root_constant_dwords);
            if (end > extraction.root_constant_dwords) {
                extraction.root_constant_dwords = static_cast<uint32_t>(end);
            }
        }
    }

    std::string normalize_hlsl_port_name(std::string_view name)
    {
        std::string out;
        out.reserve(name.size() + 4u);

        char prev = '\0';
        for (size_t i = 0; i < name.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(name[i]);
            if (!std::isalnum(ch)) {
                if (!out.empty() && out.back() != '_') {
                    out.push_back('_');
                }
                prev = '_';
                continue;
            }

            const bool upper = std::isupper(ch) != 0;
            const bool prev_lower_or_digit =
                std::islower(static_cast<unsigned char>(prev)) != 0
                || std::isdigit(static_cast<unsigned char>(prev)) != 0;
            if (upper && prev_lower_or_digit && !out.empty()
                && out.back() != '_')
            {
                out.push_back('_');
            }

            out.push_back(
                static_cast<char>(std::tolower(ch)));
            prev = static_cast<char>(ch);
        }

        while (!out.empty() && out.back() == '_') {
            out.pop_back();
        }
        return out;
    }

    HlslBindingExtraction extract_hlsl_bindings_from_source(
        std::string_view source)
    {
        HlslBindingExtraction extraction{};
        const std::string clean = strip_comments(source);
        const auto struct_sizes =
            extract_struct_sizes(clean, extraction.diagnostics);

        const std::regex buffer_re(
            R"(\b(RW)?StructuredBuffer\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*register\s*\(\s*([tu])([0-9]+)\s*(?:,\s*(space[0-9]+)\s*)?\)\s*;)");
        auto bb = std::sregex_iterator(
            clean.begin(),
            clean.end(),
            buffer_re);
        const auto end = std::sregex_iterator();
        for (auto it = bb; it != end; ++it) {
            const bool writable = (*it)[1].matched;
            const std::string element_type = (*it)[2].str();
            const std::string identifier = (*it)[3].str();
            const char register_class = (*it)[4].str()[0];
            const uint32_t shader_register =
                static_cast<uint32_t>(std::stoul((*it)[5].str()));
            const uint32_t register_space = parse_register_space((*it)[6]);
            const uint32_t stride =
                structured_stride_bytes(element_type, struct_sizes);
            if (stride == 0u) {
                extraction.diagnostics.push_back(
                    "unsupported structured buffer element type '"
                    + element_type + "' for port '" + identifier + "'");
                continue;
            }
            if ((!writable && register_class != 't')
                || (writable && register_class != 'u'))
            {
                extraction.diagnostics.push_back(
                    "register class does not match buffer direction for port '"
                    + identifier + "'");
                continue;
            }

            extraction.ports.push_back(HlslBindingPort{
                .name = normalize_hlsl_port_name(identifier),
                .port_kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                .direction = static_cast<WzGpuPortDirection>(writable
                    ? WZ_GPU_PORT_OUTPUT
                    : WZ_GPU_PORT_INPUT),
                .target = HlslBindingPortTarget::Buffer,
                .binding_kind = writable
                    ? ComputeBindingKind::StructuredBufferUAV
                    : ComputeBindingKind::StructuredBufferSRV,
                .shader_register = shader_register,
                .register_space = register_space,
                .stride_bytes = stride,
            });
        }

        const std::regex cbuffer_re(
            R"(cbuffer\s+[A-Za-z_][A-Za-z0-9_]*\s*:\s*register\s*\(\s*b[0-9]+\s*(?:,\s*space[0-9]+\s*)?\)\s*\{([^}]*)\}\s*;?)");
        const std::regex field_re(
            R"(\b(uint|float|uint[234]|float[234])\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)");
        auto cb = std::sregex_iterator(
            clean.begin(),
            clean.end(),
            cbuffer_re);
        uint32_t offset = 0u;
        for (auto it = cb; it != end; ++it) {
            const std::string body = (*it)[1].str();
            auto fb = std::sregex_iterator(
                body.begin(),
                body.end(),
                field_re);
            for (auto fit = fb; fit != end; ++fit) {
                const std::string type = (*fit)[1].str();
                const std::string identifier = (*fit)[2].str();
                const uint32_t dwords = scalar_dwords(type);
                if (dwords == 0u) {
                    extraction.diagnostics.push_back(
                        "unsupported cbuffer field type '" + type
                        + "' for port '" + identifier + "'");
                    continue;
                }
                const std::string normalized =
                    normalize_hlsl_port_name(identifier);
                if (normalized == "pad" || normalized.starts_with("pad_")
                    || normalized == "padding"
                    || normalized.starts_with("padding_"))
                {
                    extraction.root_constant_dwords =
                        (std::max)(extraction.root_constant_dwords,
                            offset + dwords);
                    offset += dwords;
                    continue;
                }

                HlslBindingPort port{
                    .name = normalized,
                    .port_kind = scalar_port_kind(type),
                    .direction = WZ_GPU_PORT_INPUT,
                    .target = HlslBindingPortTarget::RootConstant,
                    .root_constant_offset = offset,
                    .root_constant_dwords = dwords,
                };
                extraction.ports.push_back(port);
                update_root_constant_total(extraction, port);
                offset += dwords;
            }
        }

        return extraction;
    }

    HlslBindingExtraction extract_hlsl_bindings_from_file(
        const std::string& path)
    {
        const wz::fs::FileResult<wz::fs::Buffer> file =
            wz::fs::read_file(path);
        if (file.error != wz::fs::FileError::None) {
            HlslBindingExtraction extraction{};
            extraction.diagnostics.push_back(
                "failed to read HLSL file '" + path + "'");
            return extraction;
        }
        const auto& bytes = file.value;
        const std::string source(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size());
        return extract_hlsl_bindings_from_source(source);
    }
}
