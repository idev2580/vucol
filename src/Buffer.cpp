#include <vucol/Buffer.hpp>
#include <vucol/Context.hpp>

#include "InternalState.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace{
    void checkVk(VkResult result, const char* message){
        if(result != VK_SUCCESS){
            throw std::runtime_error(message);
        }
    }

    std::uint32_t findMemoryType(vk::PhysicalDevice physicalDevice,
                                 std::uint32_t typeFilter,
                                 vk::MemoryPropertyFlags required,
                                 vk::MemoryPropertyFlags preferred){
        const auto memoryProperties = physicalDevice.getMemoryProperties();
        std::uint32_t fallback = memoryProperties.memoryTypeCount;
        for(std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i){
            if(!(typeFilter & (1u << i))){
                continue;
            }
            const auto flags = memoryProperties.memoryTypes[i].propertyFlags;
            if((flags & required) != required){
                continue;
            }
            if((flags & preferred) == preferred){
                return i;
            }
            if(fallback == memoryProperties.memoryTypeCount){
                fallback = i;
            }
        }
        if(fallback == memoryProperties.memoryTypeCount){
            throw std::runtime_error("No suitable Vulkan memory type for the buffer.");
        }
        return fallback;
    }

    std::uint32_t findScoredMemoryType(vk::PhysicalDevice physicalDevice,
                                       std::uint32_t typeFilter,
                                       vk::MemoryPropertyFlags required){
        const auto memoryProperties = physicalDevice.getMemoryProperties();
        std::uint32_t best = memoryProperties.memoryTypeCount;
        int bestScore = -1;
        for(std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i){
            if(!(typeFilter & (1u << i))){
                continue;
            }
            const auto flags = memoryProperties.memoryTypes[i].propertyFlags;
            if((flags & required) != required){
                continue;
            }
            int score = 0;
            if(flags & vk::MemoryPropertyFlagBits::eDeviceLocal){
                score += 2;
            }
            if(flags & vk::MemoryPropertyFlagBits::eHostCoherent){
                score += 1;
            }
            if(score > bestScore){
                bestScore = score;
                best = i;
            }
        }
        if(best == memoryProperties.memoryTypeCount){
            throw std::runtime_error("No suitable Vulkan memory type for the buffer.");
        }
        return best;
    }

    vk::DeviceSize nonCoherentAtomSize(const vucol::detail::ContextState& context,
                                       std::uint32_t ownerDevice){
        const vk::DeviceSize atomSize =
            context.physicalDeviceProperties[ownerDevice].limits.nonCoherentAtomSize;
        return atomSize == 0 ? 1 : atomSize;
    }

    void flushMappedRange(const vucol::detail::ContextState& context,
                          vk::DeviceMemory memory,
                          vk::DeviceSize offset,
                          vk::DeviceSize size,
                          vk::DeviceSize allocationSize,
                          std::uint32_t ownerDevice,
                          bool coherent){
        if(coherent || size == 0){
            return;
        }
        const vk::DeviceSize atom = nonCoherentAtomSize(context, ownerDevice);
        const vk::DeviceSize alignedOffset = (offset / atom) * atom;
        const vk::DeviceSize end = std::min(offset + size, allocationSize);
        const vk::DeviceSize alignedEnd = std::min(((end + atom - 1) / atom) * atom,
                                                   allocationSize);
        if(alignedEnd <= alignedOffset){
            return;
        }
        vk::MappedMemoryRange range;
        range
            .setMemory(memory)
            .setOffset(alignedOffset)
            .setSize(alignedEnd - alignedOffset);
        context.device.flushMappedMemoryRanges(range);
    }

    void invalidateMappedRange(const vucol::detail::ContextState& context,
                               vk::DeviceMemory memory,
                               vk::DeviceSize offset,
                               vk::DeviceSize size,
                               vk::DeviceSize allocationSize,
                               std::uint32_t ownerDevice,
                               bool coherent){
        if(coherent || size == 0){
            return;
        }
        const vk::DeviceSize atom = nonCoherentAtomSize(context, ownerDevice);
        const vk::DeviceSize alignedOffset = (offset / atom) * atom;
        const vk::DeviceSize end = std::min(offset + size, allocationSize);
        const vk::DeviceSize alignedEnd = std::min(((end + atom - 1) / atom) * atom,
                                                   allocationSize);
        if(alignedEnd <= alignedOffset){
            return;
        }
        vk::MappedMemoryRange range;
        range
            .setMemory(memory)
            .setOffset(alignedOffset)
            .setSize(alignedEnd - alignedOffset);
        context.device.invalidateMappedMemoryRanges(range);
    }

    struct StagingBuffer{
        std::shared_ptr<vucol::detail::ContextState> context;
        vucol::detail::DeviceAllocation allocation;
        vk::DeviceSize size = 0;
        std::uint32_t ownerDevice = 0;

        ~StagingBuffer(){
            if(context && allocation.buffer){
                vucol::detail::freeDeviceBuffer(context, allocation);
            }
        }
    };

    StagingBuffer createStagingBuffer(const std::shared_ptr<vucol::detail::ContextState>& context,
                                      vk::DeviceSize bytes,
                                      std::uint32_t ownerDevice){
        StagingBuffer staging;
        staging.context = context;
        staging.size = bytes;
        staging.ownerDevice = ownerDevice;
        staging.allocation = vucol::detail::allocateDeviceBuffer(
            context,
            bytes,
            vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
            true,
            ownerDevice);
        return staging;
    }

    void submitCopyAndWait(const std::shared_ptr<vucol::detail::ContextState>& context,
                           vk::Buffer src,
                           vk::Buffer dst,
                           vk::DeviceSize bytes,
                           vk::DeviceSize srcOffset,
                           vk::DeviceSize dstOffset,
                           bool shaderWritesBeforeCopy,
                           bool shaderReadsAfterCopy,
                           std::uint32_t execDevice){
        vk::CommandBufferAllocateInfo allocateInfo;
        allocateInfo
            .setCommandPool(context->commandPool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1);
        vk::CommandBuffer commandBuffer =
            context->device.allocateCommandBuffers(allocateInfo).front();

        const std::uint32_t execMask = vucol::detail::deviceBit(execDevice);
        try{
            const VkDeviceGroupCommandBufferBeginInfo groupBeginInfo{
                .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = nullptr,
                .deviceMask = execMask,
            };
            vk::CommandBufferBeginInfo beginInfo;
            beginInfo
                .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
                .setPNext(&groupBeginInfo);
            commandBuffer.begin(beginInfo);

            vk::BufferMemoryBarrier beforeCopyBarrier;
            if(shaderWritesBeforeCopy){
                beforeCopyBarrier
                    .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                    .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setBuffer(src)
                    .setOffset(srcOffset)
                    .setSize(bytes);
                commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                              vk::PipelineStageFlagBits::eTransfer,
                                              {},
                                              {},
                                              beforeCopyBarrier,
                                              {});
            }

            vk::BufferCopy copyRegion;
            copyRegion
                .setSrcOffset(srcOffset)
                .setDstOffset(dstOffset)
                .setSize(bytes);
            commandBuffer.copyBuffer(src, dst, copyRegion);

            vk::BufferMemoryBarrier afterCopyBarrier;
            if(shaderReadsAfterCopy){
                afterCopyBarrier
                    .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                    .setDstAccessMask(vk::AccessFlagBits::eShaderRead |
                                      vk::AccessFlagBits::eShaderWrite)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setBuffer(dst)
                    .setOffset(dstOffset)
                    .setSize(bytes);
                commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                              vk::PipelineStageFlagBits::eComputeShader,
                                              {},
                                              {},
                                              afterCopyBarrier,
                                              {});
            }

            commandBuffer.end();

            vk::Fence fence = context->device.createFence({});
            const VkDeviceGroupSubmitInfo groupSubmitInfo{
                .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreCount = 0,
                .pWaitSemaphoreDeviceIndices = nullptr,
                .commandBufferCount = 1,
                .pCommandBufferDeviceMasks = &execMask,
                .signalSemaphoreCount = 0,
                .pSignalSemaphoreDeviceIndices = nullptr,
            };
            vk::SubmitInfo submitInfo;
            submitInfo
                .setPNext(&groupSubmitInfo)
                .setCommandBufferCount(1)
                .setPCommandBuffers(&commandBuffer);
            context->queue.submit(submitInfo, fence);

            const auto result = context->device.waitForFences(fence, vk::True, UINT64_MAX);
            context->device.destroyFence(fence);
            if(result != vk::Result::eSuccess){
                throw std::runtime_error("waitForFences failed during buffer staging copy.");
            }

            context->device.freeCommandBuffers(context->commandPool, commandBuffer);
        }catch(...){
            context->device.freeCommandBuffers(context->commandPool, commandBuffer);
            throw;
        }
    }
}

