#include <socl/Context.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace{
    struct alignas(16) DispatchParams{
        std::uint32_t count = 0;
    };

    std::vector<std::uint32_t> loadSpirv(const std::string& path){
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if(!file){
            throw std::runtime_error("Failed to open SPIR-V file: " + path);
        }

        const std::streamsize bytes = file.tellg();
        if(bytes <= 0 || bytes % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0){
            throw std::runtime_error("SPIR-V file has invalid byte size: " + path);
        }

        file.seekg(0, std::ios::beg);
        std::vector<std::uint32_t> spirv(static_cast<std::size_t>(bytes) / sizeof(std::uint32_t));
        if(!file.read(reinterpret_cast<char*>(spirv.data()), bytes)){
            throw std::runtime_error("Failed to read SPIR-V file: " + path);
        }
        return spirv;
    }

    bool nearlyEqual(float left, float right){
        const float diff = left > right ? left - right : right - left;
        return diff < 0.0001f;
    }
}

int main(int argc, char** argv){
    try{
        const auto gpus = socl::Context::enumerateGpus();
        if(gpus.empty()){
            std::cerr << "No Vulkan GPUs found.\n";
            return 1;
        }

        std::uint32_t gpuIndex = 0;
        if(argc > 1){
            gpuIndex = static_cast<std::uint32_t>(std::stoul(argv[1]));
        }

        socl::Context context({.physicalDeviceIndex = gpuIndex});
        context.printGpuInfo(std::cout);
        std::cout << "  VK_KHR_shader_float16_int8: "
                  << (context.supportsDeviceExtension("VK_KHR_shader_float16_int8") ? "supported" : "not supported")
                  << '\n';

        const std::vector<float> inputA = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
        const std::vector<float> inputB = {8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f};
        std::vector<float> output(inputA.size(), 0.0f);
        const DispatchParams params{.count = static_cast<std::uint32_t>(inputA.size())};

        const std::size_t bytes = inputA.size() * sizeof(float);
        auto bufferA = context.createBuffer(bytes, socl::BufferType::HostVisible);
        auto bufferB = context.createBuffer(bytes, socl::BufferType::HostVisible);
        auto bufferOut = context.createBuffer(bytes, socl::BufferType::HostVisible);
        auto paramsBuffer = context.createBuffer(sizeof(params), socl::BufferType::HostVisible);

        bufferA.write(inputA.data(), bytes);
        bufferB.write(inputB.data(), bytes);
        paramsBuffer.write(&params, sizeof(params));

        const auto spirv = loadSpirv(SOCL_ELEMENTWISE_ADD_SPV);
        auto pipeline = context.createShaderPipeline({
            .spirv = spirv,
            .bindings = {
                {0, socl::DescriptorType::StorageBuffer},
                {1, socl::DescriptorType::StorageBuffer},
                {2, socl::DescriptorType::StorageBuffer},
                {3, socl::DescriptorType::UniformBuffer},
            },
            .pushConstantSize = sizeof(std::uint32_t),
            // These specialization constants are intentionally unused by the shader.
            // They only demonstrate that the API accepts multiple scalar value types.
            .specConstants = {
                {0, socl::specConstant(std::uint32_t{64})},
                {1, socl::specConstant(std::int32_t{-1})},
                {2, socl::specConstant(1.0f)},
                {3, socl::specConstant(true)},
            },
        });

        auto descriptorSet = context.createDescriptorSet(pipeline);
        descriptorSet.bindBuffer(0, bufferA);
        descriptorSet.bindBuffer(1, bufferB);
        descriptorSet.bindBuffer(2, bufferOut);
        descriptorSet.bindBuffer(3, paramsBuffer);
        descriptorSet.update();

        context.begin();
        context.use(pipeline);
        context.bind(descriptorSet);
        context.push(params.count);
        context.dispatch((params.count + 63u) / 64u);
        context.submitAndWait();

        bufferOut.read(output.data(), bytes);

        for(std::size_t i = 0; i < output.size(); ++i){
            const float expected = inputA[i] + inputB[i];
            if(!nearlyEqual(output[i], expected)){
                std::cerr << "Mismatch at " << i << ": got " << output[i]
                          << ", expected " << expected << '\n';
                return 1;
            }
        }

        std::cout << "Element-wise add result:";
        for(float value : output){
            std::cout << ' ' << value;
        }
        std::cout << '\n';
        return 0;
    }catch(const std::exception& error){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
