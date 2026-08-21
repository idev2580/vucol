#include <socl/Context.hpp>
#include <socl/DescriptorSet.hpp>
#include <socl/ShaderCompiler.hpp>
#include <socl/ShaderPipeline.hpp>

#include "../src/DispatchResources.hpp"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include <gtest/gtest.h>

using namespace socl;

TEST(SoclApi, CreateInfoIsOpenGlLikeButObjectScoped){
    std::vector<std::uint32_t> spirv = {0x07230203u};

    ShaderPipelineCreateInfo createInfo{
        .spirv = spirv,
        .bindings = {
            {0, DescriptorType::UnifiedPreferred},
            {1, DescriptorType::UnifiedPreferred},
            {2, DescriptorType::UnifiedPreferred},
        },
        .pushConstantSize = 16,
        .specConstants = {
            {0, 8u},
            {1, 8u},
            {2, 4u},
        }
    };

    EXPECT_EQ(createInfo.bindings.size(), 3u);
    EXPECT_EQ(createInfo.pushConstantSize, 16u);
    EXPECT_EQ(createInfo.specConstants[2].id, 2u);
    EXPECT_EQ(createInfo.specConstants[2].size(), sizeof(std::uint32_t));
}

TEST(SoclApi, SpecConstantConvenienceApiSupportsScalarTypes){
    const ShaderPipelineCreateInfo createInfo{
        .specConstants = {
            {0, specConstant(std::uint32_t{8})},
            {1, specConstant(std::int32_t{-2})},
            {2, specConstant(0.5f)},
            {3, specConstant(true)},
        }
    };

    ASSERT_EQ(createInfo.specConstants.size(), 4u);
    EXPECT_EQ(createInfo.specConstants[0].size(), sizeof(std::uint32_t));
    EXPECT_EQ(createInfo.specConstants[1].size(), sizeof(std::int32_t));
    EXPECT_EQ(createInfo.specConstants[2].size(), sizeof(float));
    EXPECT_EQ(createInfo.specConstants[3].size(), sizeof(VkBool32));

    float scale = 0.0f;
    std::memcpy(&scale, createInfo.specConstants[2].data.data(), sizeof(scale));
    EXPECT_FLOAT_EQ(scale, 0.5f);

    VkBool32 enabled = VK_FALSE;
    std::memcpy(&enabled, createInfo.specConstants[3].data.data(), sizeof(enabled));
    EXPECT_EQ(enabled, VK_TRUE);
}

TEST(SoclApi, ShaderPipelineCreateInfoAllowsOptionalRequiredSubgroupSize){
    ShaderPipelineCreateInfo defaultInfo;
    EXPECT_FALSE(defaultInfo.requiredSubgroupSize.has_value());

    ShaderPipelineCreateInfo selectedInfo{
        .requiredSubgroupSize = 32,
    };
    ASSERT_TRUE(selectedInfo.requiredSubgroupSize.has_value());
    EXPECT_EQ(*selectedInfo.requiredSubgroupSize, 32u);
}

TEST(SoclApi, SubgroupSupportInfoReportsSupportAndEnablementSeparately){
    const SubgroupSupportInfo info{
        .defaultSize = 32,
        .supportedStages = vk::ShaderStageFlagBits::eCompute,
        .supportedOperations = vk::SubgroupFeatureFlagBits::eBasic,
        .requiredSizeStages = vk::ShaderStageFlagBits::eCompute,
        .sizeControlSupported = true,
        .sizeControlEnabled = true,
        .computeFullSubgroupsSupported = true,
        .computeFullSubgroupsEnabled = true,
        .minSize = 32,
        .maxSize = 64,
    };

    EXPECT_EQ(info.defaultSize, 32u);
    EXPECT_TRUE(info.sizeControlSupported);
    EXPECT_TRUE(info.sizeControlEnabled);
    EXPECT_TRUE(info.computeFullSubgroupsSupported);
    EXPECT_TRUE(info.computeFullSubgroupsEnabled);
    EXPECT_EQ(info.minSize, 32u);
    EXPECT_EQ(info.maxSize, 64u);
}

TEST(SoclApi, GpuTimingSupportInfoReportsAutomaticFeatureEnablement){
    const GpuTimingSupportInfo info{
        .synchronization2Supported = true,
        .synchronization2Enabled = true,
        .timestampSupported = true,
        .timestampValidBits = 48,
        .timestampPeriodNanoseconds = 1.5f,
    };

    EXPECT_TRUE(info.synchronization2Supported);
    EXPECT_TRUE(info.synchronization2Enabled);
    EXPECT_TRUE(info.timestampSupported);
    EXPECT_EQ(info.timestampValidBits, 48u);
    EXPECT_FLOAT_EQ(info.timestampPeriodNanoseconds, 1.5f);

    const GpuDuration duration{1500.0};
    EXPECT_DOUBLE_EQ(duration.count(), 1500.0);
    const double durationMicroseconds =
        std::chrono::duration<double, std::micro>(duration).count();
    EXPECT_DOUBLE_EQ(durationMicroseconds, 1.5);
}

TEST(SoclApi, GpuTimestampDeltaHandlesCounterWraparound){
    EXPECT_EQ(detail::timestampDelta(250, 5, 8), 11u);
    EXPECT_EQ(detail::timestampDelta(
                  std::numeric_limits<std::uint64_t>::max() - 2,
                  1,
                  64),
              4u);
    EXPECT_THROW(
        static_cast<void>(detail::timestampDelta(0, 1, 0)),
        std::invalid_argument);
}