namespace vucol::detail{
    DeviceAllocation allocateDeviceBuffer(const std::shared_ptr<ContextState>& context,
                                          vk::DeviceSize size,
                                          vk::BufferUsageFlags usage,
                                          bool needHostVisible,
                                          std::uint32_t ownerDevice,
                                          BufferPeerAccess peerAccess){
        if(ownerDevice >= context->deviceCount()){
            throw std::out_of_range("Buffer owner device index is out of range.");
        }
        if(size == 0){
            throw std::runtime_error("Cannot allocate a zero-sized device buffer.");
        }

        vk::BufferCreateInfo bufferInfo;
        bufferInfo
            .setSize(size)
            .setUsage(usage)
            .setSharingMode(vk::SharingMode::eExclusive);
        vk::Buffer buffer = context->device.createBuffer(bufferInfo);
        try{
            const vk::PhysicalDevice physicalDevice = context->physicalDevices[ownerDevice];
            const vk::MemoryRequirements requirements =
                context->device.getBufferMemoryRequirements(buffer);

            std::uint32_t memoryTypeIndex = 0;
            if(needHostVisible){
                memoryTypeIndex = findScoredMemoryType(physicalDevice,
                                                       requirements.memoryTypeBits,
                                                       vk::MemoryPropertyFlagBits::eHostVisible);
            }else{
                memoryTypeIndex = findMemoryType(physicalDevice,
                                                 requirements.memoryTypeBits,
                                                 vk::MemoryPropertyFlags{},
                                                 vk::MemoryPropertyFlagBits::eDeviceLocal);
            }

            const auto memoryProperties = physicalDevice.getMemoryProperties();
            const auto propertyFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
            const std::uint32_t heapIndex = memoryProperties.memoryTypes[memoryTypeIndex].heapIndex;

            // Verified before any allocation so a failure cannot leak resources.
            verifyPeerAccess(context, ownerDevice, peerAccess, heapIndex);

            const std::uint32_t mask = deviceBit(ownerDevice);
            const VkMemoryAllocateFlagsInfo flagsInfo{
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                .pNext = nullptr,
                .flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT,
                .deviceMask = mask,
            };
            vk::MemoryAllocateInfo allocateInfo;
            allocateInfo
                .setPNext(&flagsInfo)
                .setAllocationSize(requirements.size)
                .setMemoryTypeIndex(memoryTypeIndex);
            vk::DeviceMemory memory = context->device.allocateMemory(allocateInfo);
            try{
                context->device.bindBufferMemory(buffer, memory, 0);
            }catch(...){
                context->device.freeMemory(memory);
                throw;
            }

            DeviceAllocation allocation;
            allocation.buffer = buffer;
            allocation.memory = memory;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memoryTypeIndex;
            allocation.heapIndex = heapIndex;
            allocation.hostVisible =
                (propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) !=
                vk::MemoryPropertyFlags{};
            allocation.coherent =
                (propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent) !=
                vk::MemoryPropertyFlags{};
            if(allocation.hostVisible){
                allocation.mapped =
                    context->device.mapMemory(memory, 0, VK_WHOLE_SIZE);
            }
            return allocation;
        }catch(...){
            context->device.destroyBuffer(buffer);
            throw;
        }
    }

