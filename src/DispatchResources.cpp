#include "DispatchResources.hpp"
#include "InternalState.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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

    vk::DeviceSize rangeEnd(const socl::detail::DispatchBufferUse& use){
        if(use.size > std::numeric_limits<vk::DeviceSize>::max() - use.offset){
            throw std::out_of_range("Buffer access range end is not representable.");
        }
        return use.offset + use.size;
    }
}

namespace socl::detail{
    GpuTimingState::~GpuTimingState(){
        if(device && queryPool){
            device.destroyQueryPool(queryPool);
        }
    }

    DescriptorSetSnapshotState::~DescriptorSetSnapshotState(){
        if(device && descriptorPool){
            device.destroyDescriptorPool(descriptorPool);
        }
    }

    DispatchResources::~DispatchResources(){
        releaseBufferClaims(*this);
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

    std::uint64_t timestampDelta(std::uint64_t start,
                                 std::uint64_t end,
                                 std::uint32_t validBits){
        if(validBits == 0 || validBits > 64){
            throw std::invalid_argument("Timestamp valid-bit count must be in [1, 64].");
        }
        const std::uint64_t mask = validBits == 64
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t{1} << validBits) - 1;
        return (end - start) & mask;
    }

    bool rangesOverlap(const DispatchBufferUse& left,
                       const DispatchBufferUse& right){
        return left.buffer == right.buffer &&
               left.offset < rangeEnd(right) &&
               right.offset < rangeEnd(left);
    }

    std::vector<DispatchBufferUse> normalizeBufferUses(
        const std::vector<DispatchBufferUse>& uses){
        std::vector<DispatchBufferUse> normalized;
        std::vector<std::shared_ptr<BufferState>> visitedBuffers;

        // Split overlapping bindings into disjoint intervals so each byte range has
        // one combined access mode for the dispatch.
        for(const auto& first : uses){
            if(!first.buffer){
                throw std::invalid_argument("A buffer access cannot reference an empty buffer.");
            }
            if(first.size == 0){
                throw std::invalid_argument("A buffer access range cannot be empty.");
            }
            if(std::find(visitedBuffers.begin(), visitedBuffers.end(), first.buffer) !=
               visitedBuffers.end()){
                continue;
            }
            visitedBuffers.push_back(first.buffer);

            std::vector<vk::DeviceSize> boundaries;
            for(const auto& use : uses){
                if(use.buffer == first.buffer){
                    boundaries.push_back(use.offset);
                    boundaries.push_back(rangeEnd(use));
                }
            }
            std::sort(boundaries.begin(), boundaries.end());
            boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

            for(std::size_t i = 1; i < boundaries.size(); ++i){
                const vk::DeviceSize begin = boundaries[i - 1];
                const vk::DeviceSize end = boundaries[i];
                std::optional<BufferAccess> combined;
                for(const auto& use : uses){
                    if(use.buffer == first.buffer &&
                       use.offset <= begin && end <= rangeEnd(use)){
                        combined = combined
                            ? mergeAccess(*combined, use.access)
                            : use.access;
                    }
                }
                if(!combined){
                    continue;
                }

                if(!normalized.empty() &&
                   normalized.back().buffer == first.buffer &&
                   normalized.back().access == *combined &&
                   rangeEnd(normalized.back()) == begin){
                    normalized.back().size += end - begin;
                }else{
                    normalized.push_back({first.buffer, begin, end - begin, *combined});
                }
            }
        }
        return normalized;
    }

    void trackBuffer(DispatchResources& resources,
                     const std::shared_ptr<BufferState>& buffer){
        if(std::find(resources.buffers.begin(), resources.buffers.end(), buffer) !=
           resources.buffers.end()){
            return;
        }
        resources.buffers.push_back(buffer);
        for(const auto& access : buffer->lastQueueAccesses){
            resources.lastBufferAccesses.push_back({
                buffer,
                access.offset,
                access.size,
                access.access,
            });
        }
    }

    void recordBufferAccesses(DispatchResources& resources,
                              const std::vector<DispatchBufferUse>& accesses){
        for(const auto& current : normalizeBufferUses(accesses)){
            std::vector<DispatchBufferUse> updated;
            updated.reserve(resources.lastBufferAccesses.size() + 2);
            const vk::DeviceSize currentEnd = rangeEnd(current);

            // Overlay only the intersecting interval. Unchanged pieces keep their
            // earlier access so an unrelated dispatch cannot hide a dependency.
            for(const auto& previous : resources.lastBufferAccesses){
                if(!rangesOverlap(previous, current)){
                    updated.push_back(previous);
                    continue;
                }

                const vk::DeviceSize previousEnd = rangeEnd(previous);
                if(previous.offset < current.offset){
                    updated.push_back({
                        previous.buffer,
                        previous.offset,
                        current.offset - previous.offset,
                        previous.access,
                    });
                }
                if(currentEnd < previousEnd){
                    updated.push_back({
                        previous.buffer,
                        currentEnd,
                        previousEnd - currentEnd,
                        previous.access,
                    });
                }
            }
            updated.push_back(current);
            resources.lastBufferAccesses = normalizeBufferUses(updated);
        }
    }

    std::vector<BufferAccessCommit> prepareBufferAccessCommits(
        const DispatchResources& resources){
        std::vector<BufferAccessCommit> commits;
        commits.reserve(resources.buffers.size());
        for(const auto& buffer : resources.buffers){
            BufferAccessCommit commit;
            commit.buffer = buffer;
            for(const auto& access : resources.lastBufferAccesses){
                if(access.buffer == buffer){
                    commit.accesses.push_back({access.offset, access.size, access.access});
                }
            }
            commits.push_back(std::move(commit));
        }
        return commits;
    }

    void commitBufferAccesses(std::vector<BufferAccessCommit> commits){
        for(auto& commit : commits){
            commit.buffer->lastQueueAccesses = std::move(commit.accesses);
        }
    }

    void activateBufferClaims(DispatchResources& resources){
        if(resources.bufferClaimsActive){
            throw std::logic_error("GPU buffer claims are already active for this submission.");
        }
        for(const auto& buffer : resources.buffers){
            ++buffer->gpuUseClaims;
        }
        resources.bufferClaimsActive = true;
    }

    void releaseBufferClaims(DispatchResources& resources) noexcept{
        if(!resources.bufferClaimsActive){
            return;
        }
        for(const auto& buffer : resources.buffers){
            --buffer->gpuUseClaims;
        }
        resources.bufferClaimsActive = false;
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
                .setOffset(bound.offset)
                .setRange(bound.size);
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
