#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <socl/Context.hpp>

#include <VkBootstrap.h>

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace{
    void checkVk(VkResult result, const char* message){
        if(result != VK_SUCCESS){
            throw std::runtime_error(message);
        }
    }

    template<typename T>
    T unwrap(vkb::Result<T>&& result, const char* action){
        if(!result){
            throw std::runtime_error(std::string(action) + ": " + result.error().message());
        }
        return result.value();
    }

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

    VmaMemoryUsage toVmaMemoryUsage(socl::BufferType type){
        switch(type){
            case socl::BufferType::HostVisible:
                return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            case socl::BufferType::Auto:
            case socl::BufferType::DeviceLocal:
                return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }
        return VMA_MEMORY_USAGE_AUTO;
    }

    VmaAllocationCreateFlags toVmaAllocationFlags(socl::BufferType type){
        if(type == socl::BufferType::HostVisible){
            return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }
        return 0;
    }

    vkb::Instance createVkbInstance(){
        vkb::InstanceBuilder instanceBuilder;
        return unwrap(
            instanceBuilder
                .set_app_name("socl")
                .set_engine_name("socl")
                .require_api_version(1, 1, 0)
                .build(),
            "vk-bootstrap instance creation failed");
    }

    std::string physicalDeviceTypeName(vk::PhysicalDeviceType type){
        switch(type){
            case vk::PhysicalDeviceType::eOther:
                return "other";
            case vk::PhysicalDeviceType::eIntegratedGpu:
                return "integrated";
            case vk::PhysicalDeviceType::eDiscreteGpu:
                return "discrete";
            case vk::PhysicalDeviceType::eVirtualGpu:
                return "virtual";
            case vk::PhysicalDeviceType::eCpu:
                return "cpu";
        }
        return "unknown";
    }

    std::vector<std::string> enumerateDeviceExtensionNames(vk::PhysicalDevice physicalDevice){
        std::vector<std::string> names;
        for(const auto& extension : physicalDevice.enumerateDeviceExtensionProperties()){
            names.emplace_back(extension.extensionName.data());
        }
        return names;
    }

    bool containsExtension(const std::vector<std::string>& extensionNames,
                           std::string_view extensionName){
        return std::find(extensionNames.begin(),
                         extensionNames.end(),
                         extensionName) != extensionNames.end();
    }

    void checkCooperativeMatrixQuery(VkResult result, const char* message){
        if(result != VK_SUCCESS && result != VK_INCOMPLETE){
            throw std::runtime_error(message);
        }
    }

    std::vector<VkCooperativeMatrixPropertiesKHR> queryCooperativeMatrixTiles(
        vk::Instance instance,
        vk::PhysicalDevice physicalDevice){
        auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR"));
        if(!query){
            return {};
        }

        const VkPhysicalDevice rawPhysicalDevice = physicalDevice;
        std::uint32_t count = 0;
        checkCooperativeMatrixQuery(query(rawPhysicalDevice, &count, nullptr),
                                    "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR failed.");
        std::vector<VkCooperativeMatrixPropertiesKHR> properties;

        VkResult result = VK_INCOMPLETE;
        while(result == VK_INCOMPLETE){
            properties.assign(count, {});
            for(auto& property : properties){
                property.sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
            }

            result = query(rawPhysicalDevice, &count, properties.data());
            checkCooperativeMatrixQuery(result,
                                        "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR failed.");
        }

        properties.resize(count);
        return properties;
    }

    socl::CooperativeMatrixSupportInfo makeCooperativeMatrixSupportInfo(
        vk::Instance instance,
        vk::PhysicalDevice physicalDevice,
        const std::vector<std::string>& extensionNames){
        socl::CooperativeMatrixSupportInfo info;
        info.extensionSupported = containsExtension(extensionNames, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);
        if(!info.extensionSupported){
            return info;
        }

        VkPhysicalDeviceCooperativeMatrixFeaturesKHR features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

        info.featureSupported = features.cooperativeMatrix == VK_TRUE;
        info.robustBufferAccessSupported =
            features.cooperativeMatrixRobustBufferAccess == VK_TRUE;
        if(!info.featureSupported){
            return info;
        }

        VkPhysicalDeviceCooperativeMatrixPropertiesKHR properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &properties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
        info.supportedStages = vk::ShaderStageFlags(properties.cooperativeMatrixSupportedStages);

        const auto rawTiles = queryCooperativeMatrixTiles(instance, physicalDevice);
        info.tiles.reserve(rawTiles.size());
        for(const auto& rawTile : rawTiles){
            info.tiles.push_back({
                .m = rawTile.MSize,
                .n = rawTile.NSize,
                .k = rawTile.KSize,
                .aType = vk::ComponentTypeKHR(rawTile.AType),
                .bType = vk::ComponentTypeKHR(rawTile.BType),
                .cType = vk::ComponentTypeKHR(rawTile.CType),
                .resultType = vk::ComponentTypeKHR(rawTile.ResultType),
                .saturatingAccumulation = rawTile.saturatingAccumulation == VK_TRUE,
                .scope = vk::ScopeKHR(rawTile.scope),
            });
        }

        return info;
    }

    socl::SubgroupSupportInfo makeSubgroupSupportInfo(
        vk::PhysicalDevice physicalDevice,
        bool sizeControlAvailable){
        VkPhysicalDeviceSubgroupSizeControlProperties sizeControlProperties{};
        sizeControlProperties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;

        VkPhysicalDeviceSubgroupProperties subgroupProperties{};
        subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        subgroupProperties.pNext = sizeControlAvailable ? &sizeControlProperties : nullptr;

        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &subgroupProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

        socl::SubgroupSupportInfo info;
        info.defaultSize = subgroupProperties.subgroupSize;
        info.supportedStages = vk::ShaderStageFlags(subgroupProperties.supportedStages);
        info.supportedOperations =
            vk::SubgroupFeatureFlags(subgroupProperties.supportedOperations);
        if(!sizeControlAvailable){
            return info;
        }

        VkPhysicalDeviceSubgroupSizeControlFeatures sizeControlFeatures{};
        sizeControlFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &sizeControlFeatures;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

        info.requiredSizeStages =
            vk::ShaderStageFlags(sizeControlProperties.requiredSubgroupSizeStages);
        info.sizeControlSupported = sizeControlFeatures.subgroupSizeControl == VK_TRUE;
        info.computeFullSubgroupsSupported =
            sizeControlFeatures.computeFullSubgroups == VK_TRUE;
        info.minSize = sizeControlProperties.minSubgroupSize;
        info.maxSize = sizeControlProperties.maxSubgroupSize;
        return info;
    }

    std::uint32_t findComputeQueueFamily(vk::PhysicalDevice physicalDevice){
        const auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        for(std::uint32_t i = 0; i < queueFamilies.size(); ++i){
            if(queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute){
                return i;
            }
        }
        throw std::runtime_error("Physical device does not expose a compute queue.");
    }

    bool hasDeviceLocalHostVisibleMemory(vk::PhysicalDevice physicalDevice){
        const auto memoryProperties = physicalDevice.getMemoryProperties();
        for(std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i){
            const auto flags = memoryProperties.memoryTypes[i].propertyFlags;
            if((flags & vk::MemoryPropertyFlagBits::eDeviceLocal) &&
               (flags & vk::MemoryPropertyFlagBits::eHostVisible)){
                return true;
            }
        }
        return false;
    }

    socl::GpuInfo makeGpuInfo(vk::PhysicalDevice physicalDevice,
                              std::uint32_t index,
                              std::uint32_t computeQueueFamily){
        const auto properties = physicalDevice.getProperties();
        const auto queueFamilies = physicalDevice.getQueueFamilyProperties();

        socl::GpuInfo info;
        info.index = index;
        info.name = properties.deviceName.data();
        info.type = physicalDeviceTypeName(properties.deviceType);
        info.vendorId = properties.vendorID;
        info.deviceId = properties.deviceID;
        info.apiVersion = properties.apiVersion;
        info.driverVersion = properties.driverVersion;
        info.computeQueueFamily = computeQueueFamily;
        info.computeQueueCount = queueFamilies[computeQueueFamily].queueCount;
        return info;
    }

    vkb::PhysicalDeviceSelector makePhysicalDeviceSelector(
        const vkb::Instance& instance,
        const std::vector<const char*>& requiredDeviceExtensions){
        vk::PhysicalDeviceVulkan11Features required11{};
        vkb::PhysicalDeviceSelector selector(instance);
        selector
            .set_minimum_version(1, 1)
            .set_required_features_11(required11)
            .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
            .require_present(false);

        if(!requiredDeviceExtensions.empty()){
            selector.add_required_extensions(requiredDeviceExtensions);
        }
        return selector;
    }
}