    void freeDeviceBuffer(const std::shared_ptr<ContextState>& context,
                          DeviceAllocation& allocation) noexcept{
        if(!context || !allocation.buffer){
            return;
        }
        try{
            if(allocation.mapped){
                context->device.unmapMemory(allocation.memory);
                allocation.mapped = nullptr;
            }
            context->device.destroyBuffer(allocation.buffer);
            allocation.buffer = nullptr;
            if(allocation.memory){
                context->device.freeMemory(allocation.memory);
                allocation.memory = nullptr;
            }
        }catch(...){
        }
    }

    void verifyPeerAccess(const std::shared_ptr<ContextState>& context,
                          std::uint32_t ownerDevice,
                          BufferPeerAccess access,
                          std::uint32_t heapIndex){
        if(access == BufferPeerAccess::None){
            return;
        }
        const bool needRead =
            (static_cast<std::uint32_t>(access) &
             static_cast<std::uint32_t>(BufferPeerAccess::PeerRead)) != 0;
        const bool needWrite =
            (static_cast<std::uint32_t>(access) &
             static_cast<std::uint32_t>(BufferPeerAccess::PeerWrite)) != 0;
        for(std::uint32_t remote = 0; remote < context->deviceCount(); ++remote){
            if(remote == ownerDevice){
                continue;
            }
            const vk::PeerMemoryFeatureFlags features =
                context->device.getGroupPeerMemoryFeatures(heapIndex, ownerDevice, remote);
            if(needRead && !(features & vk::PeerMemoryFeatureFlagBits::eGenericSrc)){
                throw std::runtime_error(
                    "Peer reads of this buffer are not supported by the devices.");
            }
            if(needWrite && !(features & vk::PeerMemoryFeatureFlagBits::eGenericDst)){
                throw std::runtime_error(
                    "Peer writes to this buffer are not supported by the devices.");
            }
        }
    }

