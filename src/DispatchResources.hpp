#pragma once

#include <memory>
#include <vector>

#include <socl/DescriptorSet.hpp>

namespace socl::detail{
    struct DescriptorSetSnapshotState{
        vk::Device device;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;
        std::shared_ptr<ShaderPipelineState> pipeline;
        std::vector<DescriptorBufferBinding> buffers;

        ~DescriptorSetSnapshotState();
    };

    struct DispatchBufferUse{
        std::shared_ptr<BufferState> buffer;
        BufferAccess access = BufferAccess::ReadWrite;
    };

    struct DispatchResources{
        std::vector<std::shared_ptr<DescriptorSetSnapshotState>> descriptorSets;
        std::vector<std::shared_ptr<ShaderPipelineState>> pipelines;
        std::vector<DispatchBufferUse> buffers;
        std::vector<DispatchBufferUse> lastBufferAccesses;

        ~DispatchResources();
    };

    [[nodiscard]] bool accessReads(BufferAccess access);
    [[nodiscard]] bool accessWrites(BufferAccess access);
    [[nodiscard]] BufferAccess mergeAccess(BufferAccess left, BufferAccess right);
    [[nodiscard]] vk::AccessFlags toVulkanAccess(BufferAccess access);

    DispatchBufferUse* findBufferUse(
        std::vector<DispatchBufferUse>& uses,
        const std::shared_ptr<BufferState>& buffer);
    const DispatchBufferUse* findBufferUse(
        const std::vector<DispatchBufferUse>& uses,
        const std::shared_ptr<BufferState>& buffer);

    void claimBuffer(DispatchResources& resources,
                     const std::shared_ptr<BufferState>& buffer,
                     BufferAccess access);

    [[nodiscard]] std::shared_ptr<DescriptorSetSnapshotState>
    createDescriptorSnapshot(const std::shared_ptr<DescriptorSetState>& source);
}
