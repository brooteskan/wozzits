#pragma once

// file: gpu/dx12/dx12.h


namespace wz::gpu
{
	struct Device;
}


namespace wz::gpu::dx12
{
	wz::gpu::Device create_device(void* native_window); // create + initialize swapchain-bound device

	void destroy_device(Device& device); // impl rule: must say device.impl = nullptr;

	void resize(Device& device, int w, int h); // This MUST recreate swapchain safely.

	void begin_frame(Device& device); // acquire backbuffer
	void clear(Device& device, float r, float g, float b, float a); // record commands

	void draw_test_triangle_2(Device& d); // also temporary

	void end_frame(Device& device); // close + submit command list
	void present(Device& device); // swapchain present
}