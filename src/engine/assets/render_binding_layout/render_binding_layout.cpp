#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/type_extensions.h>

#include <cctype>
#include <string>

namespace wz::engine::assets
{
    namespace
    {
        DescriptorKind descriptor_kind_for(RenderBindingKind kind) noexcept
        {
            switch (kind) {
            case RenderBindingKind::TextureSrv:
                return DescriptorKind::TextureSRV;
            case RenderBindingKind::StructuredSrv:
                return DescriptorKind::StructuredBufferSRV;
            }
            return DescriptorKind::TextureSRV;
        }
    }

    uint32_t RenderBindingLayoutData::tail_dwords() const noexcept
    {
        // HLSL cbuffer packing, not a tight sum: the block is read as a
        // cbuffer, so a field pushed to the next 16-byte register by the
        // no-straddle rule grows the tail past the sum of its field sizes.
        // Relative-to-head is exact because heads end register-aligned
        // (static_assert in render_binding_constants.h).
        uint32_t running = 0;
        for (const RenderBindingConstantField& field : constant_fields) {
            running = render_binding_constant_field_offset(running, field.type)
                + render_binding_constant_type_dwords(field.type);
        }
        return running;
    }

    uint32_t RenderBindingLayoutData::total_constants_dwords() const noexcept
    {
        if (constants_dwords > 0u) {
            return constants_dwords;
        }
        return render_binding_constants_head_dwords(constants_head)
            + tail_dwords();
    }

    // Validation lives in the SRG derivation (one rule set, detailed reasons);
    // valid() is the boolean view of the same rules.
    bool RenderBindingLayoutData::valid() const noexcept
    {
        RenderBindingLayoutSrg srg{};
        std::string error;
        return build_render_binding_layout_srg(*this, srg, error);
    }

    bool build_render_binding_layout_srg(
        const RenderBindingLayoutData& layout,
        RenderBindingLayoutSrg& out,
        std::string& error)
    {
        out = {};

        if (!layout.has_constants()) {
            // No block: head/tail/size declarations without a semantic would
            // silently vanish from the derived SRG — reject the contradiction.
            if (layout.constants_head != RenderBindingConstantsHead::None
                || !layout.constant_fields.empty()
                || layout.constants_dwords != 0u)
            {
                error = "constants declared without a constants_semantic";
                return false;
            }
        }
        else if (layout.constants_dwords > 0u
            && layout.constants_dwords
                < render_binding_constants_head_dwords(layout.constants_head)
                    + layout.tail_dwords())
        {
            error = "constants_dwords is smaller than head + declared fields";
            return false;
        }

        if (layout.constant_fields.size()
            > kMaxRenderBindingLayoutConstantFields)
        {
            error = "too many constant fields";
            return false;
        }
        for (size_t i = 0; i < layout.constant_fields.size(); ++i) {
            if (layout.constant_fields[i].name.empty()) {
                error = "constant field with empty name";
                return false;
            }
            // Field names address per-instance values downstream (#228/#229);
            // duplicates would be unaddressable.
            for (size_t j = i + 1; j < layout.constant_fields.size(); ++j) {
                if (layout.constant_fields[i].name
                    == layout.constant_fields[j].name)
                {
                    error = "duplicate constant field name: "
                        + layout.constant_fields[i].name;
                    return false;
                }
            }
        }

        if (!layout.has_view_constants()) {
            // Same contradiction the object block rejects: a head with no
            // semantic would silently vanish from the derived SRG.
            if (layout.view_head != RenderBindingViewHead::None) {
                error = "view head declared without a view_constants_semantic";
                return false;
            }
        }
        else if (layout.view_head == RenderBindingViewHead::None) {
            // The reverse is just as bad -- a named block of zero dwords would
            // emit an empty cbuffer and a root parameter nothing ever fills.
            error = "view_constants_semantic declared with no view head";
            return false;
        }
        else if (layout.view_constants_semantic == layout.constants_semantic) {
            // Two cbuffers of the same name in one shader will not compile, and
            // the collision is silent until the program is built.
            error = "view and object constants share a semantic name: "
                + layout.view_constants_semantic;
            return false;
        }

        if (layout.bindings.size() > kMaxRenderBindingLayoutBindings) {
            error = "too many binding rows";
            return false;
        }
        if (layout.samplers.size() > kMaxRenderBindingLayoutSamplers) {
            error = "too many sampler rows";
            return false;
        }

        if (layout.has_constants()) {
            out.root_constants.push_back(RootConstantBinding{
                .visibility = layout.constants_visibility,
                .shader_register = 0,
                .register_space = kRenderBindingLayoutRegisterSpace,
                .value_count = layout.total_constants_dwords(),
                .semantic = layout.constants_semantic,
            });
        }

        // Appended AFTER the object block, so an existing layout's derived rows
        // keep their exact positions and a layout with no view block produces
        // byte-identical output to before this existed. The rhi bridge groups
        // by register_space rather than by order, so nothing downstream cares
        // which came first.
        if (layout.has_view_constants()) {
            out.root_constants.push_back(RootConstantBinding{
                .visibility = layout.view_constants_visibility,
                .shader_register = 0,
                .register_space = kRenderBindingLayoutViewRegisterSpace,
                .value_count = layout.view_constants_dwords(),
                .semantic = layout.view_constants_semantic,
            });
        }

        uint32_t srv_register = 0;
        for (size_t i = 0; i < layout.bindings.size(); ++i) {
            const RenderBindingRow& row = layout.bindings[i];
            const std::optional<DescriptorSemantic> semantic =
                descriptor_semantic_from_name(row.semantic);
            if (!semantic) {
                error = "unknown descriptor semantic: " + row.semantic;
                return false;
            }
            // The submit path locates ONE resource per semantic; a repeated
            // semantic is ambiguous.
            for (size_t j = i + 1; j < layout.bindings.size(); ++j) {
                if (row.semantic == layout.bindings[j].semantic) {
                    error = "duplicate descriptor semantic: " + row.semantic;
                    return false;
                }
            }
            out.descriptor_bindings.push_back(DescriptorBinding{
                .kind = descriptor_kind_for(row.kind),
                .visibility = row.visibility,
                .semantic = *semantic,
                .shader_register = srv_register++,
                .register_space = kRenderBindingLayoutRegisterSpace,
                .descriptor_count = 1,
            });
        }

        uint32_t sampler_register = 0;
        for (const RenderBindingSamplerRow& row : layout.samplers) {
            out.static_samplers.push_back(StaticSamplerBinding{
                .kind = row.kind,
                .visibility = row.visibility,
                .shader_register = sampler_register++,
                .register_space = kRenderBindingLayoutRegisterSpace,
            });
        }

        return true;
    }