namespace socl{
    namespace detail{
        ContextState::~ContextState(){
            if(device){
                device.waitIdle();
                if(allocator != VK_NULL_HANDLE){
                    vmaDestroyAllocator(allocator);
                }
                if(commandPool){
                    device.destroyCommandPool(commandPool);
                }
                device.destroy();
            }
            if(instance){
                instance.destroy();
            }
        }
    }

    Context::Context()
        : Context(ContextCreateInfo{}){
    }

    Context::Context(const ContextCreateInfo& createInfo)
        : state_(std::make_shared<detail::ContextState>()){
        auto vkbInstance = createVkbInstance();
        state_->instance = vk::Instance(vkbInstance.instance);

        auto selector = makePhysicalDeviceSelector(vkbInstance,
                                                   createInfo.requiredDeviceExtensions);
        auto vkbPhysicalDevices = unwrap(
            selector.select_devices(),
            "vk-bootstrap physical device selection failed");
        if(createInfo.physicalDeviceIndex >= vkbPhysicalDevices.size()){
            throw std::out_of_range("ContextCreateInfo::physicalDeviceIndex is out of range.");
        }

        auto vkbPhysicalDevice = std::move(vkbPhysicalDevices[createInfo.physicalDeviceIndex]);
        vkbPhysicalDevice.enable_extension_if_present(
            VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
        state_->physicalDevice = vk::PhysicalDevice(vkbPhysicalDevice.physical_device);
        state_->physicalDeviceProperties = state_->physicalDevice.getProperties();
        state_->supportedDeviceExtensions = enumerateDeviceExtensionNames(state_->physicalDevice);

        const bool subgroupSizeControlAvailable =
            VK_VERSION_MAJOR(state_->physicalDeviceProperties.apiVersion) > 1 ||
            (VK_VERSION_MAJOR(state_->physicalDeviceProperties.apiVersion) == 1 &&
             VK_VERSION_MINOR(state_->physicalDeviceProperties.apiVersion) >= 3) ||
            containsExtension(state_->supportedDeviceExtensions,
                              VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
        state_->subgroupSupportInfo =
            makeSubgroupSupportInfo(state_->physicalDevice, subgroupSizeControlAvailable);

        VkPhysicalDeviceSubgroupSizeControlFeatures enabledSubgroupFeatures{};
        enabledSubgroupFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;
        enabledSubgroupFeatures.subgroupSizeControl =
            state_->subgroupSupportInfo.sizeControlSupported ? VK_TRUE : VK_FALSE;
        enabledSubgroupFeatures.computeFullSubgroups =
            state_->subgroupSupportInfo.computeFullSubgroupsSupported ? VK_TRUE : VK_FALSE;

        vkb::DeviceBuilder deviceBuilder(vkbPhysicalDevice);
        if(subgroupSizeControlAvailable){
            deviceBuilder.add_pNext(&enabledSubgroupFeatures);
        }
        auto vkbDevice = unwrap(deviceBuilder.build(), "vk-bootstrap device creation failed");

        state_->subgroupSupportInfo.sizeControlEnabled =
            enabledSubgroupFeatures.subgroupSizeControl == VK_TRUE;
        state_->subgroupSupportInfo.computeFullSubgroupsEnabled =
            enabledSubgroupFeatures.computeFullSubgroups == VK_TRUE;
        state_->cooperativeMatrixSupportInfo =
            makeCooperativeMatrixSupportInfo(state_->instance,
                                             state_->physicalDevice,
                                             state_->supportedDeviceExtensions);
        state_->device = vk::Device(vkbDevice.device);
        state_->queueFamily = findComputeQueueFamily(state_->physicalDevice);
        state_->queue = state_->device.getQueue(state_->queueFamily, 0);
        state_->autoBufferUsesHostVisibleMemory =
            state_->physicalDeviceProperties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu &&
            hasDeviceLocalHostVisibleMemory(state_->physicalDevice);
        state_->gpuInfo = makeGpuInfo(state_->physicalDevice,
                                      createInfo.physicalDeviceIndex,
                                      state_->queueFamily);

        const VmaAllocatorCreateInfo allocatorInfo{
            .physicalDevice = state_->physicalDevice,
            .device = state_->device,
            .instance = state_->instance,
            .vulkanApiVersion = VK_API_VERSION_1_1,
        };
        checkVk(vmaCreateAllocator(&allocatorInfo, &state_->allocator),
                "vmaCreateAllocator failed.");

        vk::CommandPoolCreateInfo commandPoolInfo;
        commandPoolInfo
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
            .setQueueFamilyIndex(state_->queueFamily);
        state_->commandPool = state_->device.createCommandPool(commandPoolInfo);
    }

    Context::~Context() = default;

    Buffer Context::createBuffer(std::size_t bytes, BufferType type){
        if(bytes == 0){
            throw std::runtime_error("Cannot create a zero-sized socl::Buffer.");
        }

        auto state = std::make_shared<detail::BufferState>();
        state->context = state_;
        state->allocator = state_->allocator;
        state->size = static_cast<vk::DeviceSize>(bytes);

        state->type = type == BufferType::Auto
            ? (state_->autoBufferUsesHostVisibleMemory ? BufferType::HostVisible : BufferType::DeviceLocal)
            : type;

        vk::BufferCreateInfo bufferInfo;
        bufferInfo
            .setSize(state->size)
            .setUsage(vk::BufferUsageFlagBits::eStorageBuffer |
                      vk::BufferUsageFlagBits::eUniformBuffer |
                      vk::BufferUsageFlagBits::eTransferSrc |
                      vk::BufferUsageFlagBits::eTransferDst)
            .setSharingMode(vk::SharingMode::eExclusive);
        const VmaAllocationCreateInfo allocationInfo{
            .flags = toVmaAllocationFlags(state->type),
            .usage = toVmaMemoryUsage(state->type),
        };

        VkBuffer rawBuffer = VK_NULL_HANDLE;
        checkVk(vmaCreateBuffer(state_->allocator,
                                reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
                                &allocationInfo,
                                &rawBuffer,
                                &state->allocation,
                                &state->allocationInfo),
                "vmaCreateBuffer failed.");

        state->buffer = vk::Buffer(rawBuffer);
        state->hostVisible = (state->allocationInfo.pMappedData != nullptr) ||
                             state->type == BufferType::HostVisible;
        return Buffer(std::move(state));
    }

    ShaderPipeline Context::createShaderPipeline(const ShaderPipelineCreateInfo& createInfo){
        if(createInfo.spirv.empty()){
            throw std::runtime_error("ShaderPipeline SPIR-V bytecode is empty.");
        }
        if(createInfo.requiredSubgroupSize){
            const auto requestedSize = *createInfo.requiredSubgroupSize;
            const auto& subgroup = state_->subgroupSupportInfo;
            if(!subgroup.sizeControlEnabled){
                throw std::runtime_error(
                    "A required subgroup size was requested, but subgroup size control is not enabled.");
            }
            if(!(subgroup.requiredSizeStages & vk::ShaderStageFlagBits::eCompute)){
                throw std::runtime_error(
                    "Required subgroup sizes are not supported for compute shaders.");
            }
            if(requestedSize == 0 || (requestedSize & (requestedSize - 1)) != 0){
                throw std::runtime_error("The required subgroup size must be a power of two.");
            }
            if(requestedSize < subgroup.minSize || requestedSize > subgroup.maxSize){
                throw std::runtime_error(
                    "The required subgroup size is outside the GPU's supported range.");
            }
        }

        auto state = std::make_shared<detail::ShaderPipelineState>();
        state->context = state_;
        state->device = state_->device;
        state->bindings = createInfo.bindings;
        state->pushConstantSize = createInfo.pushConstantSize;

        vk::ShaderModuleCreateInfo shaderModuleInfo;
        shaderModuleInfo
            .setCodeSize(createInfo.spirv.size_bytes())
            .setPCode(createInfo.spirv.data());
        state->shaderModule = state->device.createShaderModule(shaderModuleInfo);

        std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
        layoutBindings.reserve(state->bindings.size());
        for(const auto& binding : state->bindings){
            vk::DescriptorSetLayoutBinding layoutBinding;
            layoutBinding
                .setBinding(binding.binding)
                .setDescriptorType(toVulkanDescriptorType(binding.type))
                .setDescriptorCount(1)
                .setStageFlags(vk::ShaderStageFlagBits::eCompute);
            layoutBindings.push_back(layoutBinding);
        }

        vk::DescriptorSetLayoutCreateInfo setLayoutInfo;
        setLayoutInfo
            .setBindingCount(static_cast<std::uint32_t>(layoutBindings.size()))
            .setPBindings(layoutBindings.data());
        state->descriptorSetLayout = state->device.createDescriptorSetLayout(setLayoutInfo);

        vk::PushConstantRange pushRange;
        pushRange
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(state->pushConstantSize);
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo
            .setSetLayoutCount(1)
            .setPSetLayouts(&state->descriptorSetLayout)
            .setPushConstantRangeCount(state->pushConstantSize > 0 ? 1u : 0u)
            .setPPushConstantRanges(state->pushConstantSize > 0 ? &pushRange : nullptr);
        state->pipelineLayout = state->device.createPipelineLayout(pipelineLayoutInfo);

        std::vector<vk::SpecializationMapEntry> specializationEntries;
        std::vector<std::byte> specializationData;
        specializationEntries.reserve(createInfo.specConstants.size());
        for(const auto& specConstant : createInfo.specConstants){
            const auto offset = static_cast<std::uint32_t>(specializationData.size());
            specializationData.insert(specializationData.end(), specConstant.data.begin(), specConstant.data.end());

            vk::SpecializationMapEntry entry;
            entry
                .setConstantID(specConstant.id)
                .setOffset(offset)
                .setSize(specConstant.data.size());
            specializationEntries.push_back(entry);
        }

        vk::SpecializationInfo specializationInfo;
        specializationInfo
            .setMapEntryCount(static_cast<std::uint32_t>(specializationEntries.size()))
            .setPMapEntries(specializationEntries.data())
            .setDataSize(specializationData.size())
            .setPData(specializationData.data());
        vk::PipelineShaderStageCreateInfo stageInfo;
        stageInfo
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(state->shaderModule)
            .setPName(createInfo.entryPoint ? createInfo.entryPoint : "main")
            .setPSpecializationInfo(specializationEntries.empty() ? nullptr : &specializationInfo);
        vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo requiredSubgroupSizeInfo;
        if(createInfo.requiredSubgroupSize){
            requiredSubgroupSizeInfo.setRequiredSubgroupSize(*createInfo.requiredSubgroupSize);
            stageInfo.setPNext(&requiredSubgroupSizeInfo);
        }

        vk::ComputePipelineCreateInfo pipelineInfo;
        pipelineInfo
            .setStage(stageInfo)
            .setLayout(state->pipelineLayout);
        auto pipelineResult = state->device.createComputePipeline({}, pipelineInfo);
        if(pipelineResult.result != vk::Result::eSuccess){
            throw std::runtime_error("createComputePipeline failed.");
        }
        state->pipeline = pipelineResult.value;

        return ShaderPipeline(std::move(state));
    }

    DescriptorSet Context::createDescriptorSet(const ShaderPipeline& pipeline){
        if(!pipeline.state_){
            throw std::runtime_error("Cannot create a DescriptorSet for an empty ShaderPipeline.");
        }

        auto state = std::make_shared<detail::DescriptorSetState>();
        state->context = state_;
        state->pipeline = pipeline.state_;
        state->device = state_->device;
        state->buffers.resize(pipeline.state_->bindings.size());

        std::vector<vk::DescriptorPoolSize> poolSizes;
        poolSizes.reserve(pipeline.state_->bindings.size());
        for(const auto& binding : pipeline.state_->bindings){
            vk::DescriptorPoolSize poolSize;
            poolSize
                .setType(toVulkanDescriptorType(binding.type))
                .setDescriptorCount(1);
            poolSizes.push_back(poolSize);
        }

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo
            .setMaxSets(1)
            .setPoolSizeCount(static_cast<std::uint32_t>(poolSizes.size()))
            .setPPoolSizes(poolSizes.data());
        state->descriptorPool = state->device.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo descriptorAllocateInfo;
        descriptorAllocateInfo
            .setDescriptorPool(state->descriptorPool)
            .setDescriptorSetCount(1)
            .setPSetLayouts(&pipeline.state_->descriptorSetLayout);
        state->descriptorSet = state->device.allocateDescriptorSets(descriptorAllocateInfo).front();

        return DescriptorSet(std::move(state));
    }

    void Context::begin(){
        if(state_->recording){
            throw std::runtime_error("Context is already recording commands.");
        }

        vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
        commandBufferAllocateInfo
            .setCommandPool(state_->commandPool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1);
        state_->recordingCommandBuffer = state_->device.allocateCommandBuffers(commandBufferAllocateInfo).front();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        state_->recordingCommandBuffer.begin(beginInfo);
        state_->recording = true;
        state_->currentPipeline.reset();
        state_->currentDescriptorSet.reset();
    }

    void Context::use(const ShaderPipeline& pipeline){
        if(!state_->recording){
            throw std::runtime_error("Call Context::begin() before Context::use().");
        }
        if(!pipeline.state_){
            throw std::runtime_error("Cannot use an empty ShaderPipeline.");
        }

        state_->recordingCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute,
                                                    pipeline.state_->pipeline);
        state_->currentPipeline = pipeline.state_;
    }

    void Context::bind(const DescriptorSet& descriptorSet){
        if(!state_->recording){
            throw std::runtime_error("Call Context::begin() before Context::bind().");
        }
        if(!descriptorSet.state_){
            throw std::runtime_error("Cannot bind an empty DescriptorSet.");
        }
        if(!state_->currentPipeline){
            use(ShaderPipeline(descriptorSet.state_->pipeline));
        }
        if(state_->currentPipeline != descriptorSet.state_->pipeline){
            throw std::runtime_error("DescriptorSet was created for a different ShaderPipeline.");
        }

        state_->recordingCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                                          state_->currentPipeline->pipelineLayout,
                                                          0,
                                                          descriptorSet.state_->descriptorSet,
                                                          {});
        state_->currentDescriptorSet = descriptorSet.state_;
    }

