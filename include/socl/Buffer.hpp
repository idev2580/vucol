#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace socl{
    // BufferType controls where/how buffer memory is allocated.
    // It does not describe how a shader sees the buffer; descriptor bindings do that.
    enum class BufferType{
        Auto,       //dGPU -> DeviceLocal, iGPU -> HostVisible
        DeviceLocal,
        HostVisible,
    };

    namespace detail{
        struct ContextState;

        struct BufferState{
            std::shared_ptr<ContextState> context;
            VmaAllocator allocator = VK_NULL_HANDLE;
            vk::Buffer buffer;
            VmaAllocation allocation = VK_NULL_HANDLE;
            VmaAllocationInfo allocationInfo{};
            vk::DeviceSize size = 0;
            BufferType type = BufferType::Auto;
            bool hostVisible = false;

            ~BufferState();
        };
    }

    class Buffer{
        friend class Context;
        friend class DescriptorSet;

        public:
        Buffer();
        ~Buffer();

        Buffer(const Buffer&) = default;
        Buffer& operator=(const Buffer&) = default;
        Buffer(Buffer&&) noexcept = default;
        Buffer& operator=(Buffer&&) noexcept = default;

        [[nodiscard]] std::size_t size() const;
        [[nodiscard]] BufferType type() const;
        [[nodiscard]] bool hostVisible() const;
        [[nodiscard]] explicit operator bool() const;

        // These helpers work only for host-visible allocations. Device-local
        // buffers need explicit staging/copy support, which SOCL can add later.
        void write(const void* data, std::size_t bytes, std::size_t offset = 0);
        void read(void* data, std::size_t bytes, std::size_t offset = 0) const;

        private:
        explicit Buffer(std::shared_ptr<detail::BufferState> state);
        std::shared_ptr<detail::BufferState> state_;
    };
}
