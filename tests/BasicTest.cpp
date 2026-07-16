#include <socl/Context.hpp>
#include <socl/DescriptorSet.hpp>
#include <socl/ShaderPipeline.hpp>

#include <cstdint>
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
            {0, 8},
            {1, 8},
            {2, 4},
        }
    };

    EXPECT_EQ(createInfo.bindings.size(), 3u);
    EXPECT_EQ(createInfo.pushConstantSize, 16u);
    EXPECT_EQ(createInfo.specConstants[2].value, 4u);
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
    EXPECT_EQ(buffer.size(), 0u);
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
