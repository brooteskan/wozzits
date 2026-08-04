#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

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

        // The order descriptor SRV registers are handed out in, keyed by the
        // row's SEMANTIC rather than by its position in the authored list
        // (#322, option C). Two layouts that share a semantic therefore agree
        // on its register no matter what order their rows were written in, so
        // reordering rows in the editor can no longer silently repoint a
        // binding out from under a shader that names it by register.
        //
        // This is deliberately NOT the DescriptorSemantic enum order: the enum
        // is append-only for hash stability (PulledMeshNormals was appended to
        // the end of the enum yet binds right after positions/indices), whereas
        // registers pack in the logical order below. Every layout authored today
        // is already written in this order, so sorting by it moves no existing
        // register -- it only pins the mapping against a future reorder. The
        // numbered presets in render_program_compilers.cpp hard-code the same
        // order; LayoutProgramMatchesPreset2 locks the two together.
        //
        // The three non-row semantics sort last so an accidental authored row
        // can never displace a real object binding: the two view heads are
        // emitted from layout.view_head into their own register space (never as
        // object rows), and Unknown is rejected before it is ever ranked.
        constexpr std::array<DescriptorSemantic, kDescriptorSemanticNames.size()>
            kDescriptorSemanticRegisterOrder = {
                // Mesh vertex-pull streams.
                DescriptorSemantic::PulledMeshPositions,
                DescriptorSemantic::PulledMeshIndices,
                DescriptorSemantic::PulledMeshNormals,
                DescriptorSemantic::PulledMeshUvs,
                DescriptorSemantic::PulledMeshSourceVertices,
                // Resident splat streams.
                DescriptorSemantic::SplatCloud,
                DescriptorSemantic::SortedSplatIndices,
                // Field / mask sampled textures.
                DescriptorSemantic::ScalarFieldTexture,
                DescriptorSemantic::MeshFieldVisualization,
                DescriptorSemantic::MeshMaskRules,
                // SG sky sources.
                DescriptorSemantic::SkyGaussian,
                DescriptorSemantic::SkyGaussianPoints,
                DescriptorSemantic::StarCatalog,
                // Screen-space 2D: overlay sprite, then puppet Part streams.
                DescriptorSemantic::OverlayTexture,
                DescriptorSemantic::PuppetVertices,
                DescriptorSemantic::PuppetIndices,
                DescriptorSemantic::PuppetAtlas,
                DescriptorSemantic::PuppetMask,
                // Composited surface material, sampled last.
                DescriptorSemantic::MaterialAlbedo,
                // Non-rows (see comment above): listed only for completeness.
                DescriptorSemantic::ViewConstants,
                DescriptorSemantic::ScreenConstants,
                DescriptorSemantic::Unknown,
            };

        // Every DescriptorSemantic must appear exactly once. A new semantic
        // added to the enum without a register position here fails the build
        // rather than silently defaulting to the end of every layout.
        constexpr bool descriptor_register_order_is_total()
        {
            for (size_t value = 0;
                 value < kDescriptorSemanticNames.size();
                 ++value)
            {
                int count = 0;
                for (const DescriptorSemantic semantic :
                     kDescriptorSemanticRegisterOrder)
                {
                    if (static_cast<size_t>(semantic) == value) {
                        ++count;
                    }
                }
                if (count != 1) {
                    return false;
                }
            }
            return true;
        }
        static_assert(
            descriptor_register_order_is_total(),
            "kDescriptorSemanticRegisterOrder must list every "
            "DescriptorSemantic exactly once");

        uint32_t descriptor_semantic_register_rank(
            DescriptorSemantic semantic) noexcept
        {
            for (uint32_t i = 0;
                 i < kDescriptorSemanticRegisterOrder.size();
                 ++i)
            {
                if (kDescriptorSemanticRegisterOrder[i] == semantic) {
                    return i;
                }
            }
            return static_cast<uint32_t>(
                kDescriptorSemanticRegisterOrder.size());
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

        // The view row, when the layout wants one. A StructuredBufferSRV at
        // space 0 rather than a second root-constant block: the DX12 pipeline
        // realizes only one of those per program and the object block has it,
        // so a second would make the program fail to realize (wozzits-rhi#7).
        //
        // Emitted BEFORE the object rows so it always lands on t0 of its own
        // space regardless of how many object bindings a recipe declares, and
        // so the object rows keep their existing t-registers exactly.
        if (layout.has_view_constants()) {
            // The Screen head is a distinct semantic so an overlay's viewport
            // block and a 3D program's camera/fog block coexist in one frame,
            // each bound to its own per-frame buffer.
            const DescriptorSemantic view_semantic =
                layout.view_head == RenderBindingViewHead::Screen
                    ? DescriptorSemantic::ScreenConstants
                    : DescriptorSemantic::ViewConstants;
            out.descriptor_bindings.push_back(DescriptorBinding{
                .kind = DescriptorKind::StructuredBufferSRV,
                .visibility = layout.view_visibility,
                .semantic = view_semantic,
                .shader_register = 0,
                .register_space = kRenderBindingLayoutViewRegisterSpace,
                .descriptor_count = 1,
            });
        }

        // Resolve and validate every row in AUTHORED order, so the error
        // reporting (unknown / duplicate semantic) is unchanged. Registers are
        // then assigned by canonical semantic rank below, not by row position.
        struct ResolvedBinding {
            DescriptorKind kind;
            ShaderVisibility visibility;
            DescriptorSemantic semantic;
        };
        std::vector<ResolvedBinding> resolved;
        resolved.reserve(layout.bindings.size());
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
            resolved.push_back(ResolvedBinding{
                .kind = descriptor_kind_for(row.kind),
                .visibility = row.visibility,
                .semantic = *semantic,
            });
        }

        // Assign t-registers by canonical semantic rank rather than authored
        // position (#322 option C): a stable sort into that order makes the
        // register a property of the SEMANTIC, so reordering the rows produces
        // an identical SRG. Duplicate semantics were rejected above, so no two
        // rows share a rank; the sort is a no-op for a layout already authored
        // in canonical order, which is every layout today.
        std::stable_sort(
            resolved.begin(),
            resolved.end(),
            [](const ResolvedBinding& a, const ResolvedBinding& b) {
                return descriptor_semantic_register_rank(a.semantic)
                    < descriptor_semantic_register_rank(b.semantic);
            });

        uint32_t srv_register = 0;
        for (const ResolvedBinding& binding : resolved) {
            out.descriptor_bindings.push_back(DescriptorBinding{
                .kind = binding.kind,
                .visibility = binding.visibility,
                .semantic = binding.semantic,
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
            // #290. float2, matching the tightly-packed UV buffer the mesh
            // residency publishes -- a StructuredBuffer's stride IS its element
            // type, so declaring anything wider walks off the vertices.
            case DescriptorSemantic::PulledMeshUvs:          return "float2";
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

        // View-frequency state (space 0), emitted only when the layout wants
        // it -- so a shader compiled against a layout without one sees exactly
        // the text it saw before this existed.
        //
        // A one-element StructuredBuffer, not a cbuffer: the DX12 pipeline
        // realizes one root-constant block per program and the object block
        // already has it, so a second would make the program fail to realize
        // (wozzits-rhi#7). Emitted here rather than with the object rows below
        // because the struct must precede the declaration, and because it lives
        // in its own register space where t0 is always free.
        if (layout.has_view_constants()) {
            const std::string view_space =
                "space" + std::to_string(kRenderBindingLayoutViewRegisterSpace);
            out += "\n// Frame state, identical for every program this frame.\n";
            switch (layout.view_head) {
            case RenderBindingViewHead::CameraFog:
                out += "struct WzViewConstants\n"
                       "{\n"
                       "    float4 camera;      // xyz = camera world position\n"
                       "    float4 fog_color;   // rgb = colour, w = density\n"
                       "    float4 fog_params;  // x = start, y = height "
                       "falloff, z = enabled\n"
                       "};\n";
                out += "StructuredBuffer<WzViewConstants> "
                    + std::string(descriptor_semantic_name(
                        DescriptorSemantic::ViewConstants))
                    + " : register(t0, " + view_space + ");\n";
                break;
            case RenderBindingViewHead::Screen:
                out += "struct WzScreenConstants\n"
                       "{\n"
                       "    float4 viewport;    // xy = width,height; "
                       "zw = 1/width, 1/height\n"
                       "};\n";
                out += "StructuredBuffer<WzScreenConstants> "
                    + std::string(descriptor_semantic_name(
                        DescriptorSemantic::ScreenConstants))
                    + " : register(t0, " + view_space + ");\n";
                break;
            case RenderBindingViewHead::None:
                break;
            }
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
                "    WzViewConstants v = view_constants[0];\n"
                "    if (v.fog_params.z < 0.5f) { return color; }\n"
                "    return wz_apply_fog(\n"
                "        color,\n"
                "        v.fog_color.rgb,\n"
                "        v.fog_color.w,\n"
                "        v.fog_params.x,\n"
                "        length(world_pos - v.camera.xyz));\n"
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
