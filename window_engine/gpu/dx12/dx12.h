#pragma once

// file: gpu/dx12/dx12.h
#include <gpu/gpu.h>



namespace wz::gpu::dx12
{
	wz::gpu::Device create_device(void* native_window); // create + initialize swapchain-bound device

	void destroy_device(wz::gpu::Device& device); // impl rule: must say device.impl = nullptr;

	void resize(wz::gpu::Device& device, int w, int h); // This MUST recreate swapchain safely.

	void begin_frame(wz::gpu::Device& device); // acquire backbuffer
	void clear(wz::gpu::Device& device, float r, float g, float b, float a); // record commands

	void draw_test_triangle_2(wz::gpu::Device& d); // also temporary

	struct TriangleTestContextDesc
	{
		wz::gpu::GPUHandle vertex_shader{};
		wz::gpu::GPUHandle pixel_shader{};

		bool valid() const noexcept
		{
			return vertex_shader.valid() && pixel_shader.valid();
		}
	};

	void create_triangle_test_context(
		wz::gpu::Device& device,
		const TriangleTestContextDesc& desc
	);

	void end_frame(wz::gpu::Device& device); // close + submit command list
	void present(wz::gpu::Device& device); // swapchain present


}