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
