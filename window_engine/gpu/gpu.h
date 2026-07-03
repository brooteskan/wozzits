#pragma once

// file: gpu/gpu.h

#include <gpu/gpu_types.h>
namespace wz::window
{
	struct WindowHandle;
}

namespace wz::gpu
{
	struct Device
	{
		void* impl{}; // dx12_device or vk_device 

		Device() = default;

		Device(const Device&) = delete;
		Device& operator=(const Device&) = delete;

		Device(Device&&) noexcept = default;
		Device& operator=(Device&&) noexcept = default;

		bool valid() const
		{
			return impl != nullptr;
		}

	};

	// every function should assume assert(device.impl != nullptr);

	Device create_device(const wz::window::WindowHandle& window);  // create + initialize swapchain-bound device
	void destroy_device(Device& device); // impl rule: must say device.impl = nullptr;

	DeviceStatus device_status(const Device& device);
	const DeviceLostInfo* device_lost_info(const Device& device);
	bool device_ok(const Device& device);

	bool begin_frame(Device& device); // acquire backbuffer
	void clear(Device& device, float r, float g, float b, float a); // record commands
	bool end_frame(Device& device); // close + submit command list
	bool present(Device& device); // swapchain present (vsync on, sync_interval=1)
	bool present(Device& device, uint32_t sync_interval); // explicit sync interval (0 = no vsync)
	bool resize(Device& device, int w, int h); // This MUST recreate swapchain safely.
	void wait_idle(Device& device);

	// GPU fence timeline accessors, for precise deferred release (the rhi
	// GpuResourceRegistry touch/collect mechanism).

	// The fence value the CURRENT frame's end_frame will signal — i.e. the
	// value to tag (touch) any resource this frame references with, so it is
	// not reclaimed until the GPU has passed this frame. Returns 0 for a null
	// impl or a lost device.
	uint64_t frame_timeline_value(Device& device);

	// The GPU's completed fence value (GetCompletedValue) — pass to
	// GpuResourceRegistry::collect to reclaim everything the GPU is done with.
	// Returns 0 for a null impl/fence AND for a LOST device: on a removed
	// device GetCompletedValue returns UINT64_MAX, which would make collect
	// reclaim every pending resource prematurely; device-loss cleanup is owned
	// separately by GpuResourceRegistry::on_device_lost, so this reports 0 (=
	// "nothing completed") instead.
	uint64_t completed_timeline_value(Device& device);

}