    namespace
    {
        // Canonical HLSL element type for a StructuredSrv semantic, where one
        // exists: the type every engine shader already declares for that
        // buffer (pull/mesh_style/splat shaders). nullptr = no canonical type
        // (the buffer's element layout is defined by its publisher recipe) —
        // the prelude emits a register macro and the author declares the
        // element themselves.
        const char* structured_element_type_for(
            DescriptorSemantic semantic) noexcept
        {
            switch (semantic) {
            case DescriptorSemantic::PulledMeshPositions:    return "float3";
            case DescriptorSemantic::PulledMeshIndices:      return "uint";
            case DescriptorSemantic::SortedSplatIndices:     return "uint";
            case DescriptorSemantic::MeshFieldVisualization: return "float";
            case DescriptorSemantic::SplatCloud:             return "WzSplat";
            default:                                         return nullptr;
            }
        }

        std::string packoffset_of(uint32_t offset_dwords)
        {
            constexpr char kLane[4] = { 'x', 'y', 'z', 'w' };
            std::string text =
                "packoffset(c" + std::to_string(offset_dwords / 4u);
            // The .x lane is implicit at a register base; a scalar packed into
            // a later lane of a register needs the explicit component.
            if (offset_dwords % 4u != 0u) {
                text += '.';
                text += kLane[offset_dwords % 4u];
            }
            text += ')';
            return text;
        }

        const char* hlsl_type_of(RenderBindingConstantType type) noexcept
        {
            switch (type) {
            case RenderBindingConstantType::Float:  return "float";
            case RenderBindingConstantType::Float2: return "float2";
            case RenderBindingConstantType::Float3: return "float3";
            case RenderBindingConstantType::Float4: return "float4";
            case RenderBindingConstantType::Color:  return "float4";
            }
            return "float";
        }

        const char* visibility_note_of(ShaderVisibility visibility) noexcept
        {
            switch (visibility) {
            case ShaderVisibility::All:    return "all stages";
            case ShaderVisibility::Vertex: return "vertex stage";
            case ShaderVisibility::Pixel:  return "pixel stage";
            }
            return "all stages";
        }
    }