    BufferState::~BufferState(){
        if(context && buffer){
            DeviceAllocation allocation;
            allocation.buffer = buffer;
            allocation.memory = memory;
            allocation.mapped = mapped;
            freeDeviceBuffer(context, allocation);
            buffer = nullptr;
            memory = nullptr;
            mapped = nullptr;
        }
    }
}

namespace vucol{
    Buffer::Buffer() = default;
    Buffer::~Buffer() = default;

    Buffer::Buffer(std::shared_ptr<detail::BufferState> state)
        : state_(std::move(state)){
    }

    std::size_t Buffer::size() const{
        return state_ ? static_cast<std::size_t>(state_->size) : 0;
    }

    BufferType Buffer::type() const{
        return state_ ? state_->type : BufferType::Auto;
    }

    bool Buffer::hostVisible() const{
        return state_ && state_->hostVisible;
    }

    std::uint32_t Buffer::ownerDeviceIndex() const{
        return state_ ? state_->ownerDeviceIndex : 0;
    }

    BufferPeerAccess Buffer::peerAccess() const{
        return state_ ? state_->peerAccess : BufferPeerAccess::None;
    }

    Buffer::operator bool() const{
        return static_cast<bool>(state_);
    }

    void Buffer::write(const void* data, std::size_t bytes, std::size_t offset){
        if(!state_){
            throw std::runtime_error("Cannot write to an empty vucol::Buffer.");
        }
        if(offset > size() || bytes > size() - offset){
            throw std::out_of_range("Buffer write range is out of bounds.");
        }
        if(state_->gpuUseClaims != 0){
            throw std::runtime_error(
                "Cannot write to a Buffer referenced by an uncollected GPU submission.");
        }
        if(bytes == 0){
            return;
        }

        if(!state_->hostVisible){
            auto staging = createStagingBuffer(state_->context,
                                               static_cast<vk::DeviceSize>(bytes),
                                               state_->ownerDeviceIndex);
            std::memcpy(staging.allocation.mapped, data, bytes);
            flushMappedRange(*state_->context,
                             staging.allocation.memory,
                             0,
                             static_cast<vk::DeviceSize>(bytes),
                             staging.allocation.allocationSize,
                             state_->ownerDeviceIndex,
                             staging.allocation.coherent);

            submitCopyAndWait(state_->context,
                              staging.allocation.buffer,
                              state_->buffer,
                              static_cast<vk::DeviceSize>(bytes),
                              0,
                              static_cast<vk::DeviceSize>(offset),
                              false,
                              true,
                              state_->ownerDeviceIndex);
            return;
        }

        void* mapped = static_cast<std::byte*>(state_->mapped) + offset;
        std::memcpy(mapped, data, bytes);
        flushMappedRange(*state_->context,
                         state_->memory,
                         static_cast<vk::DeviceSize>(offset),
                         static_cast<vk::DeviceSize>(bytes),
                         state_->allocationSize,
                         state_->ownerDeviceIndex,
                         state_->coherent);
    }

    void Buffer::read(void* data, std::size_t bytes, std::size_t offset) const{
        if(!state_){
            throw std::runtime_error("Cannot read from an empty vucol::Buffer.");
        }
        if(offset > size() || bytes > size() - offset){
            throw std::out_of_range("Buffer read range is out of bounds.");
        }
        if(state_->gpuUseClaims != 0){
            throw std::runtime_error(
                "Cannot read from a Buffer referenced by an uncollected GPU submission.");
        }
        if(bytes == 0){
            return;
        }

        if(!state_->hostVisible){
            auto staging = createStagingBuffer(state_->context,
                                               static_cast<vk::DeviceSize>(bytes),
                                               state_->ownerDeviceIndex);
            submitCopyAndWait(state_->context,
                              state_->buffer,
                              staging.allocation.buffer,
                              static_cast<vk::DeviceSize>(bytes),
                              static_cast<vk::DeviceSize>(offset),
                              0,
                              true,
                              false,
                              state_->ownerDeviceIndex);

            invalidateMappedRange(*state_->context,
                                   staging.allocation.memory,
                                   0,
                                   static_cast<vk::DeviceSize>(bytes),
                                   staging.allocation.allocationSize,
                                   state_->ownerDeviceIndex,
                                   staging.allocation.coherent);
            std::memcpy(data, staging.allocation.mapped, bytes);
            return;
        }

        invalidateMappedRange(*state_->context,
                               state_->memory,
                               static_cast<vk::DeviceSize>(offset),
                               static_cast<vk::DeviceSize>(bytes),
                               state_->allocationSize,
                               state_->ownerDeviceIndex,
                               state_->coherent);
        std::memcpy(data, static_cast<const std::byte*>(state_->mapped) + offset, bytes);
    }
}
