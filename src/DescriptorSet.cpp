#include <vucol/DescriptorSet.hpp>

#include "InternalState.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace{
    vk::DescriptorType toVulkanDescriptorType(vucol::DescriptorType type){
        switch(type){
            case vucol::DescriptorType::UniformBuffer:
                return vk::DescriptorType::eUniformBuffer;
            case vucol::DescriptorType::UnifiedPreferred:
            case vucol::DescriptorType::StorageBuffer:
                return vk::DescriptorType::eStorageBuffer;
        }
        return vk::DescriptorType::eStorageBuffer;
    }
}

namespace vucol{
    namespace detail{
        DescriptorSetState::~DescriptorSetState(){
            if(device && descriptorPool){
                device.destroyDescriptorPool(descriptorPool);
            }
        }
    }

    DescriptorSet::DescriptorSet() = default;
    DescriptorSet::~DescriptorSet() = default;

    DescriptorSet::DescriptorSet(std::shared_ptr<detail::DescriptorSetState> state)
        : state_(std::move(state)){
    }

    void DescriptorSet::bindBuffer(std::uint32_t binding,
                                   const Buffer& buffer,
                                   BufferAccess access){
        bindBuffer(binding, buffer, 0, buffer.size(), access);
    }

    void DescriptorSet::bindBuffer(std::uint32_t binding,
                                   const Buffer& buffer,
                                   std::size_t offset,
                                   std::size_t size,
                                   BufferAccess access){
        if(!state_ || !state_->pipeline){
            throw std::runtime_error("Cannot bind a buffer to an empty vucol::DescriptorSet.");
        }
        if(!buffer.state_){
            throw std::runtime_error("Cannot bind an empty vucol::Buffer.");
        }
        if(buffer.state_->context != state_->context){
            throw std::runtime_error("Buffer belongs to a different Context.");
        }

        const auto& bindings = state_->pipeline->bindings;
        const auto it = std::find_if(bindings.begin(), bindings.end(), [binding](const DescriptorBinding& item){
            return item.binding == binding;
        });
        if(it == bindings.end()){
            throw std::runtime_error("Descriptor binding is not part of this pipeline layout.");
        }
        if(size == 0){
            throw std::out_of_range("Descriptor buffer range must not be empty.");
        }
        const std::size_t bufferSize = buffer.size();
        if(offset > bufferSize || size > bufferSize - offset){
            throw std::out_of_range("Descriptor buffer range is out of bounds.");
        }

        const auto& limits = state_->context->physicalDeviceProperties.limits;
        const bool uniform = it->type == DescriptorType::UniformBuffer;
        const vk::DeviceSize alignment = uniform
            ? limits.minUniformBufferOffsetAlignment
            : limits.minStorageBufferOffsetAlignment;
        const vk::DeviceSize maximumRange = uniform
            ? limits.maxUniformBufferRange
            : limits.maxStorageBufferRange;
        if(alignment != 0 && static_cast<vk::DeviceSize>(offset) % alignment != 0){
            throw std::runtime_error(
                "Descriptor buffer offset does not satisfy the device alignment requirement.");
        }
        if(static_cast<vk::DeviceSize>(size) > maximumRange){
            throw std::out_of_range(
                "Descriptor buffer range exceeds the device limit for its descriptor type.");
        }

        const std::size_t index = static_cast<std::size_t>(std::distance(bindings.begin(), it));
        state_->buffers[index] = {
            .buffer = buffer.state_,
            .offset = static_cast<vk::DeviceSize>(offset),
            .size = static_cast<vk::DeviceSize>(size),
            .access = access,
        };
    }

    void DescriptorSet::update(){
        if(!state_ || !state_->pipeline){
            throw std::runtime_error("Cannot update an empty vucol::DescriptorSet.");
        }

        std::vector<vk::DescriptorBufferInfo> bufferInfos;
        std::vector<vk::WriteDescriptorSet> writes;
        bufferInfos.reserve(state_->buffers.size());
        writes.reserve(state_->buffers.size());

        for(std::size_t i = 0; i < state_->pipeline->bindings.size(); ++i){
            const auto& bound = state_->buffers[i];
            if(!bound.buffer){
                throw std::runtime_error("DescriptorSet has an unbound buffer.");
            }

            const auto& binding = state_->pipeline->bindings[i];
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo
                .setBuffer(bound.buffer->buffer)
                .setOffset(bound.offset)
                .setRange(bound.size);
            bufferInfos.push_back(bufferInfo);

            vk::WriteDescriptorSet write;
            write
                .setDstSet(state_->descriptorSet)
                .setDstBinding(binding.binding)
                .setDstArrayElement(0)
                .setDescriptorCount(1)
                .setDescriptorType(toVulkanDescriptorType(binding.type))
                .setPBufferInfo(&bufferInfos.back());
            writes.push_back(write);
        }

        state_->device.updateDescriptorSets(writes, {});
    }

    DescriptorSet::operator bool() const{
        return static_cast<bool>(state_);
    }
}