    void Context::push(const void* data, std::size_t bytes, std::size_t offset){
        if(!state_->recording){
            throw std::runtime_error("Call Context::begin() before Context::push().");
        }
        if(!state_->currentPipeline){
            throw std::runtime_error("Call Context::use() before Context::push().");
        }
        if(offset + bytes > state_->currentPipeline->pushConstantSize){
            throw std::runtime_error("Push constant range exceeds ShaderPipeline pushConstantSize.");
        }
        if(bytes == 0){
            return;
        }

        state_->recordingCommandBuffer.pushConstants(state_->currentPipeline->pipelineLayout,
                                                     vk::ShaderStageFlagBits::eCompute,
                                                     static_cast<std::uint32_t>(offset),
                                                     static_cast<std::uint32_t>(bytes),
                                                     data);
    }

    void Context::dispatch(std::uint32_t groupCountX,
                           std::uint32_t groupCountY,
                           std::uint32_t groupCountZ){
        if(!state_->recording){
            throw std::runtime_error("Call Context::begin() before Context::dispatch().");
        }
        if(!state_->currentPipeline){
            throw std::runtime_error("Call Context::use() before Context::dispatch().");
        }
        if(!state_->currentDescriptorSet && !state_->currentPipeline->bindings.empty()){
            throw std::runtime_error("Call Context::bind() before Context::dispatch().");
        }
        state_->recordingCommandBuffer.dispatch(groupCountX, groupCountY, groupCountZ);
    }

