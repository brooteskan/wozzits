#pragma once

// wozzits/rhi/handle.h
//
// Generational handle + slot map. Every device resource in this renderer is
// addressed by a { index, generation } handle, never a raw pointer. When a
// slot is reused, its generation bumps, so a handle left over from a previous
// owner (a stale renderable, a post-device-loss reference) is *detectable* by
// generation mismatch instead of dereferencing freed memory.
//
// This is the primitive the forthcoming GpuResourceRegistry is built on, and
// the reason stale-handle bugs become checkable rather than silent.

#include <cstdint>
#include <vector>

namespace wz::rhi
{
    template <typename Tag>
    struct GenerationalHandle
    {
        static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

        uint32_t index = kInvalidIndex;
        uint32_t generation = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return index != kInvalidIndex;
        }

        friend bool operator==(GenerationalHandle, GenerationalHandle) = default;
    };

    // A slot map that hands out generational handles. get() returns nullptr for
    // a stale handle (slot reused since the handle was issued), which is the
    // whole point: use-after-free becomes a nullptr you can check.
    template <typename T>
    class SlotMap
    {
    public:
        using Handle = GenerationalHandle<T>;

        Handle insert(T value)
        {
            uint32_t index;
            if (!free_list_.empty()) {
                index = free_list_.back();
                free_list_.pop_back();
                slots_[index].value = std::move(value);
                slots_[index].occupied = true;
            }
            else {
                index = static_cast<uint32_t>(slots_.size());
                slots_.push_back(Slot{ std::move(value), /*generation*/ 0, true });
            }
            return Handle{ index, slots_[index].generation };
        }

        [[nodiscard]] T* get(Handle handle)
        {
            Slot* slot = resolve(handle);
            return slot ? &slot->value : nullptr;
        }

        [[nodiscard]] const T* get(Handle handle) const
        {
            const Slot* slot = resolve(handle);
            return slot ? &slot->value : nullptr;
        }

        // Erase the slot. Bumps the generation so any outstanding handle to it
        // becomes stale (and get() will return nullptr for it).
        bool erase(Handle handle)
        {
            Slot* slot = resolve(handle);
            if (!slot) {
                return false;
            }
            slot->occupied = false;
            ++slot->generation;
            slot->value = T{};
            free_list_.push_back(handle.index);
            return true;
        }

        [[nodiscard]] size_t size() const noexcept
        {
            return slots_.size() - free_list_.size();
        }

    private:
        struct Slot
        {
            T value{};
            uint32_t generation = 0;
            bool occupied = false;
        };

        [[nodiscard]] Slot* resolve(Handle handle)
        {
            if (!handle.valid() || handle.index >= slots_.size()) {
                return nullptr;
            }
            Slot& slot = slots_[handle.index];
            if (!slot.occupied || slot.generation != handle.generation) {
                return nullptr;  // stale handle
            }
            return &slot;
        }

        [[nodiscard]] const Slot* resolve(Handle handle) const
        {
            if (!handle.valid() || handle.index >= slots_.size()) {
                return nullptr;
            }
            const Slot& slot = slots_[handle.index];
            if (!slot.occupied || slot.generation != handle.generation) {
                return nullptr;
            }
            return &slot;
        }

        std::vector<Slot> slots_;
        std::vector<uint32_t> free_list_;
    };
}
