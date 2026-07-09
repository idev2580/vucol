#include <socl/DescriptorSet.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace{
    vk::DescriptorType toVulkanDescriptorType(socl::DescriptorType type){
        switch(type){
            case socl::DescriptorType::UniformBuffer:
                return vk::DescriptorType::eUniformBuffer;
            case socl::DescriptorType::UnifiedPreferred:
            case socl::DescriptorType::StorageBuffer:
                return vk::DescriptorType::eStorageBuffer;
        }
        return vk::DescriptorType::eStorageBuffer;
    }
}

namespace socl{
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

    void DescriptorSet::bindBuffer(std::uint32_t binding, const Buffer& buffer){
        if(!state_ || !state_->pipeline){
            throw std::runtime_error("Cannot bind a buffer to an empty socl::DescriptorSet.");
        }
        if(!buffer.state_){
            throw std::runtime_error("Cannot bind an empty socl::Buffer.");
        }

        const auto& bindings = state_->pipeline->bindings;
        const auto it = std::find_if(bindings.begin(), bindings.end(), [binding](const DescriptorBinding& item){
            return item.binding == binding;
        });
        if(it == bindings.end()){
            throw std::runtime_error("Descriptor binding is not part of this pipeline layout.");
        }

        const std::size_t index = static_cast<std::size_t>(std::distance(bindings.begin(), it));
        state_->buffers[index] = buffer.state_;
    }

    void DescriptorSet::update(){
        if(!state_ || !state_->pipeline){
            throw std::runtime_error("Cannot update an empty socl::DescriptorSet.");
        }

        std::vector<vk::DescriptorBufferInfo> bufferInfos;
        std::vector<vk::WriteDescriptorSet> writes;
        bufferInfos.reserve(state_->buffers.size());
        writes.reserve(state_->buffers.size());

        for(std::size_t i = 0; i < state_->pipeline->bindings.size(); ++i){
            const auto& buffer = state_->buffers[i];
            if(!buffer){
                throw std::runtime_error("DescriptorSet has an unbound buffer.");
            }

            const auto& binding = state_->pipeline->bindings[i];
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo
                .setBuffer(buffer->buffer)
                .setOffset(0)
                .setRange(buffer->size);
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
