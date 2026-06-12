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

}
