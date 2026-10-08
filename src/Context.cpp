#include <vucol/Context.hpp>

#include "DispatchResources.hpp"
#include "InternalState.hpp"

#include <VkBootstrap.h>

#include <algorithm>
#include <array>
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

    vkb::Instance createVkbInstance(){
        vkb::InstanceBuilder instanceBuilder;
        return unwrap(
            instanceBuilder
                .set_app_name("vucol")
                .set_engine_name("vucol")
                .require_api_version(1, 3, 0)
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

    vucol::CooperativeMatrixSupportInfo makeCooperativeMatrixSupportInfo(
        vk::Instance instance,
        vk::PhysicalDevice physicalDevice,
        const std::vector<std::string>& extensionNames){
        vucol::CooperativeMatrixSupportInfo info;
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

    vucol::SubgroupSupportInfo makeSubgroupSupportInfo(
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

        vucol::SubgroupSupportInfo info;
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

    vucol::GpuTimingSupportInfo makeGpuTimingSupportInfo(
        vk::PhysicalDevice physicalDevice,
        const vk::PhysicalDeviceProperties& physicalDeviceProperties,
        std::uint32_t queueFamily,
        bool synchronization2Supported,
        bool synchronization2Enabled){
        const auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        const std::uint32_t validBits = queueFamilies[queueFamily].timestampValidBits;
        const float timestampPeriod = physicalDeviceProperties.limits.timestampPeriod;

        vucol::GpuTimingSupportInfo info;
        info.synchronization2Supported = synchronization2Supported;
        info.synchronization2Enabled = synchronization2Enabled;
        info.timestampSupported = validBits != 0 && timestampPeriod > 0.0f;
        info.timestampValidBits = validBits;
        info.timestampPeriodNanoseconds = timestampPeriod;
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

    vucol::GpuInfo makeGpuInfo(vk::PhysicalDevice physicalDevice,
                              std::uint32_t index,
                              std::uint32_t computeQueueFamily){
        const auto properties = physicalDevice.getProperties();
        const auto queueFamilies = physicalDevice.getQueueFamilyProperties();

        vucol::GpuInfo info;
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
            .set_minimum_version(1, 3)
            .set_required_features_11(required11)
            .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
            .require_present(false);

        if(!requiredDeviceExtensions.empty()){
            selector.add_required_extensions(requiredDeviceExtensions);
        }
        return selector;
    }
}

namespace vucol{
    namespace detail{
        ContextState::~ContextState(){
            if(device){
                device.waitIdle();
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
        auto selectedIndices = createInfo.physicalDeviceIndices;
        if(selectedIndices.empty()){
            selectedIndices.push_back(0);
        }
        for(const auto index : selectedIndices){
            if(index >= vkbPhysicalDevices.size()){
                throw std::out_of_range(
                    "ContextCreateInfo::physicalDeviceIndices is out of range.");
            }
        }

        std::vector<VkPhysicalDevice> selectedDevices;
        selectedDevices.reserve(selectedIndices.size());
        for(const auto index : selectedIndices){
            selectedDevices.push_back(vkbPhysicalDevices[index].physical_device);
        }
        if(selectedDevices.size() > VK_MAX_DEVICE_GROUP_SIZE){
            throw std::runtime_error("Too many GPUs for one Vulkan device group.");
        }

        const auto deviceGroups = state_->instance.enumeratePhysicalDeviceGroups();
        const bool inOneGroup = std::any_of(
            deviceGroups.begin(),
            deviceGroups.end(),
            [&](const vk::PhysicalDeviceGroupProperties& group){
                if(group.physicalDeviceCount < selectedDevices.size()){
                    return false;
                }
                return std::all_of(
                    selectedDevices.begin(),
                    selectedDevices.end(),
                    [&](VkPhysicalDevice selected){
                        return std::any_of(
                            group.physicalDevices.begin(),
                            group.physicalDevices.begin() + group.physicalDeviceCount,
                            [&](vk::PhysicalDevice grouped){
                                return static_cast<VkPhysicalDevice>(grouped) == selected;
                            });
                    });
            });
        if(!inOneGroup){
            throw std::runtime_error(
                "The selected GPUs do not form a single Vulkan device group.");
        }

        const std::uint32_t deviceCount =
            static_cast<std::uint32_t>(selectedDevices.size());
        state_->physicalDevices.reserve(deviceCount);
        for(const auto rawDevice : selectedDevices){
            state_->physicalDevices.push_back(vk::PhysicalDevice(rawDevice));
        }

        state_->physicalDeviceProperties.reserve(deviceCount);
        state_->supportedDeviceExtensions.reserve(deviceCount);
        std::vector<bool> synchronization2Supported(deviceCount, false);
        std::vector<bool> subgroupSizeControlAvailable(deviceCount, false);
        for(std::uint32_t i = 0; i < deviceCount; ++i){
            const auto physicalDevice = state_->physicalDevices[i];
            state_->physicalDeviceProperties.push_back(physicalDevice.getProperties());
            state_->supportedDeviceExtensions.push_back(
                enumerateDeviceExtensionNames(physicalDevice));

            VkPhysicalDeviceSynchronization2Features synchronization2Query{};
            synchronization2Query.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
            VkPhysicalDeviceFeatures2 featuresQuery{};
            featuresQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            featuresQuery.pNext = &synchronization2Query;
            vkGetPhysicalDeviceFeatures2(physicalDevice, &featuresQuery);
            synchronization2Supported[i] = synchronization2Query.synchronization2 == VK_TRUE;

            const auto& properties = state_->physicalDeviceProperties.back();
            subgroupSizeControlAvailable[i] =
                VK_VERSION_MAJOR(properties.apiVersion) > 1 ||
                (VK_VERSION_MAJOR(properties.apiVersion) == 1 &&
                 VK_VERSION_MINOR(properties.apiVersion) >= 3) ||
                containsExtension(state_->supportedDeviceExtensions.back(),
                                  VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
        }

        state_->queueFamily = findComputeQueueFamily(state_->physicalDevices[0]);
        for(std::uint32_t i = 1; i < deviceCount; ++i){
            const auto families = state_->physicalDevices[i].getQueueFamilyProperties();
            if(state_->queueFamily >= families.size() ||
               !(families[state_->queueFamily].queueFlags & vk::QueueFlagBits::eCompute)){
                throw std::runtime_error(
                    "Grouped GPUs expose compute queues at different family indices.");
            }
        }

        const bool synchronization2Enabled = std::all_of(synchronization2Supported.begin(),
                                                          synchronization2Supported.end(),
                                                          [](bool supported){
                                                              return supported;
                                                          });
        bool subgroupExtensionOnAll = true;
        bool subgroupControlSupportedOnAll = true;
        bool computeFullSubgroupsSupportedOnAll = true;
        bool subgroupAvailableOnAll = true;
        state_->subgroupSupportInfos.reserve(deviceCount);
        for(std::uint32_t i = 0; i < deviceCount; ++i){
            if(!containsExtension(state_->supportedDeviceExtensions[i],
                                  VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME)){
                subgroupExtensionOnAll = false;
            }
            if(!subgroupSizeControlAvailable[i]){
                subgroupAvailableOnAll = false;
            }
            state_->subgroupSupportInfos.push_back(
                makeSubgroupSupportInfo(state_->physicalDevices[i],
                                        subgroupSizeControlAvailable[i]));
            subgroupControlSupportedOnAll =
                subgroupControlSupportedOnAll &&
                state_->subgroupSupportInfos.back().sizeControlSupported;
            computeFullSubgroupsSupportedOnAll =
                computeFullSubgroupsSupportedOnAll &&
                state_->subgroupSupportInfos.back().computeFullSubgroupsSupported;
        }

        std::vector<const char*> enabledExtensions(createInfo.requiredDeviceExtensions.begin(),
                                                   createInfo.requiredDeviceExtensions.end());
        if(subgroupExtensionOnAll){
            enabledExtensions.push_back(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
        }

        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = state_->queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceGroupDeviceCreateInfo groupCreateInfo{};
        groupCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO;
        groupCreateInfo.physicalDeviceCount = deviceCount;
        groupCreateInfo.pPhysicalDevices = selectedDevices.data();

        VkPhysicalDeviceSynchronization2Features synchronization2Features{};
        synchronization2Features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        synchronization2Features.synchronization2 =
            synchronization2Enabled ? VK_TRUE : VK_FALSE;

        VkPhysicalDeviceSubgroupSizeControlFeatures subgroupFeatures{};
        subgroupFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;
        subgroupFeatures.subgroupSizeControl =
            subgroupControlSupportedOnAll ? VK_TRUE : VK_FALSE;
        subgroupFeatures.computeFullSubgroups =
            computeFullSubgroupsSupportedOnAll ? VK_TRUE : VK_FALSE;

        groupCreateInfo.pNext = &synchronization2Features;
        if(subgroupAvailableOnAll){
            synchronization2Features.pNext = &subgroupFeatures;
        }

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = &groupCreateInfo;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount =
            static_cast<std::uint32_t>(enabledExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();

        VkDevice rawDevice = VK_NULL_HANDLE;
        checkVk(vkCreateDevice(state_->instance, &deviceCreateInfo, nullptr, &rawDevice),
                "vkCreateDevice failed for the device group.");
        state_->device = vk::Device(rawDevice);
        state_->queue = state_->device.getQueue(state_->queueFamily, 0);

        state_->cooperativeMatrixSupportInfos.reserve(deviceCount);
        state_->gpuTimingSupportInfos.reserve(deviceCount);
        state_->autoBufferUsesHostVisibleMemory.reserve(deviceCount);
        state_->gpuInfos.reserve(deviceCount);
        for(std::uint32_t i = 0; i < deviceCount; ++i){
            const auto physicalDevice = state_->physicalDevices[i];
            state_->subgroupSupportInfos[i].sizeControlEnabled =
                subgroupControlSupportedOnAll;
            state_->subgroupSupportInfos[i].computeFullSubgroupsEnabled =
                computeFullSubgroupsSupportedOnAll;
            state_->cooperativeMatrixSupportInfos.push_back(
                makeCooperativeMatrixSupportInfo(state_->instance,
                                                physicalDevice,
                                                state_->supportedDeviceExtensions[i]));
            state_->gpuTimingSupportInfos.push_back(
                makeGpuTimingSupportInfo(physicalDevice,
                                         state_->physicalDeviceProperties[i],
                                         state_->queueFamily,
                                         synchronization2Supported[i],
                                         synchronization2Enabled));
            state_->autoBufferUsesHostVisibleMemory.push_back(
                state_->physicalDeviceProperties[i].deviceType ==
                    vk::PhysicalDeviceType::eIntegratedGpu &&
                hasDeviceLocalHostVisibleMemory(physicalDevice));
            state_->gpuInfos.push_back(
                makeGpuInfo(physicalDevice, selectedIndices[i], state_->queueFamily));
        }

        vk::CommandPoolCreateInfo commandPoolInfo;
        commandPoolInfo
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
            .setQueueFamilyIndex(state_->queueFamily);
        state_->commandPool = state_->device.createCommandPool(commandPoolInfo);
    }

    Context::~Context(){
        recordingResources_.reset();
        if(state_){
            state_->currentDescriptorSet.reset();
            state_->currentPipeline.reset();
        }
    }

    Buffer Context::createBuffer(std::size_t bytes,
                                  BufferType type,
                                  std::uint32_t deviceIndex,
                                  BufferPeerAccess peerAccess){
        if(bytes == 0){
            throw std::runtime_error("Cannot create a zero-sized vucol::Buffer.");
        }
        if(deviceIndex >= state_->deviceCount()){
            throw std::out_of_range("createBuffer deviceIndex is out of range.");
        }

        auto state = std::make_shared<detail::BufferState>();
        state->context = state_;
        state->size = static_cast<vk::DeviceSize>(bytes);
        state->ownerDeviceIndex = deviceIndex;
        state->peerAccess = peerAccess;

        state->type = type == BufferType::Auto
            ? (state_->autoBufferUsesHostVisibleMemory[deviceIndex]
                   ? BufferType::HostVisible
                   : BufferType::DeviceLocal)
            : type;

        auto allocation = detail::allocateDeviceBuffer(
            state_,
            state->size,
            vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eUniformBuffer |
                vk::BufferUsageFlagBits::eTransferSrc |
                vk::BufferUsageFlagBits::eTransferDst,
            state->type == BufferType::HostVisible,
            deviceIndex,
            peerAccess);
        state->buffer = allocation.buffer;
        state->memory = allocation.memory;
        state->mapped = allocation.mapped;
        state->allocationSize = allocation.allocationSize;
        state->memoryTypeIndex = allocation.memoryTypeIndex;
        state->heapIndex = allocation.heapIndex;
        state->hostVisible = allocation.hostVisible;
        state->coherent = allocation.coherent;
        return Buffer(std::move(state));
    }

    ShaderPipeline Context::createShaderPipeline(const ShaderPipelineCreateInfo& createInfo){
        if(createInfo.spirv.empty()){
            throw std::runtime_error("ShaderPipeline SPIR-V bytecode is empty.");
        }
        if(createInfo.requiredSubgroupSize){
            const auto requestedSize = *createInfo.requiredSubgroupSize;
            // The pipeline is shared across the group, so the request must satisfy
            // every device in it.
            for(std::uint32_t i = 0; i < state_->deviceCount(); ++i){
                const auto& subgroup = state_->subgroupSupportInfos[i];
                if(!subgroup.sizeControlEnabled){
                    throw std::runtime_error(
                        "A required subgroup size was requested, but subgroup size control is not enabled.");
                }
#if defined(__APPLE__)
                // MoltenVK reports subgroup size control as supported while
                // advertising empty requiredSubgroupSizeStages, even though the
                // fixed Metal SIMD width already matches common requests (e.g. 32
                // on Apple Silicon). Skip the stage check on macOS and let
                // createComputePipeline validate the request instead.
#else
                if(!(subgroup.requiredSizeStages & vk::ShaderStageFlagBits::eCompute)){
                    throw std::runtime_error(
                        "Required subgroup sizes are not supported for compute shaders.");
                }
#endif
                if(requestedSize == 0 || (requestedSize & (requestedSize - 1)) != 0){
                    throw std::runtime_error("The required subgroup size must be a power of two.");
                }
                if(requestedSize < subgroup.minSize || requestedSize > subgroup.maxSize){
                    throw std::runtime_error(
                        "The required subgroup size is outside the GPU's supported range.");
                }
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
#if defined(__APPLE__)
        // MoltenVK advertises empty requiredSubgroupSizeStages, so chaining a
        // required subgroup size is invalid usage and createComputePipeline may
        // reject it. The Metal SIMD width is fixed anyway (32 on Apple Silicon,
        // matching the usual request), so drop the requirement here and rely
        // on the default subgroup size.
#else
        vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo requiredSubgroupSizeInfo;
        if(createInfo.requiredSubgroupSize){
            requiredSubgroupSizeInfo.setRequiredSubgroupSize(*createInfo.requiredSubgroupSize);
            stageInfo.setPNext(&requiredSubgroupSizeInfo);
        }
#endif

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

    std::size_t Context::bufferOffsetAlignment(DescriptorType type,
                                                       std::uint32_t deviceIndex) const{
        if(deviceIndex >= state_->deviceCount()){
            throw std::out_of_range("bufferOffsetAlignment deviceIndex is out of range.");
        }
        const auto& limits = state_->physicalDeviceProperties[deviceIndex].limits;
        const vk::DeviceSize alignment = type == DescriptorType::UniformBuffer
            ? limits.minUniformBufferOffsetAlignment
            : limits.minStorageBufferOffsetAlignment;
        return static_cast<std::size_t>(alignment);
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

        const std::uint32_t fullMask = detail::fullDeviceMask(state_->deviceCount());
        const VkDeviceGroupCommandBufferBeginInfo groupBeginInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .deviceMask = fullMask,
        };
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo
            .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
            .setPNext(&groupBeginInfo);
        state_->recordingCommandBuffer.begin(beginInfo);
        state_->recording = true;
        state_->currentPipeline.reset();
        state_->currentDescriptorSet.reset();
        recordingResources_ = std::make_shared<detail::DispatchResources>();
    }

    void Context::beginTimed(){
        if(state_->recording){
            throw std::runtime_error("Context is already recording commands.");
        }
        if(!supportsGpuTiming()){
            throw std::runtime_error(
                "GPU timing requires synchronization2 and timestamp support on the compute queue.");
        }

        // Timestamps are interpreted with device zero's counter. Batches spanning
        // several devices report an approximate duration.
        auto timing = std::make_shared<detail::GpuTimingState>();
        timing->device = state_->device;
        timing->timestampValidBits = state_->gpuTimingSupportInfos[0].timestampValidBits;
        timing->timestampPeriodNanoseconds =
            static_cast<double>(state_->gpuTimingSupportInfos[0].timestampPeriodNanoseconds);

        vk::QueryPoolCreateInfo queryPoolInfo;
        queryPoolInfo
            .setQueryType(vk::QueryType::eTimestamp)
            .setQueryCount(2);
        timing->queryPool = state_->device.createQueryPool(queryPoolInfo);

        begin();
        recordingResources_->gpuTiming = timing;
        state_->recordingCommandBuffer.resetQueryPool(timing->queryPool, 0, 2);
        state_->recordingCommandBuffer.writeTimestamp2(
            vk::PipelineStageFlagBits2::eTopOfPipe,
            timing->queryPool,
            0);
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
        if(state_->currentDescriptorSet &&
           state_->currentDescriptorSet->pipeline != pipeline.state_){
            state_->currentDescriptorSet.reset();
        }
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
        if(descriptorSet.state_->context != state_){
            throw std::runtime_error("DescriptorSet belongs to a different Context.");
        }
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
                           std::uint32_t groupCountZ,
                           std::uint32_t deviceIndex){
        if(deviceIndex >= state_->deviceCount()){
            throw std::out_of_range("dispatch deviceIndex is out of range.");
        }
        if(!state_->recording){
            throw std::runtime_error("Call Context::begin() before Context::dispatch().");
        }
        if(!state_->currentPipeline){
            throw std::runtime_error("Call Context::use() before Context::dispatch().");
        }
        if(!state_->currentDescriptorSet && !state_->currentPipeline->bindings.empty()){
            throw std::runtime_error("Call Context::bind() before Context::dispatch().");
        }
        if(!recordingResources_){
            throw std::runtime_error("The current command recording has no resource tracker.");
        }

        std::shared_ptr<detail::DescriptorSetSnapshotState> snapshot;
        std::vector<detail::DispatchBufferUse> dispatchBuffers;
        if(!state_->currentPipeline->bindings.empty()){
            if(state_->currentDescriptorSet->pipeline != state_->currentPipeline){
                throw std::runtime_error(
                    "The current DescriptorSet belongs to a different ShaderPipeline.");
            }
            snapshot = detail::createDescriptorSnapshot(state_->currentDescriptorSet);

            for(std::size_t i = 0; i < snapshot->buffers.size(); ++i){
                const auto& bound = snapshot->buffers[i];
                detail::requireDeviceAccess(bound.buffer,
                                            bound.access,
                                            deviceIndex,
                                            state_->currentPipeline->bindings[i].binding);
                dispatchBuffers.push_back({
                    bound.buffer,
                    bound.offset,
                    bound.size,
                    bound.access,
                    deviceIndex,
                });
            }
        }
        dispatchBuffers = detail::normalizeBufferUses(dispatchBuffers);

        for(const auto& use : dispatchBuffers){
            detail::trackBuffer(*recordingResources_, use.buffer);
        }

        if(std::find(recordingResources_->pipelines.begin(),
                     recordingResources_->pipelines.end(),
                     state_->currentPipeline) == recordingResources_->pipelines.end()){
            recordingResources_->pipelines.push_back(state_->currentPipeline);
        }
        if(snapshot){
            recordingResources_->descriptorSets.push_back(snapshot);
        }

        std::vector<vk::BufferMemoryBarrier> barriers;
        barriers.reserve(dispatchBuffers.size());
        // trackBuffer() seeds this batch with the last successfully submitted access
        // to every range. Later dispatches then replace only the ranges they touch.
        for(const auto& current : dispatchBuffers){
            for(const auto& previous : recordingResources_->lastBufferAccesses){
                if(!detail::rangesOverlap(previous, current) ||
                   (!detail::accessWrites(previous.access) &&
                    !detail::accessWrites(current.access))){
                    continue;
                }

                const vk::DeviceSize overlapOffset =
                    std::max(previous.offset, current.offset);
                const vk::DeviceSize overlapEnd = std::min(
                    previous.offset + previous.size,
                    current.offset + current.size);
                vk::BufferMemoryBarrier barrier;
                barrier
                    .setSrcAccessMask(detail::toVulkanAccess(previous.access))
                    .setDstAccessMask(detail::toVulkanAccess(current.access))
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setBuffer(current.buffer->buffer)
                    .setOffset(overlapOffset)
                    .setSize(overlapEnd - overlapOffset);
                barriers.push_back(barrier);
            }
        }
        // Switch the executing device before recording anything for this dispatch,
        // so barriers and the dispatch itself run on the selected device.
        state_->recordingCommandBuffer.setDeviceMask(detail::deviceBit(deviceIndex));
        if(!barriers.empty()){
            state_->recordingCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {},
                {},
                barriers,
                {});
        }

        if(snapshot){
            state_->recordingCommandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute,
                state_->currentPipeline->pipelineLayout,
                0,
                snapshot->descriptorSet,
                {});
        }
        state_->recordingCommandBuffer.dispatch(groupCountX, groupCountY, groupCountZ);
        detail::recordBufferAccesses(*recordingResources_, dispatchBuffers);
        recordingResources_->currentDevice = deviceIndex;
        recordingResources_->usedDeviceMask |= detail::deviceBit(deviceIndex);
    }

    DispatchToken Context::submitAsync(){
        if(!state_->recording){
            throw std::runtime_error("No command buffer is currently recording.");
        }
        auto accessCommits = detail::prepareBufferAccessCommits(*recordingResources_);
        if(recordingResources_ && recordingResources_->gpuTiming){
            state_->recordingCommandBuffer.writeTimestamp2(
                vk::PipelineStageFlagBits2::eBottomOfPipe,
                recordingResources_->gpuTiming->queryPool,
                1);
        }
        state_->recordingCommandBuffer.end();

        vk::Fence fence = state_->device.createFence({});
        std::uint32_t submitMask = recordingResources_
            ? recordingResources_->usedDeviceMask
            : 0;
        if(submitMask == 0){
            submitMask = detail::fullDeviceMask(state_->deviceCount());
        }
        const VkDeviceGroupSubmitInfo groupSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphoreDeviceIndices = nullptr,
            .commandBufferCount = 1,
            .pCommandBufferDeviceMasks = &submitMask,
            .signalSemaphoreCount = 0,
            .pSignalSemaphoreDeviceIndices = nullptr,
        };
        vk::SubmitInfo submitInfo;
        submitInfo
            .setPNext(&groupSubmitInfo)
            .setCommandBufferCount(1)
            .setPCommandBuffers(&state_->recordingCommandBuffer);

        const vk::CommandBuffer submitted = state_->recordingCommandBuffer;
        detail::activateBufferClaims(*recordingResources_);
        try{
            state_->queue.submit(submitInfo, fence);
        }catch(...){
            detail::releaseBufferClaims(*recordingResources_);
            state_->device.destroyFence(fence);
            throw;
        }
        detail::commitBufferAccesses(std::move(accessCommits));

        state_->recordingCommandBuffer = nullptr;
        state_->recording = false;
        state_->currentPipeline.reset();
        state_->currentDescriptorSet.reset();
        return DispatchToken(state_, fence, submitted, std::move(recordingResources_));
    }

    void Context::submitAndWait(){
        DispatchToken token = submitAsync();
        token.wait();
    }

    namespace{
        void checkDeviceIndex(const detail::ContextState& state, std::uint32_t deviceIndex){
            if(deviceIndex >= state.deviceCount()){
                throw std::out_of_range("Context deviceIndex is out of range.");
            }
        }
    }

    bool Context::usingIntegratedGpu(std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        return state_->physicalDeviceProperties[deviceIndex].deviceType ==
               vk::PhysicalDeviceType::eIntegratedGpu;
    }

    bool Context::supportsGpuTiming(std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        const auto& info = state_->gpuTimingSupportInfos[deviceIndex];
        return info.synchronization2Enabled && info.timestampSupported;
    }

    const GpuTimingSupportInfo& Context::gpuTimingSupportInfo(std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        return state_->gpuTimingSupportInfos[deviceIndex];
    }

    const GpuInfo& Context::gpuInfo(std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        return state_->gpuInfos[deviceIndex];
    }

    std::vector<std::string> Context::supportedDeviceExtensions(std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        return state_->supportedDeviceExtensions[deviceIndex];
    }

    bool Context::supportsDeviceExtension(std::string_view extensionName,
                                           std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        return containsExtension(state_->supportedDeviceExtensions[deviceIndex], extensionName);
    }

    bool Context::supportsCooperativeMatrix(std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        const auto& info = state_->cooperativeMatrixSupportInfos[deviceIndex];
        return info.extensionSupported && info.featureSupported && !info.tiles.empty();
    }

    const CooperativeMatrixSupportInfo& Context::cooperativeMatrixSupportInfo(
        std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        return state_->cooperativeMatrixSupportInfos[deviceIndex];
    }

    const SubgroupSupportInfo& Context::subgroupSupportInfo(std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        return state_->subgroupSupportInfos[deviceIndex];
    }

    void Context::printGpuInfo(std::ostream& os, std::uint32_t deviceIndex) const{
        checkDeviceIndex(*state_, deviceIndex);
        const auto& info = gpuInfo(deviceIndex);
        os << "VUCOL GPU " << info.index << '\n'
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
           << "  Device Extensions: "
           << state_->supportedDeviceExtensions[deviceIndex].size() << '\n'
           << "  GPU Timing: "
           << (supportsGpuTiming(deviceIndex) ? "supported" : "not supported") << '\n'
           << "  Cooperative Matrix: "
           << (supportsCooperativeMatrix(deviceIndex) ? "supported" : "not supported")
           << " (" << state_->cooperativeMatrixSupportInfos[deviceIndex].tiles.size()
           << " tile combinations)\n"
           << "  Device Group Size: " << state_->deviceCount() << '\n';
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
          resources_(std::move(other.resources_)),
          fence_(std::exchange(other.fence_, nullptr)),
          commandBuffer_(std::exchange(other.commandBuffer_, nullptr)),
          gpuDuration_(std::exchange(other.gpuDuration_, std::nullopt)){
    }

    DispatchToken& DispatchToken::operator=(DispatchToken&& other) noexcept{
        if(this != &other){
            if(valid()){
                wait();
            }
            context_ = std::move(other.context_);
            resources_ = std::move(other.resources_);
            fence_ = std::exchange(other.fence_, nullptr);
            commandBuffer_ = std::exchange(other.commandBuffer_, nullptr);
            gpuDuration_ = std::exchange(other.gpuDuration_, std::nullopt);
        }
        return *this;
    }

    DispatchToken::DispatchToken(std::shared_ptr<detail::ContextState> context,
                                 vk::Fence fence,
                                 vk::CommandBuffer commandBuffer,
                                 std::shared_ptr<detail::DispatchResources> resources)
        : context_(std::move(context)),
          resources_(std::move(resources)),
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
        if(resources_ && resources_->gpuTiming){
            std::array<std::uint64_t, 2> timestamps{};
            result = context_->device.getQueryPoolResults(
                resources_->gpuTiming->queryPool,
                0,
                static_cast<std::uint32_t>(timestamps.size()),
                sizeof(timestamps),
                timestamps.data(),
                sizeof(std::uint64_t),
                vk::QueryResultFlagBits::e64);
            if(result != vk::Result::eSuccess){
                throw std::runtime_error("getQueryPoolResults failed for GPU timing.");
            }

            const std::uint64_t ticks = detail::timestampDelta(
                timestamps[0],
                timestamps[1],
                resources_->gpuTiming->timestampValidBits);
            gpuDuration_ = GpuDuration(
                static_cast<double>(ticks) *
                resources_->gpuTiming->timestampPeriodNanoseconds);
        }
        context_->device.destroyFence(fence_);
        context_->device.freeCommandBuffers(context_->commandPool, commandBuffer_);
        fence_ = nullptr;
        commandBuffer_ = nullptr;
        resources_.reset();
        context_.reset();
    }

    GpuDuration DispatchToken::waitAndGetGpuDuration(){
        if(!gpuDuration_ &&
           (!valid() || !resources_ || !resources_->gpuTiming)){
            throw std::runtime_error(
                "DispatchToken does not contain a timed GPU submission.");
        }
        wait();
        if(!gpuDuration_){
            throw std::runtime_error("GPU timing result is unavailable.");
        }
        return *gpuDuration_;
    }

    bool DispatchToken::valid() const{
        return context_ && fence_ && commandBuffer_;
    }
}
