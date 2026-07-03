#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/type_extensions.h>

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
        uint32_t total = 0;
        for (const RenderBindingConstantField& field : constant_fields) {
            total += render_binding_constant_type_dwords(field.type);
        }
        return total;
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
