#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <socl/Context.hpp>

#include <VkBootstrap.h>

#include <stdexcept>
#include <string>
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
        : state_(std::make_shared<detail::ContextState>()){
        vkb::InstanceBuilder instanceBuilder;
        auto vkbInstance = unwrap(
            instanceBuilder
                .set_app_name("socl")
                .set_engine_name("socl")
                .require_api_version(1, 1, 0)
                .build(),
            "vk-bootstrap instance creation failed");

        state_->instance = vk::Instance(vkbInstance.instance);

        vk::PhysicalDeviceVulkan11Features required11{};
        vkb::PhysicalDeviceSelector selector(vkbInstance);
        auto vkbPhysicalDevice = unwrap(
            selector
                .set_minimum_version(1, 1)
                .set_required_features_11(required11)
                .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                .select(),
            "vk-bootstrap physical device selection failed");

        vkb::DeviceBuilder deviceBuilder(vkbPhysicalDevice);
        auto vkbDevice = unwrap(deviceBuilder.build(), "vk-bootstrap device creation failed");

        state_->physicalDevice = vk::PhysicalDevice(vkbPhysicalDevice.physical_device);
        state_->physicalDeviceProperties = state_->physicalDevice.getProperties();
        state_->device = vk::Device(vkbDevice.device);
        state_->queue = vk::Queue(unwrap(vkbDevice.get_queue(vkb::QueueType::compute),
                                         "vk-bootstrap compute queue lookup failed"));
        state_->queueFamily = unwrap(vkbDevice.get_queue_index(vkb::QueueType::compute),
                                     "vk-bootstrap compute queue family lookup failed");

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

        const bool integrated = usingIntegratedGpu();
        state->type = type == BufferType::Auto
            ? (integrated ? BufferType::HostVisible : BufferType::DeviceLocal)
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
        std::vector<std::uint32_t> specializationData;
        specializationEntries.reserve(createInfo.specConstants.size());
        specializationData.reserve(createInfo.specConstants.size());
        for(std::size_t i = 0; i < createInfo.specConstants.size(); ++i){
            specializationData.push_back(createInfo.specConstants[i].value);
            vk::SpecializationMapEntry entry;
            entry
                .setConstantID(createInfo.specConstants[i].id)
                .setOffset(static_cast<std::uint32_t>(i * sizeof(std::uint32_t)))
                .setSize(sizeof(std::uint32_t));
            specializationEntries.push_back(entry);
        }

        vk::SpecializationInfo specializationInfo;
        specializationInfo
            .setMapEntryCount(static_cast<std::uint32_t>(specializationEntries.size()))
            .setPMapEntries(specializationEntries.data())
            .setDataSize(specializationData.size() * sizeof(std::uint32_t))
            .setPData(specializationData.data());
        vk::PipelineShaderStageCreateInfo stageInfo;
        stageInfo
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(state->shaderModule)
            .setPName(createInfo.entryPoint ? createInfo.entryPoint : "main")
            .setPSpecializationInfo(specializationEntries.empty() ? nullptr : &specializationInfo);

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