    DispatchToken Context::submitAsync(){
        if(!state_->recording){
            throw std::runtime_error("No command buffer is currently recording.");
        }
        state_->recordingCommandBuffer.end();

        vk::Fence fence = state_->device.createFence({});
        vk::SubmitInfo submitInfo;
        submitInfo
            .setCommandBufferCount(1)
            .setPCommandBuffers(&state_->recordingCommandBuffer);

        const vk::CommandBuffer submitted = state_->recordingCommandBuffer;
        state_->queue.submit(submitInfo, fence);

        state_->recordingCommandBuffer = nullptr;
        state_->recording = false;
        state_->currentPipeline.reset();
        state_->currentDescriptorSet.reset();
        return DispatchToken(state_, fence, submitted);
    }

    void Context::submitAndWait(){
        DispatchToken token = submitAsync();
        token.wait();
    }

    bool Context::usingIntegratedGpu() const{
        return state_->physicalDeviceProperties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu;
    }

    const GpuInfo& Context::gpuInfo() const{
        return state_->gpuInfo;
    }

    std::vector<std::string> Context::supportedDeviceExtensions() const{
        return state_->supportedDeviceExtensions;
    }

    bool Context::supportsDeviceExtension(std::string_view extensionName) const{
        return containsExtension(state_->supportedDeviceExtensions, extensionName);
    }

