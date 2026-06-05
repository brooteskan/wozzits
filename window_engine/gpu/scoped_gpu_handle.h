#pragma once
// gpu/scoped_gpu_handle.h
//
// RAII wrapper for GPUHandle. Releases the resource on destruction via a
// DeferredReleaseQueue, preventing both leaks and use-after-free.
//
// Move-only. Copying is deleted — each GPU resource has exactly one owner.
//
// Usage:
//     auto mesh = ScopedGPUHandle(queue, upload_mesh(device, data));
//     // ... use mesh.get() for draw calls ...
//     // resource is automatically deferred-released when mesh goes out of scope

#include <gpu/gpu_types.h>
#include <gpu/gpu_resource_lifecycle.h>

namespace wz::gpu
{
    class ScopedGPUHandle
    {
    public:
        ScopedGPUHandle() = default;

        ScopedGPUHandle(DeferredReleaseQueue& queue, GPUHandle handle)
            : queue_(&queue)
            , handle_(handle)
        {
        }

        ~ScopedGPUHandle()
        {
            release();
        }

        ScopedGPUHandle(ScopedGPUHandle&& other) noexcept
            : queue_(other.queue_)
            , handle_(other.handle_)
        {
            other.queue_ = nullptr;
            other.handle_ = {};
        }

        ScopedGPUHandle& operator=(ScopedGPUHandle&& other) noexcept
        {
            if (this != &other) {
                release();
                queue_ = other.queue_;
                handle_ = other.handle_;
                other.queue_ = nullptr;
                other.handle_ = {};
            }
            return *this;
        }

        ScopedGPUHandle(const ScopedGPUHandle&) = delete;
        ScopedGPUHandle& operator=(const ScopedGPUHandle&) = delete;

        [[nodiscard]] GPUHandle get() const noexcept { return handle_; }
        [[nodiscard]] bool valid() const noexcept { return handle_.valid(); }
        explicit operator bool() const noexcept { return valid(); }

        // Detach the handle from RAII management. The caller assumes
        // responsibility for releasing the resource.
        [[nodiscard]] GPUHandle detach() noexcept
        {
            GPUHandle h = handle_;
            queue_ = nullptr;
            handle_ = {};
            return h;
        }

        void release()
        {
            if (queue_ && handle_.valid()) {
                queue_->defer_release(handle_);
            }
            queue_ = nullptr;
            handle_ = {};
        }

    private:
        DeferredReleaseQueue* queue_ = nullptr;
        GPUHandle handle_{};
    };
}
