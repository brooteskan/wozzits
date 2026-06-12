#include <gtest/gtest.h>

#include <gpu/gpu.h>
#include <src/gpu/dx12/dx12_device_internal.h>

TEST(GpuDeviceStatus, DefaultDeviceIsInvalidAndNotOk)
{
    const wz::gpu::Device device{};

    EXPECT_EQ(wz::gpu::device_status(device), wz::gpu::DeviceStatus::Invalid);
    EXPECT_FALSE(wz::gpu::device_ok(device));
    EXPECT_EQ(wz::gpu::device_lost_info(device), nullptr);
}

TEST(GpuDeviceStatus, Dx12DeviceLostInfoIsRecordedOnce)
{
    wz::gpu::dx12::DX12Device device{};

    wz::gpu::dx12::dx12_mark_device_lost(
        device,
        DXGI_ERROR_DEVICE_HUNG,
        "unit-test operation");

    ASSERT_EQ(device.status, wz::gpu::DeviceStatus::Lost);
    ASSERT_EQ(wz::gpu::dx12::dx12_device_status(&device),
        wz::gpu::DeviceStatus::Lost);
    const wz::gpu::DeviceLostInfo* info =
        wz::gpu::dx12::dx12_device_lost_info(&device);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->operation_hr, static_cast<int32_t>(DXGI_ERROR_DEVICE_HUNG));
    EXPECT_EQ(info->removed_reason,
        static_cast<int32_t>(DXGI_ERROR_DEVICE_HUNG));
    EXPECT_EQ(info->operation, "unit-test operation");
    EXPECT_NE(info->message.find("DXGI_ERROR_DEVICE_HUNG"),
        std::string::npos);

    wz::gpu::dx12::dx12_mark_device_lost(
        device,
        DXGI_ERROR_DEVICE_REMOVED,
        "second operation");

    ASSERT_EQ(wz::gpu::dx12::dx12_device_lost_info(&device), info);
    EXPECT_EQ(info->operation_hr, static_cast<int32_t>(DXGI_ERROR_DEVICE_HUNG));
    EXPECT_EQ(info->operation, "unit-test operation");
}
