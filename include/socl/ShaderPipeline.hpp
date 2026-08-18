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
    /**
     * @brief Selects how a shader interprets a descriptor binding.
     *
     * This is independent of BufferType, which controls memory allocation.
     */
    enum class DescriptorType{
        UnifiedPreferred, ///< Use SOCL's preferred general-purpose buffer descriptor.
        StorageBuffer,    ///< Expose the resource as a Vulkan storage buffer.
        UniformBuffer,    ///< Expose the resource as a Vulkan uniform buffer.
    };

    /** @brief Declares one buffer binding in a shader pipeline layout. */
    struct DescriptorBinding{
        std::uint32_t binding = 0; ///< Shader descriptor binding number in set zero.
        DescriptorType type = DescriptorType::UnifiedPreferred; ///< Descriptor representation.
    };

    /** @brief Type-erased byte representation of one specialization-constant value. */
    struct SpecConstantValue{
        std::vector<std::byte> data; ///< Bytes copied into Vulkan specialization data.

        /**
         * @brief Returns the stored value size.
         * @return Number of bytes in data.
         * @par Thread safety
         * Safe for concurrent reads if data is not concurrently modified.
         */
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

    /**
     * @brief Encodes a Boolean specialization constant using Vulkan's 32-bit Boolean ABI.
     * @param value Input Boolean value.
     * @return Owned bytes containing `VK_TRUE` or `VK_FALSE` as a `VkBool32`.
     * @par Thread safety
     * Thread-safe; the function uses only local state.
     */
    inline SpecConstantValue specConstant(bool value){
        const VkBool32 storedValue = value ? VK_TRUE : VK_FALSE;
        return detail::makeSpecConstantValue(storedValue);
    }

    /**
     * @brief Encodes an arithmetic or enumeration specialization constant.
     * @tparam T Integral, floating-point, or enumeration value type.
     * @param value Input value copied byte-for-byte.
     * @return Owned bytes containing @p value in its C++ object representation.
     * @par Thread safety
     * Thread-safe; the function uses only local state.
     */
    template<typename T>
    requires(!std::is_same_v<std::remove_cv_t<T>, bool> && (std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>))
    SpecConstantValue specConstant(T value){
        return detail::makeSpecConstantValue(value);
    }

    /** @brief Associates a SPIR-V specialization-constant ID with encoded data. */
    struct SpecConstant{
        std::uint32_t id = 0;        ///< SPIR-V specialization-constant ID.
        std::vector<std::byte> data; ///< Value bytes consumed during pipeline creation.

        /** @brief Constructs constant ID zero with an empty value. */
        SpecConstant() = default;

        /**
         * @brief Constructs a 32-bit unsigned specialization constant.
         * @param constantId SPIR-V specialization-constant ID.
         * @param value Input 32-bit value.
         */
        SpecConstant(std::uint32_t constantId, std::uint32_t value)
            : SpecConstant(constantId, specConstant(value)){
        }

        /**
         * @brief Constructs a specialization constant from encoded bytes.
         * @param constantId SPIR-V specialization-constant ID.
         * @param value Encoded input value; its storage is moved into this object.
         */
        SpecConstant(std::uint32_t constantId, SpecConstantValue value)
            : id(constantId), data(std::move(value.data)){
        }

        /**
         * @brief Returns the encoded value size.
         * @return Number of bytes in data.
         * @par Thread safety
         * Safe for concurrent reads if data is not concurrently modified.
         */
        [[nodiscard]] std::size_t size() const{
            return data.size();
        }
    };

    /**
     * @brief Describes the compute pipeline and shader interface to create.
     *
     * All referenced input memory is consumed synchronously by
     * Context::createShaderPipeline(); it need not remain alive afterward.
     */
    struct ShaderPipelineCreateInfo{
        std::span<const std::uint32_t> spirv; ///< Input SPIR-V words.
        std::vector<DescriptorBinding> bindings; ///< Set-zero buffer layout declarations.
        std::uint32_t pushConstantSize = 0; ///< Push-constant range size in bytes.
        std::vector<SpecConstant> specConstants; ///< Pipeline specialization values.
        const char* entryPoint = "main"; ///< Null-terminated entry point; null selects `main`.
        std::optional<std::uint32_t> requiredSubgroupSize; ///< Optional required compute subgroup size.
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

    /**
     * @brief Shared handle to a reusable Vulkan compute pipeline.
     *
     * Copies share immutable pipeline state. Descriptor sets and recorded dispatches
     * retain that state, so destroying an application handle cannot invalidate GPU
     * work that still references the pipeline.
     *
     * @par Thread safety
     * The immutable pipeline state may be inspected concurrently. Individual handle
     * objects must not be moved, assigned, or destroyed concurrently with access.
     */
    class ShaderPipeline{
        friend class Context;
        friend class DescriptorSet;

        public:
        /** @brief Constructs an empty pipeline handle. */
        ShaderPipeline();

        /** @brief Releases this handle; shared users keep the native pipeline alive. */
        ~ShaderPipeline();

        /**
         * @brief Shares another pipeline's immutable state.
         * @param other Input handle to share.
         */
        ShaderPipeline(const ShaderPipeline& other) = default;

        /**
         * @brief Replaces this handle with another shared pipeline state.
         * @param other Input handle to share.
         * @return This handle.
         */
        ShaderPipeline& operator=(const ShaderPipeline& other) = default;

        /**
         * @brief Transfers a pipeline handle.
         * @param other Input handle, empty after the move.
         */
        ShaderPipeline(ShaderPipeline&& other) noexcept = default;

        /**
         * @brief Replaces this handle by moving another handle into it.
         * @param other Input handle, empty after the move.
         * @return This handle.
         */
        ShaderPipeline& operator=(ShaderPipeline&& other) noexcept = default;

        /**
         * @brief Returns the pipeline's descriptor binding declarations.
         * @return Read-only view valid while this pipeline state remains alive; empty
         *         for an empty handle.
         * @par Thread safety
         * Safe for concurrent reads if this handle remains alive and unmodified.
         */
        [[nodiscard]] std::span<const DescriptorBinding> bindings() const;

        /**
         * @brief Returns the declared push-constant range size.
         * @return Size in bytes, or `0` for an empty handle.
         * @par Thread safety
         * Safe for concurrent reads if this handle remains alive and unmodified.
         */
        [[nodiscard]] std::uint32_t pushConstantSize() const;

        /**
         * @brief Tests whether this handle contains a pipeline.
         * @return `true` for a non-empty handle; otherwise `false`.
         * @par Thread safety
         * Safe when no thread concurrently modifies this handle.
         */
        [[nodiscard]] explicit operator bool() const;

        private:
        explicit ShaderPipeline(std::shared_ptr<detail::ShaderPipelineState> state);
        std::shared_ptr<detail::ShaderPipelineState> state_;
    };
}
