#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <socl/Context.hpp>

#include <vk_mem_alloc.h>

namespace socl::detail{
    struct BufferState{
        std::shared_ptr<ContextState> context;
        VmaAllocator allocator = VK_NULL_HANDLE;
        vk::Buffer buffer;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo allocationInfo{};
        vk::DeviceSize size = 0;
        BufferType type = BufferType::Auto;
        bool hostVisible = false;
        std::size_t gpuUseClaims = 0;
        std::vector<BufferAccessRange> lastQueueAccesses;

        ~BufferState();
    };

    struct ContextState{
        vk::Instance instance;
        vk::PhysicalDevice physicalDevice;
        vk::PhysicalDeviceProperties physicalDeviceProperties{};
        GpuInfo gpuInfo;
        std::vector<std::string> supportedDeviceExtensions;
        CooperativeMatrixSupportInfo cooperativeMatrixSupportInfo;
        SubgroupSupportInfo subgroupSupportInfo;
        GpuTimingSupportInfo gpuTimingSupportInfo;
        vk::Device device;
        vk::Queue queue;
        std::uint32_t queueFamily = 0;
        vk::CommandPool commandPool;
        vk::CommandBuffer recordingCommandBuffer;
        VmaAllocator allocator = VK_NULL_HANDLE;
        bool autoBufferUsesHostVisibleMemory = false;
        bool recording = false;
        std::shared_ptr<ShaderPipelineState> currentPipeline;
        std::shared_ptr<DescriptorSetState> currentDescriptorSet;

        ~ContextState();
    };
}
