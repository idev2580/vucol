#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace socl{
    // DescriptorType controls how the shader sees a bound resource.
    // This is separate from BufferType, which controls memory allocation.
    enum class DescriptorType{
        UnifiedPreferred,
        StorageBuffer,
        UniformBuffer,
    };

    struct DescriptorBinding{
        std::uint32_t binding = 0;
        DescriptorType type = DescriptorType::UnifiedPreferred;
    };

    struct SpecConstant{
        std::uint32_t id = 0;
        std::uint32_t value = 0;
    };

    // Describes the shader interface SOCL should build around the SPIR-V.
    // Most compute buffers should be storage buffers; other descriptor types
    // exist for shaders that deliberately declare different resource kinds.
    struct ShaderPipelineCreateInfo{
        std::span<const std::uint32_t> spirv;
        std::vector<DescriptorBinding> bindings;
        std::uint32_t pushConstantSize = 0;
        std::vector<SpecConstant> specConstants;
        const char* entryPoint = "main";
    };

    namespace detail{
        struct ContextState;

        struct ShaderPipelineState{
            std::shared_ptr<ContextState> context;
            vk::Device device;
            vk::ShaderModule shaderModule;
            vk::DescriptorSetLayout descriptorSetLayout;
            vk::PipelineLayout pipelineLayout;
            vk::Pipeline pipeline;
            std::vector<DescriptorBinding> bindings;
            std::uint32_t pushConstantSize = 0;

            ~ShaderPipelineState();
        };
    }

    class ShaderPipeline{
        friend class Context;
        friend class DescriptorSet;

        public:
        ShaderPipeline();
        ~ShaderPipeline();

        ShaderPipeline(const ShaderPipeline&) = default;
        ShaderPipeline& operator=(const ShaderPipeline&) = default;
        ShaderPipeline(ShaderPipeline&&) noexcept = default;
        ShaderPipeline& operator=(ShaderPipeline&&) noexcept = default;

        [[nodiscard]] std::span<const DescriptorBinding> bindings() const;
        [[nodiscard]] std::uint32_t pushConstantSize() const;
        [[nodiscard]] explicit operator bool() const;

        private:
        explicit ShaderPipeline(std::shared_ptr<detail::ShaderPipelineState> state);
        std::shared_ptr<detail::ShaderPipelineState> state_;
    };
}
