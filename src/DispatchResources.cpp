#include "DispatchResources.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
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

namespace socl::detail{
    DescriptorSetSnapshotState::~DescriptorSetSnapshotState(){
        if(device && descriptorPool){
            device.destroyDescriptorPool(descriptorPool);
        }
    }

    DispatchResources::~DispatchResources(){
        for(const auto& use : buffers){
            if(accessReads(use.access)){
                --use.buffer->gpuReadClaims;
            }
            if(accessWrites(use.access)){
                --use.buffer->gpuWriteClaims;
            }
        }
    }

    bool accessReads(BufferAccess access){
        return access == BufferAccess::Read || access == BufferAccess::ReadWrite;
    }

    bool accessWrites(BufferAccess access){
        return access == BufferAccess::Write || access == BufferAccess::ReadWrite;
    }

    BufferAccess mergeAccess(BufferAccess left, BufferAccess right){
        const bool reads = accessReads(left) || accessReads(right);
        const bool writes = accessWrites(left) || accessWrites(right);
        if(reads && writes){
            return BufferAccess::ReadWrite;
        }
        return reads ? BufferAccess::Read : BufferAccess::Write;
    }

    vk::AccessFlags toVulkanAccess(BufferAccess access){
        vk::AccessFlags flags;
        if(accessReads(access)){
            flags |= vk::AccessFlagBits::eShaderRead;
        }
        if(accessWrites(access)){
            flags |= vk::AccessFlagBits::eShaderWrite;
        }
        return flags;
    }

    DispatchBufferUse* findBufferUse(
        std::vector<DispatchBufferUse>& uses,
        const std::shared_ptr<BufferState>& buffer){
        const auto found = std::find_if(uses.begin(), uses.end(), [&](const auto& use){
            return use.buffer == buffer;
        });
        return found == uses.end() ? nullptr : &*found;
    }

    const DispatchBufferUse* findBufferUse(
        const std::vector<DispatchBufferUse>& uses,
        const std::shared_ptr<BufferState>& buffer){
        const auto found = std::find_if(uses.begin(), uses.end(), [&](const auto& use){
            return use.buffer == buffer;
        });
        return found == uses.end() ? nullptr : &*found;
    }

    void claimBuffer(DispatchResources& resources,
                     const std::shared_ptr<BufferState>& buffer,
                     BufferAccess access){
        DispatchBufferUse* existing = findBufferUse(resources.buffers, buffer);
        if(!existing){
            const bool conflicts = accessWrites(access)
                ? buffer->gpuReadClaims != 0 || buffer->gpuWriteClaims != 0
                : buffer->gpuWriteClaims != 0;
            if(conflicts){
                throw std::runtime_error(
                    "Buffer is still referenced by a conflicting GPU command batch.");
            }

            resources.buffers.push_back({buffer, access});
            if(accessReads(access)){
                ++buffer->gpuReadClaims;
            }
            if(accessWrites(access)){
                ++buffer->gpuWriteClaims;
            }
            return;
        }

        const BufferAccess combined = mergeAccess(existing->access, access);
        const std::size_t externalReaders =
            buffer->gpuReadClaims - (accessReads(existing->access) ? 1u : 0u);
        const std::size_t externalWriters =
            buffer->gpuWriteClaims - (accessWrites(existing->access) ? 1u : 0u);
        const bool conflicts = accessWrites(combined)
            ? externalReaders != 0 || externalWriters != 0
            : externalWriters != 0;
        if(conflicts){
            throw std::runtime_error(
                "Buffer is still referenced by a conflicting GPU command batch.");
        }

        if(accessReads(combined) && !accessReads(existing->access)){
            ++buffer->gpuReadClaims;
        }
        if(accessWrites(combined) && !accessWrites(existing->access)){
            ++buffer->gpuWriteClaims;
        }
        existing->access = combined;
    }

    std::shared_ptr<DescriptorSetSnapshotState>
    createDescriptorSnapshot(const std::shared_ptr<DescriptorSetState>& source){
        auto snapshot = std::make_shared<DescriptorSetSnapshotState>();
        snapshot->device = source->device;
        snapshot->pipeline = source->pipeline;
        snapshot->buffers = source->buffers;

        std::vector<vk::DescriptorPoolSize> poolSizes;
        for(const auto& binding : source->pipeline->bindings){
            const vk::DescriptorType type = toVulkanDescriptorType(binding.type);
            auto poolSize = std::find_if(poolSizes.begin(), poolSizes.end(), [type](const auto& item){
                return item.type == type;
            });
            if(poolSize == poolSizes.end()){
                vk::DescriptorPoolSize item;
                item.setType(type).setDescriptorCount(1);
                poolSizes.push_back(item);
            }else{
                ++poolSize->descriptorCount;
            }
        }

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo
            .setMaxSets(1)
            .setPoolSizeCount(static_cast<std::uint32_t>(poolSizes.size()))
            .setPPoolSizes(poolSizes.data());
        snapshot->descriptorPool = snapshot->device.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocateInfo;
        allocateInfo
            .setDescriptorPool(snapshot->descriptorPool)
            .setDescriptorSetCount(1)
            .setPSetLayouts(&snapshot->pipeline->descriptorSetLayout);
        snapshot->descriptorSet = snapshot->device.allocateDescriptorSets(allocateInfo).front();

        std::vector<vk::DescriptorBufferInfo> bufferInfos;
        std::vector<vk::WriteDescriptorSet> writes;
        bufferInfos.reserve(snapshot->buffers.size());
        writes.reserve(snapshot->buffers.size());
        for(std::size_t i = 0; i < snapshot->buffers.size(); ++i){
            const auto& bound = snapshot->buffers[i];
            if(!bound.buffer){
                throw std::runtime_error("DescriptorSet has an unbound buffer at dispatch time.");
            }

            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo
                .setBuffer(bound.buffer->buffer)
                .setOffset(0)
                .setRange(bound.buffer->size);
            bufferInfos.push_back(bufferInfo);

            vk::WriteDescriptorSet write;
            write
                .setDstSet(snapshot->descriptorSet)
                .setDstBinding(snapshot->pipeline->bindings[i].binding)
                .setDstArrayElement(0)
                .setDescriptorCount(1)
                .setDescriptorType(
                    toVulkanDescriptorType(snapshot->pipeline->bindings[i].type))
                .setPBufferInfo(&bufferInfos.back());
            writes.push_back(write);
        }
        snapshot->device.updateDescriptorSets(writes, {});
        return snapshot;
    }
}
