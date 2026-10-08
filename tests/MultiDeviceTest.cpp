#include <vucol/Context.hpp>
#include <vucol/DescriptorSet.hpp>
#include <vucol/ShaderCompiler.hpp>
#include <vucol/ShaderPipeline.hpp>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace{
    bool hasAnyGpu(){
        try{
            return !vucol::Context::enumerateGpus().empty();
        }catch(...){
            return false;
        }
    }

    const char* addShaderSource = R"(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) writeonly buffer OutputBuffer {
    uint value;
} outputBuffer;

layout(set = 0, binding = 1, std430) readonly buffer LeftBuffer {
    uint value;
} leftBuffer;

layout(set = 0, binding = 2, std430) readonly buffer RightBuffer {
    uint value;
} rightBuffer;

void main(){
    outputBuffer.value = leftBuffer.value + rightBuffer.value;
}
)";
}

TEST(VucolMultiDevice, EmptyBufferHasNeutralDeviceAttributes){
    const vucol::Buffer buffer;

    EXPECT_EQ(buffer.ownerDeviceIndex(), 0u);
    EXPECT_EQ(buffer.peerAccess(), vucol::BufferPeerAccess::None);
}

TEST(VucolMultiDevice, DefaultBufferSelectsFirstGpu){
    if(!hasAnyGpu()){
        GTEST_SKIP() << "No Vulkan compute device is available.";
    }

    vucol::Context context;
    auto buffer = context.createBuffer(sizeof(std::uint32_t),
                                        vucol::BufferType::HostVisible);

    EXPECT_EQ(buffer.ownerDeviceIndex(), 0u);
    EXPECT_EQ(buffer.peerAccess(), vucol::BufferPeerAccess::None);
}

TEST(VucolMultiDevice, OutOfRangeDeviceIndexThrows){
    if(!hasAnyGpu()){
        GTEST_SKIP() << "No Vulkan compute device is available.";
    }

    vucol::Context context;
    EXPECT_THROW(static_cast<void>(context.createBuffer(sizeof(std::uint32_t),
                                                        vucol::BufferType::HostVisible,
                                                        1000)),
                 std::out_of_range);
    EXPECT_THROW(context.dispatch(1, 1, 1, 1000), std::out_of_range);
    EXPECT_THROW(static_cast<void>(context.bufferOffsetAlignment(
                     vucol::DescriptorType::StorageBuffer,
                     1000)),
                 std::out_of_range);
    EXPECT_THROW(static_cast<void>(context.gpuInfo(1000)), std::out_of_range);
}

TEST(VucolMultiDevice, ExplicitFirstDeviceDispatchComputes){
    if(!hasAnyGpu()){
        GTEST_SKIP() << "No Vulkan compute device is available.";
    }

    const auto spirv = vucol::compileGlslToSpirv(addShaderSource);
    vucol::Context context;
    auto pipeline = context.createShaderPipeline({
        .spirv = spirv,
        .bindings = {
            {0, vucol::DescriptorType::StorageBuffer},
            {1, vucol::DescriptorType::StorageBuffer},
            {2, vucol::DescriptorType::StorageBuffer},
        },
    });

    auto left = context.createBuffer(sizeof(std::uint32_t),
                                      vucol::BufferType::HostVisible);
    auto right = context.createBuffer(sizeof(std::uint32_t),
                                       vucol::BufferType::HostVisible);
    auto output = context.createBuffer(sizeof(std::uint32_t),
                                        vucol::BufferType::HostVisible);
    const std::uint32_t leftValue = 40;
    const std::uint32_t rightValue = 2;
    left.write(&leftValue, sizeof(leftValue));
    right.write(&rightValue, sizeof(rightValue));

    auto descriptorSet = context.createDescriptorSet(pipeline);
    descriptorSet.bindBuffer(0, output, vucol::BufferAccess::Write);
    descriptorSet.bindBuffer(1, left, vucol::BufferAccess::Read);
    descriptorSet.bindBuffer(2, right, vucol::BufferAccess::Read);

    context.begin();
    context.use(pipeline);
    context.bind(descriptorSet);
    context.dispatch(1, 1, 1, 0);
    context.submitAndWait();

    std::uint32_t result = 0;
    output.read(&result, sizeof(result));
    EXPECT_EQ(result, 42u);
}

TEST(VucolMultiDevice, PrintGpuInfoReportsGroupSize){
    if(!hasAnyGpu()){
        GTEST_SKIP() << "No Vulkan compute device is available.";
    }

    vucol::Context context;
    std::ostringstream output;
    context.printGpuInfo(output);

    EXPECT_NE(output.str().find("Device Group Size"), std::string::npos);
}

