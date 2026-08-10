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

// The asymmetry end_frame's failed-Signal handling depends on: check_hr reports
// the failure but marks the device lost ONLY for the three device-lost HRESULTs.
// A plain E_OUTOFMEMORY therefore leaves status Ok -- which is why end_frame has
// to mark it explicitly. Without that, the unreachable fence value it records
// passes the dx12_device_lost gate in begin_frame and the next frame on that slot
// blocks in WaitForSingleObject(INFINITE) forever.
TEST(GpuDeviceStatus, CheckHrDoesNotMarkLostForANonDeviceLostFailure)
{
    wz::gpu::dx12::DX12Device device{};

    EXPECT_FALSE(wz::gpu::dx12::dx12_check_hr(
        device, E_OUTOFMEMORY, "ID3D12CommandQueue::Signal"));
    EXPECT_NE(device.status, wz::gpu::DeviceStatus::Lost)
        << "check_hr must not escalate a non-device-lost HRESULT; end_frame "
           "relies on marking it itself";

    EXPECT_TRUE(wz::gpu::dx12::dx12_check_hr(device, S_OK, "ok"));
    EXPECT_NE(device.status, wz::gpu::DeviceStatus::Lost);
}

// ...and marking it explicitly with that same non-device-lost HRESULT is a
// supported call: with no ID3D12Device to query a removed-reason from, the
// operation HRESULT stands in as the reason rather than being lost.
TEST(GpuDeviceStatus, MarkDeviceLostAcceptsANonDeviceLostHresult)
{
    wz::gpu::dx12::DX12Device device{};

    wz::gpu::dx12::dx12_mark_device_lost(
        device, E_OUTOFMEMORY, "ID3D12CommandQueue::Signal");

    ASSERT_EQ(device.status, wz::gpu::DeviceStatus::Lost);
    const wz::gpu::DeviceLostInfo* info =
        wz::gpu::dx12::dx12_device_lost_info(&device);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->operation_hr, static_cast<int32_t>(E_OUTOFMEMORY));
    EXPECT_EQ(info->removed_reason, static_cast<int32_t>(E_OUTOFMEMORY));
    EXPECT_EQ(info->operation, "ID3D12CommandQueue::Signal");
}