TEST(SoclApi, CompileGlslToSpirvReturnsComputeShaderBytecode){
    const char* source = R"(#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main(){
}
)";

    const auto spirv = compileGlslToSpirv(source);

    ASSERT_FALSE(spirv.empty());
    EXPECT_EQ(spirv[0], 0x07230203u);
}

TEST(SoclApi, ShaderCompileOptionsCarryVulkanAndSpirvTargets){
    const ShaderCompileOptions defaults;
    EXPECT_EQ(defaults.vulkanVersion, VulkanVersion::Vulkan13);
    EXPECT_EQ(defaults.spirvVersion, SpirvVersion::Spirv13);

    const ShaderCompileOptions selected{
        .vulkanVersion = VulkanVersion::Vulkan13,
        .spirvVersion = SpirvVersion::Spirv16,
    };
    EXPECT_EQ(selected.vulkanVersion, VulkanVersion::Vulkan13);
    EXPECT_EQ(selected.spirvVersion, SpirvVersion::Spirv16);
}

TEST(SoclApi, CompileGlslToSpirvReportsInvalidGlsl){
    EXPECT_THROW(
        static_cast<void>(compileGlslToSpirv("#version 450\ninvalid glsl\n")),
        std::runtime_error);
}

TEST(SoclApi, DefaultObjectsAreEmpty){
    Buffer buffer;
    ShaderPipeline pipeline;
    DescriptorSet descriptorSet;
    DispatchToken token;

    EXPECT_FALSE(buffer);
    EXPECT_FALSE(pipeline);
    EXPECT_FALSE(descriptorSet);
    EXPECT_FALSE(token.valid());
    EXPECT_THROW(static_cast<void>(token.waitAndGetGpuDuration()), std::runtime_error);
    EXPECT_EQ(buffer.size(), 0u);
}

TEST(SoclApi, BufferAccessExposesReadWriteIntent){
    EXPECT_NE(BufferAccess::Read, BufferAccess::Write);
    EXPECT_NE(BufferAccess::Read, BufferAccess::ReadWrite);
    EXPECT_NE(BufferAccess::Write, BufferAccess::ReadWrite);
}

TEST(SoclApi, ContextCreateInfoAllowsGpuSelectionAndExtensionRequirements){
    ContextCreateInfo createInfo{
        .physicalDeviceIndex = 2,
        .requiredDeviceExtensions = {
            "VK_KHR_storage_buffer_storage_class",
        },
    };

    EXPECT_EQ(createInfo.physicalDeviceIndex, 2u);
    ASSERT_EQ(createInfo.requiredDeviceExtensions.size(), 1u);
    EXPECT_STREQ(createInfo.requiredDeviceExtensions[0], "VK_KHR_storage_buffer_storage_class");
}

TEST(SoclApi, CooperativeMatrixSupportInfoCarriesTileCombinations){
    const CooperativeMatrixTileProperties tile{
        .m = 16,
        .n = 8,
        .k = 16,
        .aType = vk::ComponentTypeKHR::eFloat16,
        .bType = vk::ComponentTypeKHR::eFloat16,
        .cType = vk::ComponentTypeKHR::eFloat32,
        .resultType = vk::ComponentTypeKHR::eFloat32,
        .saturatingAccumulation = false,
        .scope = vk::ScopeKHR::eSubgroup,
    };
    const CooperativeMatrixSupportInfo info{
        .extensionSupported = true,
        .featureSupported = true,
        .robustBufferAccessSupported = true,
        .supportedStages = vk::ShaderStageFlagBits::eCompute,
        .tiles = {tile},
    };

    ASSERT_EQ(info.tiles.size(), 1u);
    EXPECT_TRUE(info.extensionSupported);
    EXPECT_TRUE(info.featureSupported);
    EXPECT_TRUE(info.robustBufferAccessSupported);
    EXPECT_TRUE(static_cast<bool>(info.supportedStages & vk::ShaderStageFlagBits::eCompute));
    EXPECT_EQ(info.tiles[0].m, 16u);
    EXPECT_EQ(info.tiles[0].n, 8u);
    EXPECT_EQ(info.tiles[0].k, 16u);
    EXPECT_EQ(info.tiles[0].aType, vk::ComponentTypeKHR::eFloat16);
    EXPECT_EQ(info.tiles[0].resultType, vk::ComponentTypeKHR::eFloat32);
    EXPECT_EQ(info.tiles[0].scope, vk::ScopeKHR::eSubgroup);
}

TEST(SoclApi, GpuInfoCarriesPrintableDeviceData){
    GpuInfo info{
        .index = 1,
        .name = "Example GPU",
        .type = "discrete",
        .vendorId = 0x1234,
        .deviceId = 0x5678,
        .apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0),
        .driverVersion = 42,
        .computeQueueFamily = 3,
        .computeQueueCount = 2,
    };

    EXPECT_EQ(info.index, 1u);
    EXPECT_EQ(info.name, "Example GPU");
    EXPECT_EQ(info.type, "discrete");
    EXPECT_EQ(VK_VERSION_MAJOR(info.apiVersion), 1u);
    EXPECT_EQ(VK_VERSION_MINOR(info.apiVersion), 3u);
    EXPECT_EQ(info.computeQueueFamily, 3u);
    EXPECT_EQ(info.computeQueueCount, 2u);
}