    bool generate_hlsl_binding_prelude(
        const RenderBindingLayoutData& layout,
        std::string& out,
        std::string& error)
    {
        out.clear();

        // The SRG derivation is the authority for validation and register
        // assignment; the prelude re-emits ITS rows so the two views cannot
        // disagree.
        RenderBindingLayoutSrg srg{};
        if (!build_render_binding_layout_srg(layout, srg, error)) {
            return false;
        }

        out +=
            "// Generated binding prelude (issue #231) — derived from the "
            "authored render\n"
            "// binding layout, prepended to the shader body by the binding-"
            "prelude asset\n"
            "// node. The layout is the single source of truth for these "
            "registers and\n"
            "// cbuffer offsets: do not hand-declare them in the body.\n";

        const std::string space =
            "space" + std::to_string(kRenderBindingLayoutRegisterSpace);

        // Resident decoded splat element (64 bytes) — must match the CPU-side
        // decode; copied from the splat shaders' canonical declaration.
        for (const DescriptorBinding& row : srg.descriptor_bindings) {
            if (row.semantic != DescriptorSemantic::SplatCloud) {
                continue;
            }
            out +=
                "\n"
                "struct WzSplat\n"
                "{\n"
                "    float3 position;   // offset  0\n"
                "    float  opacity;    // offset 12\n"
                "    float3 scale;      // offset 16\n"
                "    float  pad0;       // offset 28\n"
                "    float4 rotation;   // offset 32\n"
                "    float3 color;      // offset 48\n"
                "    uint   pad1;       // offset 60\n"
                "};\n";
            break;
        }

        if (layout.has_constants()) {
            // packoffset on EVERY member: the offsets are the ones the recipe
            // bakes (render_binding_constant_field_offset), so the shader is
            // FORCED onto the CPU byte contract instead of relying on the
            // packing rules agreeing.
            out += "\ncbuffer " + layout.constants_semantic + " : register(b0, "
                + space + ")\n{\n";
            switch (layout.constants_head) {
            case RenderBindingConstantsHead::Mvp16:
                out += "    float4x4 mvp : packoffset(c0);\n";
                break;
            case RenderBindingConstantsHead::WorldViewProjCamera36:
                // The splat-cloud packer's contract; a custom renderable
                // packs the trailing diameter float as 0.
                out += "    float4x4 world : packoffset(c0);\n"
                       "    float4x4 view_proj : packoffset(c4);\n"
                       "    float4 camera_and_diameter : packoffset(c8);\n";
                break;
            case RenderBindingConstantsHead::CameraSnappedTerrain:
                // The camera-follow terrain block (issue #233), matching
                // ClipmapDrawConstants / the clipmap VS cbuffer BYTE-FOR-BYTE:
                // view_projection + the per-level snap / world→uv / vertical /
                // texel-extent params the renderer fills each frame.
                out += "    float4x4 view_projection : packoffset(c0);\n"
                       "    float4 snap_params : packoffset(c4);"
                       "        // xy = camera world XZ, z = c0, w = snapped?\n"
                       "    float4 world_to_uv : packoffset(c5);"
                       "          // xy = uv scale, zw = uv offset\n"
                       "    float4 texel_and_vertical : packoffset(c6);"
                       "   // xy = texel world size, z = vscale, w = base\n"
                       "    float4 texel_dims_extent : packoffset(c7);"
                       "    // xy = texel dims, z = base_resolution, w = mips\n";
                break;
            case RenderBindingConstantsHead::None:
                break;
            }

            uint32_t running =
                render_binding_constants_head_dwords(layout.constants_head);
            for (const RenderBindingConstantField& field :
                 layout.constant_fields)
            {
                running =
                    render_binding_constant_field_offset(running, field.type);
                out += "    ";
                out += hlsl_type_of(field.type);
                out += ' ';
                out += field.name;
                out += " : " + packoffset_of(running) + ";\n";
                running += render_binding_constant_type_dwords(field.type);
            }
            out += "};\n";
        }

        // View-frequency block (space 0). Emitted only when the layout declares
        // one, so a shader compiled against a layout without it sees exactly the
        // text it saw before this existed.
        if (layout.has_view_constants()) {
            const std::string view_space =
                "space" + std::to_string(kRenderBindingLayoutViewRegisterSpace);
            out += "\n// Frame state, identical for every program this frame:\n"
                   "// the observer, and the atmosphere it looks through.\n";
            out += "cbuffer " + layout.view_constants_semantic
                + " : register(b0, " + view_space + ")\n{\n";
            switch (layout.view_head) {
            case RenderBindingViewHead::CameraFog:
                out += "    float4 wz_view_camera : packoffset(c0);"
                       "       // xyz = camera world position\n"
                       "    float4 wz_view_fog_color : packoffset(c1);"
                       "    // rgb = fog colour, w = density\n"
                       "    float4 wz_view_fog_params : packoffset(c2);"
                       "   // x = start, y = height falloff, z = enabled\n";
                break;
            case RenderBindingViewHead::None:
                break;
            }
            out += "};\n";
        }

        if (!srg.descriptor_bindings.empty()) {
            out += '\n';
        }
        for (size_t i = 0; i < srg.descriptor_bindings.size(); ++i) {
            const DescriptorBinding& row = srg.descriptor_bindings[i];
            const std::string_view name =
                descriptor_semantic_name(row.semantic);
            const std::string reg = "register(t"
                + std::to_string(row.shader_register) + ", " + space + ")";
            if (row.kind == DescriptorKind::TextureSRV) {
                out += "Texture2D<float4> ";
                out += name;
                out += " : " + reg + ";  // "
                    + visibility_note_of(row.visibility) + "\n";
                continue;
            }
            if (const char* element =
                    structured_element_type_for(row.semantic))
            {
                out += "StructuredBuffer<";
                out += element;
                out += "> ";
                out += name;
                out += " : " + reg + ";  // "
                    + visibility_note_of(row.visibility) + "\n";
                continue;
            }
            // No canonical element type — the register still comes from the
            // layout; the author supplies the element declaration:
            //   StructuredBuffer<YourElement> <name> : WZ_BINDING_<NAME>;
            std::string macro = "WZ_BINDING_";
            for (const char c : name) {
                macro += static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
            }
            out += "// " + std::string(name)
                + ": StructuredBuffer with no canonical element type — "
                  "declare your own\n"
                  "// element struct and bind it with the macro below ("
                + visibility_note_of(row.visibility) + ").\n"
                  "#define " + macro + " " + reg + "\n";
        }

        if (!srg.static_samplers.empty()) {
            out += '\n';
        }
        for (size_t i = 0; i < srg.static_samplers.size(); ++i) {
            const StaticSamplerBinding& row = srg.static_samplers[i];
            out += "SamplerState sampler" + std::to_string(i) + " : register(s"
                + std::to_string(row.shader_register) + ", " + space + ");  // "
                + visibility_note_of(row.visibility) + "\n";
        }

        // Standard helper library — pure HLSL functions with no bindings, so
        // they compile regardless of what the layout declares. Emitted into
        // EVERY prelude so custom programs consume common projections via a
        // one-liner. Placed after the binding declarations so it always
        // compiles; guarded so a shader that (unexpectedly) sees the prelude
        // twice does not redefine the helpers.
        out +=
            "\n"
            "// --- wozzits standard helpers ---\n"
            "#ifndef WZ_STANDARD_HELPERS\n"
            "#define WZ_STANDARD_HELPERS\n"
            "float3 wz_origin_relative_direction(float3 world_pos, float3 origin)\n"
            "{\n"
            "    return normalize(world_pos - origin);\n"
            "}\n"
            "\n"
            "// Exponential distance fog past a start radius. Takes its terms as\n"
            "// ARGUMENTS rather than reading the view block, so it stays in the\n"
            "// no-bindings contract of this section and compiles against a\n"
            "// layout that declares nothing.\n"
            "float3 wz_apply_fog(float3 color, float3 fog_color,\n"
            "                    float density, float start, float distance)\n"
            "{\n"
            "    float d = max(distance - start, 0.0f);\n"
            "    return lerp(color, fog_color, saturate(1.0f - exp(-d * density)));\n"
            "}\n";

        // The binding-aware one-liner, emitted only when the layout declares a
        // view block -- it names cbuffer members, so it cannot be unconditional.
        // It sits HERE rather than beside the cbuffer above because HLSL needs
        // wz_apply_fog declared before it is called, and that lives in this
        // section. The cbuffer is declared earlier in the prelude, so reading it
        // from here is fine.
        if (layout.view_head == RenderBindingViewHead::CameraFog) {
            out +=
                "\n"
                "float3 wz_fog_from_view(float3 color, float3 world_pos)\n"
                "{\n"
                "    if (wz_view_fog_params.z < 0.5f) { return color; }\n"
                "    return wz_apply_fog(\n"
                "        color,\n"
                "        wz_view_fog_color.rgb,\n"
                "        wz_view_fog_color.w,\n"
                "        wz_view_fog_params.x,\n"
                "        length(world_pos - wz_view_camera.xyz));\n"
                "}\n";
        }

        out += "#endif // WZ_STANDARD_HELPERS\n";

        return true;
    }

    RenderBindingLayoutTable::RenderBindingLayoutTable()
    {
        layouts_.emplace_back();
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle RenderBindingLayoutTable::add(
        RenderBindingLayoutData data)
    {
        if (!data.valid()) {
            return {};
        }

        const uint32_t id = static_cast<uint32_t>(layouts_.size());
        layouts_.push_back(std::move(data));
        epochs_.push_back(1);
        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = epochs_[id],
            .type = kAssetTypeRenderBindingLayout,
        };
    }

    const RenderBindingLayoutData* RenderBindingLayoutTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (handle.type != kAssetTypeRenderBindingLayout) {
            return nullptr;
        }
        if (handle.id >= layouts_.size()) {
            return nullptr;
        }
        if (epochs_[handle.id] != handle.epoch) {
            return nullptr;
        }
        return &layouts_[handle.id];
    }

    void RenderBindingLayoutTable::destroy()
    {
        layouts_.clear();
        epochs_.clear();

        layouts_.emplace_back();
        epochs_.push_back(0);
    }
}
