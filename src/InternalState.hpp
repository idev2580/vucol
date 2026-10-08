#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vucol/Context.hpp>

#include <vulkan/vulkan.hpp>

namespace vucol::detail{
    struct BufferState{
        std::shared_ptr<ContextState> context;
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        void* mapped = nullptr;
        vk::DeviceSize size = 0;
        vk::DeviceSize allocationSize = 0;
        std::uint32_t memoryTypeIndex = 0;
        std::uint32_t heapIndex = 0;
        BufferType type = BufferType::Auto;
        bool hostVisible = false;
        bool coherent = false;
        std::uint32_t ownerDeviceIndex = 0;
        BufferPeerAccess peerAccess = BufferPeerAccess::None;
        std::size_t gpuUseClaims = 0;
        std::vector<BufferAccessRange> lastQueueAccesses;

        ~BufferState();
    };

    struct ContextState{
        vk::Instance instance;
        std::vector<vk::PhysicalDevice> physicalDevices;
        std::vector<vk::PhysicalDeviceProperties> physicalDeviceProperties;
        std::vector<GpuInfo> gpuInfos;
        std::vector<std::vector<std::string>> supportedDeviceExtensions;
        std::vector<CooperativeMatrixSupportInfo> cooperativeMatrixSupportInfos;
        std::vector<SubgroupSupportInfo> subgroupSupportInfos;
        std::vector<GpuTimingSupportInfo> gpuTimingSupportInfos;
        std::vector<bool> autoBufferUsesHostVisibleMemory;
        vk::Device device;
        vk::Queue queue;
        std::uint32_t queueFamily = 0;
        vk::CommandPool commandPool;
        vk::CommandBuffer recordingCommandBuffer;
        bool recording = false;
        std::shared_ptr<ShaderPipelineState> currentPipeline;
        std::shared_ptr<DescriptorSetState> currentDescriptorSet;

        [[nodiscard]] std::uint32_t deviceCount() const{
            return static_cast<std::uint32_t>(physicalDevices.size());
        }

        ~ContextState();
    };

    struct DeviceAllocation{
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        void* mapped = nullptr;
        vk::DeviceSize allocationSize = 0;
        std::uint32_t memoryTypeIndex = 0;
        std::uint32_t heapIndex = 0;
        bool hostVisible = false;
        bool coherent = false;
    };

    [[nodiscard]] DeviceAllocation allocateDeviceBuffer(
        const std::shared_ptr<ContextState>& context,
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        bool needHostVisible,
        std::uint32_t ownerDevice,
        BufferPeerAccess peerAccess = BufferPeerAccess::None);

    void freeDeviceBuffer(const std::shared_ptr<ContextState>& context,
                          DeviceAllocation& allocation) noexcept;

    void verifyPeerAccess(const std::shared_ptr<ContextState>& context,
                          std::uint32_t ownerDevice,
                          BufferPeerAccess access,
                          std::uint32_t heapIndex);

    [[nodiscard]] inline std::uint32_t deviceBit(std::uint32_t deviceIndex){
        return 1u << deviceIndex;
    }

    [[nodiscard]] inline std::uint32_t fullDeviceMask(std::uint32_t deviceCount){
        return deviceCount >= 32 ? 0xFFFFFFFFu : (1u << deviceCount) - 1u;
    }
}
