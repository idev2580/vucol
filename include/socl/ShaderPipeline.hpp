#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
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

    struct SpecConstantValue{
        std::vector<std::byte> data;

        [[nodiscard]] std::size_t size() const{
            return data.size();
        }
    };

    namespace detail{
        template<typename T>
        SpecConstantValue makeSpecConstantValue(const T& value){
            static_assert(std::is_trivially_copyable_v<T>, "Specialization constants must be trivially copyable.");

            SpecConstantValue result;
            result.data.resize(sizeof(T));
            std::memcpy(result.data.data(), &value, sizeof(T));
            return result;
        }
    }

    inline SpecConstantValue specConstant(bool value){
        const VkBool32 storedValue = value ? VK_TRUE : VK_FALSE;
        return detail::makeSpecConstantValue(storedValue);
    }

    template<typename T>
    requires(!std::is_same_v<std::remove_cv_t<T>, bool> && (std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>))
    SpecConstantValue specConstant(T value){
        return detail::makeSpecConstantValue(value);
    }

    struct SpecConstant{
        std::uint32_t id = 0;
        std::vector<std::byte> data;

        SpecConstant() = default;

        SpecConstant(std::uint32_t constantId, std::uint32_t value)
            : SpecConstant(constantId, specConstant(value)){
        }

        SpecConstant(std::uint32_t constantId, SpecConstantValue value)
            : id(constantId), data(std::move(value.data)){
        }

        [[nodiscard]] std::size_t size() const{
            return data.size();
        }
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
        std::optional<std::uint32_t> requiredSubgroupSize;
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
