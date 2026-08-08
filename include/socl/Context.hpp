#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <iosfwd>
#include <socl/Buffer.hpp>
#include <socl/DescriptorSet.hpp>
#include <socl/ShaderPipeline.hpp>
#include <string>
#include <string_view>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <vector>

namespace socl{
    struct GpuInfo{
        std::uint32_t index = 0;
        std::string name;
        std::string type;
        std::uint32_t vendorId = 0;
        std::uint32_t deviceId = 0;
        std::uint32_t apiVersion = 0;
        std::uint32_t driverVersion = 0;
        std::uint32_t computeQueueFamily = 0;
        std::uint32_t computeQueueCount = 0;
    };

    struct ContextCreateInfo{
        std::uint32_t physicalDeviceIndex = 0;
        std::vector<const char*> requiredDeviceExtensions;
    };

    struct CooperativeMatrixTileProperties{
        std::uint32_t m = 0;
        std::uint32_t n = 0;
        std::uint32_t k = 0;
        vk::ComponentTypeKHR aType = vk::ComponentTypeKHR::eFloat16;
        vk::ComponentTypeKHR bType = vk::ComponentTypeKHR::eFloat16;
        vk::ComponentTypeKHR cType = vk::ComponentTypeKHR::eFloat16;
        vk::ComponentTypeKHR resultType = vk::ComponentTypeKHR::eFloat16;
        bool saturatingAccumulation = false;
        vk::ScopeKHR scope = vk::ScopeKHR::eSubgroup;
    };

    struct CooperativeMatrixSupportInfo{
        bool extensionSupported = false;
        bool featureSupported = false;
        bool robustBufferAccessSupported = false;
        vk::ShaderStageFlags supportedStages;
        std::vector<CooperativeMatrixTileProperties> tiles;
    };

    struct SubgroupSupportInfo{
        std::uint32_t defaultSize = 0;
        vk::ShaderStageFlags supportedStages;
        vk::SubgroupFeatureFlags supportedOperations;
        vk::ShaderStageFlags requiredSizeStages;
        bool sizeControlSupported = false;
        bool sizeControlEnabled = false;
        bool computeFullSubgroupsSupported = false;
        bool computeFullSubgroupsEnabled = false;
        std::uint32_t minSize = 0;
        std::uint32_t maxSize = 0;
    };

    namespace detail{
        struct DispatchResources;

        struct ContextState{
            vk::Instance instance;
            vk::PhysicalDevice physicalDevice;
            vk::PhysicalDeviceProperties physicalDeviceProperties{};
            GpuInfo gpuInfo;
            std::vector<std::string> supportedDeviceExtensions;
            CooperativeMatrixSupportInfo cooperativeMatrixSupportInfo;
            SubgroupSupportInfo subgroupSupportInfo;
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

    // Returned by submitAsync(). It represents one submitted GPU command batch,
    // like an explicit process/job handle for work that may still be running.
    class DispatchToken{
        friend class Context;

        public:
        DispatchToken();
        ~DispatchToken();

        DispatchToken(const DispatchToken&) = delete;
        DispatchToken& operator=(const DispatchToken&) = delete;
        DispatchToken(DispatchToken&&) noexcept;
        DispatchToken& operator=(DispatchToken&&) noexcept;

        void wait();
        [[nodiscard]] bool valid() const;

        private:
        explicit DispatchToken(std::shared_ptr<detail::ContextState> context,
                               vk::Fence fence,
                               vk::CommandBuffer commandBuffer,
                               std::shared_ptr<detail::DispatchResources> resources);

        std::shared_ptr<detail::ContextState> context_;
        std::shared_ptr<detail::DispatchResources> resources_;
        vk::Fence fence_;
        vk::CommandBuffer commandBuffer_;
    };

    class Context{
        public:
        Context();
        explicit Context(const ContextCreateInfo& createInfo);
        ~Context();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) noexcept = default;
        Context& operator=(Context&&) noexcept = default;

        Buffer createBuffer(std::size_t bytes, BufferType type = BufferType::Auto);
        ShaderPipeline createShaderPipeline(const ShaderPipelineCreateInfo& createInfo);
        DescriptorSet createDescriptorSet(const ShaderPipeline& pipeline);

        void begin();
        void use(const ShaderPipeline& pipeline);
        // Selects a logical descriptor set. dispatch() captures its current
        // buffer bindings into an immutable native descriptor set.
        void bind(const DescriptorSet& descriptorSet);
        void push(const void* data, std::size_t bytes, std::size_t offset = 0);

        template<typename T>
        void push(const T& value){
            push(&value, sizeof(T), 0);
        }

        void dispatch(std::uint32_t groupCountX,
                      std::uint32_t groupCountY = 1,
                      std::uint32_t groupCountZ = 1);

        // submitAsync returns a token so callers can wait for a specific batch.
        // The token retains every dispatch snapshot and buffer until completion.
        // submitAndWait is the OpenGL-like convenience path for immediate waits.
        DispatchToken submitAsync();
        void submitAndWait();

        [[nodiscard]] bool usingIntegratedGpu() const;
        [[nodiscard]] const GpuInfo& gpuInfo() const;
        [[nodiscard]] std::vector<std::string> supportedDeviceExtensions() const;
        [[nodiscard]] bool supportsDeviceExtension(std::string_view extensionName) const;
        [[nodiscard]] bool supportsCooperativeMatrix() const;
        [[nodiscard]] const CooperativeMatrixSupportInfo& cooperativeMatrixSupportInfo() const;
        [[nodiscard]] const SubgroupSupportInfo& subgroupSupportInfo() const;
        void printGpuInfo(std::ostream& os) const;

        [[nodiscard]] static std::vector<GpuInfo> enumerateGpus();

        private:
        std::shared_ptr<detail::ContextState> state_;
        std::shared_ptr<detail::DispatchResources> recordingResources_;
    };
}
