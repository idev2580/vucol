#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <socl/Buffer.hpp>
#include <socl/DescriptorSet.hpp>
#include <socl/ShaderPipeline.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace socl{
    namespace detail{
        struct ContextState{
            vk::Instance instance;
            vk::PhysicalDevice physicalDevice;
            vk::PhysicalDeviceProperties physicalDeviceProperties{};
            vk::Device device;
            vk::Queue queue;
            std::uint32_t queueFamily = 0;
            vk::CommandPool commandPool;
            vk::CommandBuffer recordingCommandBuffer;
            VmaAllocator allocator = VK_NULL_HANDLE;
            bool recording = false;
            std::shared_ptr<ShaderPipelineState> currentPipeline;
            std::shared_ptr<DescriptorSetState> currentDescriptorSet;

            ~ContextState();
        };
    }

    // Returned by submitAsync(). It represents one submitted GPU command batch,
    // like an explicit process/job handle for work that may still be running.
    class DispatchToken{
        friend class Context;

        public:
        DispatchToken();
        ~DispatchToken();

        DispatchToken(const DispatchToken&) = delete;
        DispatchToken& operator=(const DispatchToken&) = delete;
        DispatchToken(DispatchToken&&) noexcept;
        DispatchToken& operator=(DispatchToken&&) noexcept;

        void wait();
        [[nodiscard]] bool valid() const;

        private:
        explicit DispatchToken(std::shared_ptr<detail::ContextState> context,
                               vk::Fence fence,
                               vk::CommandBuffer commandBuffer);

        std::shared_ptr<detail::ContextState> context_;
        vk::Fence fence_;
        vk::CommandBuffer commandBuffer_;
    };

    class Context{
        public:
        Context();
        ~Context();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) noexcept = default;
        Context& operator=(Context&&) noexcept = default;

        Buffer createBuffer(std::size_t bytes, BufferType type = BufferType::Auto);
        ShaderPipeline createShaderPipeline(const ShaderPipelineCreateInfo& createInfo);
        DescriptorSet createDescriptorSet(const ShaderPipeline& pipeline);

        void begin();
        void use(const ShaderPipeline& pipeline);
        void bind(const DescriptorSet& descriptorSet);
        void push(const void* data, std::size_t bytes, std::size_t offset = 0);

        template<typename T>
        void push(const T& value){
            push(&value, sizeof(T), 0);
        }

        void dispatch(std::uint32_t groupCountX,
                      std::uint32_t groupCountY = 1,
                      std::uint32_t groupCountZ = 1);

        // submitAsync returns a token so callers can wait for a specific batch.
        // submitAndWait is the OpenGL-like convenience path for immediate waits.
        DispatchToken submitAsync();
        void submitAndWait();

        [[nodiscard]] bool usingIntegratedGpu() const;

        private:
        std::shared_ptr<detail::ContextState> state_;
    };
}
