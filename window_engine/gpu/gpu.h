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

	void begin_frame(Device& device); // acquire backbuffer
	void clear(Device& device, float r, float g, float b, float a); // record commands
	void end_frame(Device& device); // close + submit command list
	void present(Device& device); // swapchain present
	void resize(Device& device, int w, int h); // This MUST recreate swapchain safely.

}