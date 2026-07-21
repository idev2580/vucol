#include <socl/Context.hpp>
#include <socl/DescriptorSet.hpp>
#include <socl/ShaderCompiler.hpp>
#include <socl/ShaderPipeline.hpp>

#include <cstring>
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
