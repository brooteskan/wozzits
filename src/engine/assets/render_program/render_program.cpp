#include <engine/assets/render_program/render_program.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    RenderProgramTable::RenderProgramTable()
    {
        programs_.push_back({});
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle RenderProgramTable::add(RenderProgramData data)
    {
        if (!data.valid())
            return {};

        const uint32_t id = static_cast<uint32_t>(programs_.size());

        programs_.push_back(std::move(data));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = 1,
            .type = kAssetTypeRenderProgram,
        };
    }

    const RenderProgramData* RenderProgramTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeRenderProgram)
            return nullptr;

        if (handle.id >= programs_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &programs_[handle.id];
    }

    void RenderProgramTable::destroy()
    {
        programs_.clear();
        epochs_.clear();

        programs_.push_back({});
        epochs_.push_back(0);
    }
}