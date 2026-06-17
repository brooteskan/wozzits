#pragma once

#include <wozzits/rhi/gpu_resource.h>

#include <cstdint>
#include <vector>

namespace wz::engine::rendering::test
{
    class FakeGpuBackend final : public wz::rhi::GpuBackend
    {
    public:
        struct RecordedWrite
        {
            uint64_t id = 0;
            uint64_t offset = 0;
            std::vector<uint8_t> bytes;
        };

        wz::rhi::BackendResource create(
            const wz::rhi::GpuResourceDesc& desc) override
        {
            created_.push_back(desc);
            return wz::rhi::BackendResource{ ++next_id_ };
        }

        void destroy(wz::rhi::BackendResource resource) override
        {
            destroyed_.push_back(resource.id);
        }

        bool write(wz::rhi::BackendResource resource,
                   const void* data,
                   uint64_t size,
                   uint64_t offset) override
        {
            const auto* first = static_cast<const uint8_t*>(data);
            writes_.push_back(RecordedWrite{
                resource.id,
                offset,
                std::vector<uint8_t>{ first, first + size },
            });
            return true;
        }

        [[nodiscard]] size_t live_count() const noexcept
        {
            return created_.size() - destroyed_.size();
        }

        [[nodiscard]] const std::vector<RecordedWrite>& writes() const noexcept
        {
            return writes_;
        }

        [[nodiscard]] const std::vector<wz::rhi::GpuResourceDesc>&
        created() const noexcept
        {
            return created_;
        }

    private:
        uint64_t next_id_ = 0;
        std::vector<wz::rhi::GpuResourceDesc> created_;
        std::vector<RecordedWrite> writes_;
        std::vector<uint64_t> destroyed_;
    };
}