    bool Context::supportsCooperativeMatrix() const{
        const auto& info = state_->cooperativeMatrixSupportInfo;
        return info.extensionSupported && info.featureSupported && !info.tiles.empty();
    }

    const CooperativeMatrixSupportInfo& Context::cooperativeMatrixSupportInfo() const{
        return state_->cooperativeMatrixSupportInfo;
    }

    const SubgroupSupportInfo& Context::subgroupSupportInfo() const{
        return state_->subgroupSupportInfo;
    }

    void Context::printGpuInfo(std::ostream& os) const{
        const auto& info = gpuInfo();
        os << "SOCL GPU " << info.index << '\n'
           << "  Name: " << info.name << '\n'
           << "  Type: " << info.type << '\n'
           << "  Vendor ID: 0x" << std::hex << info.vendorId << std::dec << '\n'
           << "  Device ID: 0x" << std::hex << info.deviceId << std::dec << '\n'
           << "  Vulkan API: "
           << VK_VERSION_MAJOR(info.apiVersion) << '.'
           << VK_VERSION_MINOR(info.apiVersion) << '.'
           << VK_VERSION_PATCH(info.apiVersion) << '\n'
           << "  Driver Version: " << info.driverVersion << '\n'
           << "  Compute Queue Family: " << info.computeQueueFamily << '\n'
           << "  Compute Queue Count: " << info.computeQueueCount << '\n'
           << "  Device Extensions: " << state_->supportedDeviceExtensions.size() << '\n'
           << "  Cooperative Matrix: "
           << (supportsCooperativeMatrix() ? "supported" : "not supported")
           << " (" << state_->cooperativeMatrixSupportInfo.tiles.size() << " tile combinations)\n";
    }

