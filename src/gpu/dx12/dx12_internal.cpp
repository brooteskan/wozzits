// src/gpu/dx12/dx12_internal.cpp

#include <gpu/dx12/dx12_internal.h>

namespace wz::gpu::dx12::internal {

	DX12ScalarFieldTextureTable::DX12ScalarFieldTextureTable()
	{
		slots_.emplace_back(); // id 0 invalid
	}

	void DX12ScalarFieldTextureTable::destroy()
	{
		for (Slot& slot : slots_) {
			if (slot.occupied && slot.tex.texture) {
				slot.tex.texture->Release();
				slot.tex.texture = nullptr;
			}
		}

		slots_.clear();
		slots_.emplace_back();
	}

    GPUHandle DX12ScalarFieldTextureTable::add(DX12ScalarFieldTexture tex)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.tex = tex;

        slots_.push_back(slot);

        return GPUHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1), // because slot 0 is sentinel
            .epoch = slot.epoch,
            .type = GPUResourceType::Texture,
        };
    }

    const DX12ScalarFieldTexture* DX12ScalarFieldTextureTable::get(
        GPUHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != GPUResourceType::Texture)
            return nullptr;

        if (handle.id == 0)
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied)
            return nullptr;

        if (slot.epoch != handle.epoch)
            return nullptr;

        return &slot.tex;
    }
}