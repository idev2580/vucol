#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace socl{
    enum class VulkanVersion{
        Vulkan10,
        Vulkan11,
        Vulkan12,
        Vulkan13,
    };

    enum class SpirvVersion{
        Spirv10,
        Spirv11,
        Spirv12,
        Spirv13,
        Spirv14,
        Spirv15,
        Spirv16,
    };

    struct ShaderCompileOptions{
        VulkanVersion vulkanVersion = VulkanVersion::Vulkan11;
        SpirvVersion spirvVersion = SpirvVersion::Spirv13;
    };

    [[nodiscard]] std::vector<std::uint32_t> compileGlslToSpirv(
        std::string_view source,
        std::string_view sourceName = "shader.comp");

    [[nodiscard]] std::vector<std::uint32_t> compileGlslToSpirv(
        std::string_view source,
        const ShaderCompileOptions& options,
        std::string_view sourceName = "shader.comp");
}
