#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <vucol/DescriptorSet.hpp>

namespace vucol::detail{
    struct GpuTimingState{
        vk::Device device;
        vk::QueryPool queryPool;
        std::uint32_t timestampValidBits = 0;
        double timestampPeriodNanoseconds = 0.0;

        ~GpuTimingState();
    };

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
        vk::DeviceSize offset = 0;
        vk::DeviceSize size = 0;
        BufferAccess access = BufferAccess::ReadWrite;
    };

    struct BufferAccessCommit{
        std::shared_ptr<BufferState> buffer;
        std::vector<BufferAccessRange> accesses;
    };

    struct DispatchResources{
        std::vector<std::shared_ptr<DescriptorSetSnapshotState>> descriptorSets;
        std::vector<std::shared_ptr<ShaderPipelineState>> pipelines;
        std::vector<std::shared_ptr<BufferState>> buffers;
        std::vector<DispatchBufferUse> lastBufferAccesses;
        std::shared_ptr<GpuTimingState> gpuTiming;
        bool bufferClaimsActive = false;

        ~DispatchResources();
    };

    [[nodiscard]] bool accessReads(BufferAccess access);
    [[nodiscard]] bool accessWrites(BufferAccess access);
    [[nodiscard]] BufferAccess mergeAccess(BufferAccess left, BufferAccess right);
    [[nodiscard]] vk::AccessFlags toVulkanAccess(BufferAccess access);
    [[nodiscard]] std::uint64_t timestampDelta(std::uint64_t start,
                                               std::uint64_t end,
                                               std::uint32_t validBits);

    [[nodiscard]] bool rangesOverlap(const DispatchBufferUse& left,
                                     const DispatchBufferUse& right);
    [[nodiscard]] std::vector<DispatchBufferUse> normalizeBufferUses(
        const std::vector<DispatchBufferUse>& uses);
    void trackBuffer(DispatchResources& resources,
                     const std::shared_ptr<BufferState>& buffer);
    void recordBufferAccesses(DispatchResources& resources,
                              const std::vector<DispatchBufferUse>& accesses);
    [[nodiscard]] std::vector<BufferAccessCommit> prepareBufferAccessCommits(
        const DispatchResources& resources);
    void commitBufferAccesses(std::vector<BufferAccessCommit> commits);
    void activateBufferClaims(DispatchResources& resources);
    void releaseBufferClaims(DispatchResources& resources) noexcept;

    [[nodiscard]] std::shared_ptr<DescriptorSetSnapshotState>
    createDescriptorSnapshot(const std::shared_ptr<DescriptorSetState>& source);
}