    std::vector<GpuInfo> Context::enumerateGpus(){
        auto vkbInstance = createVkbInstance();
        vk::Instance instance = vk::Instance(vkbInstance.instance);
        std::vector<GpuInfo> gpus;
        try{
            auto selector = makePhysicalDeviceSelector(vkbInstance, {});
            auto physicalDevices = unwrap(
                selector.select_devices(),
                "vk-bootstrap physical device enumeration failed");
            gpus.reserve(physicalDevices.size());
            for(std::uint32_t i = 0; i < physicalDevices.size(); ++i){
                auto device = vk::PhysicalDevice(physicalDevices[i].physical_device);
                gpus.push_back(makeGpuInfo(device, i, findComputeQueueFamily(device)));
            }
        }catch(...){
            instance.destroy();
            throw;
        }
        instance.destroy();
        return gpus;
    }

    DispatchToken::DispatchToken() = default;

    DispatchToken::~DispatchToken(){
        if(valid()){
            wait();
        }
    }

    DispatchToken::DispatchToken(DispatchToken&& other) noexcept
        : context_(std::move(other.context_)),
          fence_(std::exchange(other.fence_, nullptr)),
          commandBuffer_(std::exchange(other.commandBuffer_, nullptr)){
    }

    DispatchToken& DispatchToken::operator=(DispatchToken&& other) noexcept{
        if(this != &other){
            if(valid()){
                wait();
            }
            context_ = std::move(other.context_);
            fence_ = std::exchange(other.fence_, nullptr);
            commandBuffer_ = std::exchange(other.commandBuffer_, nullptr);
        }
        return *this;
    }

    DispatchToken::DispatchToken(std::shared_ptr<detail::ContextState> context,
                                 vk::Fence fence,
                                 vk::CommandBuffer commandBuffer)
        : context_(std::move(context)),
          fence_(fence),
          commandBuffer_(commandBuffer){
    }

    void DispatchToken::wait(){
        if(!valid()){
            return;
        }

        auto result = context_->device.waitForFences(fence_, vk::True, UINT64_MAX);
        if(result != vk::Result::eSuccess){
            throw std::runtime_error("waitForFences failed.");
        }
        context_->device.destroyFence(fence_);
        context_->device.freeCommandBuffers(context_->commandPool, commandBuffer_);
        fence_ = nullptr;
        commandBuffer_ = nullptr;
        context_.reset();
    }

    bool DispatchToken::valid() const{
        return context_ && fence_ && commandBuffer_;
    }
}
