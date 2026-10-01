#include <vucol/Context.hpp>
#include <vucol/DescriptorSet.hpp>
#include <vucol/ShaderCompiler.hpp>
#include <vucol/ShaderPipeline.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <gtest/gtest.h>

namespace{
    constexpr std::size_t rangeSize = sizeof(std::uint32_t);

    struct Operation{
        std::size_t output;
        std::size_t left;
        std::size_t right;
    };

}

TEST(VucolBufferRanges, PreserveOrderAcrossDispatchesAndSubmissions){
    try{
        if(vucol::Context::enumerateGpus().empty()){
            GTEST_SKIP() << "No Vulkan compute device is available.";
        }
    }catch(const std::runtime_error& error){
        GTEST_SKIP() << "Vulkan compute device enumeration failed: " << error.what();
    }

    const char* shaderSource = R"(
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
    outputBuffer.value = leftBuffer.value * 10u + rightBuffer.value;
}
)";

    const auto spirv = vucol::compileGlslToSpirv(shaderSource);
    vucol::Context context;
    auto pipeline = context.createShaderPipeline({
        .spirv = spirv,
        .bindings = {
            {0, vucol::DescriptorType::StorageBuffer},
            {1, vucol::DescriptorType::StorageBuffer},
            {2, vucol::DescriptorType::StorageBuffer},
        },
    });
    auto descriptorSet = context.createDescriptorSet(pipeline);
    const std::size_t alignment = std::max<std::size_t>(
        1,
        context.bufferOffsetAlignment(vucol::DescriptorType::StorageBuffer));
    const std::size_t rangeStride =
        ((rangeSize + alignment - 1) / alignment) * alignment;
    const auto rangeOffset = [rangeStride](std::size_t index){
        return index * rangeStride;
    };
    auto buffer = context.createBuffer(
        rangeOffset(2) + rangeSize,
        vucol::BufferType::HostVisible);

    const auto writeValues = [&](const std::array<std::uint32_t, 3>& values){
        for(std::size_t i = 0; i < values.size(); ++i){
            buffer.write(&values[i], rangeSize, rangeOffset(i));
        }
    };
    const auto readValues = [&]{
        std::array<std::uint32_t, 3> values{};
        for(std::size_t i = 0; i < values.size(); ++i){
            buffer.read(&values[i], rangeSize, rangeOffset(i));
        }
        return values;
    };
    const auto record = [&](const Operation& operation){
        descriptorSet.bindBuffer(
            0,
            buffer,
            rangeOffset(operation.output),
            rangeSize,
            vucol::BufferAccess::Write);
        descriptorSet.bindBuffer(
            1,
            buffer,
            rangeOffset(operation.left),
            rangeSize,
            vucol::BufferAccess::Read);
        descriptorSet.bindBuffer(
            2,
            buffer,
            rangeOffset(operation.right),
            rangeSize,
            vucol::BufferAccess::Read);
        context.bind(descriptorSet);
        context.dispatch(1);
    };

    const Operation updateFirst{0, 1, 2};
    const Operation updateSecond{1, 0, 2};
    const Operation updateThird{2, 1, 0};

    writeValues({1, 2, 3});
    context.begin();
    context.use(pipeline);
    record(updateFirst);
    record(updateSecond);

    std::uint32_t valueWhileRecording = 0;
    EXPECT_NO_THROW(
        buffer.read(&valueWhileRecording, rangeSize, rangeOffset(0)));
    EXPECT_EQ(valueWhileRecording, 1u);
    auto orderedFirstSubmit = context.submitAsync();

    std::uint32_t valueWhileSubmitted = 0;
    EXPECT_THROW(
        buffer.read(&valueWhileSubmitted, rangeSize, rangeOffset(0)),
        std::runtime_error);

    context.begin();
    context.use(pipeline);
    record(updateThird);
    auto orderedSecondSubmit = context.submitAsync();

    orderedFirstSubmit.wait();
    EXPECT_THROW(
        buffer.read(&valueWhileSubmitted, rangeSize, rangeOffset(0)),
        std::runtime_error);
    orderedSecondSubmit.wait();
    EXPECT_EQ(readValues(), (std::array<std::uint32_t, 3>{23, 233, 2353}));

    writeValues({1, 2, 3});
    context.begin();
    context.use(pipeline);
    record(updateSecond);
    record(updateFirst);
    auto reorderedFirstSubmit = context.submitAsync();

    context.begin();
    context.use(pipeline);
    record(updateThird);
    auto reorderedSecondSubmit = context.submitAsync();

    reorderedFirstSubmit.wait();
    reorderedSecondSubmit.wait();
    EXPECT_EQ(readValues(), (std::array<std::uint32_t, 3>{133, 13, 263}));
}
