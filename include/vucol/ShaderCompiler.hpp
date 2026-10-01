#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace vucol{
    /** @brief Vulkan environment version targeted by shader compilation. */
    enum class VulkanVersion{
        Vulkan10, ///< Target Vulkan 1.0.
        Vulkan11, ///< Target Vulkan 1.1.
        Vulkan12, ///< Target Vulkan 1.2.
        Vulkan13, ///< Target Vulkan 1.3.
    };

    /** @brief SPIR-V version emitted by shader compilation. */
    enum class SpirvVersion{
        Spirv10, ///< Emit SPIR-V 1.0.
        Spirv11, ///< Emit SPIR-V 1.1.
        Spirv12, ///< Emit SPIR-V 1.2.
        Spirv13, ///< Emit SPIR-V 1.3.
        Spirv14, ///< Emit SPIR-V 1.4.
        Spirv15, ///< Emit SPIR-V 1.5.
        Spirv16, ///< Emit SPIR-V 1.6.
    };

    /** @brief Target versions used when compiling GLSL compute source. */
    struct ShaderCompileOptions{
        VulkanVersion vulkanVersion = VulkanVersion::Vulkan13; ///< Target Vulkan environment.
        SpirvVersion spirvVersion = SpirvVersion::Spirv13;     ///< Target SPIR-V version.
    };

    /**
     * @brief Compiles GLSL compute-shader source to SPIR-V with shaderc defaults.
     * @param source Input GLSL source text. The view need only remain valid for this call.
     * @param sourceName Diagnostic source name reported by shaderc.
     * @return Newly allocated SPIR-V words.
     * @throws std::runtime_error If the source is empty, shaderc cannot initialize,
     *         compilation fails, or shaderc returns empty output.
     * @par Synchronization
     * Synchronous CPU operation; compilation is complete when the function returns.
     * @par Thread safety
     * Calls are independent and may run concurrently; each call creates its own
     * shaderc compiler and options objects.
     */
    [[nodiscard]] std::vector<std::uint32_t> compileGlslToSpirv(
        std::string_view source,
        std::string_view sourceName = "shader.comp");

    /**
     * @brief Compiles GLSL compute-shader source to explicitly targeted SPIR-V.
     * @param source Input GLSL source text. The view need only remain valid for this call.
     * @param options Vulkan and SPIR-V targets used by shaderc.
     * @param sourceName Diagnostic source name reported by shaderc.
     * @return Newly allocated SPIR-V words.
     * @throws std::runtime_error If an option is unsupported, the source is empty,
     *         shaderc cannot initialize, compilation fails, or output is empty.
     * @par Synchronization
     * Synchronous CPU operation; compilation is complete when the function returns.
     * @par Thread safety
     * Calls are independent and may run concurrently when their input views and
     * options are not concurrently modified.
     */
    [[nodiscard]] std::vector<std::uint32_t> compileGlslToSpirv(
        std::string_view source,
        const ShaderCompileOptions& options,
        std::string_view sourceName = "shader.comp");
}
