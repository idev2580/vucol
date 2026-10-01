#include <vucol/Buffer.hpp>
#include <vucol/Context.hpp>

#include "InternalState.hpp"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace{
    void checkVk(VkResult result, const char* message){
        if(result != VK_SUCCESS){
            throw std::runtime_error(message);
        }
    }

    struct StagingBuffer{
        VmaAllocator allocator = VK_NULL_HANDLE;
        vk::Buffer buffer;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo allocationInfo{};
        vk::DeviceSize size = 0;

        ~StagingBuffer(){
            if(allocator != VK_NULL_HANDLE && buffer){
                vmaDestroyBuffer(allocator, buffer, allocation);
            }
        }
    };

    StagingBuffer createStagingBuffer(const vucol::detail::ContextState& context,
                                      vk::DeviceSize bytes){
        StagingBuffer staging;
        staging.allocator = context.allocator;
        staging.size = bytes;

        vk::BufferCreateInfo bufferInfo;
        bufferInfo
            .setSize(bytes)
            .setUsage(vk::BufferUsageFlagBits::eTransferSrc |
                      vk::BufferUsageFlagBits::eTransferDst)
            .setSharingMode(vk::SharingMode::eExclusive);

        const VmaAllocationCreateInfo allocationInfo{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        };

        VkBuffer rawBuffer = VK_NULL_HANDLE;
        checkVk(vmaCreateBuffer(context.allocator,
                                reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
                                &allocationInfo,
                                &rawBuffer,
                                &staging.allocation,
                                &staging.allocationInfo),
                "vmaCreateBuffer failed while creating staging buffer.");
        staging.buffer = vk::Buffer(rawBuffer);
        return staging;
    }

    void* mapAllocation(VmaAllocator allocator,
                        VmaAllocation allocation,
                        const char* action){
        void* mapped = nullptr;
        const VkResult result = vmaMapMemory(allocator, allocation, &mapped);
        if(result != VK_SUCCESS){
            throw std::runtime_error(action);
        }
        return mapped;
    }

    void submitCopyAndWait(const std::shared_ptr<vucol::detail::ContextState>& context,
                           vk::Buffer src,
                           vk::Buffer dst,
                           vk::DeviceSize bytes,
                           vk::DeviceSize srcOffset,
                           vk::DeviceSize dstOffset,
                           bool shaderWritesBeforeCopy,
                           bool shaderReadsAfterCopy){
        vk::CommandBufferAllocateInfo allocateInfo;
        allocateInfo
            .setCommandPool(context->commandPool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1);
        vk::CommandBuffer commandBuffer =
            context->device.allocateCommandBuffers(allocateInfo).front();

        try{
            vk::CommandBufferBeginInfo beginInfo;
            beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
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
            vk::SubmitInfo submitInfo;
            submitInfo
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

namespace vucol{
    namespace detail{
        BufferState::~BufferState(){
            if(allocator != VK_NULL_HANDLE && buffer){
                vmaDestroyBuffer(allocator, buffer, allocation);
            }
        }
    }

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
            auto staging = createStagingBuffer(*state_->context, static_cast<vk::DeviceSize>(bytes));
            void* mapped = mapAllocation(staging.allocator,
                                         staging.allocation,
                                         "vmaMapMemory failed while writing staging buffer.");
            std::memcpy(mapped, data, bytes);
            checkVk(vmaFlushAllocation(staging.allocator, staging.allocation, 0, bytes),
                    "vmaFlushAllocation failed while writing staging buffer.");
            vmaUnmapMemory(staging.allocator, staging.allocation);

            submitCopyAndWait(state_->context,
                              staging.buffer,
                              state_->buffer,
                              static_cast<vk::DeviceSize>(bytes),
                              0,
                              static_cast<vk::DeviceSize>(offset),
                              false,
                              true);
            return;
        }

        void* mapped = mapAllocation(state_->allocator,
                                     state_->allocation,
                                     "vmaMapMemory failed while writing buffer.");
        std::memcpy(static_cast<std::byte*>(mapped) + offset, data, bytes);
        checkVk(vmaFlushAllocation(state_->allocator, state_->allocation, offset, bytes),
                "vmaFlushAllocation failed while writing buffer.");
        vmaUnmapMemory(state_->allocator, state_->allocation);
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
            auto staging = createStagingBuffer(*state_->context, static_cast<vk::DeviceSize>(bytes));
            submitCopyAndWait(state_->context,
                              state_->buffer,
                              staging.buffer,
                              static_cast<vk::DeviceSize>(bytes),
                              static_cast<vk::DeviceSize>(offset),
                              0,
                              true,
                              false);

            checkVk(vmaInvalidateAllocation(staging.allocator, staging.allocation, 0, bytes),
                    "vmaInvalidateAllocation failed while reading staging buffer.");
            void* mapped = mapAllocation(staging.allocator,
                                         staging.allocation,
                                         "vmaMapMemory failed while reading staging buffer.");
            std::memcpy(data, mapped, bytes);
            vmaUnmapMemory(staging.allocator, staging.allocation);
            return;
        }

        checkVk(vmaInvalidateAllocation(state_->allocator, state_->allocation, offset, bytes),
                "vmaInvalidateAllocation failed while reading buffer.");
        void* mapped = mapAllocation(state_->allocator,
                                     state_->allocation,
                                     "vmaMapMemory failed while reading buffer.");
        std::memcpy(data, static_cast<std::byte*>(mapped) + offset, bytes);
        vmaUnmapMemory(state_->allocator, state_->allocation);
    }
}