TEST(VucolMultiDevice, CrossDeviceUseWithoutPeerThrows){
    if(!hasAnyGpu()){
        GTEST_SKIP() << "No Vulkan compute device is available.";
    }

    vucol::Context context;
    try{
        context = vucol::Context({.physicalDeviceIndices = {0, 1}});
    }catch(const std::exception& error){
        GTEST_SKIP() << "Two-device group is unavailable: " << error.what();
    }

    const auto spirv = vucol::compileGlslToSpirv(addShaderSource);
    auto pipeline = context.createShaderPipeline({
        .spirv = spirv,
        .bindings = {
            {0, vucol::DescriptorType::StorageBuffer},
            {1, vucol::DescriptorType::StorageBuffer},
            {2, vucol::DescriptorType::StorageBuffer},
        },
    });

    auto owned = context.createBuffer(sizeof(std::uint32_t),
                                       vucol::BufferType::HostVisible,
                                       0);
    auto scratch = context.createBuffer(sizeof(std::uint32_t),
                                         vucol::BufferType::HostVisible,
                                         1);
    auto descriptorSet = context.createDescriptorSet(pipeline);
    descriptorSet.bindBuffer(0, scratch, vucol::BufferAccess::Write);
    descriptorSet.bindBuffer(1, owned, vucol::BufferAccess::Read);
    descriptorSet.bindBuffer(2, scratch, vucol::BufferAccess::Read);

    context.begin();
    context.use(pipeline);
    context.bind(descriptorSet);
    EXPECT_THROW(context.dispatch(1, 1, 1, 1), std::runtime_error);
}

TEST(VucolMultiDevice, OrderedBatchAcrossDevicesComputes){
    if(!hasAnyGpu()){
        GTEST_SKIP() << "No Vulkan compute device is available.";
    }

    vucol::Context context;
    try{
        context = vucol::Context({.physicalDeviceIndices = {0, 1}});
    }catch(const std::exception& error){
        GTEST_SKIP() << "Two-device group is unavailable: " << error.what();
    }

    auto input = context.createBuffer(sizeof(std::uint32_t),
                                       vucol::BufferType::HostVisible,
                                       0);
    vucol::Buffer middle;
    try{
        middle = context.createBuffer(sizeof(std::uint32_t),
                                       vucol::BufferType::HostVisible,
                                       0,
                                       vucol::ALLOW_PEER_READ_WRITE);
    }catch(const std::exception& error){
        GTEST_SKIP() << "Peer access is unsupported: " << error.what();
    }
    auto output = context.createBuffer(sizeof(std::uint32_t),
                                        vucol::BufferType::HostVisible,
                                        1);
    auto oneOnZero = context.createBuffer(sizeof(std::uint32_t),
                                           vucol::BufferType::HostVisible,
                                           0);
    auto oneOnOne = context.createBuffer(sizeof(std::uint32_t),
                                          vucol::BufferType::HostVisible,
                                          1);

    const std::uint32_t inputValue = 40;
    const std::uint32_t one = 1;
    input.write(&inputValue, sizeof(inputValue));
    oneOnZero.write(&one, sizeof(one));
    oneOnOne.write(&one, sizeof(one));

    const auto spirv = vucol::compileGlslToSpirv(addShaderSource);
    auto pipeline = context.createShaderPipeline({
        .spirv = spirv,
        .bindings = {
            {0, vucol::DescriptorType::StorageBuffer},
            {1, vucol::DescriptorType::StorageBuffer},
            {2, vucol::DescriptorType::StorageBuffer},
        },
    });

    auto first = context.createDescriptorSet(pipeline);
    first.bindBuffer(0, middle, vucol::BufferAccess::Write);
    first.bindBuffer(1, input, vucol::BufferAccess::Read);
    first.bindBuffer(2, oneOnZero, vucol::BufferAccess::Read);

    auto second = context.createDescriptorSet(pipeline);
    second.bindBuffer(0, output, vucol::BufferAccess::Write);
    second.bindBuffer(1, middle, vucol::BufferAccess::Read);
    second.bindBuffer(2, oneOnOne, vucol::BufferAccess::Read);

    context.begin();
    context.use(pipeline);
    context.bind(first);
    context.dispatch(1, 1, 1, 0);
    context.bind(second);
    context.dispatch(1, 1, 1, 1);
    context.submitAndWait();

    std::uint32_t result = 0;
    output.read(&result, sizeof(result));
    EXPECT_EQ(result, 42u);
}
