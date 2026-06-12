#include "dx12_device_internal.h"

#include <cstdio>

namespace wz::gpu::dx12
{
    namespace
    {
        const char* dx12_hr_name(HRESULT hr) noexcept
        {
            switch (hr) {
            case S_OK:
                return "S_OK";
            case DXGI_ERROR_DEVICE_REMOVED:
                return "DXGI_ERROR_DEVICE_REMOVED";
            case DXGI_ERROR_DEVICE_RESET:
                return "DXGI_ERROR_DEVICE_RESET";
            case DXGI_ERROR_DEVICE_HUNG:
                return "DXGI_ERROR_DEVICE_HUNG";
            default:
                return "HRESULT";
            }
        }

        std::string dx12_device_lost_message(
            HRESULT operation_hr,
            HRESULT removed_reason,
            const char* operation)
        {
            char buffer[256]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "GPU device lost: %s"
                " operation=%s hr=0x%08lx removed_reason=%s(0x%08lx);"
                " restart required.",
                dx12_hr_name(removed_reason),
                operation ? operation : "unknown",
                static_cast<unsigned long>(operation_hr),
                dx12_hr_name(removed_reason),
                static_cast<unsigned long>(removed_reason));
            return buffer;
        }
    }

    bool dx12_is_device_lost_hr(HRESULT hr) noexcept
    {
        return hr == DXGI_ERROR_DEVICE_REMOVED
            || hr == DXGI_ERROR_DEVICE_RESET
            || hr == DXGI_ERROR_DEVICE_HUNG;
    }

    bool dx12_device_lost(const DX12Device& device) noexcept
    {
        return device.status == wz::gpu::DeviceStatus::Lost;
    }

    bool dx12_check_hr(
        DX12Device& device,
        HRESULT hr,
        const char* operation)
    {
        if (SUCCEEDED(hr)) {
            return true;
        }

        if (dx12_is_device_lost_hr(hr)) {
            dx12_mark_device_lost(device, hr, operation);
        }
        return false;
    }

    void dx12_mark_device_lost(
        DX12Device& device,
        HRESULT hr,
        const char* operation)
    {
        if (device.status == wz::gpu::DeviceStatus::Lost) {
            return;
        }

        HRESULT removed_reason = hr;
        if (device.device) {
            const HRESULT queried = device.device->GetDeviceRemovedReason();
            if (dx12_is_device_lost_hr(queried)) {
                removed_reason = queried;
            }
        }

        device.status = wz::gpu::DeviceStatus::Lost;
        device.lost_info = wz::gpu::DeviceLostInfo{
            .operation_hr = static_cast<int32_t>(hr),
            .removed_reason = static_cast<int32_t>(removed_reason),
            .operation = operation ? operation : "unknown",
            .message =
                dx12_device_lost_message(hr, removed_reason, operation),
        };

        OutputDebugStringA(device.lost_info.message.c_str());
        OutputDebugStringA("\n");
    }

    wz::gpu::DeviceStatus dx12_device_status(
        const DX12Device* device) noexcept
    {
        if (!device) {
            return wz::gpu::DeviceStatus::Invalid;
        }
        return device->status;
    }

    const wz::gpu::DeviceLostInfo* dx12_device_lost_info(
        const DX12Device* device) noexcept
    {
        if (!device || device->status != wz::gpu::DeviceStatus::Lost) {
            return nullptr;
        }
        return &device->lost_info;
    }
}
