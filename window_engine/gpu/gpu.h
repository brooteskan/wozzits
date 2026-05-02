#pragma once

// file: gpu/gpu.h

#include <render/frame/render_frame.h>

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
	};

	// every function should assume assert(device.impl != nullptr);

	Device create_device(const wz::window::WindowHandle& window);  // create + initialize swapchain-bound device

	// void* get_backend_context(Device& d);
	void render(Device& device, const wz::render::RenderFrame& frame);

	void destroy_device(Device& device); // impl rule: must say device.impl = nullptr;

	void resize(Device& device, int w, int h); // This MUST recreate swapchain safely.

	void begin_frame(Device& device); // acquire backbuffer
	void clear(Device& device, float r, float g, float b, float a); // record commands

	void end_frame(Device& device); // close + submit command list
	void present(Device& device); // swapchain present
}